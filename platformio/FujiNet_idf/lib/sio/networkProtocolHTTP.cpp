#include "networkProtocolHTTP.h"
#include "../../include/debug.h"

networkProtocolHTTP::networkProtocolHTTP()
{
    //c = nullptr;
    httpState = DATA;
    requestStarted = false;
}

networkProtocolHTTP::~networkProtocolHTTP()
{
    for (int i = 0; i < headerCollectionIndex; i++)
        free(headerCollection[i]);

    // client.end();
    client.close();
}

bool networkProtocolHTTP::startConnection(uint8_t *buf, unsigned short len)
{
    bool ret = false;

    fnSystem.delay(1);

#ifdef DEBUG
    Debug_printf("startConnection()\n");
#endif

    switch (openMode)
    {
    case DIR:
        //client.addHeader("Depth", "1");
        //resultCode = client.sendRequest("PROPFIND", "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n</D:prop>\r\n</D:propfind>\r\n");
        resultCode = client.PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n</D:prop>\r\n</D:propfind>\r\n");
        ret = true;
        break;
    case GET:
        //client.collectHeaders((const char **)headerCollection, (const size_t)headerCollectionIndex);
        client.collect_headers((const char **)headerCollection, headerCollectionIndex);

        resultCode = client.GET();

        headerIndex = 0;
        //numHeaders = client.headers();
        numHeaders = client.get_header_count();
        ret = true;
        break;
    case POST:
        //resultCode = client.POST(buf, len);
        resultCode = client.POST((const char *)buf, len);
        //numHeaders = client.headers();
        numHeaders = client.get_header_count();
        headerIndex = 0;
        ret = true;
        break;
    case PUT:
        // Don't start connection here.
        ret = true;
        break;
    default:
        ret = false;
        break;
    }

    requestStarted = ret;

    /*
    if (requestStarted)
        c = client.getStreamPtr();
    */

#ifdef DEBUG
    Debug_printf("Result code: %d\n", resultCode);
#endif

    return ret;
}

bool networkProtocolHTTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->aux1)
    {
    case 6:
        openMode = DIR;
        break;
    case 4:
    case 12:
        openMode = GET;
        break;
    case 13:
        openMode = POST;
        break;
    case 8:
    case 14:
        openMode = PUT;
        break;
    }

    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    openedUrlParser = urlParser;
    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();

    if (openMode == PUT)
    {
        fpPUT = fnSystem.make_tempfile(nPUT);
    }

    return client.begin(openedUrl.c_str());
}

bool networkProtocolHTTP::close()
{
    size_t putPos;
    uint8_t *putBuf;

    // Close and Remove temporary PUT file, if needed.
    if (openMode == PUT)
    {
        putPos = ftell(fpPUT);
        Debug_printf("putPos is %d", putPos);
        putBuf = (uint8_t *)malloc(putPos);
        fseek(fpPUT, 0, SEEK_SET);
        fread(putBuf, 1, putPos, fpPUT);
        Debug_printf("\n");
        client.PUT((const char *)putBuf, putPos);
        fclose(fpPUT);
        fnSystem.delete_tempfile(nPUT);
        free(putBuf);
    }

    //client.end();
    client.close();
    return true;
}

bool networkProtocolHTTP::read(uint8_t *rx_buf, unsigned short len)
{
    if (!requestStarted)
    {
        if (!startConnection(rx_buf, len))
            return true;
    }

    switch (httpState)
    {
    case CMD:
        // Do nothing.
        break;
    case DATA:
        if (openMode == DIR)
        {
            /*
            if (c == nullptr)
                return true;

            if (c->readBytes(rx_buf, len) != len)
                return true;
            */
            if (client.read(rx_buf, len) != len)
                return true;
            // massage data slightly.
            for (int z = 0; z < len; z++)
            {
                if (rx_buf[z] == 0x0D)
                    rx_buf[z] = 0x20;
                else if (rx_buf[z] == 0x0A)
                    rx_buf[z] = 0x9B;
            }
        }
        else
        {
            /*
            if (c == nullptr)
                return true;

            if (c->readBytes(rx_buf, len) != len)
                return true;
            */
            if (client.read(rx_buf, len) != len)
                return true;
            break;
        case HEADERS:
            if (headerIndex < numHeaders)
            {
                //strncpy((char *)rx_buf, client.header(headerIndex++).c_str(), len);
                client.get_header(headerIndex++, (char *)rx_buf, len);
            }
            else
                return true;
        }
        break;
    case COLLECT_HEADERS:
        // collect headers is write only. Return error.
        return true;
    case CA:
        // CA is write only. Return error.
        return true;
    }

    return false;
}

bool networkProtocolHTTP::write(uint8_t *tx_buf, unsigned short len)
{
    int b;
    string headerKey;
    string headerValue;
    char tmpKey[256];
    char tmpValue[256];
    char *p;

    switch (httpState)
    {
    case DATA:
        if (openMode == PUT)
        {
            if (!fpPUT)
                return true;

            fwrite(tx_buf, 1, len, fpPUT);
        }
        else
        {
            if (!requestStarted)
            {
                if (!startConnection(tx_buf, len))
                    return true;
            }
        }
        break;
    case CMD:
        // Do nothing
        break;
    case HEADERS:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        p = strtok((char *)tx_buf, ":");

        strcpy(tmpKey, p);
        p = strtok(NULL, "");
        strcpy(tmpValue, p);
        /*
        headerKey = String(tmpKey);
        headerValue = String(tmpValue);
        client.addHeader(headerKey, headerValue);
        */
        client.set_header(tmpKey, tmpValue);
#ifdef DEBUG
        Debug_printf("headerKey: %s\n", headerKey.c_str());
        Debug_printf("headerValue: %s\n", headerValue.c_str());
#endif
        break;
    case COLLECT_HEADERS:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        headerCollection[headerCollectionIndex++] = strndup((const char *)tx_buf, len);
        break;
    case CA:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        if (strlen(cert) + strlen((const char *)tx_buf) < sizeof(cert))
        {
            strcat(cert, (const char *)tx_buf);
            strcat(cert, "\n");
#ifdef DEBUG
            Debug_printf("(%d) %s\n", strlen(cert), cert);
#endif
        }
        break;
    }

    return false;
}

bool networkProtocolHTTP::status(uint8_t *status_buf)
{
    int a; // available bytes

    status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0;

    switch (httpState)
    {
    case CMD:
        status_buf[0] = 0;
        status_buf[1] = 0;
        status_buf[2] = 1;
        status_buf[3] = 0;
        break;
    case DATA:
        if (openMode == PUT)
        {
            status_buf[0] = 0;
            status_buf[1] = 0;
            status_buf[2] = 1;
            status_buf[3] = 0;
        }
        else
        {
            if (requestStarted == false)
            {
                if (!startConnection(status_buf, 4))
                    return true;
            }

            /*
            if (c == nullptr)
                return true;

            // Limit to reporting max of 65535 bytes available.
            a = (c->available() > 65535 ? 65535 : c->available());
            */
            a = client.available();
            a = a > 0xFFFF ? 0xFFFF : a;

            status_buf[0] = a & 0xFF;
            status_buf[1] = a >> 8;
            status_buf[2] = resultCode & 0xFF;
            status_buf[3] = resultCode >> 8;
            assertInterrupt = a > 0;
        }
        break;
    case HEADERS:
        if (headerIndex < numHeaders)
        {
            //status_buf[0] = client.header(headerIndex).length() & 0xFF;
            //status_buf[1] = client.header(headerIndex).length() >> 8;
            uint16_t ha = client.get_header(headerIndex).length();
            status_buf[0] = ha & 0xFF;
            status_buf[1] = ha >> 8;
            status_buf[2] = resultCode & 0xFF;
            status_buf[3] = resultCode >> 8;
        }
        break;
    case COLLECT_HEADERS:
        status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0xFF;
        break;
    case CA:
        status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0xFE;
        break;
    }

    return false;
}

bool networkProtocolHTTP::special_supported_00_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 'G': // toggle collect headers
        return true;
    case 'H': // toggle headers
        return true;
    case 'I': // Get Certificate
        return true;
    default:
        return false;
    }

    return false;
}

void networkProtocolHTTP::special_header_toggle(unsigned char a)
{
    httpState = (a == 1 ? HEADERS : DATA);
}

void networkProtocolHTTP::special_collect_headers_toggle(unsigned char a)
{
    httpState = (a == 1 ? COLLECT_HEADERS : DATA);
}

void networkProtocolHTTP::special_ca_toggle(unsigned char a)
{
    httpState = (a == 1 ? CA : DATA);
    switch (a)
    {
    case 0:
        if (strlen(cert) > 0)
        {
            //client.end();
            client.close();
            //client.begin(openedUrl.c_str(), cert);
            client.begin(openedUrl);
        }
        break;
    case 1:
        memset(cert, 0, sizeof(cert));
        break;
    }
    if (a > 0)
    {
        memset(cert, 0, sizeof(cert));
    }
}

bool networkProtocolHTTP::isConnected()
{
    //
    //if (c != nullptr)
    //    return c->connected();
    //else
    //    return false;
    //
    return client.available() > 0;
}

bool networkProtocolHTTP::del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    //return client.sendRequest("DELETE");
    return client.DELETE();
}

bool networkProtocolHTTP::mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    //return client.sendRequest("MKCOL");
    return client.MKCOL();
}

bool networkProtocolHTTP::rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    //return client.sendRequest("DELETE");
    return client.DELETE();
}

bool networkProtocolHTTP::rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    // Remove leading slash!
    urlParser->path = urlParser->path.substr(1);

    // parse away the src, dest file.
    comma_pos = urlParser->path.find(",");

    if (comma_pos == string::npos)
        return false;

    rnFrom = urlParser->path.substr(0, comma_pos);
    rnTo = urlParser->path.substr(comma_pos + 1);
    rnTo = "/" + rnTo;
    urlParser->path = urlParser->path.substr(0, comma_pos);

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    /*
    client.addHeader("Destination", rnTo.c_str());
    client.addHeader("Overwrite", "F");
    client.addHeader("translate", "f");
    return client.sendRequest("MOVE");
    */
    return client.MOVE(rnTo.c_str(), false);
}

bool networkProtocolHTTP::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'G': // toggle collect headers
        special_collect_headers_toggle(cmdFrame->aux1);
        return false;
    case 'H': // toggle headers
        special_header_toggle(cmdFrame->aux1);
        return false;
    case 'I': // toggle CA
        special_ca_toggle(cmdFrame->aux1);
        return false;
    default:
        return true;
    }
    return true;
}
