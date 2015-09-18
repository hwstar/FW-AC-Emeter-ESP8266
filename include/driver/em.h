/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */
 
/*
 * Register Addresses
 * 
 * See pages 19 and 20 of the datasheet
 */

#define EM_SOFTRESET 0x00
#define EM_SYSSTATUS 0x01
#define EM_FUNCEN 0x02
#define EM_SAGTH 0x03
#define EM_SMALLPMOD 0x04
#define EM_LASTSPIDATA 0x06
#define EM_CALSTART 0x20
#define EM_PLCONSTH 0x21
#define EM_PLCONSTL 0x22
#define EM_LGAIN 0x23
#define EM_LPHI 0x24
#define EM_NGAIN 0x25
#define EM_NPHI 0x26
#define EM_PSTARTTH 0x27
#define EM_PNOLTH 0x28
#define EM_QSTARTTH 0x29
#define EM_QNOLTH 0x2A
#define EM_MMODE 0x2B
#define EM_CS1 0x2C
#define EM_ADJSTART 0x30
#define EM_UGAIN 0x31
#define EM_IGAINL 0x32
#define EM_IGAINN 0x33
#define EM_UOFFSET 0x34
#define EM_IOFFSETL 0x35
#define EM_IOFFSETN 0x36
#define EM_POFFSETL 0x37
#define EM_QOFFSETL 0x38
#define EM_POFFSETN 0x39
#define EM_QOFFSETN 0x3A
#define EM_CS2 0x3B
#define EM_APENERGY 0x40
#define EM_ANENERGY 0x41
#define EM_ATENERGY 0x42
#define EM_RPENERGY 0x43
#define EM_ENENERGY 0x44
#define EM_RTENERGY 0x45
#define EM_ENSTATUS 0x46
#define EM_IRMS 0x48
#define EM_URMS 0x49
#define EM_PMEAN 0x4A
#define EM_QMEAN 0x4B
#define EM_FREQ 0x4C
#define EM_POWERF 0x4D
#define EM_PANGLE 0x4E
#define EM_SMEAN 0x4F
#define EM_IRMS2 0x68
#define EM_PMEAN2 0x6A
#define EM_QMEAN2 0x6B
#define EM_POWERF2 0x6D
#define EM_PANGLE2 0x6E
#define EM_SMEAN2 0x6F

#define EM_CAL_FIRST EM_PLCONSTH
#define EM_CAL_LAST EM_MMODE
#define EM_MEAS_FIRST EM_UGAIN
#define EM_MEAS_LAST EM_QOFFSETN

 
/*
 * Function prototypes
 */
 
// Initialize I/O to the em chip
void em_init(void); 
// Do a 24 bit write transaction
void em_write_transaction(uint8_t addr, uint16_t data);
// Do a 24 bit read transaction
uint16_t em_read_transaction(uint8_t addr);
// Write a block of data
uint16_t em_write_block(uint8_t first, uint8_t last, uint16_t *block);
// Read a block of data
uint16_t em_read_block(uint8_t first, uint8_t last, uint16_t *block);


