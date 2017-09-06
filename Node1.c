/*
 * Node1 with rime address 1.0. It is placed in the entrance hall of the house,
 * close to the door.
 *
 * Received commands:
 * 1. Activate/Deactivate the alarm signal - when the alarm signal is activated,
 * 		all the LEDs of Node1 start blinking with a period of 2 seconds. When
 * 		and only when the alarm is deactivated (the user gives again command 1
 * 		to the Central Unit), the LEDs of Node1 return to their previous state
 * 		(the one before the alarm activation). Besides, when the
 * 		alarm is active, all the other commands are disabled;
 * 3. Open (and automatically close) both the door and the gate in order to let
 * 		a guest enter - When the command is received by Node1 and Node2, their
 * 		blue LEDs have to blink. The blinking must have a period of 2 seconds
 * 		and must last for 16 seconds. The blue LEDof Node2 immediately starts
 * 		blinking, whereas the blue LED of Node1 starts blinking only after 14
 * 		seconds (so, 2 seconds before the blue LED of Node2 stops blinking).
 * 4. Obtain the average of the last 5 temperature values measured by Node1.
 * 		Node1 continuously measures temperature with a period of 10 seconds;
 *
 * Finally, the user also has the possibility to switch on and switch off the
 * lights in the garden. This is done by directly pressing the button of Node1.
 * The garden lights are on when the green LED of Node1 is on, and the red one
 * is off. Vice versa, the garden lights are off when the red LED is on, and the
 * green one is off.
 */

#include "contiki.h"
#include "stdio.h"
#include "sys/etimer.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "net/rime/rime.h"
#include "lib/random.h"

#define MAX_RETRANSMISSIONS 5

static int command;
static int temp_measurements[5] = {-100, -100, -100, -100, -100};
static int alarm = 0;
static unsigned char led_status;

PROCESS(BaseProcess, "Base process");
PROCESS(TempProcess, "Temperature monitoring process");

//Command 1: start/stop alarm
PROCESS(AlarmProcess, "Alarm process");
PROCESS(StopAlarmProcess, "Stop alarm process");

//Command 3: open gate/door process
PROCESS(BlinkingProcess, "Blinking process");
PROCESS(StopBlinkingProcess, "Stop blinking process");
PROCESS(OpenDoorProcess, "Open gate process");

//Command 4: send temperature measurements
PROCESS(SendTempProcess, "Send temperature process");


static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("broadcast message received from %d.%d\nCommand: %d\n", from->u8[0], from->u8[1], command); //sender address + communication buffer
	if (command==1) {
		if (alarm==0)
			process_start(&AlarmProcess, NULL);
		else
			process_start(&StopAlarmProcess, NULL);
	} else if (command==3) {
		if (alarm==0)
			process_start(&OpenDoorProcess, NULL);
	}
}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
	printf("broadcast message sent (status %d), transmission number %d\n", status, num_tx);
}

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("runicast message received from %d.%d, seqno %d\nCommand: %d\n", from->u8[0], from->u8[1], seqno, command);
	if (command==4) {
		if (alarm==0)
			process_start(&SendTempProcess, NULL);
	}
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent};
static struct broadcast_conn broadcast;
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;


AUTOSTART_PROCESSES(&BaseProcess, &TempProcess);

PROCESS_THREAD(BaseProcess, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast));
	PROCESS_EXITHANDLER(runicast_close(&runicast));

	int outer_lights_off;

	PROCESS_BEGIN();

	//open broadcast connection with Node2 and CU
	broadcast_open(&broadcast, 129, &broadcast_call);

	//open runicast connection with CU
	runicast_open(&runicast, 144, &runicast_calls);

	SENSORS_ACTIVATE(button_sensor);

	//start with outer lights off
	outer_lights_off = 1;
	leds_on(LEDS_RED);

	while(1){
		//when the button is pressed, switch on/off the outer lights
		PROCESS_WAIT_EVENT_UNTIL(ev==sensors_event && data==&button_sensor);
		if (alarm==0) {
			outer_lights_off= (outer_lights_off==1)? 0:1;
			leds_toggle(LEDS_GREEN);
			leds_toggle(LEDS_RED);
		}
	}

	PROCESS_END();
}

PROCESS_THREAD(TempProcess, ev, data) {
	static struct etimer et_temp;
	int index = 0;
	int temp;

	PROCESS_BEGIN();

	//monitor temperature every 10 seconds
	etimer_set(&et_temp, 10*CLOCK_SECOND);

	while(1) {
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_temp));

		SENSORS_ACTIVATE(sht11_sensor);

		//adjust the sensed value
		temp = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
		/*randomize: as RANDOM_RAND_MAX=65535, random_rand()/10000 returns
		approximately 6 values --> +/-3 C*/
		temp += (int)random_rand()/10000;

		SENSORS_DEACTIVATE(sht11_sensor);

		temp_measurements[index]=temp;
		index=(index+1)%5;

		//printf("Temperature: %d C\n", temp);

		etimer_reset(&et_temp);
	}

	PROCESS_END();
}

PROCESS_THREAD(AlarmProcess, ev, data) {
	static struct etimer et_alarm;

	PROCESS_BEGIN();

	alarm = 1;

	//save the led state
	led_status = leds_get();
	leds_off(LEDS_ALL);

	etimer_set(&et_alarm, CLOCK_SECOND);

	while(1){
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_alarm));
		leds_toggle(LEDS_ALL);
		etimer_reset(&et_alarm);
	}

	PROCESS_END();
}

PROCESS_THREAD(StopAlarmProcess, ev, data) {
	PROCESS_BEGIN();

	alarm = 0;

	//kill blinking process
	process_exit(&AlarmProcess);

	//restore the led state
	leds_set(led_status);

	PROCESS_END();
}

PROCESS_THREAD(BlinkingProcess, ev, data) {
	static struct etimer et_blink;
	PROCESS_BEGIN();

	etimer_set(&et_blink, CLOCK_SECOND);

	while(1){
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_blink));
		leds_toggle(LEDS_BLUE);
		etimer_reset(&et_blink);
	}

	PROCESS_END();
}

PROCESS_THREAD(StopBlinkingProcess, ev, data) {
	static struct etimer et_stop_blinking;
	PROCESS_BEGIN();

	//blinking must last 16 seconds
	etimer_set(&et_stop_blinking, 16*CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_stop_blinking));
	process_exit(&BlinkingProcess);

	PROCESS_END();
}

PROCESS_THREAD(OpenDoorProcess, ev, data) {
	static struct etimer et_door;
	PROCESS_BEGIN();

	led_status = leds_get();

	//Node1 has to wait 14 seconds before start blinking
	etimer_set(&et_door, 14*CLOCK_SECOND);

	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_door));

	//start blinking process
	process_start(&BlinkingProcess, NULL);
	process_start(&StopBlinkingProcess, NULL);

	PROCESS_END();
}

PROCESS_THREAD(SendTempProcess, ev, data) {
	PROCESS_BEGIN();

	int sum = 0;
	int num_elements = 0;
	int i;
	for (i=0; i<5; i++) {
		if (temp_measurements[i]!=-100) {
			sum += temp_measurements[i];
			num_elements++;
		}
	}

	int avg = -100;
	if (num_elements!=0)
		avg = sum/num_elements;

	//transmit the avg of temperature measurements to the CU
	if(!runicast_is_transmitting(&runicast)){
		linkaddr_t recv;
		recv.u8[0] = 3;
		recv.u8[1] = 0;
		packetbuf_copyfrom((void*)&avg, sizeof(int));
		printf("Sending temperature %d C to %d.%d\n", avg, recv.u8[0], recv.u8[1]);
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
	}
	PROCESS_END();
}

