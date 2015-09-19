/* user_main.c -- ESP8266 MQTT battery monitor
*
* Configuration parameters set with the Makefile using a Python patching
* utility which is avalable on my github site. This allows the configurations
* to differ between nodes and also protects the WIFI login credentials by
* removing them from the source.
*
* Copyright (C) 2015, Stephen Rodgers <steve at rodgers 619 dot com>
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 
* Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* Neither the name of Redis nor the names of its contributors may be used
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

// API includes
#include "ets_sys.h"
#include "osapi.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "jsonparse.h"
// Project includes
#include "driver/uart.h"
#include "mqtt.h"
#include "wifi.h"
#include "easygpio.h"
#include "util.h"
#include "kvstore.h"
#include "driver/em.h"


/* General definitions */

#define ON 1
#define OFF 0


/* Application specific definitions  */

// EM chip constants
#define IBASIC 1								// Basic current (A)
#define VREF 240								// Reference voltage (V)
#define IGAIN 8									// Current Gain
#define MVISAMPLE 1								// Millivolts across shunt resistor at basic current
#define MVVSAMPLE 248							// Millivolts at bottom tap of voltage divider at vref
#define MC 3200									// Metering pulse constant (impulses/kWh)
 
// EM Chip power line constant calculated using constants above.
#define PLC ((838860800ULL*IGAIN*MVISAMPLE*MVVSAMPLE)/(1ULL*MC*VREF*IBASIC))	// Power Line Constant

// EM chip mode word
#define MODE_WORD	0x3422						// Gain of 8 for current, rest are defaults

// Buffer offsets into in RAM copy of EM chip registers
enum {PLCONSTH=0, PLCONSTL, LGAIN, LPHI, NGAIN, NPHI, PSTARTTH, PNOLTH, QSTARTTH, QNOLTH, MMODE};
enum {UGAIN = 0, IGAINL, IGAINN, UOFFSET, IOFFSETL, IOFFSETN, POFFSETL, QOFFSETL, POFFSETN, QOFFSETN};


#define MAX_INFO_ELEMENTS 16			// Patcher number of elements
#define INFO_BLOCK_MAGIC 0x3F2A6C17		// Patcher magic
#define INFO_BLOCK_SIG "ESP8266HWSTARSR"// Patcher pattern
#define CONFIG_FLD_REQD 0x01			// Patcher field required flag

// EEPROM and ram EM chip calibration data structure

struct eeprom_cal_data_tag {
	uint16_t meter_cal[11];						// Meter calibration data
	uint16_t measure_cal[10];					// Measurement calibration data
	uint16_t pad[34];							// Unused
	uint16_t cal_crc;							// CRC of the calibration data
} __attribute__((__packed__));

typedef struct eeprom_cal_data_tag eeprom_cal_data_t;

// Definition for a patcher config element

struct config_info_element_tag{
	uint8_t flags;
	uint8_t key[15];
	uint8_t value[80];
}  __attribute__((__packed__));

typedef struct config_info_element_tag config_info_element;


// Definition for a patcher config element

struct config_info_block_tag{
	uint8_t signature[16];
	uint32_t magic;
	uint8_t numelements;
	uint8_t recordLength;
	uint8_t pad[10];
	config_info_element e[MAX_INFO_ELEMENTS];
}  __attribute__((__packed__));

// Definition of a common element for MQTT command parameters

typedef union {
	char *sp;
	unsigned u;
	int i;
} pu;

// Definition of an MQTT command element

typedef struct {
	const char *command;
	uint8_t type;
	pu p;
} command_element;


typedef struct config_info_block_tag config_info_block;

// Definition of command codes and types

enum {WIFISSID=0, WIFIPASS, MQTTHOST, MQTTPORT, MQTTSECUR, MQTTDEVID, 
	MQTTUSER, MQTTPASS, MQTTKPALIV, MQTTDEVPATH, MQTTBTLOCAL};
enum {CP_NONE= 0, CP_INT, CP_BOOL, CP_QSTRING, CP_REGISTER};
 
 
/* Local storage */

// Patcher configuration information


LOCAL config_info_block configInfoBlock = {
	.signature = INFO_BLOCK_SIG,
	.magic = INFO_BLOCK_MAGIC,
	.numelements = MAX_INFO_ELEMENTS,
	.recordLength = sizeof(config_info_element),
	.e[WIFISSID] = {.flags = CONFIG_FLD_REQD, .key = "WIFISSID", .value="your_ssid_here"},
	.e[WIFIPASS] = {.flags = CONFIG_FLD_REQD, .key = "WIFIPASS", .value="its_a_secret"},
	.e[MQTTHOST] = {.flags = CONFIG_FLD_REQD, .key = "MQTTHOST", .value="your_mqtt_broker_hostname_here"}, // May also be an IP address
	.e[MQTTPORT] = {.key = "MQTTPORT", .value="1883"}, // destination Port for mqtt broker
	.e[MQTTSECUR] = {.key = "MQTTSECUR",.value="0"}, // Security 0 - no encryption
	.e[MQTTDEVID] = {.key = "MQTTDEVID", .value="your_mqtt_device_id_here"}, // Unique device ID
	.e[MQTTUSER] = {.key = "MQTTUSER", .value="your_mqtt_client_name_here"}, // MQTT User name
	.e[MQTTPASS] = {.key = "MQTTPASS", .value="its_a_secret"},// MQTT Password
	.e[MQTTKPALIV] = {.key = "MQTTKPALIV", .value="120"}, // Keepalive interval
	.e[MQTTDEVPATH] = {.flags = CONFIG_FLD_REQD, .key = "MQTTDEVPATH", .value = "/home/lab/acpowermon"} // Device path

};

// Command elements 
// Additional commands are added here
 
enum {CMD_QUERY = 0, CMD_RESET_KWH, CMD_REGISTER, CMD_SURVEY, CMD_SSID, CMD_RESTART, CMD_WIFIPASS};

LOCAL command_element commandElements[] = {
	{.command = "query", .type = CP_NONE},
	{.command = "resetkwh", .type = CP_NONE},
	{.command = "register", .type = CP_REGISTER},
	{.command = "survey", .type = CP_NONE},
	{.command = "ssid", .type = CP_QSTRING},
	{.command = "restart",.type = CP_NONE},
	{.command = "wifipass",.type = CP_QSTRING},
	{.command = ""} /* End marker */
};
	
// Misc Local variables 

LOCAL char *commandTopic, *statusTopic;
const char *emCalDataKey = "EMCALDATA";
LOCAL char *controlTopic = "/node/control";
LOCAL char *infoTopic = "/node/info";
LOCAL flash_handle_s *configHandle;
LOCAL eeprom_cal_data_t *eepromCalData;	// Place to store calibration data
// Total forward active energy
LOCAL uint32_t fae_total;
LOCAL MQTT_Client mqttClient;				// Control block used by MQTT functions



/**
 * Convert unsigned 16 bit integer to fixed point number
 */
 
LOCAL char * ICACHE_FLASH_ATTR to_fixed_decimal_uint16(char *dest,
	uint8_t places, uint16_t val){
	uint16_t rem;
	uint16_t quot;
	const char *format;
	
	if(2 == places){
		rem = val % 100;
		quot = val / 100;
		format = "%d.%02d";
	}
	else{
		rem = val % 1000;
		quot = val / 1000;
		format = "%d.%03d";
	}
	
	/* Generate string */
	
	os_sprintf(dest, format, quot, rem);
	return dest;
}

/**
 * Convert ones complement signed 16 bit integer to fixed point number
 */

LOCAL char * ICACHE_FLASH_ATTR ones_compl_to_fixed_decimal_uint16(char *dest, 
uint8_t places, uint16_t val){
	int16_t rem;
	int16_t quot;
	const char *format = NULL;
	uint8_t negative = val & 0x8000 ? TRUE : FALSE;
	
	val = val & 0x7FFF;// Strip sign bit
	
	if(1 == places){
		rem = val%10;
		quot = val/10;
		format ="%d.%d";
	}
	else if(2 == places){
		rem = val%100;
		quot = val/100;
		format = "%d.%02d";

	}
	else{
		rem = val%1000;
		quot = val/1000;
		format = "%d.%03d";
	}
	
	// Set the sign of the integer part if negative
	
	if(negative)
		quot *= -1;
		
	// Generate string 
	os_sprintf(dest, format, quot, rem);
	
	return dest;
}


/**
 * Convert twos complement signed 16 bit integer to fixed point number
 */

LOCAL char * ICACHE_FLASH_ATTR twos_compl_to_fixed_decimal_int16(char *dest, 
uint8_t places, int16_t val){
	int16_t rem;
	int16_t quot;
	const char *format = NULL;
	
	if(1 == places){
		rem = val%10;
		quot = val/10;
		format = "%d.%d";
	}
	else if(2 == places){
		rem = val%100;
		quot = val/100;
		format = "%d.%02d";

	}
	else{
		rem = val%1000;
		quot = val/1000;
		format = "%d.%03d";
	}
	
	// Decimal part is always positive.
	
	if(val < 0)
		rem *= -1;
		
	// Generate string 
	os_sprintf(dest, format, quot, rem);
	
	return dest;
}

/**
 * Debug function. Dump registers as a set
 */

LOCAL void ICACHE_FLASH_ATTR dump_words(const uint16_t *buffer, uint8_t addr, uint8_t len)
{
	uint8_t i;
	
	for(i = 0; i < len; i++, addr++)
		INFO("%02X:%04X\n", addr, buffer[i]);
}

/**
 * Convert a hex string into a 16 bit unsigned integer
 */

LOCAL int ICACHE_FLASH_ATTR str2hex(uint16_t *val, const char *str)
{
	uint8_t i;
	char c;
	
	if(!val)
		return FALSE;

	*val = 0;
	for(i = 0; i < 4; i++){
		c = str[i];
		if(!c)
			break;
		*val <<= 4;
		if((c >= '0') && (c <= '9'))
			*val |= (c - '0');
		else if((c >= 'A') && (c <= 'F'))
			*val |= (c - 0x37);
		else if((c >= 'a') && (c <= 'f'))
			*val |= (c - 0x57);
		else
			return FALSE;
	}
	return TRUE;
}

	
		

/** 
 * Calculate CRC over buffer using polynomial: X^16 + X^12 + X^5 + 1 
 */

LOCAL uint16_t ICACHE_FLASH_ATTR calcCRC16(void *buf, int len)
{
	uint8_t i;
	uint16_t crc = 0;
	uint8_t *b = (uint8_t *) buf;
	

	while(len--){
		crc ^= (((uint16_t) *b++) << 8);
		for ( i = 0 ; i < 8 ; ++i ){
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
          	}
	}
	return crc;
}


/**
 * Publish connection info
 */
LOCAL void ICACHE_FLASH_ATTR publishConnInfo(MQTT_Client *client)
{
	struct ip_info ipConfig;
	char *buf = util_zalloc(256);	
		
	// Publish who we are and where we live
	wifi_get_ip_info(STATION_IF, &ipConfig);
	os_sprintf(buf, "{\"muster\":{\"connstate\":\"online\",\"device\":\"%s\",\"ip4\":\"%d.%d.%d.%d\",\"schema\":\"hwstar_acpowermon\",\"ssid\":\"%s\"}}",
			configInfoBlock.e[MQTTDEVPATH].value,
			*((uint8_t *) &ipConfig.ip.addr),
			*((uint8_t *) &ipConfig.ip.addr + 1),
			*((uint8_t *) &ipConfig.ip.addr + 2),
			*((uint8_t *) &ipConfig.ip.addr + 3),
			commandElements[CMD_SSID].p.sp);

	INFO("MQTT Node info: %s\r\n", buf);

	// Publish
	MQTT_Publish(client, infoTopic, buf, os_strlen(buf), 0, 0);
	
	// Free the buffer
	util_free(buf);
	
}



/**
 * Handle qstring command
 */
 
LOCAL int ICACHE_FLASH_ATTR handleQstringCommand(char *new_value, command_element *ce)
{
	char *buf = util_zalloc(128);
	

	if(!new_value){
		const char *cur_value = kvstore_get_string(configHandle, ce->command);
		os_sprintf(buf, "{\"%s\":\"%s\"}", ce->command, cur_value);
		util_free(cur_value);
		INFO("Query Result: %s\r\n", buf );
		MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
		util_free(buf);
		return FALSE;
	}
	else{
		util_free(ce->p.sp); // Free old value
		ce->p.sp = new_value; // Save reference to new value
		kvstore_put(configHandle, ce->command, ce->p.sp);
		
	}

	util_free(buf);
	return TRUE;
}

/**
 *  Register command
 */

LOCAL bool ICACHE_FLASH_ATTR registerCommand(struct jsonparse_state *state )
{
	char addr_str[3];
	char value_str[5];
	char response_str[32];
	uint16_t addr, offset, value, cs, stat;
	bool wr = FALSE;
	uint8_t range;
	
	if (util_parse_json_param(state, "addr", addr_str, sizeof(addr_str)) != 2){
		//INFO("register: address field missing\n");
		return FALSE;
	}
	if (util_parse_json_param(state, "value", value_str, sizeof(value_str)) != 2){
		//INFO("register: no value field, operation will be read\n");
	}
	else{
		wr = TRUE;
	}
		
	if(!str2hex(&addr, addr_str) || (addr > 0x6F)){
		INFO("register: bad address\n");
		return FALSE;
	}
	
	INFO("address: %02X\n", addr);
		
	if(wr){
		// Convert write data from string to a uint16_t
		if(!str2hex(&value, value_str)){
			INFO("register: bad value\n");
			return FALSE;
		}
		// Check to see the range is valid
		if((addr >= EM_CAL_FIRST) && (addr <= EM_CAL_LAST)){
			offset = addr - EM_CAL_FIRST;
			range = 0;
		}
		else if((addr >= EM_MEAS_FIRST) && (addr <= EM_MEAS_LAST)){
			offset = addr - EM_MEAS_FIRST;
			range = 1;
		}
		else{
			INFO("register: address out of range\n");
			return FALSE; // Address out of range
		}
		// Print for debug purposes
		INFO("value: %04X\n", value);	
		
		if(!range){
			// Meter calibration range
			// Put new value in RAM
			eepromCalData->meter_cal[offset] = value;
			// Unlock meter cal
			em_write_transaction(EM_CALSTART, 0x5678);
			// Rewrite the block to the em chip
			cs = em_write_block(EM_CAL_FIRST, EM_CAL_LAST, eepromCalData->meter_cal);
			// Write the new checksum
			em_write_transaction(EM_CS1, cs);
			// Lock the meter cal
			em_write_transaction(EM_CALSTART, 0x8765);
		}
		else{
			// Measurement calibration range
			// Put new value in RAM
			eepromCalData->measure_cal[offset] = value;
			// Unlock meter cal
			em_write_transaction(EM_ADJSTART, 0x5678);
			// Rewrite the block to the em chip
			cs = em_write_block(EM_MEAS_FIRST, EM_MEAS_LAST, eepromCalData->measure_cal);
			// Write the new checksum
			em_write_transaction(EM_CS2, cs);
			// Lock the meter cal
			em_write_transaction(EM_ADJSTART, 0x8765);
		}
		// Wait for chip to calculate internal checksum
		os_delay_us(50000);
		// Check for errors
		if(! (stat = em_read_transaction(EM_SYSSTATUS))){
			// Calculate a CRC and store it in the last word
			eepromCalData->cal_crc = calcCRC16(eepromCalData, 
				sizeof(eeprom_cal_data_t) - sizeof(uint16_t));	
			// Update the EEPROM with the new register value
			kvstore_put_blob(configHandle, emCalDataKey, eepromCalData);
			INFO("register: write successful\n");
			
		}
		else{
			INFO("register: bad system status: %04X", stat);
			return FALSE;
		}	
	}
	
	// Read a register from the em chip
	// In write mode, this is used to verify the correct value was written to the em chip.
	
	value = em_read_transaction(addr);
	INFO("register: value from chip register address %02X: %04X\n", addr, value);
	ets_sprintf(response_str, "{\"addr\": \"%02X\",\"value\":\"%04X\"}", addr, value);
	MQTT_Publish(&mqttClient, infoTopic, response_str, os_strlen(response_str), 0, 0);
	return TRUE;
}


/**
 * WIFI connect call back
 */
 

LOCAL void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status)
{
	if(status == STATION_GOT_IP){
		MQTT_Connect(&mqttClient);
	}
}

/**
 * Survey complete,
 * publish results
 */


LOCAL void ICACHE_FLASH_ATTR
surveyCompleteCb(void *arg, STATUS status)
{
	struct bss_info *bss = arg;
	
	#define SURVEY_CHUNK_SIZE 256
	
	if(status == OK){
		uint8_t i;
		char *buf = util_zalloc(SURVEY_CHUNK_SIZE);
		bss = bss->next.stqe_next; //ignore first
		for(i = 2; (bss); i++){
			if(2 == i)
				os_sprintf(strlen(buf) + buf,"{\"access_points\":[");
			else
				os_strcat(buf,",");
			os_sprintf(strlen(buf)+ buf, "\"%s\":{\"chan\":\"%d\",\"rssi\":\"%d\"}", bss->ssid, bss->channel, bss->rssi);
			bss = bss->next.stqe_next;
			buf = util_str_realloc(buf, i * SURVEY_CHUNK_SIZE); // Grow buffer
		}
		if(buf[0])
			os_strcat(buf,"]}");
		
		INFO("Survey Results:\r\n", buf);
		INFO(buf);
		MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
		util_free(buf);
	}

}


/**
 * MQTT Connect call back
 */
 
LOCAL void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{
	
	MQTT_Client* client = (MQTT_Client*)args;


	
	INFO("MQTT: Connected\r\n");

	
	
	

	publishConnInfo(client);
	
	// Subscribe to the control topic
	MQTT_Subscribe(client, controlTopic, 0);
	// Subscribe to command topic
	MQTT_Subscribe(client, commandTopic, 0);

}

/**
 * MQTT Disconnect call back
 */
 

LOCAL void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

/**
 * MQTT published call back
 */

LOCAL void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

/**
 * MQTT Data call back
 * Commands are decoded and acted upon here
 */

LOCAL void ICACHE_FLASH_ATTR 
mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, 
const char *data, uint32_t data_len)
{
	char *topicBuf, *dataBuf, *buf;
	uint8_t i;
	struct jsonparse_state state;
	char command[32];
	

	MQTT_Client* client = (MQTT_Client*)args; // Pointer to MQTT control block passed in as args
	
	command[0] = 0; // Zero command string length to prevent stack junk from printing during debug

	// Save local copies of the topic and data
	topicBuf = util_strndup(topic, topic_len);
	dataBuf = util_strndup(data, data_len);
	buf = (char *) os_zalloc(256);
	
	
	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
	
	// Control Message?
	if(!os_strcmp(topicBuf, controlTopic)){
		jsonparse_setup(&state, dataBuf, data_len);
		if (util_parse_json_param(&state, "control", command, sizeof(command)) != 2)
			goto cleanup; /* Control field not present in json object */
		if(!os_strcmp(command, "muster")){
			publishConnInfo(&mqttClient);
		}
	}
	
	// Command Message?
	else if (!os_strcmp(topicBuf, commandTopic)){ // Check for match to command topic
		// Parse command
		jsonparse_setup(&state, dataBuf, data_len);
		if (util_parse_json_param(&state, "command", command, sizeof(command)) != 2)
			goto cleanup; /* Command not present in json object */
				
		for(i = 0; commandElements[i].command[0]; i++){
			command_element *ce = &commandElements[i];
			//INFO("Trying %s\r\n", ce->command);
			if(CP_NONE == ce->type){ // Parameterless command
				if(!os_strcmp(command, ce->command)){
					static uint32_t calc_kwh;
					static int16_t qmean;
					static uint16_t irms, urms, pmean, freq, powerf, pangle, smean, fae;
					static char irms_s[8], urms_s[8], pmean_s[8], qmean_s[8], freq_s[8], powerf_s[8], pangle_s[8], smean_s[8], kwh_s[8];
					switch(i){
						case CMD_QUERY:
							/* Query the em chip */
							
							// Get the meter and measurement data from the EM chip
						
							// RMS Line current
							irms = em_read_transaction(EM_IRMS);
							// unsigned 2.3
							to_fixed_decimal_uint16(irms_s, 3, irms);
							
							// RMS Line voltage
							urms = em_read_transaction(EM_URMS);
							// unsigned 3.2
							to_fixed_decimal_uint16(urms_s, 2, urms);
							
							// Mean active power
							pmean = em_read_transaction(EM_PMEAN);
							// complement 2.3
							ones_compl_to_fixed_decimal_uint16(pmean_s, 3, pmean);
							
							qmean = em_read_transaction(EM_QMEAN);
							// complement 2.3
							twos_compl_to_fixed_decimal_int16(qmean_s, 3, qmean);
							
							freq = em_read_transaction(EM_FREQ);
							// unsigned 2.2
							to_fixed_decimal_uint16(freq_s, 2, freq);
							
							powerf = em_read_transaction(EM_POWERF);
							// signed 1.3
							twos_compl_to_fixed_decimal_int16(powerf_s, 3, powerf);
							
							pangle = em_read_transaction(EM_PANGLE);
							ones_compl_to_fixed_decimal_uint16(pangle_s, 1, pangle);
							// signed 3.1
							
							smean = em_read_transaction(EM_SMEAN);
							// complement 2.3
							ones_compl_to_fixed_decimal_uint16(smean_s, 3, smean);
							
							// Total Forward Active Energy
							// Add what was read to the total.
							fae_total += em_read_transaction(EM_APENERGY);
		
							// KWH is equivalent to  fae_total divided by MC integer pulses 
							// Since the fractional pulses are included in fae_total,
							// we need to account for them.  We do this by multiplying
							// by 1000 so that we get a kwh number which can be represented
							// with 4 decimal digits.
							//
							calc_kwh = ((fae_total * 1000L)/ MC);
							os_sprintf(kwh_s, "%d.%04d",((uint16_t) (calc_kwh / 10000L)), ((uint16_t) (calc_kwh % 10000L)));
							
							/* Encode strings into JSON representation */
							os_sprintf(buf, "{\"irms\":\"%s\",\"urms\":\"%s\",\"pmean\":\"%s\",\"qmean\":\"%s\",\"freq\":\"%s\",\"powerf\":\"%s\",\"pangle\":\"%s\",\"smean\":\"%s\",\"kwh\":\"%s\"}",
							irms_s, urms_s, pmean_s, qmean_s, freq_s, powerf_s, pangle_s, smean_s, kwh_s);
							
							/* Publish data */
							MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
							break;
							
						case CMD_RESET_KWH:
							// Throw away any residual energy
						    em_read_transaction(EM_APENERGY);
							fae_total = 0;
							// Send proof the energy register was zeroed.
							os_sprintf(buf, "{\"resetkwh\":\"%ld\"}", fae_total);
							MQTT_Publish(&mqttClient, statusTopic, buf, os_strlen(buf), 0, 0);
							break;
							
						case CMD_SURVEY:
							// Return WIFI survey data
							wifi_station_scan(NULL, surveyCompleteCb);
							break;
							
						case CMD_RESTART:
							// Restart the firmware
							util_restart();
							break;
							
						default:
							util_assert(FALSE, "Unsupported command: %d", i);
					}
					break;
				}
			}
			
			if((CP_INT == ce->type) || (CP_BOOL == ce->type)){ // Integer/bool parameter
				int arg;
				if(util_parse_command_int(command, ce->command, dataBuf, &arg)){
					switch(i){								
						default:
							util_assert(FALSE, "Unsupported command: %d", i);
						}
					break;
				}				
			}
			if(CP_QSTRING == ce->type){ // Query strings
				char *val = NULL;
				if(util_parse_command_qstring(command, ce->command, dataBuf, &val) != FALSE){
					if((CMD_SSID == i) || (CMD_WIFIPASS == i)){ // Qstring command?
						handleQstringCommand(val, ce);
					}
				}
			}
			if(CP_REGISTER == ce->type){ // EM Chip registers
					if(!strcmp(command, ce->command))
						registerCommand(&state);
			}
			
		} /* END for */
		kvstore_flush(configHandle); // Flush any changes back to the kvs
	} /* END if topic test */
				
	// Free local copies of the topic and data strings
	
cleanup:	
	util_free(topicBuf);
	util_free(dataBuf);
	util_free(buf);
}


/**
 * Place unsigned integer into buffer in big endian format
 */
 
LOCAL void ICACHE_FLASH_ATTR u16_to_bebytes(uint16_t val, uint8_t *bytes)
{
	bytes[0] = (uint8_t) (val >> 8);
	bytes[1] = (uint8_t) (val & 0xFF);
}

/**
 * Extract a big endian unsigned integer from a buffer
 */
  
LOCAL uint16_t ICACHE_FLASH_ATTR bebytes_to_u16(uint8_t *bytes)
{
	int val = ((uint16_t) bytes[0]) << 8;
	val += bytes[1];
	return val;
}


/**
 * System initialization
 * Called once from user_init
 */

LOCAL void ICACHE_FLASH_ATTR sysInit(void)
{

	char *buf = util_zalloc(256); // Working buffer
	uint16_t cs;
	int res;
	uint8_t cal_init_required = FALSE;
	
	
	// I/O system initialization
	gpio_init();
	
	// Uart init
	uart0_init(BIT_RATE_115200);
	
	// I/O Pin initialization

	em_init();

	
	os_delay_us(2000000); // To allow gtkterm to come up
	

	// Read in the config sector from flash
	configHandle = kvstore_open(KVS_DEFAULT_LOC);
	
	
	const char *ssidKey = commandElements[CMD_SSID].command;
	const char *WIFIPassKey = commandElements[CMD_WIFIPASS].command;


	// If calibration data isn't in the kv store, 
	// flag it for initialization here.
	if(!kvstore_exists(configHandle, emCalDataKey)){
		INFO("Initializing calibration data in EEPROM\n");
		cal_init_required = TRUE;
	}
	// The key exists, read the cal data and check the CRC
	else{
		eepromCalData = kvstore_get_blob(configHandle, emCalDataKey);
		if(eepromCalData->cal_crc != calcCRC16(eepromCalData,
			sizeof(eeprom_cal_data_t) - sizeof(uint16_t))){
			// CRC mismatch. Flag it for initialization.
			INFO("CRC error detected in calibration block, re-initializing calibration data");
			os_free(eepromCalData);
			cal_init_required = TRUE;
		}
	}
	// Re-initialize calibration data defaults if
	// it is the first init or there was a CRC error.		
	if(cal_init_required){
		// Allocate the calibration data structure
		eepromCalData = (eeprom_cal_data_t *) os_zalloc(sizeof(eeprom_cal_data_t));
		em_read_block(EM_CAL_FIRST, EM_CAL_LAST, eepromCalData->meter_cal);
		em_read_block(EM_MEAS_FIRST, EM_MEAS_LAST, eepromCalData->measure_cal);
		// Change defaults to suit our configuration
		eepromCalData->meter_cal[PLCONSTH] = (uint16_t) (PLC >> 16);
		eepromCalData->meter_cal[PLCONSTL] = (uint16_t) PLC;
		eepromCalData->meter_cal[MMODE] = MODE_WORD;
		
		// Calculate a CRC and store it in the last word
		eepromCalData->cal_crc = calcCRC16(eepromCalData, 
			sizeof(eeprom_cal_data_t) - sizeof(uint16_t));	
		kvstore_put_blob(configHandle, emCalDataKey, eepromCalData);
	}
	


	// Check for default configuration overrides
	if(!kvstore_exists(configHandle, ssidKey)){ // if no ssid, assume the rest of the defaults need to be set as well
		kvstore_put(configHandle, ssidKey, configInfoBlock.e[WIFISSID].value);
		kvstore_put(configHandle, WIFIPassKey, configInfoBlock.e[WIFIPASS].value);
	

	}
	
	// Write the KVS back out to flash	
	
	kvstore_flush(configHandle);
	
	
	// Write the meter calibration data out to the EM chip
	// Enter meter calibration
	em_write_transaction(EM_CALSTART, 0x5678);
	// Write out the meter cal values
    cs = em_write_block(EM_CAL_FIRST, EM_CAL_LAST, eepromCalData->meter_cal);
    // Write CS1 checksum
    em_write_transaction(EM_CS1, cs);
    // Exit meter calibration
    em_write_transaction(EM_CALSTART, 0x8765); 
    os_delay_us(100000);
	
	
	// Write the measurement calibration data out to the EM chip
	// Enter measurement calibration
	em_write_transaction(EM_ADJSTART, 0x5678);
	// Write out the measurement calibration values
	cs = em_write_block(EM_MEAS_FIRST, EM_MEAS_LAST, eepromCalData->measure_cal);
	// Write the CS2 checksum
	em_write_transaction(EM_CS2, cs);
	// Exit measurement calibration
	em_write_transaction(EM_ADJSTART, 0x8765);
	os_delay_us(100000);
	
	uint16_t readback[11];
	em_read_block(EM_CAL_FIRST, EM_CAL_LAST, readback);
	dump_words(readback, EM_CAL_FIRST, 11);
	em_read_block(EM_MEAS_FIRST, EM_MEAS_LAST, readback);
	dump_words(readback, EM_MEAS_FIRST, 10);
	
	
	
	INFO("Em chip SYSSTATUS: %04X\n", em_read_transaction(EM_SYSSTATUS));
	
	// Get the configurations we need from the KVS and store them in the commandElement data area
	
	commandElements[CMD_SSID].p.sp = kvstore_get_string(configHandle, ssidKey); // Retrieve SSID
	
	commandElements[CMD_WIFIPASS].p.sp = kvstore_get_string(configHandle, WIFIPassKey); // Retrieve WIFI Pass
	
	
	// Initialize MQTT connection 
	
	uint8_t *host = configInfoBlock.e[MQTTHOST].value;
	uint32_t port = (uint32_t) atoi(configInfoBlock.e[MQTTPORT].value);
	
	MQTT_InitConnection(&mqttClient, host, port,
	(uint8_t) atoi(configInfoBlock.e[MQTTSECUR].value));

	MQTT_InitClient(&mqttClient, configInfoBlock.e[MQTTDEVID].value, 
	configInfoBlock.e[MQTTUSER].value, configInfoBlock.e[MQTTPASS].value,
	atoi(configInfoBlock.e[MQTTKPALIV].value), 1);

	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);
	
	// Last will and testament

	os_sprintf(buf, "{\"muster\":{\"connstate\":\"offline\",\"device\":\"%s\"}}", configInfoBlock.e[MQTTDEVPATH].value);
	MQTT_InitLWT(&mqttClient, infoTopic, buf, 0, 0);

	// Subtopics
	commandTopic = util_make_sub_topic(configInfoBlock.e[MQTTDEVPATH].value, "command");
	statusTopic = util_make_sub_topic(configInfoBlock.e[MQTTDEVPATH].value, "status");
	INFO("Command subtopic: %s\r\n", commandTopic);
	INFO("Status subtopic: %s\r\n", statusTopic);
	
	// Attempt WIFI connection
	
	char *wifipass = commandElements[CMD_WIFIPASS].p.sp;
	char *ssid = commandElements[CMD_SSID].p.sp;
	
	INFO("Attempting connection with: %s\r\n", ssid);
	
	// Attempt to connect to AP
	WIFI_Connect(ssid, wifipass, wifiConnectCb);
	
	
	// Free working buffer
	util_free(buf);
	
	
	INFO("\r\nSystem started ...\r\n");

}

/**
 * Called from startup
 */
 
void user_init(void)
{
	sysInit();
}

