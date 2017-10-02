/******************************************************************
 *****                                                        *****
 *****  Name: easyweb.c                                       *****
 *****  Ver.: 1.0                                             *****
 *****  Date: 07/05/2001                                      *****
 *****  Auth: Andreas Dannenberg                              *****
 *****        HTWK Leipzig                                    *****
 *****        university of applied sciences                  *****
 *****        Germany                                         *****
 *****        adannenb@et.htwk-leipzig.de                     *****
 *****  Func: implements a dynamic HTTP-server by using       *****
 *****        the easyWEB-API                                 *****
 *****  Rem.: In IAR-C, use  linker option                    *****
 *****        "-e_medium_write=_formatted_write"              *****
 *****                                                        *****
 ******************************************************************/

// Modifications by Code Red Technologies for NXP LPC1768
// CodeRed - removed header for MSP430 microcontroller
//#include "msp430x14x.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "stdint.h"

// CodeRed - added #define extern on next line (else variables
// not defined). This has been done due to include the .h files 
// rather than the .c files as in the original version of easyweb.
#define extern 

#include "easyweb.h"
#include "acc.h"

// CodeRed - removed header for original ethernet controller
//#include "cs8900.c"                              // ethernet packet driver

//CodeRed - added for LPC ethernet controller
#include "ethmac.h"

// CodeRed - include .h rather than .c file
// #include "tcpip.c"                               // easyWEB TCP/IP stack
#include "tcpip.h"                               // easyWEB TCP/IP stack
// CodeRed - added NXP LPC register definitions header
#include "LPC17xx.h"

// CodeRed - include renamed .h rather than .c file
// #include "webside.c"                             // webside for our HTTP server (HTML)
#include "webside.h"                             // webside for our HTTP server (HTML)
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_i2c.h"
#include "oled.h"

// CodeRed - added for use in dynamic side of web page
unsigned int aaPagecounter = 0;
unsigned int adcValue = 0;

int32_t xoff = 0;
int32_t yoff = 0;
int32_t zoff = 0;
int8_t x = 0;

int8_t y = 0;
int8_t z = 0;

static void init_ssp(void) {
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

int main(void) {
	uint32_t buf[50];
	char NewKey[6];

	init_ssp();
	init_i2c();
	acc_init();
	oled_init();


	oled_clearScreen(OLED_COLOR_WHITE);

	/*oled_putString(1, 1, (uint8_t*) "EasyWeb Demo", OLED_COLOR_BLACK,
			OLED_COLOR_WHITE);
	oled_putString(1, 17, (uint8_t*) "IP Address:", OLED_COLOR_BLACK,
			OLED_COLOR_WHITE);

	sprintf((char*) buf, " %d.%d.%d.%d", MYIP_1, MYIP_2, MYIP_3, MYIP_4);
	oled_putString(1, 25, (uint8_t*) buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);*/

	TCPLowLevelInit();

	HTTPStatus = 0;                         // clear HTTP-server's flag register

	TCPLocalPort = TCP_PORT_HTTP;               // set port we want to listen to

	while (1)                                      // repeat forever
	{
		acc_read(&x, &y, &z);
		xoff = 0 - x;
		yoff = 0 - y;
		zoff = 64 - z;

		sprintf(NewKey, "%04d", xoff);
		oled_putString(1,25, NewKey, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

		sprintf(NewKey, "%04d", yoff);
		oled_putString(1,33, NewKey, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

		sprintf(NewKey, "%04d", zoff);
		oled_putString(1,41, NewKey, OLED_COLOR_WHITE, OLED_COLOR_BLACK);

		if (!(SocketStatus & SOCK_ACTIVE))
			TCPPassiveOpen();   // listen for incoming TCP-connection
		DoNetworkStuff();                    // handle network and easyWEB-stack
											 // events
		HTTPServer();

	}
}

// This function implements a very simple dynamic HTTP-server.
// It waits until connected, then sends a HTTP-header and the
// HTML-code stored in memory. Before sending, it replaces
// some special strings with dynamic values.
// NOTE: For strings crossing page boundaries, replacing will
// not work. In this case, simply add some extra lines
// (e.g. CR and LFs) to the HTML-code.

void HTTPServer(void) {
	if (SocketStatus & SOCK_CONNECTED) // check if somebody has connected to our TCP
	{
		if (SocketStatus & SOCK_DATA_AVAILABLE) // check if remote TCP sent data
			TCPReleaseRxBuffer();                      // and throw it away

		if (SocketStatus & SOCK_TX_BUF_RELEASED) // check if buffer is free for TX
		{
			if (!(HTTPStatus & HTTP_SEND_PAGE)) // init byte-counter and pointer to webside
			{                                          // if called the 1st time
				HTTPBytesToSend = sizeof(WebSide) - 1; // get HTML length, ignore trailing zero
				PWebSide = (unsigned char *) WebSide;    // pointer to HTML-code
			}

			if (HTTPBytesToSend > MAX_TCP_TX_DATA_SIZE) // transmit a segment of MAX_SIZE
			{
				if (!(HTTPStatus & HTTP_SEND_PAGE)) // 1st time, include HTTP-header
				{
					memcpy(TCP_TX_BUF, GetResponse, sizeof(GetResponse) - 1);
					memcpy(TCP_TX_BUF + sizeof(GetResponse) - 1, PWebSide,
							MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1);
					HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE
							- sizeof(GetResponse) + 1;
					PWebSide += MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1;
				} else {
					memcpy(TCP_TX_BUF, PWebSide, MAX_TCP_TX_DATA_SIZE);
					HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE;
					PWebSide += MAX_TCP_TX_DATA_SIZE;
				}

				TCPTxDataCount = MAX_TCP_TX_DATA_SIZE;   // bytes to xfer
				InsertDynamicValues();               // exchange some strings...
				TCPTransmitTxBuffer();                   // xfer buffer
			} else if (HTTPBytesToSend)               // transmit leftover bytes
			{
				memcpy(TCP_TX_BUF, PWebSide, HTTPBytesToSend);
				TCPTxDataCount = HTTPBytesToSend;        // bytes to xfer
				InsertDynamicValues();// exchange some strings...
				InsertDynamicValuesOLED();
				TCPTransmitTxBuffer();                   // send last segment
				TCPClose();                              // and close connection
				HTTPBytesToSend = 0;                     // all data sent
			}

			HTTPStatus |= HTTP_SEND_PAGE;              // ok, 1st loop executed
		}
	} else
		HTTPStatus &= ~HTTP_SEND_PAGE;       // reset help-flag if not connected
}

volatile unsigned int aaScrollbar = 400;

unsigned int GetAD7Val(void) {
	aaScrollbar = (aaScrollbar + 16) % 1024;
	adcValue = (aaScrollbar / 10) * 1000 / 1024;
	return aaScrollbar;
}

void InsertDynamicValues(void) {
	unsigned char *Key;
	char NewKey[6];
	unsigned int i;

	if (TCPTxDataCount < 4)
		return;                     // there can't be any special string

	Key = TCP_TX_BUF;

	for (i = 0; i < (TCPTxDataCount - 3); i++) {
		if (*Key == 'A')
			if (*(Key + 1) == 'D')
				if (*(Key + 3) == '%')
					switch (*(Key + 2)) {
					case '8':                                 // "AD8%"?
					{
						sprintf(NewKey, "%04d", xoff); // insert pseudo-ADconverter value
						memcpy(Key, NewKey, 4);
						break;
					}
					case '7':                                 // "AD7%"?
					{
						sprintf(NewKey, "%04d", yoff); // insert pseudo-ADconverter value
						memcpy(Key, NewKey, 4);
						break;
					}
					case '1':                                 // "AD1%"?
					{
						sprintf(NewKey, "%04d", zoff); // insert pseudo-ADconverter value
						memcpy(Key, NewKey, 4);
						break;
					}
					}
		Key++;
	}
}
void InsertDynamicValuesOLED(void) {
	unsigned char *Key;
	char NewKey[6];
	unsigned int i;

	if (TCPTxDataCount < 4)
		return;                     // there can't be any special string

	Key = TCP_TX_BUF;

	for (i = 0; i < (TCPTxDataCount - 3); i++) {
		if (*Key == 'A')
			if (*(Key + 1) == 'D')
				if (*(Key + 3) == '%')
					switch (*(Key + 2)) {
					case '8':                                 // "AD8%"?
					{
						sprintf(NewKey, "%04d", xoff); // insert pseudo-ADconverter value

						oled_putString(1, 25, (uint8_t*) NewKey, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
						break;


					}
					case '7':                                 // "AD7%"?
					{
						sprintf(NewKey, "%04d", yoff); // insert pseudo-ADconverter value

						oled_putString(2, 25, (uint8_t*) NewKey, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
						break;
					}
					case '1':                                 // "AD1%"?
					{
						sprintf(NewKey, "%04d", zoff); // insert pseudo-ADconverter value

						oled_putString(3, 25, (uint8_t*) NewKey, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
						break;
					}
					}
		Key++;
	}
}




