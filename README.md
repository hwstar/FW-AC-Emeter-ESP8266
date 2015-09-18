**FW-AC-Emeter-ESP8266**
==========
This is an implementation of an MQTT AC Power Monitor.
Allows an AC load to be monitored via WIFI. Requires hardware with the Atmel 90E24 energy monitoring chip.
Code is compiled using the toolchain referenced below.

**Features:**

* Monitor Voltage, Current, Apparent Power, Power Factor, Reactive Power, Line Frequency and 


**Hardware Project**

[hardware project](https://github.com/hwstar/HW-AC-Emeter)

**Device Path**

The device path encompasses subtopics command and status. Commands are sent to $devicepath/command (which the nodes subscribes to.) All status messages are
published by the node on $devicepath/status except for the node configuration which is published on /node/info. The device path is set using the patching procedure described later.

**Control Messages**

Control messages are received by all nodes on /node/control. These are meant to be used to interrogate the nodes connected to the network, 
and perform other system-wide control functions.

One control message is currently supported: *muster*. This directs the node to re-send the node configuration information to /node/info. See the power on message below for further details


**Command Messages**

Command messages are sent using JSON encoding as follows:

{"command":"command from table below"} For commands without a parameter

{"command":"$COMMAND","param","$PARAM"} For commands with a parameter

Because of limitations with the Espressif JSON parser library, all numbers should be sent as text fields 
(i.e. quoted)

MQTT commands supported:

|Command| Description |
|--------| ----------- |
|query	 | Returns voltage, current, frequency, power, and energy in a JSON encoded string|
|register| Reads or writes a 90E24 register (used in calibration, see source code).
|resetkwh| Resets the kilowatt hours count.
|survey	 | Returns WIFI survey information as seen by the node|
|ssid    | Query or set SSID|
|restart | Restart system|
|wifipass| Query or set WIFI Password|
|mqttdevpath| Query or set MQTT device path

Notes:
* $ indicates a variable. e.g.: $COMMAND would be one of the commands in the table above.
* Sending an ssid, or wifi command without "parameter":"$PARAM" will return the current value.


**Power on Message**

After booting, the node posts a JSON encoded "muster" message to /node/info with the following data:

|Field		| Description|
|-----      | -----------|
|connstate  | Connection state (online)
|device		| A device path (e.g. /home/lab/acpowermon)|
|ip4		| The IP address assigned to the node|
|schema		| A schema name of hwstar_acpowermon (vendor_product)|
|ssid       | SSID utilized|


The schema may be used to design a database of supported commands for each device.

Here is an example:

{"muster":{"connstate":"online","device":"/home/lab/acpowermon","ip4":"$IP","schema":"hwstar_acpowermon","ssid":"$SSID"}}

**Last Will and Testament**

The following will be published to /node/info if the node is not heard from by the MQTT broker:

{"muster":{"connstate":"offline","device":"$DEVICE"}}

Where $DEVICE is the configured device path

**Configuration Patcher**

WIFI and MQTT Configuration can optionally not be stored in the source files. It can be patched in using a custom Python utility which is available on my github account as
a separate project:

[config patcher](https://github.com/hwstar/ESP8266-MQTT-config-patcher)

Post patching allows the configuration to be changed without having sensitive information in the source files.

NB:Post-patching is optional. You can just edit the user_main.c source file and hardcode the configuration. 

**Toolchain**

Requires the ESP8266 toolchain be installed on the Linux system per the instructions available here:

[toolchain](https://github.com/pfalcon/esp-open-sdk)

Toolchain should be installed in the /opt directory. Other directories will require Makefile modifications.

NB:Current Makefile supports Linux build hosts only at this time. If someone wants to submit a working Makefile for Windows, I'd be happy to add it to the repository.

**LICENSE - "MIT License"**

Copyright (c) 2015 Stephen Rodgers 
Copyright (c) 2014-2015 Tuan PM, https://twitter.com/TuanPMT

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.



