#include "wire_protocol.h"
#include <iostream>
#include <string.h>

namespace mochila {

static std::string utf8ToMacRoman(const std::string &input) {
  std::string out;
  out.reserve(input.length());

  size_t i = 0;
  while (i < input.length()) {
    unsigned char c = (unsigned char)input[i];

    if (c < 0x80) { // ASCII
      out.push_back(c);
      i++;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < input.length()) { // 2-byte UTF-8
      unsigned char c2 = (unsigned char)input[i + 1];
      uint32_t code = ((c & 0x1F) << 6) | (c2 & 0x3F);
      if (code == 0xA0)
        out.push_back(' '); // Non-breaking space
      else if (code == 0xA7)
        out.push_back(0xA4); // Section symbol §
      else if (code == 0xA9)
        out.push_back(0xA9); // Copyright ©
      else if (code == 0xAE)
        out.push_back(0xA8); // Registered ®
      else if (code >= 0xC0 && code <= 0xFF)
        out.push_back(c); // Latin-1 fallback
      else
        out.push_back(' ');
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.length()) { // 3-byte UTF-8
      unsigned char c2 = (unsigned char)input[i + 1];
      unsigned char c3 = (unsigned char)input[i + 2];
      uint32_t code = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);

      if (code == 0x2018 || code == 0x2019)
        out.push_back(0xD5); // Single quotes ‘ ’ -> Mac Roman ’
      else if (code == 0x201C || code == 0x201D)
        out.push_back(0xD2); // Double quotes “ ” -> Mac Roman “
      else if (code == 0x2013)
        out.push_back(0xD6); // En-dash –
      else if (code == 0x2014)
        out.push_back(0xD7); // Em-dash —
      else if (code == 0x2022)
        out.push_back(0xA5); // Bullet •
      else if (code == 0x2026)
        out.push_back(0xC9); // Ellipsis …
      else
        out.push_back(' ');
      i += 3;
    } else {
      out.push_back(' ');
      i++;
    }
  }
  return out;
}

static bool readBytesHelper(void *dest, size_t size, size_t &offset,
                            const std::vector<uint8_t> &bytes) {
  if (offset + size > bytes.size())
    return false;
  std::memcpy(dest, &bytes[offset], size);
  offset += size;
  return true;
}

static uint16_t readUInt16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t readUInt32LE(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int32_t readInt32LE(const uint8_t *p) {
  return (int32_t)readUInt32LE(p);
}

static void writeUInt16LE(uint8_t *p, uint16_t val) {
  p[0] = val & 0xFF;
  p[1] = (val >> 8) & 0xFF;
}

static void writeUInt32LE(uint8_t *p, uint32_t val) {
  p[0] = val & 0xFF;
  p[1] = (val >> 8) & 0xFF;
  p[2] = (val >> 16) & 0xFF;
  p[3] = (val >> 24) & 0xFF;
}

FrameUpdate WireProtocol::parseFrameUpdate(const std::vector<uint8_t> &bytes) {
  FrameUpdate frame;
  size_t offset = 0;

  std::cout << "[WireProtocol] Parsing frame with " << bytes.size() << " bytes"
            << std::endl;

  uint8_t messageType = 0;
  if (!readBytesHelper(&messageType, 1, offset, bytes) || messageType != 1) {
    std::cerr << "[WireProtocol] Invalid message type: "
              << static_cast<int>(messageType) << std::endl;
    return frame;
  }

  if (offset + 4 > bytes.size()) {
    std::cerr << "[WireProtocol] Frame too short for frameId" << std::endl;
    return frame;
  }
  frame.frameId = readUInt32LE(&bytes[offset]);
  offset += 4;
  frame.messageType = "FrameUpdate";

  if (offset + 2 > bytes.size()) {
    std::cerr << "[WireProtocol] Frame too short for primitive count"
              << std::endl;
    return frame;
  }
  uint16_t primitiveCount = readUInt16LE(&bytes[offset]);
  offset += 2;
  frame.primitiveCount = primitiveCount;

  std::cout << "[WireProtocol] Frame #" << frame.frameId << " expects "
            << primitiveCount << " primitives" << std::endl;

  for (int i = 0; i < primitiveCount; i++) {
    uint8_t primitiveType = 0;
    if (!readBytesHelper(&primitiveType, 1, offset, bytes)) {
      std::cerr << "[WireProtocol] Failed to read primitive type at index " << i
                << ", offset " << offset << std::endl;
      break;
    }

    if (offset + 2 > bytes.size()) {
      std::cerr << "[WireProtocol] Not enough bytes for identityLen at index "
                << i << std::endl;
      break;
    }
    uint16_t identityLen = readUInt16LE(&bytes[offset]);
    offset += 2;

    if (i == 0) {
      std::cout << "[WireProtocol] First primitive: type=" << (int)primitiveType
                << " identityLen=" << identityLen << " offset=" << offset
                << std::endl;
    }

    std::string identity;
    if (identityLen > 0) {
      if (offset + identityLen > bytes.size()) {
        std::cerr << "[WireProtocol] Not enough bytes for identity at index "
                  << i << " (need " << identityLen << " bytes, have "
                  << (bytes.size() - offset) << ")" << std::endl;
        break;
      }
      identity.resize(identityLen);
      if (!readBytesHelper(&identity[0], identityLen, offset, bytes)) {
        std::cerr << "[WireProtocol] Failed to read identity at index " << i
                  << std::endl;
        break;
      }
    }

    if (primitiveType == 1) { // DrawRect
      DrawRectPrimitive *prim = new DrawRectPrimitive();
      prim->type = PrimitiveType_DrawRect;
      prim->identity = identity;

      if (offset + 16 > bytes.size()) {
        if (i < 3)
          std::cerr << "[WireProtocol] DrawRect at index " << i
                    << ": not enough bytes (need 16, have "
                    << (bytes.size() - offset) << ")" << std::endl;
        delete prim;
        break;
      }
      prim->x = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->y = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->width = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->height = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->color.r = bytes[offset++];
      prim->color.g = bytes[offset++];
      prim->color.b = bytes[offset++];
      prim->color.a = bytes[offset++];

      uint8_t rectFlags = 0;
      if (!readBytesHelper(&rectFlags, 1, offset, bytes)) {
        delete prim;
        break;
      }

      if ((rectFlags & 0x01) != 0) {
        if (offset + 4 > bytes.size()) {
          delete prim;
          break;
        }
        uint8_t hr = bytes[offset++];
        uint8_t hg = bytes[offset++];
        uint8_t hb = bytes[offset++];
        uint8_t ha = bytes[offset++];
        prim->hasHoverColor = true;
        prim->hoverColor = Color(hr, hg, hb, ha);
      }

      // Read borderRadius (new field for rounded corners)
      if (offset + 1 > bytes.size()) {
        delete prim;
        break;
      }
      prim->borderRadius = bytes[offset++];

      // DEBUG: Log first few borderRadius values
      static int debugCount = 0;
      if (prim->borderRadius > 0 && debugCount < 5) {
        std::cout << "[WireProtocol] DrawRect borderRadius="
                  << (int)prim->borderRadius << std::endl;
        debugCount++;
      }

      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
      prim->zIndex = (int16_t)readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->treeOrder = readUInt16LE(&bytes[offset]);
      offset += 2;

      frame.primitives.push_back(prim);
    } else if (primitiveType == 2) { // DrawText
      DrawTextPrimitive *prim = new DrawTextPrimitive();
      prim->type = PrimitiveType_DrawText;
      prim->identity = identity;

      if (offset + 12 > bytes.size()) {
        if (i < 3)
          std::cerr << "[WireProtocol] DrawText at index " << i
                    << ": not enough bytes for header (need 12, have "
                    << (bytes.size() - offset) << ")" << std::endl;
        delete prim;
        break;
      }
      prim->x = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->y = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->fontId = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->fontSize = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->color.r = bytes[offset++];
      prim->color.g = bytes[offset++];
      prim->color.b = bytes[offset++];
      prim->color.a = bytes[offset++];

      if (offset + 2 > bytes.size()) {
        delete prim;
        break;
      }
      uint16_t textLen = readUInt16LE(&bytes[offset]);
      offset += 2;

      std::string text;
      if (textLen > 0) {
        text.resize(textLen);
        if (!readBytesHelper(&text[0], textLen, offset, bytes)) {
          delete prim;
          break;
        }
      }

      // Read maxWidth field
      if (offset + 2 > bytes.size()) {
        delete prim;
        break;
      }
      prim->maxWidth = readUInt16LE(&bytes[offset]);
      offset += 2;

      uint8_t styleFlags = 0;
      if (!readBytesHelper(&styleFlags, 1, offset, bytes)) {
        delete prim;
        break;
      }

      if ((styleFlags & 0x08) != 0) { // HasHoverColor
        if (offset + 4 > bytes.size()) {
          delete prim;
          break;
        }
        uint8_t hr = bytes[offset++];
        uint8_t hg = bytes[offset++];
        uint8_t hb = bytes[offset++];
        uint8_t ha = bytes[offset++];
        prim->hasHoverColor = true;
        prim->hoverColor = Color(hr, hg, hb, ha);
      }

      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
      prim->zIndex = (int16_t)readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->treeOrder = readUInt16LE(&bytes[offset]);
      offset += 2;

      prim->text = text;
      prim->macRomanText = utf8ToMacRoman(text);
      // Server bit mapping: 0x01=isItalic, 0x02=isUnderline, 0x04=isBold,
      // 0x08=hoverColor, 0x10=hoverUnderline
      prim->isItalic = (styleFlags & 0x01) != 0;
      prim->isUnderline = (styleFlags & 0x02) != 0;
      prim->isBold = (styleFlags & 0x04) != 0;
      prim->hoverUnderline = (styleFlags & 0x10) != 0;
      frame.primitives.push_back(prim);
    } else if (primitiveType == 3) { // DrawBorder
      DrawBorderPrimitive *prim = new DrawBorderPrimitive();
      prim->type = PrimitiveType_DrawBorder;
      prim->identity = identity;

      if (offset + 18 > bytes.size()) {
        delete prim;
        break;
      }
      prim->x = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->y = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->width = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->height = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->thickness = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->color.r = bytes[offset++];
      prim->color.g = bytes[offset++];
      prim->color.b = bytes[offset++];
      prim->color.a = bytes[offset++];

      // Read borderRadius (new field for rounded corners)
      if (offset + 1 > bytes.size()) {
        delete prim;
        break;
      }
      prim->borderRadius = bytes[offset++];

      prim->zIndex = (int16_t)readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->treeOrder = readUInt16LE(&bytes[offset]);
      offset += 2;

      frame.primitives.push_back(prim);
    } else if (primitiveType == 4) { // DrawImage
      DrawImagePrimitive *prim = new DrawImagePrimitive();
      prim->type = PrimitiveType_DrawImage;
      prim->identity = identity;

      if (offset + 16 > bytes.size()) {
        delete prim;
        break;
      }
      prim->x = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->y = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->width = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->height = readUInt16LE(&bytes[offset]);
      offset += 2;
      uint32_t imageLen = readUInt32LE(&bytes[offset]);
      offset += 4;

      if (imageLen > 0) {
        prim->pictBytes.resize(imageLen);
        if (!readBytesHelper(&prim->pictBytes[0], imageLen, offset, bytes)) {
          delete prim;
          break;
        }
      }

      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
      prim->zIndex = (int16_t)readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->treeOrder = readUInt16LE(&bytes[offset]);
      offset += 2;

      frame.primitives.push_back(prim);
    } else if (primitiveType == 7) { // DrawMaskedImage
      DrawMaskedImagePrimitive *prim = new DrawMaskedImagePrimitive();
      prim->type = PrimitiveType_DrawMaskedImage;
      prim->identity = identity;

      if (offset + 16 > bytes.size()) {
        delete prim;
        break;
      }
      prim->x = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->y = readInt32LE(&bytes[offset]);
      offset += 4;
      prim->width = readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->height = readUInt16LE(&bytes[offset]);
      offset += 2;

      // Read fill color (RGBA)
      prim->fillColor.r = bytes[offset++];
      prim->fillColor.g = bytes[offset++];
      prim->fillColor.b = bytes[offset++];
      prim->fillColor.a = bytes[offset++];

      // Read 1-bit mask data
      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
      uint32_t maskLen = readUInt32LE(&bytes[offset]);
      offset += 4;

      if (maskLen > 0) {
        prim->maskData.resize(maskLen);
        if (!readBytesHelper(&prim->maskData[0], maskLen, offset, bytes)) {
          delete prim;
          break;
        }
      }

      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
      prim->zIndex = (int16_t)readUInt16LE(&bytes[offset]);
      offset += 2;
      prim->treeOrder = readUInt16LE(&bytes[offset]);
      offset += 2;

      frame.primitives.push_back(prim);
    } else if (primitiveType == 5) { // RemovePrimitive
      RemovePrimitive *prim = new RemovePrimitive();
      prim->type = PrimitiveType_RemovePrimitive;
      prim->identity = identity;
      frame.primitives.push_back(prim);
    } else {
      std::cerr << "[WireProtocol] Unknown primitive type "
                << (int)primitiveType << " at index " << i << std::endl;
      break;
    }
  }

  std::cout << "[WireProtocol] Successfully parsed " << frame.primitives.size()
            << " / " << primitiveCount << " primitives" << std::endl;

  // Parse optional scrollMetadata (scrollY)
  if (offset < bytes.size()) {
    uint8_t hasScrollMetadata = bytes[offset++];
    if (hasScrollMetadata) {
      // Read scrollY
      if (offset + 4 <= bytes.size()) {
        frame.scrollY = (int32_t)readUInt32LE(&bytes[offset]);
        offset += 4;
        std::cout << "[WireProtocol] Parsed scrollY=" << frame.scrollY << std::endl;
      }

      // Skip remaining scrollMetadata fields (scrollX, viewportWidth, viewportHeight, documentWidth, documentHeight)
      offset += 4 + 2 + 2 + 2 + 2;  // scrollX (4) + viewport dims (2+2) + document dims (2+2)

      // Skip stickyElements array
      if (offset + 2 <= bytes.size()) {
        uint16_t stickyCount = readUInt16LE(&bytes[offset]);
        offset += 2;
        for (int i = 0; i < stickyCount && offset < bytes.size(); i++) {
          if (offset + 2 > bytes.size()) break;
          uint16_t posLen = readUInt16LE(&bytes[offset]);
          offset += 2;
          offset += posLen;  // Skip position string
          offset += 4 + 4 + 2 + 2;  // x, y, width, height
        }
      }
    }
  }

  // Parse currentUrl (optional)
  if (offset < bytes.size()) {
    uint8_t hasCurrentUrl = bytes[offset++];
    if (hasCurrentUrl && offset + 2 <= bytes.size()) {
      uint16_t urlLen = readUInt16LE(&bytes[offset]);
      offset += 2;
      if (urlLen > 0 && offset + urlLen <= bytes.size()) {
        frame.currentUrl.assign(reinterpret_cast<const char*>(&bytes[offset]), urlLen);
        frame.hasCurrentUrl = true;
        offset += urlLen;
        std::cout << "[WireProtocol] Parsed currentUrl=" << frame.currentUrl << std::endl;
      }
    }
  }

  // Parse lastProcessedScrollSeq (optional)
  if (offset < bytes.size()) {
    uint8_t hasScrollSeq = bytes[offset++];
    if (hasScrollSeq && offset + 4 <= bytes.size()) {
      frame.lastProcessedScrollSeq = readUInt32LE(&bytes[offset]);
      frame.hasLastProcessedScrollSeq = true;
      offset += 4;
      std::cout << "[WireProtocol] Parsed lastProcessedScrollSeq=" << frame.lastProcessedScrollSeq << std::endl;
    }
  }

  return frame;
}

std::vector<uint8_t> WireProtocol::serializeFrameAck(const FrameAck &ack) {
  std::vector<uint8_t> result(5);
  result[0] = 2; // MessageType 2
  writeUInt32LE(&result[1], ack.frameId);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeClick(int32_t x, int32_t y) {
  std::vector<uint8_t> result(9);
  result[0] = 6; // MessageType 6 (Click)
  writeUInt32LE(&result[1], (uint32_t)x);
  writeUInt32LE(&result[5], (uint32_t)y);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeScroll(int32_t scrollY, uint32_t sequenceNumber) {
  std::vector<uint8_t> result(9);  // 1 + 4 + 4 bytes
  result[0] = 4; // MessageType 4 (Scroll)
  writeUInt32LE(&result[1], (uint32_t)scrollY);
  writeUInt32LE(&result[5], sequenceNumber);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeNavigateCommand(uint8_t action) {
  std::vector<uint8_t> result;
  result.push_back(7); // MessageType 7 (NavigateCommand)
  result.push_back(action);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeKeyInput(bool isText,
                                                     const std::string &text) {
  uint16_t len = static_cast<uint16_t>(text.length());
  std::vector<uint8_t> result(4 + len);
  result[0] = 8; // MessageType 8 (KeyInput)
  result[1] = isText ? 1 : 0;
  writeUInt16LE(&result[2], len);
  if (len > 0) {
    std::memcpy(&result[4], text.data(), len);
  }
  return result;
}

std::vector<uint8_t> WireProtocol::serializeMouseMove(int32_t x, int32_t y) {
  std::vector<uint8_t> result(9);
  result[0] = 9; // MessageType 9 (MouseMove)
  writeUInt32LE(&result[1], (uint32_t)x);
  writeUInt32LE(&result[5], (uint32_t)y);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeMouseEnter(int32_t x, int32_t y) {
  std::vector<uint8_t> result(9);
  result[0] = 10; // MessageType 10 (MouseEnter)
  writeUInt32LE(&result[1], (uint32_t)x);
  writeUInt32LE(&result[5], (uint32_t)y);
  return result;
}

std::vector<uint8_t> WireProtocol::serializeMouseLeave() {
  std::vector<uint8_t> result;
  result.push_back(11); // MessageType 11 (MouseLeave)
  return result;
}

} // namespace mochila
