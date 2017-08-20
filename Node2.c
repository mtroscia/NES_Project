/*
 * Node2 with rime address 2.0. It is placed in the garden of the house, close
 * to the gate.
 *
 * Received commands:
 * 1. Activate/Deactivate the alarm signal - when the alarm signal is activated,
 * 		all the LEDs of Node2 start blinking with a period of 2 seconds. When
 * 		and only when the alarm is deactivated (the user gives again command 1
 * 		to the Central Unit), the LEDs of Node2 return to their previous state
 * 		(the one before the alarm activation).
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

PROCESS(BaseProcess, "Base process");
PROCESS(SendLightProcess, "Send light process");

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("broadcast message received from %d.%d\nCommand: %d\n", from->u8[0], from->u8[1], command); //sender address + communication buffer
}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
	printf("broadcast message sent (status %d), transmission number %d\n", status, num_tx); //status = if the communication was successful or not; number of necessary retransmissions
}

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	command = *data;
	printf("runicast message received from %d.%d, seqno %d\nCommand: %d\n", from->u8[0], from->u8[1], seqno, command);
	if (command==5) {
		process_start(&SendLightProcess, NULL);
	}
}

//when the ACK is received
static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; //Be careful to the order: receive callback always before send one (you should always specify both)
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

	PROCESS_WAIT_EVENT_UNTIL(0);

	PROCESS_END();
}

PROCESS_THREAD(SendLightProcess, ev, data) {
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(light_sensor);

	int light = 10*light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/7;

	SENSORS_DEACTIVATE(light_sensor);

	if(!runicast_is_transmitting(&runicast)){
		linkaddr_t recv;
		recv.u8[0] = 3;
		recv.u8[1] = 0;
		packetbuf_copyfrom((void*)&light, 1);
		printf("Sending light %d to %d.%d\n", light, recv.u8[0], recv.u8[1]);
		runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
	}

	PROCESS_END();
}
