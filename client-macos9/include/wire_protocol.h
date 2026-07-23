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
    int32_t scrollY;  // Vertical scroll position from server
    int32_t scrollX;  // Horizontal scroll position from server
    uint16_t viewportWidth;
    uint16_t viewportHeight;
    uint16_t documentWidth;
    uint16_t documentHeight;
    bool hasScrollMetadata;  // Whether scroll metadata is present
    uint32_t lastProcessedScrollSeq;  // Last scroll sequence processed by server
    bool hasLastProcessedScrollSeq;   // Whether sequence number is present
    std::string currentUrl;  // Current page URL from server
    bool hasCurrentUrl;      // Whether URL is present

    FrameUpdate() : messageType("FrameUpdate"), frameId(0), primitiveCount(0), scrollY(0), scrollX(0),
                    viewportWidth(0), viewportHeight(0), documentWidth(0), documentHeight(0), hasScrollMetadata(false),
                    lastProcessedScrollSeq(0), hasLastProcessedScrollSeq(false), currentUrl(""), hasCurrentUrl(false) {}
};

struct FrameAck {
    std::string messageType;  // "FrameAck"
    int frameId;

    FrameAck() : messageType("FrameAck"), frameId(0) {}
};

struct ImageData {
    std::string messageType;  // "ImageData"
    std::string imageId;      // Hash of source URL
    std::vector<uint8_t> pictBytes;  // PICT image data

    ImageData() : messageType("ImageData"), imageId("") {}
};

class WireProtocol {
public:
    // Peek at message type without parsing
    static uint8_t peekMessageType(const std::vector<uint8_t>& bytes);

    // Parse binary FrameUpdate from server (MessageType 1)
    static FrameUpdate parseFrameUpdate(const std::vector<uint8_t>& bytes);

    // Parse binary ImageData from server (MessageType 12)
    static ImageData parseImageData(const std::vector<uint8_t>& bytes);

    // Serialize FrameAck to binary
    static std::vector<uint8_t> serializeFrameAck(const FrameAck& ack);

    // Serialize Click event to binary
    static std::vector<uint8_t> serializeClick(int32_t x, int32_t y);

    // Serialize Scroll event to binary (MessageType 4)
    static std::vector<uint8_t> serializeScroll(int32_t scrollX, int32_t scrollY, uint32_t sequenceNumber);

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

    // Serialize ResizeViewport event to binary (MessageType 13)
    static std::vector<uint8_t> serializeResizeViewport(uint16_t width, uint16_t height);
};

} // namespace mochila

#endif // MACOS9_WIRE_PROTOCOL_H
