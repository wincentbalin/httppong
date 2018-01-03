/**
  pingpong.c
  
  This is a test for serial communication with 8051 controller.
  
  Type 'ping' and you should see 'pong' on terminal.
*/

#include <mcs51/8051.h>

char rx_data;
bit rx_data_flag = 0;
bit tx_data_flag = 1;

void serial_isr(void) interrupt 4
{
	if(RI == 1)
	{
		RI = 0;	/* Clear receiver flag. */
		rx_data = SBUF;	/* Get character from UART. */
		rx_data_flag = 1;
	}
	else
	{
		TI = 0;	/* Clear transmitter flag. */
		tx_data_flag = 1;	/* Allow transmitting of the next byte. */
	}
}

char uart_rx(void)
{
	while(!rx_data_flag);	/* Wait until there is some data. */
	rx_data_flag = 0;		/* Clear flag. */
	return rx_data;			/* Return flag. */
}

void uart_tx(char c)
{
	while(!tx_data_flag);	/* Wait until transmit buffer is free. */
	SBUF = c;
	tx_data_flag = 0;		/* Clear flag. */
}

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
	
	
	/* Main loop. */
	while(1)
	{
		char c = uart_rx();
		if(c == 'i')
			uart_tx('o');
		else
			uart_tx(c);
	}
}
