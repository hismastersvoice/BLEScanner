#ifndef UDP_CLIENT_H
#define UDP_CLIENT_H
#include "main.h"
#include "settings.h"

namespace UDPClient
{
    void initializeUDP();
    void publishBLEStateUDP(const BLETrackedDevice &device);
    void publishSySInfoUDP();
}

#endif // UDP_CLIENT_H