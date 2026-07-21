#ifndef MACOS9_WIRE_PROTOCOL_H
#define MACOS9_WIRE_PROTOCOL_H

#include "types.h"
#include <vector>
#include <string>
#include <stdint.h>

namespace mochila {

typedef Primitive* PrimitivePtr;

struct FrameUpdate {
    std::string messageType;  // "FrameUpdate"
    int frameId;
    int primitiveCount;
    std::vector<PrimitivePtr> primitives;
    int32_t scrollY;  // Scroll position from server
    uint32_t lastProcessedScrollSeq;  // Last scroll sequence processed by server
    bool hasLastProcessedScrollSeq;   // Whether sequence number is present
    std::string currentUrl;  // Current page URL from server
    bool hasCurrentUrl;      // Whether URL is present

    FrameUpdate() : messageType("FrameUpdate"), frameId(0), primitiveCount(0), scrollY(0), lastProcessedScrollSeq(0), hasLastProcessedScrollSeq(false), currentUrl(""), hasCurrentUrl(false) {}
};

struct FrameAck {
    std::string messageType;  // "FrameAck"
    int frameId;

    FrameAck() : messageType("FrameAck"), frameId(0) {}
};

class WireProtocol {
public:
    // Parse binary FrameUpdate from server
    static FrameUpdate parseFrameUpdate(const std::vector<uint8_t>& bytes);

    // Serialize FrameAck to binary
    static std::vector<uint8_t> serializeFrameAck(const FrameAck& ack);

    // Serialize Click event to binary
    static std::vector<uint8_t> serializeClick(int32_t x, int32_t y);

    // Serialize Scroll event to binary (MessageType 4)
    static std::vector<uint8_t> serializeScroll(int32_t scrollY, uint32_t sequenceNumber);

    // Serialize NavigateCommand (1 = Back, 2 = Forward, 3 = Reload) to binary
    static std::vector<uint8_t> serializeNavigateCommand(uint8_t action);

    // Serialize KeyInput (isText = true/false, text or key name) to binary
    static std::vector<uint8_t> serializeKeyInput(bool isText, const std::string& text);

    // Serialize MouseMove event to binary
    static std::vector<uint8_t> serializeMouseMove(int32_t x, int32_t y);

    // Serialize MouseEnter event to binary (MessageType 10)
    static std::vector<uint8_t> serializeMouseEnter(int32_t x, int32_t y);

    // Serialize MouseLeave event to binary (MessageType 11)
    static std::vector<uint8_t> serializeMouseLeave();
};

} // namespace mochila

#endif // MACOS9_WIRE_PROTOCOL_H
