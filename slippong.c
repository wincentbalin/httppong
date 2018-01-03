/**
  slippong.c
  
  This is a test for SLIP communication with 8051 controller.
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
	rx_buffer_tail = (rx_buffer_tail + 1) % BUF_LENGTH;
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
	tx_buffer_tail = (tx_buffer_tail + 1) % BUF_LENGTH;
	tx_buffer_n--;
	tx_busy = 1;
}
/*-----------------------------------------------------------------------------------*/
void serial_tx(unsigned char c) using 1
{
	/* Wait if buffer full. */
	while(tx_buffer_n == BUF_LENGTH);
	
	tx_buffer[tx_buffer_head] = c;
	tx_buffer_head = (tx_buffer_head + 1) % BUF_LENGTH;
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
		rx_buffer_head = (rx_buffer_head + 1) % BUF_LENGTH;
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
void wait_for_slip_connection(void)
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
	}
}
