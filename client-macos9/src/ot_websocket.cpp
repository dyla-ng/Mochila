#include "ot_websocket.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mochila {

static InetHost parseIpAddress(const std::string &ipStr) {
  int a = 0, b = 0, c = 0, d = 0;
  if (sscanf(ipStr.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
    return ((InetHost)(a & 0xFF) << 24) | ((InetHost)(b & 0xFF) << 16) |
           ((InetHost)(c & 0xFF) << 8) | (InetHost)(d & 0xFF);
  }
  return 0;
}

OpenTransportWebSocket::OpenTransportWebSocket()
    : endpoint_(kOTInvalidEndpointRef), isConnected_(false), port_(8080) {
#if defined(__MWERKS__) || defined(macintosh)
  InitOpenTransportInContext(kInitOTForApplicationMask, NULL);
#endif
}

OpenTransportWebSocket::~OpenTransportWebSocket() {
  disconnect();
#if defined(__MWERKS__) || defined(macintosh)
  CloseOpenTransportInContext(NULL);
#endif
}

bool OpenTransportWebSocket::connect(const std::string &host, int port,
                                     const std::string &targetUrl) {
  host_ = host;
  port_ = port;

  OSStatus err = noErr;
  OTConfiguration *config = OTCreateConfiguration(kTCPName);
#if defined(__MWERKS__) || defined(macintosh)
  endpoint_ = OTOpenEndpointInContext(config, 0, NULL, &err, NULL);
#else
  endpoint_ = OTOpenEndpoint(config, 0, NULL, &err);
#endif

  if (err != noErr || endpoint_ == kOTInvalidEndpointRef) {
    std::cerr << "[OpenTransport] Failed to open TCP endpoint, err: " << err
              << std::endl;
    return false;
  }

  OTBind(endpoint_, NULL, NULL);
  OTSetNonBlocking(endpoint_);

  // Connect to target host and port via InetAddress
  InetAddress inetAddr;
  memset(&inetAddr, 0, sizeof(InetAddress));
#if defined(__MWERKS__) || defined(macintosh)
  OTInitInetAddress(&inetAddr, (InetPort)port, parseIpAddress(host));
#else
  inetAddr.fAddressType = AF_INET;
  inetAddr.fPort = (InetPort)port;
  inetAddr.fHost = parseIpAddress(host);
#endif

  TCall call;
  memset(&call, 0, sizeof(TCall));
  call.addr.buf = (UInt8 *)&inetAddr;
  call.addr.len = sizeof(InetAddress);

  err = OTConnect(endpoint_, &call, NULL);
  if (err != noErr && err != kOTNoDataErr) {
    std::cerr << "[OpenTransport] OTConnect failed, err: " << err << std::endl;
    return false;
  }

  // Poll endpoint state without hanging CPU
  OTResult state = 0;
  UInt32 startTime = TickCount();
  while ((TickCount() - startTime) < 180) { // 3 second timeout
#if defined(__MWERKS__) || defined(macintosh)
    EventRecord dummyEvt;
    WaitNextEvent(0, &dummyEvt, 0, NULL); // Carbon yield to OS 9 system tasks
#endif
    state = OTGetEndpointState(endpoint_);
    if (state == T_DATAXFER) {
      isConnected_ = true;
      break;
    }
  }

  if (!isConnected_) {
    std::cerr << "[OpenTransport] Connection timeout to " << host << ":" << port
              << std::endl;
    return false;
  }

  char portStr[16];
  sprintf(portStr, "%d", port);

  // Send HTTP WebSocket Upgrade Request
  std::string handshake = "GET / HTTP/1.1\r\n"
                          "Host: " +
                          host + ":" + std::string(portStr) +
                          "\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                          "Sec-WebSocket-Version: 13\r\n\r\n";

  OTSnd(endpoint_, (void *)handshake.data(), handshake.length(), 0);

  // Wait for HTTP 101 Response ("\r\n\r\n") in non-blocking mode
  std::string httpResp;
  char respBuf[512];
  startTime = TickCount();
  while ((TickCount() - startTime) < 180) { // 3 second timeout
#if defined(__MWERKS__) || defined(macintosh)
    EventRecord dummyEvt;
    WaitNextEvent(0, &dummyEvt, 0, NULL);
#endif
    OTFlags flags = 0;
    OTResult res = OTRcv(endpoint_, respBuf, sizeof(respBuf) - 1, &flags);
    if (res > 0) {
      respBuf[res] = '\0';
      httpResp += respBuf;
      if (httpResp.find("\r\n\r\n") != std::string::npos) {
        break; // Handshake completed!
      }
    }
  }

  std::cout << "[OpenTransport] WebSocket handshake established!" << std::endl;
  return true;
}

void OpenTransportWebSocket::disconnect() {
  if (endpoint_ != kOTInvalidEndpointRef) {
    OTSndDisconnect(endpoint_, NULL);
    OTUnbind(endpoint_);
    OTCloseProvider(endpoint_);
    endpoint_ = kOTInvalidEndpointRef;
  }
  isConnected_ = false;
}

bool OpenTransportWebSocket::pollData(std::vector<uint8_t> &outBuffer) {
  if (!isConnected_ || endpoint_ == kOTInvalidEndpointRef)
    return false;

  // Drain ALL available TCP socket buffer chunks in a fast non-blocking loop
  uint8_t tempBuf[8192];
  OTFlags flags = 0;
  OTResult bytesRead = 0;

  while ((bytesRead = OTRcv(endpoint_, tempBuf, sizeof(tempBuf), &flags)) > 0) {
    receiveBuffer_.insert(receiveBuffer_.end(), tempBuf, tempBuf + bytesRead);
  }

  // Check if full WebSocket frame is available (Header >= 2 bytes)
  if (receiveBuffer_.size() >= 2) {
    uint8_t opcode = receiveBuffer_[0] & 0x0F;
    uint8_t payloadLen = receiveBuffer_[1] & 0x7F;
    size_t headerSize = 2;
    size_t totalPayloadLen = payloadLen;

    if (payloadLen == 126) {
      if (receiveBuffer_.size() < 4)
        return false;
      totalPayloadLen = (receiveBuffer_[2] << 8) | receiveBuffer_[3];
      headerSize = 4;
    } else if (payloadLen == 127) {
      if (receiveBuffer_.size() < 10)
        return false;
      // 64-bit WebSocket payload size (Big-Endian bytes 2-9, reading lower 32
      // bits from bytes 6-9)
      totalPayloadLen = ((size_t)receiveBuffer_[6] << 24) |
                        ((size_t)receiveBuffer_[7] << 16) |
                        ((size_t)receiveBuffer_[8] << 8) |
                        (size_t)receiveBuffer_[9];
      headerSize = 10;
    }

    std::cout << "[OpenTransport] Frame: opcode=" << (int)opcode
              << " payloadLen=" << (int)payloadLen
              << " totalPayloadLen=" << totalPayloadLen
              << " headerSize=" << headerSize
              << " bufferSize=" << receiveBuffer_.size() << std::endl;

    if (receiveBuffer_.size() >= headerSize + totalPayloadLen) {
      std::cout << "[OpenTransport] Complete frame received! Extracting "
                << totalPayloadLen << " bytes" << std::endl;
      outBuffer.assign(receiveBuffer_.begin() + headerSize,
                       receiveBuffer_.begin() + headerSize + totalPayloadLen);
      receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() +
                                                       headerSize +
                                                       totalPayloadLen);
      return true;
    } else {
      std::cout << "[OpenTransport] Incomplete frame, waiting for more data..."
                << std::endl;
    }
  }

  return false;
}

bool OpenTransportWebSocket::sendBinary(const std::vector<uint8_t> &data) {
  if (!isConnected_ || endpoint_ == kOTInvalidEndpointRef)
    return false;

  // Build client WebSocket binary frame (opcode 0x02) with 4-byte mask key
  size_t len = data.size();
  std::vector<uint8_t> frame;
  frame.push_back(0x82); // FIN + binary opcode

  if (len < 126) {
    frame.push_back(0x80 | (uint8_t)len); // MASK bit = 1
  } else if (len <= 65535) {
    frame.push_back(0x80 | 126);
    frame.push_back((len >> 8) & 0xFF);
    frame.push_back(len & 0xFF);
  } else {
    frame.push_back(0x80 | 127);
    for (int i = 7; i >= 0; i--) {
      frame.push_back((len >> (i * 8)) & 0xFF);
    }
  }

  uint8_t maskKey[4] = {0x12, 0x34, 0x56, 0x78};
  frame.insert(frame.end(), maskKey, maskKey + 4);

  for (size_t i = 0; i < len; i++) {
    frame.push_back(data[i] ^ maskKey[i % 4]);
  }

  OTResult sent = OTSnd(endpoint_, (void *)&frame[0], frame.size(), 0);
  return (sent == (OTResult)frame.size());
}

} // namespace mochila
