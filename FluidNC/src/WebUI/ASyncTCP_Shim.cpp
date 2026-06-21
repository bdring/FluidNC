#ifdef IDFBUILD

#    include <AsyncTCP.h>
#    include <ESPAsyncWebServer.h>
#    include <IPAddress.h>
#    include <lwip/tcp.h>

/*
 * This is a shim to provide missing symbols from AsyncTCP and ESPAsyncWebServer
 * when they are compiled under ESP-IDF without the ARDUINO macro defined.
 * These libraries conditionally compile IPAddress-based helper methods,
 * which causes "undefined reference" linker errors when called from other code
 * that *is* compiled as Arduino.
*/

IPAddress AsyncClient::remoteIP() const {
#    if ESP_IDF_VERSION_MAJOR < 5
    return IPAddress(getRemoteAddress());
#    else
    if (!_pcb) {
        return IPAddress();
    }
    IPAddress ip;
    ip.from_ip_addr_t(&(_pcb->remote_ip));
    return ip;
#    endif
}

IPAddress AsyncClient::localIP() const {
#    if ESP_IDF_VERSION_MAJOR < 5
    return IPAddress(getLocalAddress());
#    else
    if (!_pcb) {
        return IPAddress();
    }
    IPAddress ip;
    ip.from_ip_addr_t(&(_pcb->local_ip));
    return ip;
#    endif
}
#endif
