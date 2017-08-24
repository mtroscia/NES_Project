/*
 * Node4 with rime address 4.0. It is placed next to the sauna/steam bath room
 * (these treatments require different ranges of temperature and humidity).
 * The user can start this sensor by invoking the command 6 on the CU and can
 * stop it by invoking the same command.
 * Node4 monitors the temperature and the humidity every 5 seconds.
 * By pressing the button of Node4 once the sauna is selected, by pressing it
 * twice the steam bath is selected. The command is actually
 * determined when 3 seconds have elapsed since the last button press. After
 * that, a message is sent to the CU to inform it about the choice.
 * Node4 provides a protection mechanism:
 * 		-) 	after 2 minutes from when the node is switched on, Node4
 * 			is switched off automatically and CU is informed (in a real situation it would be set
 * 			to 20 min, as it is the maximum time these treatments should last
 * 			to avoid health problems);
 * 		-)	if the temperature or the humidity are above the maximum threshold
 * 			for 3 consecutive measurements, Node4 is switched off automatically
 * 			and CU is informed.
 */

#include "contiki.h"
#include <stdio.h>
#include "sys/etimer.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/button-sensor.h"
#include "net/rime/rime.h"

#define MAX_RETRANSMISSIONS 5

static int command;

//steam room off by default (and no treatment selected)
static int steam_room_on = 0;
static int steam_room_treatment = -1; //=0 sauna; =1 steam bath

PROCESS(BaseProcess, "Base process");
PROCESS(MeasurementProcess, "Temperature and humidity monitoring process");
PROCESS(SwitchOffProcess, "Switch off process");
PROCESS(SwitchOnProcess, "Switch on process");
PROCESS(TimeoutProcess, "Timer to switch sensor off");

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("runicast message received from %d.%d, seqno %d\nCommand: %d\n", from->u8[0], from->u8[1], seqno, command);
	if (command==6) {
		steam_room_on = (steam_room_on==0)?1:0;

		//steam room has already been switched off
		if (steam_room_on == 0)
			process_start(&SwitchOffProcess, NULL);
		else
			process_start(&SwitchOnProcess, NULL);
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
	PROCESS_EXITHANDLER(runicast_close(&runicast));

	PROCESS_BEGIN();

	runicast_open(&runicast, 146, &runicast_calls);

	PROCESS_WAIT_EVENT_UNTIL(0);

	PROCESS_END();
}

PROCESS_THREAD(MeasurementProcess, ev, data) {
	PROCESS_BEGIN();

	//set timer of 5 seconds
	//make checks --> SwitchOffProcess

	PROCESS_END();
}

PROCESS_THREAD(SwitchOffProcess, ev, data) {
	PROCESS_BEGIN();

	//set steam_room_treatment
	//send info to the CU
	//process_exit(&MeasurementProcess);
	//TimeoutProcessprocess_start(&MeasurementProcess, NULL);

	PROCESS_END();
}

PROCESS_THREAD(TimeoutProcess, ev, data) {
	static struct etimer_t et_timeout;
	PROCESS_BEGIN();

	etimer_set(&et_timeout, 2*60*CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_timeout));
	process_start(&SwitchOffProcess, NULL);

	PROCESS_END();
}

PROCESS_THREAD(SwitchOnProcess, ev, data) {
	static struct etimer_t et_treatment;
	int num_button_presses;
	PROCESS_BEGIN();

	process_start(&TimeoutProcess, NULL);
	etimer_set(&et_treatment, 3*CLOCK_SECOND);
	SENSORS_ACTIVATE(button_sensor);

	while(steam_room_treatment == -1) {
		PROCESS_WAIT_EVENT();
		if (ev==sensors_event && data==&button_sensor) {
			num_button_presses++;
			etimer_set(&et_treatment, 4*CLOCK_SECOND);
		} else if (etimer_expired(&et_treatment)) {
			if (num_button_presses==1 || num_button_presses==2) {
				steam_room_treatment = num_button_presses-1;
				SENSORS_DEACTIVATE(button_sensor);
				if(!runicast_is_transmitting(&runicast)){
					linkaddr_t recv;
					recv.u8[0] = 3;
					recv.u8[1] = 0;
					packetbuf_copyfrom((void*)&steam_room_treatment, 1);
					printf("Sending treatment %d to %d.%d\n", steam_room_treatment, recv.u8[0], recv.u8[1]);
					runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
					process_start(&MeasurementProcess, NULL);
				}
			}
		}
	}

	PROCESS_END();
}

