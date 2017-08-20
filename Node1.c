/*
 * Node1 with rime address 1.0. It is placed in the entrance hall of the house,
 * close to the door.
 *
 * Received commands:
 * 1. Activate/Deactivate the alarm signal - when the alarm signal is activated,
 * 		all the LEDs of Node1 start blinking with a period of 2 seconds. When
 * 		and only when the alarm is deactivated (the user gives again command 1
 * 		to the Central Unit), the LEDs of Node1 return to their previous state
 * 		(the one before the alarm activation).
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
 *
 */

#include "contiki.h"
#include <stdio.h>
#include "sys/etimer.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"

#include "net/rime/rime.h"

#define MAX_RETRANSMISSIONS 5

int command;

static void recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno) {
	int* data = (int*)packetbuf_dataptr();
	int command = *data;
	printf("runicast message received from %d.%d, seqno %d\nCommand: %d\n", from->u8[0], from->u8[1], seqno, command);
}

//when the ACK is received
static void sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions) {
	printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;

PROCESS(BaseProcess, "Base process");
AUTOSTART_PROCESSES(&BaseProcess);

PROCESS_THREAD(BaseProcess, ev, data) {
	PROCESS_EXITHANDLER(runicast_close(&runicast));

	PROCESS_BEGIN();

	runicast_open(&runicast, 144, &runicast_calls);

	PROCESS_WAIT_EVENT_UNTIL(0);

	PROCESS_END();
}
