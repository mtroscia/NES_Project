/*
 * Node2 with rime address 2.0. It is placed in the garden of the house, close
 * to the gate.
 *
 * Received commands:
 * 1. Activate/Deactivate the alarm signal - when the alarm signal is activated,
 * 		all the LEDs of Node2 start blinking with a period of 2 seconds. When
 * 		and only when the alarm is deactivated (the user gives again command 1
 * 		to the Central Unit), the LEDs of Node2 return to their previous state
 * 		(the one before the alarm activation). Besides, when the
 * 		alarm is active, all the other commands are disabled;
 * 2. Lock/Unlock the gate - when the gate is locked, the green LED of Node2 is
 * 		switched off, while the red one is switched on. Vice versa, when the
 * 		gate is unlocked (the user gives again command 2 to the Central Unit),
 * 		the green LED of Node2 is switched on, while the red one is switched
 * 		off;
 * 3. Open (and automatically close) both the door and the gate in order to let
 * 		a guest enter - When the command is received by Node1 and Node2, their
 * 		blue LEDs have to blink. The blinking must have a period of 2 seconds
 * 		and must last for 16 seconds. The blue LEDof Node2 immediately starts
 * 		blinking, whereas the blue LED of Node1 starts blinking only after 14
 * 		seconds (so, 2 seconds before the blue LED of Node2 stops blinking).
 * 5. Obtain the external light value measured by Node2.
 *
 */

#include "contiki.h"
#include <stdio.h>
#include "sys/etimer.h"
#include "dev/leds.h"
#include "dev/light-sensor.h"
#include "net/rime/rime.h"

#define MAX_RETRANSMISSIONS 5


static int command;
static int unlocked_gate;
static int alarm = 0;
static unsigned char led_status;


PROCESS(BaseProcess, "Base process");

//Command 1: start/stop alarm
PROCESS(AlarmProcess, "Alarm process");
PROCESS(StopAlarmProcess, "Stop alarm process");

//Command 2: lock/unlock gate
PROCESS(GateUnlockProcess, "Gate lock and unlock process");

//Command 3: open gate/door process
PROCESS(BlinkingProcess, "Blinking process");
PROCESS(OpenGateProcess, "Open gate process");

//Command 5: send light measurements
PROCESS(SendLightProcess, "Send light process");


static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("broadcast message received from %d.%d\nCommand: %d\n", from->u8[0], from->u8[1], command);
	if (command==1) {
		if (alarm==0)
			process_start(&AlarmProcess, NULL);
		else
			process_start(&StopAlarmProcess, NULL);
	} else if (command==3) {
		if (alarm==0)
			process_start(&OpenGateProcess, NULL);
	}
}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
	printf("broadcast message sent (status %d), transmission number %d\n", status, num_tx);
}

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("runicast message received from %d.%d, seqno %d\nCommand: %d\n", from->u8[0], from->u8[1], seqno, command);
	if (command==2) {
		if (alarm==0)
			process_start(&GateUnlockProcess, NULL);
	} else if (command==5) {
		if (alarm==0)
			process_start(&SendLightProcess, NULL);
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


AUTOSTART_PROCESSES(&BaseProcess);

PROCESS_THREAD(BaseProcess, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast));
	PROCESS_EXITHANDLER(runicast_close(&runicast));

	PROCESS_BEGIN();

	broadcast_open(&broadcast, 129, &broadcast_call);
	runicast_open(&runicast, 145, &runicast_calls);

	//start with unlocked gate
	unlocked_gate = 1;
	leds_on(LEDS_GREEN);

	PROCESS_WAIT_EVENT_UNTIL(0);

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

PROCESS_THREAD(GateUnlockProcess, ev, data) {
	PROCESS_BEGIN();

	unlocked_gate = (unlocked_gate==1)? 0:1;
	leds_toggle(LEDS_GREEN);
	leds_toggle(LEDS_RED);

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

PROCESS_THREAD(OpenGateProcess, ev, data) {
	static struct etimer et_gate;
	PROCESS_BEGIN();

	led_status = leds_get();

	//start blinking process
	process_start(&BlinkingProcess, NULL);
	etimer_set(&et_gate, 16*CLOCK_SECOND);

	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_gate));
	process_exit(&BlinkingProcess);

	//restore the led state
	leds_set(led_status);

	PROCESS_END();
}

PROCESS_THREAD(SendLightProcess, ev, data) {
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(light_sensor);
	//adjust the sensed value
	int light = 10*light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/7;
	SENSORS_DEACTIVATE(light_sensor);

	//transmit the measurement to the CU
	if(!runicast_is_transmitting(&runicast)){
		linkaddr_t recv;
		recv.u8[0] = 3;
		recv.u8[1] = 0;
		packetbuf_copyfrom((void*)&light, 1);
		printf("Sending light %d lux to %d.%d\n", light, recv.u8[0], recv.u8[1]);
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
	}

	PROCESS_END();
}
