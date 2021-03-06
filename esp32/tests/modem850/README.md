FujiNet Atari SIO Virtual Modem
===============================

Copyright (C) 2015 Jussi Salin <salinjus@gmail.com> under GPLv3 license.

Copyright (C) 2019 Joe Honold <mozzwald@gmail.com>

Overview
--------

ESP8266 Requirements: BSP v2.6.3 or higher
ESP32 Requirements: BSP v1.0.3 or higher

This is the first FujiNet test of an Atari 850 R: Device emulator. It will load the Altirra 850 Handler and boot into BobTerm 1.23. By default BobTerm is set for 2400 Baud. Setting the baud rate in BobTerm will also automatically tell the modem to change it's working baud rate (no need to use AT commands). Current tests have shown that rates at or above 9600 baud are unstable.

AT command examples
-------------------

* Change baud rate: AT115200
* Connect to WIFI: ATWIFIMyAccessPoint,MyPassword1234
* Connect by TCP: ATDTsome.bbs.com:23
* Disable telnet command handling: ATNET0
* Get my IP: ATIP
* Make a HTTP GET request: ATGEThttp://host:80/path
* Answer a RING: ATA
* Disconnect: +++ (following a delay of a second)

Note that the key and port are optional parameters. Port defaults to 23. All parameters are case sensitive, the command itself not. You must always connect to an access point before dialing, otherwise you get an error. When you connect to WIFI you get either OK or ERROR after a while, depending on if it succeeded. If you get ERROR the connection might still occur by itself some time later, in case you had a slow AP or slow DHCP server in the network. When dialing, you get either CONNECT when successfully connected or ERROR if the connection couldn't be made. Reasons can be that the remote service is down or the host name is mistyped.

Default Baud rate is defined in the code. 2400 is safe for C64 and 19200 for any PC and Amiga. 115200 for PC's with "new" 16550 UART.  You must always have that default rate on the terminal when powering on. After giving a command for a higher rate nothing is replied, just switch to the new baud rate in your terminal as well. Then you can give next command in the new baud rate. Note that the first command after switching baud rate might fail because the serial port hardware is not fully synchronized yet, so it might be good idea to simply give "AT" command and wait for "ERROR" or "OK" before giving an actual command.

Example communication
---------------------

	OK
	ATWIFIMyAccessPoint,MyPassword
	Connecting to MyAccessPoint/MyPassword
	OK
	ATDTbat.org
	Connecting to bat.org:23
	CONNECT

Hints
-----

The module can also be used for other than telnet connections, for example you can connect to HTTP port, send a HTTP request and receive a response.

