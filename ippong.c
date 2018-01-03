/**
  slippong.c
  
  This is a test for IP communication with 8051 controller.
*/

#include <mcs51/8051.h>
#include <stdint.h>
#include <stdio.h>

#define WINDOWS

#define BUF_LENGTH 16

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
#ifdef WINDOWS
void wait_for_slip_connection(void) using 1
{
	unsigned char client_buffer[6];
	uint8_t client_buffer_n = 0;
	
	while(1)
	{
		if(rx_buffer_n == 0)
			continue;
			
		client_buffer[0] = client_buffer[1];
		client_buffer[1] = client_buffer[2];
		client_buffer[2] = client_buffer[3];
		client_buffer[3] = client_buffer[4];
		client_buffer[4] = client_buffer[5];
		client_buffer[5] = serial_isr_rx();
		
		if(client_buffer_n < 5)
		{
			client_buffer_n++;
		}
		else
		{
			if(client_buffer[0] == 'C' &&
			   client_buffer[1] == 'L' &&
			   client_buffer[2] == 'I' &&
			   client_buffer[3] == 'E' &&
			   client_buffer[4] == 'N' &&
			   client_buffer[5] == 'T')
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
		}
	}
}
#endif
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
void slip_tx_end(void)
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

bit ip_error;

uint8_t byte_number;

enum ip_proto
{
	IP_PROTO_ICMP = 1,
	IP_PROTO_TCP = 6
}
ip_packet_protocol;

uint16_t ip_packet_length;

const uint8_t local_ip_address_1 = 192;
const uint8_t local_ip_address_2 = 168;
const uint8_t local_ip_address_3 = 3;
const uint8_t local_ip_address_4 = 2;

uint8_t remote_ip_address_1;
uint8_t remote_ip_address_2;
uint8_t remote_ip_address_3;
uint8_t remote_ip_address_4;

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
void ip_rx(void)
{
	unsigned char c;
	
	while(1)
	{				
		/* Receive one character. */
		c = slip_rx_waiting();

		switch(byte_number)
		{
			case IP_HDR_VHL:
				/* If packet is IPv4 and has no options, advance, else error. */
				if(c != 0x45)
				{
					ip_error = 1;
					return;
				}
				break;
				
			case IP_HDR_TOS:
				break;
				
			case IP_HDR_LEN_1:
				ip_packet_length = ((uint16_t) c) << 8;
				break;

			case IP_HDR_LEN_2:
				ip_packet_length = ip_packet_length | c;
				/* If length > 255, bail out with error,
				   because byte_number counter counts up to 255. */
				if(ip_packet_length > 0xFF)
				{
					ip_error = 1;
					return;
				}
				break;

			case IP_HDR_IPID_1:
				break;
				
			case IP_HDR_IPID_2:
				break;
				
			case IP_HDR_OFFSET_1:
				break;
				
			case IP_HDR_OFFSET_2:
				break;
				
			case IP_HDR_TTL:
				break;
				
			case IP_HDR_PROTO:
				if(c == IP_PROTO_ICMP || c == IP_PROTO_TCP)
				{
					ip_packet_protocol = c;
				}
				else
				{
					ip_error = 1;
					return;
				}
				break;
				
			case IP_HDR_CHKSUM_1:
				break;
				
			case IP_HDR_CHKSUM_2:
				break;
				
			case IP_HDR_SRCADDR_1:
				remote_ip_address_1 = c;
				break;
				
			case IP_HDR_SRCADDR_2:
				remote_ip_address_2 = c;
				break;
				
			case IP_HDR_SRCADDR_3:
				remote_ip_address_3 = c;
				break;
				
			case IP_HDR_SRCADDR_4:
				remote_ip_address_4 = c;
				break;
				
			case IP_HDR_DESTADDR_1:
				if(c != local_ip_address_1)
				{
					ip_error = 1;
					return;
				}
				break;
				
			case IP_HDR_DESTADDR_2:
				if(c != local_ip_address_2)
				{
					ip_error = 1;
					return;
				}
				break;

			case IP_HDR_DESTADDR_3:
				if(c != local_ip_address_3)
				{
					ip_error = 1;
					return;
				}
				break;

			case IP_HDR_DESTADDR_4:
				if(c == local_ip_address_4)
				{
				}
				else
				{
					ip_error = 1;
					return;
				}
				break;
		}
		
		/* Add value to IP checksum. */
		add_to_checksum(c);
		
		/* Advance to the next byte. */
		byte_number++;
		
		/* If over the boundary of the IP header, mark IP processing as done. */
		if(byte_number > IP_HDR_DESTADDR_4)
		{
			/* Check resulting checksum. */
			if(resulting_checksum() != 0xFFFF)
			{
				ip_error = 1;
			}
			return;
		}
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

	/* Transfer version and IP header length. */
	ip_tx1(ip_tx_vhl);
	
	/* Transfer TOS field. */
	ip_tx1(ip_tx_tos);
	
	/* Transfer IP packet length. */
	ip_packet_length = ip_packet_length + 20;	/* Add IP header length. */
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
	add_to_checksum(local_ip_address_1);
	byte_number++;
	add_to_checksum(local_ip_address_2);
	byte_number--;
	add_to_checksum(local_ip_address_3);
	byte_number++;
	add_to_checksum(local_ip_address_4);
	byte_number--;
	add_to_checksum(remote_ip_address_1);
	byte_number++;
	add_to_checksum(remote_ip_address_2);
	byte_number--;
	add_to_checksum(remote_ip_address_3);
	byte_number++;
	add_to_checksum(remote_ip_address_4);
	byte_number--;
	ip_tx2(~resulting_checksum());
	
	/* Transfer source address. */
	ip_tx1(local_ip_address_1);
	ip_tx1(local_ip_address_2);
	ip_tx1(local_ip_address_3);
	ip_tx1(local_ip_address_4);
	
	/* Transfer destination address. */
	ip_tx1(remote_ip_address_1);
	ip_tx1(remote_ip_address_2);
	ip_tx1(remote_ip_address_3);
	ip_tx1(remote_ip_address_4);
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

bit icmp_error ;

uint16_t icmp_id;
uint16_t icmp_seqno;


/*-----------------------------------------------------------------------------------*/
void icmp_rx(void)
{
	unsigned char c;
	
	/* Reset checksum. */
	checksum = 0;
	
	while(1)
	{		
		/* Receive one character. */
		c = slip_rx_waiting();

		switch(byte_number)
		{
			case ICMP_HDR_TYPE:
				if(c == ICMP_ECHO)
				{
					icmp_packet_type = c;
				}
				else
				{
					icmp_error = 1;
				}
				break;
				
			case ICMP_HDR_CODE:
				break;
				
			case ICMP_HDR_CHKSUM_1:
				break;
				
			case ICMP_HDR_CHKSUM_2:
				break;
				
			case ICMP_HDR_ID_1:
				icmp_id = ((uint16_t) c) << 8;
				break;
				
			case ICMP_HDR_ID_2:
				icmp_id = icmp_id | c;
				break;
				
			case ICMP_HDR_SEQNO_1:
				icmp_seqno = ((uint16_t) c) << 8;
				break;
				
			case ICMP_HDR_SEQNO_2:
				icmp_seqno = icmp_seqno | c;
				break;
		}
		
		/* Add value to ICMP checksum. */
		add_to_checksum(c);

		/* Advance to the next byte. */
		byte_number++;
		
		/* If over the boundary of the IP header, end ICMP processing. */
		if(byte_number > ICMP_HDR_SEQNO_2)
		{
			/* Check resulting checksum. */
			add_to_checksum(c);
			if(resulting_checksum() != 0xFFFF)
			{
				ip_error = 1;

			}
			return;
		}
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
	add_to_checksum(icmp_seqno >> 8);
	byte_number++;
	add_to_checksum(icmp_seqno & 0xFF);
	byte_number--;
	ip_tx2(~resulting_checksum());
	
	/* Transfer ICMP ID. */
	ip_tx2(icmp_id);
	
	/* Transfer ICMP sequence number. */
	ip_tx2(icmp_seqno);
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


bit tcp_error;

/*-----------------------------------------------------------------------------------*/
void add_pseudo_header_to_checksum(void)
{
	add_to_checksum(local_ip_address_1);
	byte_number++;
	add_to_checksum(local_ip_address_2);
	byte_number--;
	add_to_checksum(local_ip_address_3);
	byte_number++;
	add_to_checksum(local_ip_address_4);
	byte_number--;
	add_to_checksum(remote_ip_address_1);
	byte_number++;
	add_to_checksum(remote_ip_address_2);
	byte_number--;
	add_to_checksum(remote_ip_address_3);
	byte_number++;
	add_to_checksum(remote_ip_address_4);
	
	add_to_checksum(IP_PROTO_TCP);
	byte_number--;
	
	add_to_checksum((ip_packet_length-20) >> 8);
	byte_number++;
	add_to_checksum((ip_packet_length-20) & 0xFF);
	byte_number--;
	
}
/*-----------------------------------------------------------------------------------*/
void tcp_rx(void)
{
	unsigned char c;
	
	/* Reinitialize TCP checksum. */
	checksum = 0;
	/* Add pseudo header. */
	add_pseudo_header_to_checksum();
	
	while(1)
	{		
		/* Receive one character. */
		c = slip_rx_waiting();

		switch(byte_number)
		{
			case TCP_HDR_SRCPORT_1:
				break;
		}
		
		/* Add value to TCP checksum. */
		add_to_checksum(c);

		/* Advance to the next byte. */
		byte_number++;		
	}
}
/*-----------------------------------------------------------------------------------*/
void tcp_tx(void)
{
}
/*-----------------------------------------------------------------------------------*/
void stack_reset(void)
{
	byte_number = 0;
	checksum = 0;
	ip_error = 0;
	icmp_error = 0;
	tcp_error = 0;
}
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
	
#ifdef WINDOWS
	wait_for_slip_connection();
#endif
	
	/* Main loop. */
	while(1)
	{
		ip_rx();
		
		if(!ip_error)
		{
			switch(ip_packet_protocol)
			{
				case IP_PROTO_ICMP:
					/* Check whether ICMP packet has any data.
					   If so, signal an error, because we do not
					   process ICMP packets with data. */
					if(ip_packet_length > 20+8)
					{
						icmp_error = 1;
					}
					else
					{
						icmp_rx();
					}
					
					if(!icmp_error)
					{
						icmp_tx();
						slip_tx_end();
					}
					break;
					
				case IP_PROTO_TCP:
					tcp_rx();
					if(!tcp_error)
					{
						tcp_tx();
						slip_tx_end();
					}
					break;
			}
			
			
		}
		
		stack_reset();
	}
}
