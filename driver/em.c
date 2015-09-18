/*
* Copyright (c) 2015 Stephen Rodgers, All Rights Reserved.
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
 

#ifdef __AVR__
	#include "includes.h"
 
	#define SCLK_HIGH EM_SCLK_PORT |= _BV(EM_SCLK_PIN)
	#define SCLK_LOW EM_SCLK_PORT &= ~_BV(EM_SCLK_PIN)
 

	#define MOSI_HIGH EM_MOSI_PORT |= _BV(EM_MOSI_PIN)
	#define MOSI_LOW EM_MOSI_PORT &= ~_BV(EM_MOSI_PIN)
	#define MOSI_SET(x) if((x)) MOSI_HIGH; else MOSI_LOW
 
	#define MISO_STATE (((EM_MISO_PINPORT & _BV(EM_MISO_PIN)) > 0))
 
	#define CLK_DELAY _delay_us(5)
	#define START_DELAY _delay_us(500)
	#define SPI_TIMEOUT  _delay_ms(7.0); 

#endif

#ifdef __XTENSA__
	// API includes
	#include "ets_sys.h"
	#include "osapi.h"
	#include "debug.h"
	#include "gpio.h"
	#include "user_interface.h"
	#include "mem.h"
	#include "easygpio.h"

	
	#define SCLK_PIN 12
	#define MOSI_PIN 14 
	#define MISO_PIN 13
	
	
	#define SCLK_HIGH  GPIO_OUTPUT_SET(SCLK_PIN, 1)
	#define SCLK_LOW  GPIO_OUTPUT_SET(SCLK_PIN, 0)
 
	#define MOSI_HIGH GPIO_OUTPUT_SET(MOSI_PIN, 1)
	#define MOSI_LOW  GPIO_OUTPUT_SET(MOSI_PIN, 0)
	#define MOSI_SET(x) if((x)) MOSI_HIGH; else MOSI_LOW
 
	#define MISO_STATE ((( GPIO_INPUT_GET( MISO_PIN )) > 0))
 
	#define CLK_DELAY os_delay_us(5)
	#define START_DELAY os_delay_us(500)
	#define SPI_TIMEOUT  os_delay_us(7000); 
	
#endif


 
 

/*
 * Do a full duplex SPI transaction
 *
 * Start with clock low
 * 
 * Pass in the byte to be sent.
 * 
 * Returns the received byte
 * 
 * Ends with clock high
 *
 */
 
 static uint16_t em_transact_byte(uint8_t out_byte)
 {
	 uint8_t in_byte = 0;
	 uint8_t i;
	 
	 for(i = 0; i < 8; i++){
		 // Set MOSI
		 SCLK_LOW;
		 // Output data bit to MOSI
		 MOSI_SET(out_byte & 0x80);
		 out_byte <<= 1;
		 // Wait MISO setup time
		 CLK_DELAY;
		 // Sample input state
		 
		 in_byte <<= 1;
		 if(MISO_STATE){
			in_byte |= 0x01;
		 }

		 // Clock high
		 SCLK_HIGH;
		 // Wait clock high time
		 CLK_DELAY;
	 }
	 return (uint16_t) in_byte;

 }
 
/*
 * Initialize I/O pins
 */
 
 
 void em_init(void)
 { 
	 
	 // Set I/O direction on software SPI pins
	 
	 #ifdef __AVR__
	 EM_SCLK_DDR |= _BV(EM_SCLK_PIN);
	 EM_MOSI_DDR |= _BV(EM_MOSI_PIN);
	 EM_MISO_DDR &= ~_BV(EM_MISO_PIN);
	 #endif
	 
	 #ifdef __XTENSA__
	 easygpio_pinMode(SCLK_PIN, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	 easygpio_pinMode(MOSI_PIN, EASYGPIO_NOPULL, EASYGPIO_OUTPUT);
	 easygpio_pinMode(MISO_PIN, EASYGPIO_PULLUP, EASYGPIO_INPUT);
	 #endif
	 
	 // Set the CS Low, and the SCLK and MOSI bits high
	 
	 MOSI_HIGH;
	 SCLK_HIGH;

	 
	 // Wait for em chip SPI engine to time out and clear any spurious transaction
	 
	 SPI_TIMEOUT;
	 
	
	 
 }
 
 /*
  * Do a write transaction
  */
 
 void em_write_transaction(uint8_t addr, uint16_t data)
 {
	 // Tell the chip we want to start a transaction
	 SCLK_LOW;
	 START_DELAY;
	 // Clock out the address, and the 16 bit data to the chip
	 em_transact_byte(addr);
	 em_transact_byte((uint8_t) (data >> 8));
	 em_transact_byte((uint8_t) data);
 }
 
 /*
  * Do a read transaction
  */
 
 uint16_t em_read_transaction(uint8_t addr)
 {
	 uint16_t res;
 
	 // Tell the chip we want to start a transaction
	 SCLK_LOW;
	 START_DELAY;
	 // Clock out the address to the chip
	 em_transact_byte(addr | 0x80);
	 //Clock in the 16 bit data from the chip
	 res = (em_transact_byte(0) << 8);
	 res |= em_transact_byte(0);
	 // Return the result
	 return res;
 }
 
/*
 * Write a block of values, and calculate the checksum on the fly.
 * Return the checksum to the caller.
 */
  
uint16_t em_write_block(uint8_t first, uint8_t last, uint16_t *block)
{
	uint8_t cshigh = 0, cslow = 0;
	uint8_t i;
	
	
	for(i = first; i < last + 1; i++){
		uint8_t j = i - first;
		em_write_transaction(i, block[j]);
		// Calculate checksum on the fly
		// Low byte is modulo 256 sum of all high and low bytes
		cslow += (uint8_t) ((block[j] & 0xff) + (block[j] >> 8));
		// High byte is the XOR of all the high and low bytes.
		cshigh ^= ((uint8_t) (block[j]));
		cshigh ^= ((uint8_t) (block[j] >> 8));
	}
	// Return the checksum
	return (((uint16_t) cshigh) << 8) + cslow;
		
}

 
/*
 * Read a block of values, and calculate the checksum on the fly.
 * Return the checksum to the caller.
 */

uint16_t em_read_block(uint8_t first, uint8_t last, uint16_t *block)
{
	uint8_t cshigh = 0, cslow = 0;
	uint8_t i;
	for(i = first; i < last + 1; i++){
		uint8_t j = i - first;
		block[j] = em_read_transaction(i);
		// Calculate checksum on the fly
		// Low byte is modulo 256 sum of all high and low bytes
		cslow += (uint8_t) ((block[j] & 0xff) + (block[j] >> 8));
		// High byte is the XOR of all the high and low bytes.
		cshigh ^= ((uint8_t) (block[j]));
		cshigh ^= ((uint8_t) (block[j] >> 8));
	}
	// Return the checksum
	return (((uint16_t) cshigh) << 8) + cslow;
	
}

		
	
	

