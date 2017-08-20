/*
 * Node with rime address 3.0. It is placed in the living room and it is the
 * principal node of the WSAN. Imagine this node to be connected in output to a
 * serial monitor. The user mainly interacts with this node, giving it the
 * commands for the smart home and reading the feedbacks on the serial monitor.
 *
 * There exist 5 possible commands that the user may give to the CU. Each
 * command corresponds to a number N. The user decides the command N by
 * consecutively pressing N times the button of the CU. The command is actually
 * determined when 4 seconds have elapsed since the last button press. After
 * that, the CU is ready to receive a new command from the user. Every time the
 * CU is ready to receive a new command, it will have to show on the monitor the
 * set of possible commands with the associated number N:
 * 1. Activate/Deactivate the alarm signal - when the alarm signal is activated,
 * 		all the LEDs of Node1 and Node2 start blinking with a period of 2
 * 		seconds. When and only when the alarm is deactivated (the user gives
 * 		again command 1), the LEDs of both Node1 and Node2 return to their
 * 		previous state (the one before the alarm activation). Besides, when the
 * 		alarm is active, all the other commands are disabled;
 * 2. Lock/Unlock the gate - when the gate is locked, the green LED of Node2 is
 * 		switched off, while the red one is switched on. Vice versa, when the
 * 		gate is unlocked (the user gives again command 2), the green LED of
 * 		Node2 is switched on, while the red one is switched off;
 * 3. Open (and automatically close) both the door and the gate in order to let
 * 		a guest enter - When the command is received by Node1 and Node2, their
 * 		blue LEDs have to blink. The blinking must have a period of 2 seconds
 * 		and must last for 16 seconds. The blue LEDof Node2 immediately starts
 * 		blinking, whereas the blue LED of Node1 starts blinking only after 14
 * 		seconds (so, 2 seconds before the blue LED of Node2 stops blinking). The
 * 		16 seconds represent the time required for the gate/door to open and
 * 		then close. The 14 seconds represent the time required for the guest to
 * 		reach the entrance hall by crossing the garden;
 * 4. Obtain the average of the last 5 temperature values measured by Node1.
 * 		Node1 continuously measures temperature with a period of 10 seconds;
 * 5. Obtain the external light value measured by Node2.
 *
 * Finally, the user also has the possibility to switch on and switch off the
 * lights in the garden. This is done by directly pressing the button of Node1.
 * The garden lights are on when the green LED of Node1 is on, and the red one
 * is off. Vice versa, the garden lights are off when the red LED is on, and the
 * green one is off.
 *
 */

#include "contiki.h"
#include "stdio.h"
#include "sys/etimer.h"
#include "dev/button-sensor.h"
#include "net/rime/rime.h"

#define MAX_RETRANSMISSIONS 5

static int lastCommand = 0;

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){
	printf("broadcast message received from %d.%d\n", from->u8[0], from->u8[1]); //sender address + communication buffer
}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
	printf("broadcast message sent (status %d), transmission number %d\n", status, num_tx); //status = if the communication was successful or not; number of necessary retransmissions
}

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	printf("runicast message received from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);
	int* data = (int*)packetbuf_dataptr();
	int measure = *data;
	if (lastCommand==4) {
		if (measure==-100)
			printf("No measurement available yet\n");
		else
			printf("Temperature: %d\n", measure);
	} else if (lastCommand==5) {
		printf("Light: %d\n", measure);
	}
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; //Be careful to the order: receive callback always before send one (you should always specify both)
static struct broadcast_conn broadcast;
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast1, runicast2;

PROCESS(WaitCommand, "Wait command");
AUTOSTART_PROCESSES(&WaitCommand);

PROCESS_THREAD(WaitCommand, ev, data) {

	PROCESS_EXITHANDLER(broadcast_close(&broadcast));
	PROCESS_EXITHANDLER(runicast_close(&runicast1));
	PROCESS_EXITHANDLER(runicast_close(&runicast2));

	PROCESS_BEGIN();

	static struct etimer et;
	static int num = 0;

	broadcast_open(&broadcast, 129, &broadcast_call);
	runicast_open(&runicast1, 144, &runicast_calls);
	runicast_open(&runicast2, 145, &runicast_calls);
	SENSORS_ACTIVATE(button_sensor);

	while(1) {
		PROCESS_WAIT_EVENT();
		if (ev==sensors_event && data == &button_sensor) {
			num++;
			etimer_set(&et, 4*CLOCK_SECOND);
		} else if (etimer_expired(&et)) {
			lastCommand = num;
			if(!runicast_is_transmitting(&runicast1) && !runicast_is_transmitting(&runicast2)) {
				linkaddr_t recv;
				if (num == 1 || num == 3) {
					packetbuf_copyfrom((void*)&num, 1);
					/*recv.u8[0] = 1;
					recv.u8[1] = 0;
					printf("Sending command %d to %d.%d\n", num, recv.u8[0], recv.u8[1]);
					runicast_send(&runicast1, &recv, MAX_RETRANSMISSIONS);
					packetbuf_copyfrom((void*)&num, 1);
					recv.u8[0] = 2;
					printf("Sending command %d to %d.%d\n", num, recv.u8[0], recv.u8[1]);
					runicast_send(&runicast2, &recv, MAX_RETRANSMISSIONS);*/
					broadcast_send(&broadcast);
				} else {
					packetbuf_copyfrom((void*)&num, 1);
					if (num == 4) {
						recv.u8[0] = 1;
						recv.u8[1] = 0;
						printf("Sending command %d to %d.%d\n", num, recv.u8[0], recv.u8[1]);
						runicast_send(&runicast1, &recv, MAX_RETRANSMISSIONS);
					} else if (num == 2 || num == 5) {
						recv.u8[0] = 2;
						recv.u8[1] = 0;
						printf("Sending command %d to %d.%d\n", num, recv.u8[0], recv.u8[1]);
						runicast_send(&runicast2, &recv, MAX_RETRANSMISSIONS);
					}
				}
			}
			num = 0;
		}
	}
	PROCESS_END();
}