# How to get the library to compile?

Unfortunately not everything goes according to plan 
right from the get go. You need:

1. arduinoWebSockets
2. SSD1306
3. TMCStepper

Most of it works as-is. The sole exception is the canSend.
This should be patched, really, but for the moment I just wrote
a little workaround as follows:

```c++
int WebSocketsServerCore::canSend(uint8_t num) {
    WSclient_t * client = &_clients[num];
    if(!clientIsConnected(client)) {
        return -1;
    }
#ifndef IDFBUILD
    return client->tcp->canWrite(0);
#else
    return 0; // TODO FIXME.
#endif
}
```
