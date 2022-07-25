#include <stdio.h>
#include <string.h>
#include<stdlib.h>

#include "protocol.h"
#include "gobackn.h"

int between(unsigned int a, unsigned int b, unsigned int c)
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)))
		return 1;
	else
		return 0;
}

struct FRAME {
	unsigned int kind;
	unsigned int ack;
	unsigned int seq;
	unsigned char data[MAX_PKT];
	unsigned int padding;
};

static unsigned char buffer[MAX_SEQ + 1][MAX_PKT], nbuffered = 0;
static unsigned int frame_expected = 0, frame_nr = 0, next_frame_to_send = 0, ack_expected = 0;
static int phl_ready = 0;

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_ack_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 8);
}

static void send_data(unsigned int frame_nr, unsigned int frame_expected)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, buffer[frame_nr], MAX_PKT);

	put_frame((unsigned char *)&s, 12 + MAX_PKT);
	dbg_frame("Send DATA %d ACK %d, ID %d,Window %d,PHL Queue %d\n", s.seq, s.ack, *(short *)s.data, nbuffered, phl_sq_len);

	start_timer(frame_nr, DATA_TIMER);
}

int main(int argc, char **argv)
{
	int event, arg, ack = 0;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);
	lprintf("Designed by Cui Siying, build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();

	for (;;) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			get_packet(buffer[next_frame_to_send]);
			nbuffered++;
			send_data(next_frame_to_send, frame_expected);
			stop_ack_timer();
			next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			break;

		case PHYSICAL_LAYER_READY:
			//dbg_event("Physical layer is ready.\n");
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);
			//dbg_event("Recv a frame, %d bytes\n", len);
			if (len < 11 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				break;
			}

			if (f.kind == FRAME_DATA)
			{
				dbg_frame("Recv DATA %d ACK %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
				if (f.seq == frame_expected) {
					put_packet(f.data, len - 16);
					//dbg_event("Put the packet to the network layer.\n");
					start_ack_timer(ACK_TIMER);
					frame_expected = (frame_expected + 1) % (MAX_SEQ + 1);
				}

				//dbg_event("Process the ACK.\n");
				while (between(ack_expected, f.ack, next_frame_to_send) == 1)
				{
					nbuffered = nbuffered - 1;
					stop_timer(ack_expected);
					ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
				}
			}

			if (f.kind == FRAME_ACK)
			{
				dbg_frame("Recv ACK %d\n", f.ack);
				//dbg_event("ack_expected=%d, f.ack=%d, next_frame_to_send=%d\n",ack_expected, f.ack, next_frame_to_send);
				while (between(ack_expected, f.ack, next_frame_to_send) == 1)
				{
					nbuffered = nbuffered - 1;
					stop_timer(ack_expected);
					ack_expected = (ack_expected + 1) % (MAX_SEQ + 1);
				}
			}
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			next_frame_to_send = ack_expected;
			for (int i = 1; i <= nbuffered; i++)
			{
				send_data(next_frame_to_send, frame_expected);
				next_frame_to_send = (next_frame_to_send + 1) % (MAX_SEQ + 1);
			}
			break;

		case ACK_TIMEOUT:
			dbg_event("---- ACK timeout.\n");
			send_ack_frame();
			break;
		}
		if (nbuffered < MAX_SEQ && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
