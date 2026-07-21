#ifndef OT_WEBSOCKET_H
#define OT_WEBSOCKET_H

#if defined(__MWERKS__) || defined(macintosh)
#include <Carbon.h>
#include <OpenTransport.h>
#include <OpenTransportProviders.h>
#else
#include <Carbon/Carbon.h>
#endif

#include <vector>
#include <string>
#include <stdint.h>

namespace mochila {

class OpenTransportWebSocket {
public:
    OpenTransportWebSocket();
    ~OpenTransportWebSocket();

    bool connect(const std::string& host, int port, const std::string& targetUrl);
    void disconnect();

    bool isConnected() const { return isConnected_; }

    // Poll for incoming binary data packets over OpenTransport TCP
    bool pollData(std::vector<uint8_t>& outBuffer);

    // Send binary message over WebSocket wire frame
    bool sendBinary(const std::vector<uint8_t>& data);

private:
    EndpointRef endpoint_;
    bool isConnected_;
    std::string host_;
    int port_;
    std::vector<uint8_t> receiveBuffer_;
};

} // namespace mochila

#endif // OT_WEBSOCKET_H
