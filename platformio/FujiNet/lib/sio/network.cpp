#include "driver/timer.h"
#include "esp32-hal-psram.h"

#include "../../include/debug.h"
#include "../hardware/fnSystem.h"
#include "../hardware/fnWiFi.h"

#include "network.h"

#include "networkProtocol.h"
#include "networkProtocolTCP.h"
#include "networkProtocolUDP.h"
#include "networkProtocolHTTP.h"
#include "networkProtocolTNFS.h"
#include "networkProtocolFTP.h"

volatile bool interruptRateLimit = true;
//hw_timer_t *rateTimer = NULL;
esp_timer_handle_t rateTimerHandle = nullptr; // Used a different name just to be clear
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Latch the rate limiting flag
// The esp_timer_* functions don't mention requiring the callback being in IRAM, so removing that
//void IRAM_ATTR onTimer()
void onTimer(void *info)
{
    portENTER_CRITICAL_ISR(&timerMux);
    interruptRateLimit = true;
    portEXIT_CRITICAL_ISR(&timerMux);
}

/**
 * Allocate input/output buffers
 */
bool sioNetwork::allocate_buffers()
{
    // NOTE: ps_calloc() results in heap corruption, at least in Arduino-ESP. 
#ifdef BOARD_HAS_PSRAM
/*
    rx_buf = (byte *)ps_calloc(INPUT_BUFFER_SIZE, 1);
    tx_buf = (byte *)ps_calloc(OUTPUT_BUFFER_SIZE, 1);
    sp_buf = (byte *)ps_calloc(SPECIAL_BUFFER_SIZE, 1);
*/
    rx_buf = (byte *)ps_malloc(INPUT_BUFFER_SIZE);
    tx_buf = (byte *)ps_malloc(OUTPUT_BUFFER_SIZE);
    sp_buf = (byte *)ps_malloc(SPECIAL_BUFFER_SIZE);

#else
    rx_buf = (byte *)calloc(1, INPUT_BUFFER_SIZE);
    tx_buf = (byte *)calloc(1, OUTPUT_BUFFER_SIZE);
    sp_buf = (byte *)calloc(1, SPECIAL_BUFFER_SIZE);
#endif
    if ((rx_buf == nullptr) || (tx_buf == nullptr) || (sp_buf == nullptr))
        return false;

    memset(rx_buf, 0, INPUT_BUFFER_SIZE);
    memset(tx_buf, 0, OUTPUT_BUFFER_SIZE);
    memset(sp_buf, 0, SPECIAL_BUFFER_SIZE);

    HEAP_CHECK("sioNetwork::allocate_buffers");
    return true;
}

/**
 * Deallocate input/output buffers
 */
void sioNetwork::deallocate_buffers()
{
    if (rx_buf != nullptr)
        free(rx_buf);
    if (tx_buf != nullptr)
        free(tx_buf);
    if (sp_buf != nullptr)
        free(sp_buf);
}

bool sioNetwork::open_protocol()
{
    if (urlParser->scheme == "TCP")
    {
        protocol = new networkProtocolTCP();
        return true;
    }
    else if (urlParser->scheme == "UDP")
    {
        protocol = new networkProtocolUDP();
        if (protocol != nullptr)
            protocol->set_saved_rx_buffer(rx_buf, &rx_buf_len);
        return true;
    }
    else if (urlParser->scheme == "HTTP")
    {
        protocol = new networkProtocolHTTP();
        return true;
    }
    else if (urlParser->scheme == "HTTPS")
    {
        protocol = new networkProtocolHTTP();
        return true;
    }
    else if (urlParser->scheme == "TNFS")
    {
        protocol = new networkProtocolTNFS();
        return true;
    }
    else if (urlParser->scheme == "FTP")
    {
        protocol = new networkProtocolFTP();
        return true;
    }
    else
    {
        return false;
    }
}

bool sioNetwork::isValidURL(EdUrlParser *url)
{
    if (url->scheme == "")
        return false;
    else if ((url->path == "") && (url->port == ""))
        return false;
    else
        return true;
}

bool sioNetwork::parseURL()
{
    if (urlParser != nullptr)
        delete urlParser;

    Debug_printf("parseURL: %s\r\n", filespecBuf);

    // Preprocess URL
    if (cmdFrame.comnd == 0x20)
    {
        for (int i = 0; i < sizeof(filespecBuf); i++)
            if ((filespecBuf[i] > 0x7F))
                filespecBuf[i] = 0x00;
    }
    else if (cmdFrame.aux1 != 6)
    {
        for (int i = 0; i < sizeof(filespecBuf); i++)
            if ((filespecBuf[i] > 0x7F) || (filespecBuf[i] == ',') || (filespecBuf[i] == '*'))
                filespecBuf[i] = 0x00;
    }
    else
    {
        for (int i = 0; i < sizeof(filespecBuf); i++)
            if ((filespecBuf[i] > 0x7F))
                filespecBuf[i] = 0x00;
    }

    if (filespecBuf[strlen(filespecBuf) - 1] == '.')
        filespecBuf[strlen(filespecBuf) - 1] = 0x00;

    if (prefix.length() > 0)
        deviceSpec = prefix + string(filespecBuf).substr(string(filespecBuf).find(":") + 1);
    else
        deviceSpec = string(filespecBuf).substr(string(filespecBuf).find(":") + 1);

    urlParser = EdUrlParser::parseUrl(deviceSpec);

    Debug_printf("parseURL isValidURL: %s\n", deviceSpec.c_str());

    return (isValidURL(urlParser));
}

void sioNetwork::sio_open()
{
    Debug_println("sioNetwork::sio_open()");

    aux1 = cmdFrame.aux1;
    aux2 = cmdFrame.aux2;

    sio_ack();

    if (protocol != nullptr)
    {
        delete protocol;
        deallocate_buffers();
    }

    memset(&filespecBuf, 0, sizeof(filespecBuf));
    memset(&status_buf.rawData, 0, sizeof(status_buf.rawData));

    sio_to_peripheral((byte *)&filespecBuf, sizeof(filespecBuf));

    if (parseURL() == false)
    {
        Debug_printf("Invalid devicespec %s\n",filespecBuf);
        status_buf.error = 165;
        sio_error();
        return;
    }

    // deviceSpec set by parseURL.
    Debug_printf("Open: %s\n", deviceSpec.c_str());

    if (allocate_buffers() == false)
    {
        Debug_printf("Could not allocate memory for buffers\n");
        status_buf.error = 129;
        sio_error();
        return;
    }

    if (open_protocol() == false)
    {
        Debug_printf("Could not open protocol.\n");
        status_buf.error = 128;
        sio_error();
        return;
    }

    if (!protocol->open(urlParser, &cmdFrame))
    {
        Debug_printf("Protocol unable to make connection.");
        protocol->close();
        delete protocol;
        protocol = nullptr;
        status_buf.error = 170;
        sio_error();
        return;
    }

    if (rateTimerHandle != nullptr)
    {
        Debug_println("Deleting rateTimer");
        esp_timer_stop(rateTimerHandle);
        esp_timer_delete(rateTimerHandle);
        rateTimerHandle = nullptr;
    }

    esp_timer_create_args_t tcfg;
    tcfg.arg = nullptr;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &rateTimerHandle);
    esp_timer_start_periodic(rateTimerHandle, 100000); // 100ms

    sio_complete();
}

void sioNetwork::sio_close()
{
    Debug_println("sioNetwork::sio_close()\n");
    sio_ack();

    status_buf.error = 0; // clear error

    if (protocol == nullptr)
    {
        sio_complete();
        return;
    }

    if (protocol->close())
        sio_complete();
    else
        sio_error();

    delete protocol;
    protocol = nullptr;

    deallocate_buffers();
}

void sioNetwork::sio_read()
{
    Debug_println("sioNetwork::sio_read()");
    if (rx_buf == nullptr)
    {
        Debug_println("Unallocated read buffer!");
        sio_nak();
        return;
    }
    sio_ack();

    // Clean out RX buffer.
    memset(rx_buf, 0, INPUT_BUFFER_SIZE);

    Debug_printf("Read %d bytes\n", cmdFrame.aux2 * 256 + cmdFrame.aux1);

    if (protocol == nullptr)
    {
        err = true;
        status_buf.error = 128;
    }
    else
    {
        rx_buf_len = cmdFrame.aux2 * 256 + cmdFrame.aux1;
        err = protocol->read(rx_buf, cmdFrame.aux2 * 256 + cmdFrame.aux1);

        // Convert CR and/or LF to ATASCII EOL
        // 1 = CR, 2 = LF, 3 = CR/LF
        if (aux2 > 0)
        {
            Debug_printf("sio_read conversion rx_buf_len = %hu\n",rx_buf_len);
            for (int i = 0; i < rx_buf_len; i++)
            {
                switch (aux2 & 3)
                {
                case 1:
                    if (rx_buf[i] == 0x0D)
                        rx_buf[i] = 0x9B;
                    break;
                case 2:
                    if (rx_buf[i] == 0x0A)
                        rx_buf[i] = 0x9B;
                    break;
                case 3:
                    if ((rx_buf[i] == 0x0D) && (rx_buf[i + 1] == 0x0A))
                    {
                        memmove(&rx_buf[i - 1], &rx_buf[i], rx_buf_len);
                        rx_buf[i] = 0x9B;
                        rx_buf_len--;
                    }
                    break;
                }
            }
        }
    }
    sio_to_computer(rx_buf, rx_buf_len, err);
}

void sioNetwork::sio_write()
{
    Debug_printf("sioNetwork::sio_write() %d bytes\n", cmdFrame.aux2 * 256 + cmdFrame.aux1);
    sio_ack();

    memset(tx_buf, 0, OUTPUT_BUFFER_SIZE);

    if (protocol == nullptr)
    {
        Debug_printf("Not connected\n");
        err = true;
        status_buf.error = 128;
        sio_error();
    }
    else
    {
        ck = sio_to_peripheral(tx_buf, sio_get_aux());
        tx_buf_len = cmdFrame.aux2 * 256 + cmdFrame.aux1;

        // Handle EOL to CR/LF translation.
        // 1 = CR, 2 = LF, 3 = CR/LF

        if (aux2 > 0)
        {
            for (int i = 0; i < tx_buf_len; i++)
            {
                switch (aux2 & 3)
                {
                case 1:
                    if (tx_buf[i] == 0x9B)
                        tx_buf[i] = 0x0D;
                    break;
                case 2:
                    if (tx_buf[i] == 0x9B)
                        tx_buf[i] = 0x0A;
                    break;
                case 3:
                    if (tx_buf[i] == 0x9B)
                    {
                        memmove(&tx_buf[i + 1], &tx_buf[i], tx_buf_len);
                        tx_buf[i] = 0x0D;
                        tx_buf[i + 1] = 0x0A;
                        tx_buf_len++;
                    }
                    break;
                }
            }
        }

        if (!protocol->write(tx_buf, tx_buf_len))
        {
            sio_complete();
        }
        else
        {
            sio_error();
        }
    }
}

void sioNetwork::sio_status_local()
{
    uint8_t ipAddress[4];
    uint8_t ipNetmask[4];
    uint8_t ipGateway[4];
    uint8_t ipDNS[4];

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (cmdFrame.aux2)
    {
    case 1: // IP Address
        memcpy(status_buf.rawData, &ipAddress, sizeof(ipAddress));
        break;
    case 2: // Netmask
        memcpy(status_buf.rawData, &ipNetmask, sizeof(ipNetmask));
        break;
    case 3: // Gateway
        memcpy(status_buf.rawData, &ipGateway, sizeof(ipGateway));
        break;
    case 4: // DNS
        memcpy(status_buf.rawData, &ipDNS, sizeof(ipDNS));
        break;
    default:
        status_buf.rawData[0] =
            status_buf.rawData[1] = 0;
        status_buf.rawData[2] = fnWiFi.connected() ? 1 : 0;
        status_buf.rawData[3] = 1;
        break;
    }
    Debug_printf("Output: %u.%u.%u.%u\n", status_buf.rawData[0], status_buf.rawData[1], status_buf.rawData[2], status_buf.rawData[3]);
}

void sioNetwork::sio_status()
{
    sio_ack();
    Debug_printf("sioNetwork::sio_status()\n");
    if (!protocol)
    {
        status_buf.rawData[0] =
            status_buf.rawData[1] = 0;

        status_buf.rawData[2] = fnWiFi.connected() ? 1 : 0;
        err = false;
        // sio_status_local();
    }
    else
    {
        err = protocol->status(status_buf.rawData);
    }
    Debug_printf("Status bytes: %02x %02x %02x %02x\n", status_buf.rawData[0], status_buf.rawData[1], status_buf.rawData[2], status_buf.rawData[3]);
    sio_to_computer(status_buf.rawData, 4, err);
}

// Process a SPECIAL sio command (not R,W,O,C,S)
void sioNetwork::sio_special()
{
    Debug_printf("sioNetwork::sio_special\n");
    err = false;
    if (cmdFrame.comnd == 0x2C) // CHDIR
    {
        string path;
        sio_ack();
        sio_to_peripheral((byte *)filespecBuf, 256);

        for (int i = 0; i < 256; i++)
            if (filespecBuf[i] == 0x9B)
                filespecBuf[i] = 0x00;

        path = filespecBuf;
        path = path.substr(path.find_first_of(":") + 1);

        if (path.empty())
        {
            prefix = "";
            initial_prefix = "";
        }
        else if (path.find(":") != string::npos)
        {
            prefix = path;
            initial_prefix = prefix;
        }
        else if (path[0] == '/')
        {
            prefix = initial_prefix;

            if (prefix[prefix.length() - 1] == '/')
                path = path.substr(1);
            prefix += path;
        }
        else if (path == "..")
        {
            vector<int> pathLocations;
            for (int i = 0; i < prefix.size(); i++)
            {
                if (prefix[i] == '/')
                {
                    pathLocations.push_back(i);
                }
            }

            if (prefix[prefix.size() - 1] == '/')
            {
                // Get rid of last path segment.
                pathLocations.pop_back();
            }

            // truncate to that location.
            prefix = prefix.substr(0, pathLocations.back() + 1);
        }
        else
        {
            if (prefix[prefix.length() - 1] != '/')
                prefix += "/";
            prefix += path;
        }

        Debug_printf("NCD: %s\r\n", prefix.c_str());

        sio_complete();
    }
    else if (cmdFrame.comnd == 0x20) // RENAME
    {
        sio_ack();
        sio_to_peripheral((byte *)&filespecBuf, sizeof(filespecBuf));

        for (int i = 0; i < 256; i++)
            if (filespecBuf[i] == 0x9B)
                filespecBuf[i] = 0x00;

        if (parseURL() == false)
        {
            Debug_printf("Invalid devicespec\n");
            status_buf.error = 165;
            sio_error();
            return;
        }

        // deviceSpec set by parseURL.
        Debug_printf("Rename: %s\n", deviceSpec.c_str());

        if (open_protocol() == false)
        {
            Debug_printf("Could not open protocol.\n");
            status_buf.error = 128;
            sio_error();
            return;
        }

        if (!protocol->rename(urlParser, &cmdFrame))
        {
            Debug_printf("Protocol unable to perform rename.");
            protocol->close();
            delete protocol;
            protocol = nullptr;
            status_buf.error = 170;
            sio_error();
            return;
        }

        sio_complete();
        protocol->close();
        delete protocol;
        protocol = nullptr;
    }
    else if (cmdFrame.comnd == 0x21) // DELETE
    {
        sio_ack();
        sio_to_peripheral((byte *)&filespecBuf, sizeof(filespecBuf));

        for (int i = 0; i < 256; i++)
            if (filespecBuf[i] == 0x9B)
                filespecBuf[i] = 0x00;

        if (parseURL() == false)
        {
            Debug_printf("Invalid devicespec\n");
            status_buf.error = 165;
            sio_error();
            return;
        }

        // deviceSpec set by parseURL.
        Debug_printf("Delete: %s\n", deviceSpec.c_str());

        if (open_protocol() == false)
        {
            Debug_printf("Could not open protocol.\n");
            status_buf.error = 128;
            sio_error();
            return;
        }

        if (!protocol->del(urlParser, &cmdFrame))
        {
            Debug_printf("Protocol unable to perform delete.");
            protocol->close();
            delete protocol;
            protocol = nullptr;
            status_buf.error = 170;
            sio_error();
            return;
        }

        sio_complete();
        protocol->close();
        delete protocol;
        protocol=nullptr;
    }
    else if (cmdFrame.comnd == 0x2A) // MKDIR
    {
        sio_ack();
        sio_to_peripheral((byte *)&filespecBuf, sizeof(filespecBuf));

        for (int i = 0; i < 256; i++)
            if (filespecBuf[i] == 0x9B)
                filespecBuf[i] = 0x00;

        if (parseURL() == false)
        {
            Debug_printf("Invalid devicespec %s \n",filespecBuf);
            status_buf.error = 165;
            sio_error();
            return;
        }

        // deviceSpec set by parseURL.
        Debug_printf("Mkdir: %s\n", deviceSpec.c_str());

        if (open_protocol() == false)
        {
            Debug_printf("Could not open protocol.\n");
            status_buf.error = 128;
            sio_error();
            return;
        }

        if (!protocol->mkdir(urlParser, &cmdFrame))
        {
            Debug_printf("Protocol unable to perform mkdir.");
            protocol->close();
            delete protocol;
            protocol = nullptr;
            status_buf.error = 170;
            sio_error();
            return;
        }

        sio_complete();
        protocol->close();
        delete protocol;
        protocol=nullptr;
    }
    else if (cmdFrame.comnd == 0x2B) // RMDIR
    {
        sio_ack();
        sio_to_peripheral((byte *)&filespecBuf, sizeof(filespecBuf));

        for (int i = 0; i < 256; i++)
            if (filespecBuf[i] == 0x9B)
                filespecBuf[i] = 0x00;

        if (parseURL() == false)
        {
            Debug_printf("Invalid devicespec %s \n",filespecBuf);
            status_buf.error = 165;
            sio_error();
            return;
        }

        // deviceSpec set by parseURL.
        Debug_printf("Rmdir: %s\n", deviceSpec.c_str());

        if (open_protocol() == false)
        {
            Debug_printf("Could not open protocol.\n");
            status_buf.error = 128;
            sio_error();
            return;
        }

        if (!protocol->rmdir(urlParser, &cmdFrame))
        {
            Debug_printf("Protocol unable to perform rmdir.");
            protocol->close();
            delete protocol;
            protocol = nullptr;
            status_buf.error = 170;
            sio_error();
            return;
        }

        sio_complete();
        protocol->close();
        delete protocol;
        protocol=nullptr;
    }
    else if (cmdFrame.comnd == 0xFF) // Get DSTATS for protocol command.
    {
        byte ret = 0xFF;
        Debug_printf("INQ\n");
        sio_ack();
        if (protocol == nullptr)
        {
            if (sio_special_supported_00_command(cmdFrame.aux1))
            {
                ret = 0x00;
            }
            else if (sio_special_supported_40_command(cmdFrame.aux1))
            {
                ret = 0x40;
            }
            else if (sio_special_supported_80_command(cmdFrame.aux1))
            {
                ret = 0x80;
            }
            Debug_printf("Local Ret %d\n", ret);
        }
        else
        {
            if (protocol->special_supported_00_command(cmdFrame.aux1))
            {
                ret = 0x00;
            }
            else if (protocol->special_supported_40_command(cmdFrame.aux1))
            {
                ret = 0x40;
            }
            else if (protocol->special_supported_80_command(cmdFrame.aux1))
            {
                ret = 0x80;
            }
            Debug_printf("Protocol Ret %d\n", ret);
        }
        sio_to_computer(&ret, 1, false);
    }
    else if (sio_special_supported_00_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_00();
    }
    else if (sio_special_supported_40_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_40();
    }
    else if (sio_special_supported_80_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_80();
    }
    else if (protocol->special_supported_00_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_protocol_00();
    }
    else if (protocol->special_supported_40_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_protocol_40();
    }
    else if (protocol->special_supported_80_command(cmdFrame.comnd))
    {
        sio_ack();
        sio_special_protocol_80();
    }

    if (err == true)
    {
        sio_nak();
    }

    // sio_completes() happen in sio_special_XX()
}

// supported global network device commands that have no payload
bool sioNetwork::sio_special_supported_00_command(unsigned char c)
{
    switch (c)
    {
    case 0x10: // Acknowledge interrupt
        return true;
    }
    return false;
}

// supported global network device commands that go Peripheral->Computer
bool sioNetwork::sio_special_supported_40_command(unsigned char c)
{
    switch (c)
    {
    case 0x30: // ?DIR
        return true;
    }
    return false;
}

// supported global network device commands that go Computer->Peripheral
bool sioNetwork::sio_special_supported_80_command(unsigned char c)
{
    switch (c)
    {
    case 0x20: // RENAME
        return true;
    case 0x21: // DELETE
        return true;
    case 0x2C: // CHDIR
        return true;
    case 0x2A: // MKDIR
        return true;
    case 0x2B: // RMDIR
        return true;
    case 0xFE: // Set prefix
        return true;
    }
    return false;
}

// For global commands with no payload
void sioNetwork::sio_special_00()
{
    switch (cmdFrame.comnd)
    {
    case 0x10: // Ack interrupt
        sio_complete();
        interruptRateLimit = true;
        break;
    }
}

// For global commands with Peripheral->Computer payload
void sioNetwork::sio_special_40()
{
    char buf[256];

    switch (cmdFrame.comnd)
    {
    case 0x30: // ?DIR
        strcpy((char *)buf, prefix.c_str());
        break;
    }
    Debug_printf("Read buf: %s\n", buf);
    sio_to_computer((byte *)buf, 256, err); // size of DVSTAT
}

// For global commands with Computer->Peripheral payload
void sioNetwork::sio_special_80()
{
    err = sio_to_peripheral(sp_buf, 256);

    for (int i = 0; i < 256; i++)
        if (sp_buf[i] == 0x9b)
            sp_buf[i] = 0x00;

    if (err == true)
        sio_error();
    else
        sio_complete();
}

// For commands with no payload.
void sioNetwork::sio_special_protocol_00()
{
    if (!protocol->special(sp_buf, 0, &cmdFrame))
        sio_complete();
    else
        sio_error();
}

// For commands with Peripheral->Computer payload
void sioNetwork::sio_special_protocol_40()
{
    err = protocol->special(sp_buf, 4, &cmdFrame);
    sio_to_computer(sp_buf, sp_buf_len, err);
}

// For commands with Computer->Peripheral payload
void sioNetwork::sio_special_protocol_80()
{
    sio_to_peripheral(sp_buf, 256);

    for (int i = 0; i < 256; i++)
        if (sp_buf[i] == 0x9b)
            sp_buf[i] = 0x00;

    err = protocol->special(sp_buf, sp_buf_len, &cmdFrame);
    if (err == true)
        sio_error();
    else
        sio_complete();
}

void sioNetwork::sio_assert_interrupts()
{
    if (protocol != nullptr)
    {
        protocol->status(status_buf.rawData); // Prime the status buffer
        if (((status_buf.rx_buf_len > 0) || (status_buf.connection_status != previous_connection_status)) && (interruptRateLimit == true))
        {
            //Debug_println("sioNetwork::sio_assert_interrupts toggling PROC pin");
            fnSystem.digital_write(PIN_PROC, DIGI_LOW);
            fnSystem.delay_microseconds(50);
            fnSystem.digital_write(PIN_PROC, DIGI_HIGH);

            // The timer_* (as opposed to esp_timer_*) functions allow for much more granular control, including
            // pausing and restarting the timer.  Would be nice here, but it's also a lot more work to use...
            portENTER_CRITICAL(&timerMux);
            interruptRateLimit = false;
            portEXIT_CRITICAL(&timerMux);
        }
        previous_connection_status = status_buf.connection_status;
    }
}

void sioNetwork::sio_process()
{
    Debug_printf("sioNetwork::sio_process 0x%02hx '%c': 0x%02hx, 0x%02hx\n",
                 cmdFrame.comnd, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);

    switch (cmdFrame.comnd)
    {
    case 0x3f:
        sio_ack();
        sio_high_speed();
        break;
    case 'O':
        sio_open();
        break;
    case 'C':
        sio_close();
        break;
    case 'R':
        sio_read();
        break;
    case 'W':
        sio_write();
        break;
    case 'S':
        sio_status();
        break;
    default:
        sio_special();
        break;
    }
}
