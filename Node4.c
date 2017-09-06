/*
 * Node4 with rime address 4.0. It is placed next to the sauna/steam bath room
 * (these treatments require different ranges of temperature and humidity).
 * The user can start this sensor by invoking the command 6 on the CU and can
 * stop it by invoking the same command.
 * By pressing the button of Node4 once, the sauna is selected and  by pressing
 * it twice, the steam bath is selected. The command is actually determined when
 * 3 seconds have elapsed since the last button press. After that, a message is
 * sent to the CU to inform it about the choice. At this point, Node4 starts
 * monitoring the temperature and the humidity every 5 seconds.
 * Node4 provides a protection mechanism:
 * 		-) 	after 1 minute from when the node is switched on, Node4	is switched
 * 			off automatically and CU is informed (in a real situation it would
 * 			be set to 20min, as it is the maximum time these treatments should
 * 			last to avoid health problems);
 * 		-)	if the temperature or the humidity are above the maximum thresholds
 * 			for 3 consecutive measurements, Node4 is switched off automatically
 * 			and CU is informed.
 * The green led on indicates that the steam room is on.
 */

#include "contiki.h"
#include <stdio.h>
#include "sys/etimer.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "net/rime/rime.h"
#include "lib/random.h"

#define MAX_RETRANSMISSIONS 5

//steam room off by default (and no treatment selected)
static int steam_room_on = 0;
static int steam_room_treatment = 0; //=1 sauna; =2 steam bath

//protection thresholds
static int MAX_TEMPERATURE_SAUNA = 80; //C
static int MAX_HUMIDITY_SAUNA = 40; //%
static int MAX_TEMPERATURE_STEAM_BATH = 50; //C
static int MAX_HUMIDITY_STEAM_BATH = 90; //%

PROCESS(BaseProcess, "Base process");
PROCESS(MeasurementProcess, "Temperature and humidity monitoring process");
PROCESS(SwitchOffProcess, "Switch off process");
PROCESS(TimeoutProcess, "Timer to switch sensor off");

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	int command = *data;
	printf("runicast message received from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);

	if (command==6) {
		steam_room_on = (steam_room_on==0)?1:0;

		if (steam_room_on == 0) {
			printf("Steam room is switching off...\n");
			steam_room_treatment = 0;
			leds_off(LEDS_GREEN);
			process_exit(&TimeoutProcess);
			process_exit(&MeasurementProcess);
		} else {
			printf("Steam room is switching on...\n");
			leds_on(LEDS_GREEN);
			process_start(&TimeoutProcess, NULL);
			process_start(&MeasurementProcess, NULL);
		}
	}
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;

AUTOSTART_PROCESSES(&BaseProcess);

PROCESS_THREAD(BaseProcess, ev, data) {
	static struct etimer et_treatment;
	PROCESS_EXITHANDLER(runicast_close(&runicast));

	PROCESS_BEGIN();

	static int button_presses = 0;

	//open runicast connection with CU
	runicast_open(&runicast, 146, &runicast_calls);

	SENSORS_ACTIVATE(button_sensor);

	while(1) {
		PROCESS_WAIT_EVENT();

		if (ev==sensors_event && data==&button_sensor) {
			if (steam_room_on == 0)
				/* if steam room is off suppress the possibility to accept the
				 button press command */
				button_presses = 0;
			else {
				button_presses++;
				etimer_set(&et_treatment, 3*CLOCK_SECOND);
			}
		} else if (etimer_expired(&et_treatment)) {
			if (button_presses==1 || button_presses==2) {
				steam_room_treatment = button_presses;

				//inform the CU about the user's choice
				if(!runicast_is_transmitting(&runicast)){
					linkaddr_t recv;
					recv.u8[0] = 3;
					recv.u8[1] = 0;
					packetbuf_copyfrom((void*)&steam_room_treatment, sizeof(int));
					printf("Sending treatment %d to %d.%d\n", steam_room_treatment, recv.u8[0], recv.u8[1]);
					runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
				}
			} else {
				if (steam_room_on == 1 && button_presses!=0)
					printf("Command not found\n");
			}
			button_presses = 0;
		}
	}

	PROCESS_END();
}

PROCESS_THREAD(SwitchOffProcess, ev, data) {
	PROCESS_BEGIN();

	steam_room_on = 0;
	steam_room_treatment = 0;
	leds_off(LEDS_GREEN);

	//inform the CU about the automatic switch off
	if(!runicast_is_transmitting(&runicast)){
		linkaddr_t recv;
		recv.u8[0] = 3;
		recv.u8[1] = 0;
		packetbuf_copyfrom((void*)&steam_room_treatment, sizeof(int));
		printf("Sending stop treatment to %d.%d\n", recv.u8[0], recv.u8[1]);
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
	}

	PROCESS_END();
}

PROCESS_THREAD(MeasurementProcess, ev, data) {
	static struct etimer et_measurement;

	int temp;
	int hum;

	PROCESS_BEGIN();

	static int count_temp_overcome_sauna = 0;
	static int count_hum_overcome_sauna = 0;
	static int count_temp_overcome_steam_bath = 0;
	static int count_hum_overcome_steam_bath = 0;

	etimer_set(&et_measurement, 5*CLOCK_SECOND);
	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_measurement));

		SENSORS_ACTIVATE(sht11_sensor);
		//adjust the sensed values
		temp = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
		hum = (0.0405*sht11_sensor.value(SHT11_SENSOR_HUMIDITY)-4)+
				(-2.8*0.000001)*sht11_sensor.value(SHT11_SENSOR_HUMIDITY)*
				sht11_sensor.value(SHT11_SENSOR_HUMIDITY);
		SENSORS_DEACTIVATE(sht11_sensor);

		/*	TEMPERATURE: add MAX_TEMP_SAUNA-24 or MAX_TEMP_STEAM_BATH-24 to
		 	work next to the threshold
			HUMIDITY: remove 116-MAX_HUM_SAUNA or 116-MAX_HUM_STEAM_BATH to
			work next to the threshold
			RANDOMIZE: as RANDOM_RAND_MAX=65535, random_rand()/6000 returns
			approximately 10 values --> +/-5 C
		*/
		if (steam_room_treatment==1) {
			temp = temp+(MAX_TEMPERATURE_SAUNA-24)+(int)random_rand()/6000;
			hum = hum-(116-MAX_HUMIDITY_SAUNA)+(int)random_rand()/6000;
		} else if (steam_room_treatment==2) {
			temp = temp+(MAX_TEMPERATURE_STEAM_BATH-24)+(int)random_rand()/6000;
			hum = hum-(116-MAX_HUMIDITY_STEAM_BATH)+(int)random_rand()/6000;
		}

		if (steam_room_treatment!=0)
			printf("Sensed temperature: %d C; sensed humidity: %d%%\n", temp, hum);

		if (steam_room_treatment==1) { //sauna
			count_temp_overcome_steam_bath = 0;
			count_hum_overcome_steam_bath = 0;
			if (temp > MAX_TEMPERATURE_SAUNA) {
				count_temp_overcome_sauna++;
				if (count_temp_overcome_sauna==3) {
					printf("Temperature is too high!\nSteam room is switching off...\n\n");
					process_exit(&TimeoutProcess);
					process_start(&SwitchOffProcess, NULL);
					PROCESS_EXIT();
				}
			} else
				count_temp_overcome_sauna = 0;
			if (hum > MAX_HUMIDITY_SAUNA) {
				count_hum_overcome_sauna++;
				if (count_hum_overcome_sauna==3) {
					printf("Humidity is too high!\nSteam room is switching off...\n\n");
					process_exit(&TimeoutProcess);
					process_start(&SwitchOffProcess, NULL);
					PROCESS_EXIT();
				}
			} else
				count_hum_overcome_sauna = 0;
		} else if (steam_room_treatment==2) { //steam bath
			count_temp_overcome_sauna = 0;
			count_hum_overcome_sauna = 0;
			if (temp > MAX_TEMPERATURE_STEAM_BATH) {
				count_temp_overcome_steam_bath++;
				if (count_temp_overcome_steam_bath==3) {
					printf("Temperature is too high!\nSteam room is switching off...\n\n");
					process_exit(&TimeoutProcess);
					process_start(&SwitchOffProcess, NULL);
					PROCESS_EXIT();
				}
			} else
				count_temp_overcome_steam_bath = 0;
			if (hum > MAX_HUMIDITY_STEAM_BATH) {
				count_hum_overcome_steam_bath++;
				if (count_hum_overcome_steam_bath==3) {
					printf("Humidity is too high!\nSteam room is switching off...\n\n");
					process_exit(&TimeoutProcess);
					process_start(&SwitchOffProcess, NULL);
					PROCESS_EXIT();
				}
			} else
				count_hum_overcome_steam_bath = 0;
		}

		etimer_reset(&et_measurement);
	}

	PROCESS_END();
}

PROCESS_THREAD(TimeoutProcess, ev, data) {
	static struct etimer et_timeout;
	PROCESS_BEGIN();

	etimer_set(&et_timeout, 60*CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_timeout));
	printf("\nTimer expired!\nSteam room is switching off...\n\n");
	process_exit(&MeasurementProcess);
	process_start(&SwitchOffProcess, NULL);

	PROCESS_END();
}
