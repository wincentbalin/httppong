/**
  httppong.c
  
  Copyright (c) 2008, 2009 by Wincent Balin
  
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * The name of Wincent Balin may not be used to endorse or promote
        products derived from this software without specific prior written
        permission.

  THIS SOFTWARE IS PROVIDED BY Wincent Balin ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL Wincent Balin BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  
  This is a very small HTTP server running on a 8051 controller.
  The memory (IRAM) usage is 76 bytes, so the program is able to run
  without externam RAM. ROM usage is of 2950 bytes, so any 8051 compatible
  controller with ROM of 4kb or more may be used. Only the peripherals
  every 8051 compatible controller has were used, so the resulting
  hex-file should be binary compatible across the whole gross of the
  8051 family.
  
  Of the four banks of the 8051 banks 0 and 1 are used; bank 0 is used
  for the main program and bank 1 is used for serial communication.
  Two bytes in the bit memory are used for, well, the bit memory.
  
  The inspiration and partly a source for this program was found
  in the phpstack, PHP TCP/IP stack by Adam Dunkels, and in the miniweb,
  the minimal HTTP server, by Adam Dunkels too. The idea how to overcome
  SLIP handshake by WinXX OS was found in the application note AN-32
  for the Pumpkin RTOS. Initial setup of the UART was taken
  from the book "Mikrocomputertechnik" by Bernd-Dieter Schaaf.
  
  The name of the program comes from using a xxxpong.c names for
  different stages of TCP/ICMP/IP/SLIP/RS-232 processing, i.e. to see
  that the controller sends back an answer, a "pong", to every
  TCP connection, the stage was named "tcppong.c". So "httppong"
  is the name for the last stage of development.

  ICMP protocol (i.e. ping) requests are supported.
  
  The HTTP server is stateless, and the TCP/IP stack does not support
  packet fragmentation. That means, that the HTTP request has to fit
  into one packet; same goes for the HTTP answer and consequently
  for the WWW page which server sends to the HTTP client.
  Another peculiarity, attributed both to the need of low memory usage
  and to the statelessness of the HTTP server is that the data to be sent
  over HTTP is read twice, first time for checksum calculation and
  second time for actual transfer.

  The function names in the source are pretty self-explanatory.
  For example, ip_rx2() means "receive 2 bytes using IP protocol".
  
  The source code was compiled using SDCC 2.8.0 compiler.
  
*/

#include <mcs51/8051.h>
#include <stdint.h>

#define BUF_LENGTH 0x10

/* Old XOR swap trick. */
#define SWAP(a, b) { a ^= b; b ^= a; a ^= b; }


/* UART communication infrastructure. */
volatile unsigned char rx_buffer[BUF_LENGTH];
volatile uint8_t rx_buffer_n = 0;
volatile uint8_t rx_buffer_head = 0;
volatile uint8_t rx_buffer_tail = 0;
volatile unsigned char tx_buffer[BUF_LENGTH];
volatile uint8_t tx_buffer_n = 0;
volatile uint8_t tx_buffer_head = 0;
volatile uint8_t tx_buffer_tail = 0;
volatile bit tx_busy = 0;
/*-----------------------------------------------------------------------------------*/
unsigned char serial_isr_rx(void) using 1
{
	unsigned char c = rx_buffer[rx_buffer_tail];
	rx_buffer_tail = (rx_buffer_tail + 1) & (BUF_LENGTH-1);
	rx_buffer_n--;
	return c;
}
/*-----------------------------------------------------------------------------------*/
unsigned char serial_rx(void) using 1
{
	if(rx_buffer_n > 0)
	{
		return serial_isr_rx();
	}
	else
	{
		return '\0';
	}
}
/*-----------------------------------------------------------------------------------*/
unsigned char serial_rx_waiting(void) using 1
{
	/* If no characters, wait. */
	while(rx_buffer_n == 0);
	
	/* Return next character. */
	return serial_isr_rx();
}
/*-----------------------------------------------------------------------------------*/
void serial_isr_tx(void) using 1
{
	SBUF = tx_buffer[tx_buffer_tail];
	tx_buffer_tail = (tx_buffer_tail + 1) & (BUF_LENGTH-1);
	tx_buffer_n--;
	tx_busy = 1;
}
/*-----------------------------------------------------------------------------------*/
void serial_tx(unsigned char c) using 1
{
	/* Wait if buffer full. */
	while(tx_buffer_n == BUF_LENGTH);
	
	tx_buffer[tx_buffer_head] = c;
	tx_buffer_head = (tx_buffer_head + 1) & (BUF_LENGTH-1);
	tx_buffer_n++;
	
	if(tx_busy == 0)	/* If only one character, kick the transmitter. */
	{
		serial_isr_tx();
	}
}
/*-----------------------------------------------------------------------------------*/
void serial_isr(void) interrupt SI0_VECTOR using 1
{
	if(RI == 1)	/* Character was received. */
	{
		RI = 0;	/* Clear receiver flag. */
		rx_buffer[rx_buffer_head] = SBUF;	/* Get character from UART. */
		rx_buffer_head = (rx_buffer_head + 1) & (BUF_LENGTH-1);
		rx_buffer_n++;
	}
	else	/* Character was transmitted. */
	{
		TI = 0;	/* Clear transmitter flag. */
		tx_busy = 0;
		
		if(tx_buffer_n > 0)	/* If there is a character in a buffer, transmit it. */
		{
			serial_isr_tx();
		}
	}
}
/*-----------------------------------------------------------------------------------*/
void wait_for_slip_connection(void) using 1
{
	enum waiter_for_slip
	{
		SLIP_WAIT_CHAR_0,
		SLIP_WAIT_CHAR_1,
		SLIP_WAIT_CHAR_2,
		SLIP_WAIT_CHAR_3,
		SLIP_WAIT_CHAR_4,
		SLIP_WAIT_CHAR_5
	}
	wait_for_slip_state = SLIP_WAIT_CHAR_0;
	
	char c;
	
	while(1)
	{
		/* Wair for a character. */
		if(rx_buffer_n == 0)
 			continue;
 		
 		/* Read the character. */
 		c = serial_isr_rx();

 		switch(wait_for_slip_state)
 		{
			case SLIP_WAIT_CHAR_0:
				if(c == 'C')
					wait_for_slip_state = SLIP_WAIT_CHAR_1;
				else
					return;	/* Not a connection from MS Windows. Proceed. */
				break;
				
			case SLIP_WAIT_CHAR_1:
				if(c == 'L')
					wait_for_slip_state = SLIP_WAIT_CHAR_2;
				break;
		
			case SLIP_WAIT_CHAR_2:
				if(c == 'I')
					wait_for_slip_state = SLIP_WAIT_CHAR_3;
				break;
				
			case SLIP_WAIT_CHAR_3:
				if(c == 'E')
					wait_for_slip_state = SLIP_WAIT_CHAR_4;
				break;
				
			case SLIP_WAIT_CHAR_4:
				if(c == 'N')
					wait_for_slip_state = SLIP_WAIT_CHAR_5;
				break;
				
			case SLIP_WAIT_CHAR_5:
				if(c == 'T')
				{
					serial_tx('C');
					serial_tx('L');
					serial_tx('I');
					serial_tx('E');
					serial_tx('N');
					serial_tx('T');
					serial_tx('S');
					serial_tx('E');
					serial_tx('R');
					serial_tx('V');
					serial_tx('E');
					serial_tx('R');
					serial_tx('\n');
					return;
				}
				break;
 		}
	}
}
/*-----------------------------------------------------------------------------------*/

/* SLIP infrastructure. */

/* SLIP packet boundary. */
#define SLIP_END 0300
/* SLIP escape character. */
#define SLIP_ESC 0333
/* Escaped SLIP_END character. */
#define SLIP_ESC_END 0334
/* Escaped SLIP_ESC character. */
#define SLIP_ESC_ESC 0335

typedef enum decoder_for_slip
{
    SLIP_IDLE,
    SLIP_START,
    SLIP_ESCAPED,
    SLIP_PACKET
}
slip_state_t;

slip_state_t slip_rx_state = SLIP_IDLE;
slip_state_t slip_tx_state = SLIP_IDLE;

/*-----------------------------------------------------------------------------------*/
void slip_tx(unsigned char c)
{
	if(slip_tx_state != SLIP_PACKET)
	{
		serial_tx(SLIP_END);
		slip_tx_state = SLIP_PACKET;
	}
	
    switch(c)
    {
        case SLIP_END:
            serial_tx(SLIP_ESC);
            serial_tx(SLIP_ESC_END);
            break;

        case SLIP_ESC:
            serial_tx(SLIP_ESC);
            serial_tx(SLIP_ESC_ESC);
            break;

        default:
            serial_tx(c);
            break;
    }
}
/*-----------------------------------------------------------------------------------*/
#define start_packet end_packet
void end_packet(void)
{
	serial_tx(SLIP_END);
	slip_tx_state = SLIP_IDLE;
}
/*-----------------------------------------------------------------------------------*/
unsigned char slip_decode(unsigned char c)
{	
	switch(slip_rx_state)
	{
		case SLIP_IDLE:
			if(c == SLIP_END)
			{
				slip_rx_state = SLIP_START;	
			}
			break;
		
		case SLIP_START:
			if(c != SLIP_END)
			{
				if(c != SLIP_ESC)
				{
					slip_rx_state = SLIP_PACKET;
					return c;
				}
				else
				{
					slip_rx_state = SLIP_ESCAPED;
				}
			}
			break;
		
		case SLIP_PACKET:
			if(c == SLIP_ESC)
			{
				slip_rx_state = SLIP_ESCAPED;
			}
			else if(c == SLIP_END)
			{
				slip_rx_state = SLIP_IDLE;
			}
			else
			{
				return c;
			}
			break;
			
		case SLIP_ESCAPED:
			if(c == SLIP_ESC_END)
			{
				slip_rx_state = SLIP_PACKET;
				return SLIP_END;
			}
			else if(c == SLIP_ESC_ESC)
			{
				slip_rx_state = SLIP_PACKET;
				return SLIP_ESC;
			}
			else if(c == SLIP_END)
			{
				slip_rx_state = SLIP_IDLE;
			}
			break;
	}

	return '\0';
}
/*-----------------------------------------------------------------------------------*/
unsigned char slip_rx(void)
{
	unsigned char c;
	
	/* At least one character should be available. */
	if(rx_buffer_n > 0)
	{
		/* Try to decode one character. */
		c = slip_decode(serial_rx());
	
		/* If one character is still needed and is available, decode it too. */
		if(slip_rx_state != SLIP_PACKET && rx_buffer_n > 0)
		{
			c = slip_decode(serial_rx());
		}
		
		/* Return result. */
		return c;
	}
	
	/* No result. */
	return '\0';
}
/*-----------------------------------------------------------------------------------*/
unsigned char slip_rx_waiting(void)
{
	unsigned char c;

	while(1)
	{	
		c = slip_decode(serial_rx_waiting());
	
		if(slip_rx_state == SLIP_PACKET)
		{
			return c;
		}
	}
}
/*-----------------------------------------------------------------------------------*/

/* IP infrastructure. */

#define IP_HDR_VHL 0
#define IP_HDR_TOS 1
#define IP_HDR_LEN_1 2
#define IP_HDR_LEN_2 3
#define IP_HDR_IPID_1 4
#define IP_HDR_IPID_2 5
#define IP_HDR_OFFSET_1 6
#define IP_HDR_OFFSET_2 7
#define IP_HDR_TTL 8
#define IP_HDR_PROTO 9
#define IP_HDR_CHKSUM_1 10
#define IP_HDR_CHKSUM_2 11
#define IP_HDR_SRCADDR_1 12
#define IP_HDR_SRCADDR_2 13
#define IP_HDR_SRCADDR_3 14
#define IP_HDR_SRCADDR_4 15
#define IP_HDR_DESTADDR_1 16
#define IP_HDR_DESTADDR_2 17
#define IP_HDR_DESTADDR_3 18
#define IP_HDR_DESTADDR_4 19

#define IP_HEADER_LENGTH 20

enum ip_proto
{
	IP_PROTO_ICMP = 1,
	IP_PROTO_TCP = 6
}
ip_packet_protocol;

uint16_t byte_number;

uint16_t ip_packet_length;

const uint8_t ip_local_address_1 = 192;
const uint8_t ip_local_address_2 = 168;
const uint8_t ip_local_address_3 = 3;
const uint8_t ip_local_address_4 = 2;

uint8_t ip_remote_address_1;
uint8_t ip_remote_address_2;
uint8_t ip_remote_address_3;
uint8_t ip_remote_address_4;

uint32_t checksum;


/*-----------------------------------------------------------------------------------*/
void add_to_checksum(unsigned char c)
{
	uint16_t tmp;

	/* If even byte number, shift one byte left. */	
	if(byte_number & 0x1)
	{
		tmp = c;
	}
	else
	{
		tmp = ((uint16_t) c) << 8;
	}
	
	/* Add value to checksum. */
	checksum = checksum + tmp;

}
/*-----------------------------------------------------------------------------------*/
uint16_t resulting_checksum()
{
	while(checksum & 0xFFFF0000)
	{
		checksum = (checksum & 0xFFFF) + (checksum >> 16);
	}
	
	return checksum;
}
/*-----------------------------------------------------------------------------------*/
/* Receive one byte and add it to checksum. */
uint8_t ip_rx1()
{
	uint8_t c;
	
	c = slip_rx_waiting();
	add_to_checksum(c);
	byte_number++;
	
	return c;
}
/*-----------------------------------------------------------------------------------*/
/* Receive two bytes and add them to checksum. */
uint16_t ip_rx2()
{
	uint16_t i;
	
	i = ((uint16_t) ip_rx1()) << 8;
	i = i | ip_rx1();
	
	return i;
}
/*-----------------------------------------------------------------------------------*/
void ip_rx(void)
{
drop_ip_packet:

	/* Reset variables. */
	byte_number = 0;
	checksum = 0;

/* IP_HDR_VHL */
	/* If packet is IPv4 and has no options, advance, else error. */
	if(ip_rx1() != 0x45)
	{
		goto drop_ip_packet;
	}

/* IP_HDR_TOS */
	ip_rx1();

/* IP_HDR_LEN_1
   IP_HDR_LEN_2 */
	ip_packet_length = ip_rx2();

/* IP_HDR_IPID_1
   IP_HDR_IPID_2 */
	ip_rx2();

/* IP_HDR_OFFSET_1
   IP_HDR_OFFSET_2 */
	ip_rx2();

/* IP_HDR_TTL */
	ip_rx1();

/* IP_HDR_PROTO */
	ip_packet_protocol = ip_rx1();
	if(ip_packet_protocol != IP_PROTO_ICMP &&
	   ip_packet_protocol != IP_PROTO_TCP)
	{
		goto drop_ip_packet;
	}

/* IP_HDR_CHKSUM_1
   IP_HDR_CHKSUM_2 */
	ip_rx2();

/* IP_HDR_SRCADDR_1 */
	ip_remote_address_1 = ip_rx1();

/* IP_HDR_SRCADDR_2 */
	ip_remote_address_2 = ip_rx1();

/* IP_HDR_SRCADDR_3 */
	ip_remote_address_3 = ip_rx1();

/* IP_HDR_SRCADDR_4 */
	ip_remote_address_4 = ip_rx1();
	
/* IP_HDR_DESTADDR_1 */
	if(ip_rx1() != ip_local_address_1)
	{
		goto drop_ip_packet;
	}
	
/* IP_HDR_DESTADDR_2 */
	if(ip_rx1() != ip_local_address_2)
	{
		goto drop_ip_packet;
	}

/* IP_HDR_DESTADDR_3 */
	if(ip_rx1() != ip_local_address_3)
	{
		goto drop_ip_packet;
	}
	
/* IP_HDR_DESTADDR_4 */
	if(ip_rx1() != ip_local_address_4)
	{
		goto drop_ip_packet;
	}

	/* Check resulting checksum. */
	if(resulting_checksum() != 0xFFFF)
	{
		goto drop_ip_packet;
	}
}
/*-----------------------------------------------------------------------------------*/
/* Transfer one byte. */
void ip_tx1(uint8_t i)
{
	slip_tx(i);
	add_to_checksum(i);
	byte_number++;
}
/*-----------------------------------------------------------------------------------*/
/* Transfer two bytes. */
void ip_tx2(uint16_t i)
{
	ip_tx1(i >> 8);
	ip_tx1(i & 0xFF);
}
/*-----------------------------------------------------------------------------------*/
void ip_tx()
{
	const uint8_t	ip_tx_vhl = 0x45;	/* IPv4, IP header 5*4 bytes long. */
	const uint8_t	ip_tx_tos = 0;		/* Nothing unusual. */
	const uint16_t	ip_tx_ipid = 0;		/* If every packet is smaller than 576 bytes, */
										/* no fragmentation is needed. */
	const uint16_t	ip_tx_offset = 0;	/* Same as for IP ID goes for flags and offset. */
	const uint8_t	ip_tx_ttl = 0x80;	/* No particular meaning for Time to Live field. */
	
	/* Reset variables. */
	byte_number = 0;
	checksum = 0;
	
	start_packet();
	
	/* Transfer version and IP header length. */
	ip_tx1(ip_tx_vhl);
	
	/* Transfer TOS field. */
	ip_tx1(ip_tx_tos);
	
	/* Transfer IP packet length. */
	ip_packet_length = ip_packet_length + IP_HEADER_LENGTH;
	ip_tx2(ip_packet_length);
	
	/* Transfer IP ID. */
	ip_tx2(ip_tx_ipid);
	
	/* Transfer IP offset and flags. */
	ip_tx2(ip_tx_offset);
	
	/* Transfer TTL. */
	ip_tx1(ip_tx_ttl);
	
	/* Transfer protocol. */
	ip_tx1(ip_packet_protocol);
	
	/* Calculate and transfer checksum. */
	add_to_checksum(ip_local_address_1);
	byte_number++;
	add_to_checksum(ip_local_address_2);
	byte_number--;
	add_to_checksum(ip_local_address_3);
	byte_number++;
	add_to_checksum(ip_local_address_4);
	byte_number--;
	add_to_checksum(ip_remote_address_1);
	byte_number++;
	add_to_checksum(ip_remote_address_2);
	byte_number--;
	add_to_checksum(ip_remote_address_3);
	byte_number++;
	add_to_checksum(ip_remote_address_4);
	byte_number--;
	ip_tx2(~resulting_checksum());
	
	/* Transfer source address. */
	ip_tx1(ip_local_address_1);
	ip_tx1(ip_local_address_2);
	ip_tx1(ip_local_address_3);
	ip_tx1(ip_local_address_4);
	
	/* Transfer destination address. */
	ip_tx1(ip_remote_address_1);
	ip_tx1(ip_remote_address_2);
	ip_tx1(ip_remote_address_3);
	ip_tx1(ip_remote_address_4);
}
/*-----------------------------------------------------------------------------------*/

/* ICMP infrastructure. */

#define ICMP_HDR_TYPE 20
#define ICMP_HDR_CODE 21
#define ICMP_HDR_CHKSUM_1 22
#define ICMP_HDR_CHKSUM_2 23
#define ICMP_HDR_ID_1 24
#define ICMP_HDR_ID_2 25
#define ICMP_HDR_SEQNO_1 26
#define ICMP_HDR_SEQNO_2 27

#define ICMP_HDR_CODE_FOR_ECHO_REPLY 0

enum icmp_type
{
	ICMP_ECHO_REPLY = 0,
	ICMP_ECHO = 8
}
icmp_packet_type;

uint16_t icmp_id;
uint16_t icmp_seq_number;


void icmp_tx(void);
/*-----------------------------------------------------------------------------------*/
void icmp_rx(void)
{
	/* Reset checksum. */
	checksum = 0;

/* ICMP_HDR_TYPE */
	/* We answer only to ICMP echo requests. */
	if(ip_rx1() != ICMP_ECHO)
	{
		return;
	}
	
/* ICMP_HDR_CODE */
	ip_rx1();

/* ICMP_HDR_CHKSUM_1
   ICMP_HDR_CHKSUM_2 */
	ip_rx2();

/* ICMP_HDR_ID_1
   ICMP_HDR_ID_2 */
	icmp_id = ip_rx2();

/* ICMP_HDR_SEQNO_1
   ICMP_HDR_SEQNO_2 */
	icmp_seq_number = ip_rx2();

	if(resulting_checksum() != 0xFFFF)
	{
		return;
	}
	else
	{
		icmp_tx();
	}
}
/*-----------------------------------------------------------------------------------*/
void icmp_tx(void)
{
	const uint16_t icmp_length = 8;
			
	/* Specify IP content length. */
	ip_packet_length = icmp_length;

	/* Transfer IP header. */
	ip_tx();
	
	/* Zero the IP checksum for using it as ICMP checksum. */
	checksum = 0;
	
	/* Transfer ICMP type (ICMP_ECHO_REPLY). */
	ip_tx1(ICMP_ECHO_REPLY);
	
	/* Transfer ICMP code. */
	ip_tx1(ICMP_HDR_CODE_FOR_ECHO_REPLY);
	
	/* Calculate checksum and transfer it. */
	add_to_checksum(icmp_id >> 8);
	byte_number++;
	add_to_checksum(icmp_id & 0xFF);
	byte_number--;
	add_to_checksum(icmp_seq_number >> 8);
	byte_number++;
	add_to_checksum(icmp_seq_number & 0xFF);
	byte_number--;
	ip_tx2(~resulting_checksum());
	
	/* Transfer ICMP ID. */
	ip_tx2(icmp_id);
	
	/* Transfer ICMP sequence number. */
	ip_tx2(icmp_seq_number);

 	/* End packet (SLIP). */
	end_packet();
}
/*-----------------------------------------------------------------------------------*/

/* TCP infrastructure. */

#define TCP_HDR_SRCPORT_1 20
#define TCP_HDR_SRCPORT_2 21
#define TCP_HDR_DESTPORT_1 22
#define TCP_HDR_DESTPORT_2 23
#define TCP_HDR_SEQNO_1 24
#define TCP_HDR_SEQNO_2 25
#define TCP_HDR_SEQNO_3 26
#define TCP_HDR_SEQNO_4 27
#define TCP_HDR_ACKNO_1 28
#define TCP_HDR_ACKNO_2 29
#define TCP_HDR_ACKNO_3 30
#define TCP_HDR_ACKNO_4 31
#define TCP_HDR_OFFSET 32
#define TCP_HDR_FLAGS 33
#define TCP_HDR_WINDOW_1 34
#define TCP_HDR_WINDOW_2 35
#define TCP_HDR_CHKSUM_1 36
#define TCP_HDR_CHKSUM_2 37
#define TCP_HDR_URGPTR_1 38
#define TCP_HDR_URGPTR_2 39

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAGS 0x3F


uint16_t tcp_local_port;
uint16_t tcp_remote_port;

uint32_t tcp_seq;
uint32_t tcp_ack;

uint8_t tcp_flags;

uint16_t tcp_data_length;


/* Interface to HTTP server. */
typedef enum server_stage_enum
{
	RECEIVING,
	CHECKSUM,
	SENDING
}
server_stage;

unsigned char http_server(server_stage,unsigned char);

/* End of interface to HTTP server. */


void tcp_tx(void);
/*-----------------------------------------------------------------------------------*/
void add_pseudo_header_to_checksum()
{
	checksum += ((uint16_t) ip_local_address_1) << 8 | ip_local_address_2;
	checksum += ((uint16_t) ip_local_address_3) << 8 | ip_local_address_4;
	checksum += ((uint16_t) ip_remote_address_1) << 8 | ip_remote_address_2;
	checksum += ((uint16_t) ip_remote_address_3) << 8 | ip_remote_address_4;
	checksum += IP_PROTO_TCP;
	checksum += (ip_packet_length-IP_HEADER_LENGTH);
}
/*-----------------------------------------------------------------------------------*/
void tcp_rx(void)
{
	uint8_t data_offset = 40;
	
	/* Reinitialize TCP checksum. */
	checksum = 0;
	/* Add pseudo header. */
	add_pseudo_header_to_checksum();

/* TCP_HDR_SRCPORT_1
   TCP_HDR_SRCPORT_2 */
	tcp_remote_port = ip_rx2();

/* TCP_HDR_DESTPORT_1
   TCP_HDR_DESTPORT_2 */
	tcp_local_port = ip_rx2();

/* TCP_HDR_SEQNO_1
   TCP_HDR_SEQNO_2 */
	tcp_seq = ((uint32_t) ip_rx2()) << 16;

/* TCP_HDR_SEQNO_3
   TCP_HDR_SEQNO_4 */
	tcp_seq = tcp_seq | ip_rx2();

/* TCP_HDR_ACKNO_1
   TCP_HDR_ACKNO_2 */
	tcp_ack = ((uint32_t) ip_rx2()) << 16;

/* TCP_HDR_ACKNO_3
   TCP_HDR_ACKNO_4 */
	tcp_ack = tcp_ack | ip_rx2();

/* TCP_HDR_OFFSET */
	data_offset = (ip_rx1() >> 4) * 4 + IP_HEADER_LENGTH;
	tcp_data_length = ip_packet_length - data_offset;

/* TCP_HDR_FLAGS */
	tcp_flags = ip_rx1() & TCP_FLAGS;
	
/* TCP_HDR_WINDOW_1
   TCP_HDR_WINDOW_2 */
	ip_rx2();

/* TCP_HDR_CHKSUM_1
   TCP_HDR_CHKSUM_2 */
	ip_rx2();

/* TCP_HDR_URGPTR_1
   TCP_HDR_URGPTR_2 */
	ip_rx2();

	/* Discard TCP options. */
	while(byte_number < data_offset)
	{
		ip_rx1();
	}

	/* Receive available data. */
	while(byte_number < ip_packet_length)
	{
		http_server(RECEIVING, ip_rx1());
	}

	/* Check for correct TCP checksum. */
 	if(resulting_checksum() != 0xFFFF)
 	{
 		return;
 	}

	/* Process TCP request. */
	if(tcp_local_port == 80)
	{
		if(tcp_flags == TCP_FLAG_SYN)
		{
			tcp_flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
			tcp_ack = tcp_seq + 1;
			tcp_seq = 0xFFFFFFFF;
			tcp_data_length = 0;
			tcp_tx();
		}
		else if(tcp_ack == 0 && tcp_data_length > 0)
		{
			tcp_flags = TCP_FLAG_ACK | TCP_FLAG_PSH | TCP_FLAG_FIN;
			tcp_ack = tcp_seq + tcp_data_length;
			tcp_seq = 0;
			tcp_tx();
		}
		else if(tcp_data_length > 0)
		{
			tcp_flags = TCP_FLAG_ACK;
			SWAP(tcp_ack, tcp_seq);
			tcp_ack += tcp_data_length;
			tcp_data_length = 0;
			tcp_tx();
		}
	}
	else
	{
		tcp_flags = TCP_FLAG_RST | TCP_FLAG_ACK;
		SWAP(tcp_ack, tcp_seq);
		tcp_ack++;
		tcp_data_length = 0;
		tcp_tx();
	}
}
/*-----------------------------------------------------------------------------------*/
void tcp_tx(void)
{
	#define TCP_TX_HEADER_LENGTH 20

	uint16_t byte_number_backup;

	/* Adjust packet length. */
	ip_packet_length = tcp_data_length + TCP_TX_HEADER_LENGTH;
	
	/* Transfer IP header. */
	ip_tx();

	/* Reinitialize TCP checksum. */
	checksum = 0;

 	/* Add pseudo header. */
 	add_pseudo_header_to_checksum();

 	/* Transfer source port. */
 	ip_tx2(tcp_local_port);
 	
 	/* Transfer destination port. */
 	ip_tx2(tcp_remote_port);
 	
 	/* Transfer sequence number. */
 	ip_tx2(tcp_seq >> 16);
 	ip_tx2(tcp_seq & 0xFFFF);
 	
 	/* Transfer acknowledge number. */
 	ip_tx2(tcp_ack >> 16);
 	ip_tx2(tcp_ack & 0xFFFF);
 	
 	/* Transfer data offset. */
 	ip_tx1(5 << 4);
 	
 	/* Transfer flags. */
 	ip_tx1(tcp_flags);
 	
 	/* Transfer window size. */
 	ip_tx2(0x2000);

 	/* Calculate and transfer checksum. */
 	if(ip_packet_length > IP_HEADER_LENGTH+TCP_TX_HEADER_LENGTH)	/* Only if there is a connection. */
 	{
	 	byte_number_backup = byte_number;
	 	/* Advance to TCP data and calculate checksum. */
	 	byte_number = byte_number + 4;
	 	while(byte_number < ip_packet_length)
	 	{
			add_to_checksum(http_server(CHECKSUM, '\0'));
			byte_number++;
 		}
 		byte_number = byte_number_backup;
	}
 	ip_tx2(~resulting_checksum());
 	
 	/* Transfer urgent pointer. We need it not. Make it zero. */
 	ip_tx2(0x0000);

 	/* Transfer TCP data. */
 	while(byte_number < ip_packet_length)
	{
		ip_tx1(http_server(SENDING, '\0'));
	}

 	/* End packet (SLIP). */
	end_packet();
}
/*-----------------------------------------------------------------------------------*/

/* HTTP infrastructure. */


const unsigned char welcomepage[] = "HTTP/1.0 200 OK\r\n"
                                    "Content-type: text/html\r\n"
                                    "Server: you would not know anyway\r\n"
                                    "\r\n"
                                    "<html>\r\n"
                                    "<head>\r\n"
                                    "<title>Welcome</title>\r\n"
                                    "</head>\r\n"
                                    "<body>\r\n"
                                    "<h1>Welcome to the HTTPPONG server!</h1>\r\n"
                                    "It seems to work indeed.\r\n"
                                    "</body>\r\n"
                                    "</html>\r\n";

void http_cgi(void)
{
}
/*-----------------------------------------------------------------------------------*/
unsigned char http_server(server_stage stage, unsigned char c)
{
	uint16_t char_index;
	
	switch(stage)
	{
		case RECEIVING:
			c=c;
			/* At the end of the HTTP request specify length of the answer. Also do CGI. */
		    if(byte_number == ip_packet_length-1)
		    {
			    http_cgi();
			    tcp_data_length = sizeof(welcomepage);
		    }
			break;
			
		case CHECKSUM:
		
		case SENDING:
			char_index = byte_number - (IP_HEADER_LENGTH + TCP_TX_HEADER_LENGTH);
			return welcomepage[char_index];
			break;
	}
	
	return '\0';
}
/*-----------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------*/
void main(void)
{	
	/* Initialize UART. */
	SCON = 0x50;	/* UART mode 1, receiver enabled. */
	PCON |= 0x80;	/* Double baud rate. */
	TMOD |= 0x20;	/* Auto-reload Timer1. */
	TH1 = 230;		/* Timer1 counter's initial value. */
	TL1 = 230;		/* Calculated for 4800 baud @ 24 MHz, 2400 baud @ 12 MHz. */
	TR1 = 1;		/* Run Timer1. */
	
	/* Enable interrupts. */
	ES = 1;
	EA = 1;

	/* Windows always send "CLIENT" and waits for "CLIENTSERVER\n".
	  Other operating systems just connect. */
	wait_for_slip_connection();

	/* Main loop. */
	while(1)
	{
		ip_rx();

		switch(ip_packet_protocol)
		{
			case IP_PROTO_ICMP:
				/* Check whether ICMP packet has any data.
				   If so, do not process the packet,
				   because we do not process ICMP packets with data. */
				if(ip_packet_length == IP_HEADER_LENGTH+8)
				{
					icmp_rx();
				}
				break;
				
			case IP_PROTO_TCP:
				tcp_rx();
				break;
		}
	}
}
