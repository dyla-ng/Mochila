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

      // Comprehensive Latin-1 Supplement mappings (0x00A0-0x00FF)
      if (code == 0x00A0) out.push_back(0xCA); // Non-breaking space
      else if (code == 0x00A1) out.push_back(0xC1); // ¡
      else if (code == 0x00A2) out.push_back(0xA2); // ¢
      else if (code == 0x00A3) out.push_back(0xA3); // £
      else if (code == 0x00A5) out.push_back(0xB4); // ¥
      else if (code == 0x00A7) out.push_back(0xA4); // §
      else if (code == 0x00A8) out.push_back(0xAC); // ¨
      else if (code == 0x00A9) out.push_back(0xA9); // ©
      else if (code == 0x00AA) out.push_back(0xBB); // ª
      else if (code == 0x00AB) out.push_back(0xC7); // «
      else if (code == 0x00AC) out.push_back(0xC2); // ¬
      else if (code == 0x00AE) out.push_back(0xA8); // ®
      else if (code == 0x00AF) out.push_back(0xF8); // ¯
      else if (code == 0x00B0) out.push_back(0xA1); // °
      else if (code == 0x00B1) out.push_back(0xB1); // ±
      else if (code == 0x00B4) out.push_back(0xAB); // ´
      else if (code == 0x00B5) out.push_back(0xB5); // µ
      else if (code == 0x00B6) out.push_back(0xA6); // ¶
      else if (code == 0x00B7) out.push_back(0xE1); // ·
      else if (code == 0x00B8) out.push_back(0xFC); // ¸
      else if (code == 0x00BA) out.push_back(0xBC); // º
      else if (code == 0x00BB) out.push_back(0xC8); // »
      else if (code == 0x00BF) out.push_back(0xC0); // ¿
      else if (code == 0x00C0) out.push_back(0xCB); // À
      else if (code == 0x00C1) out.push_back(0xE7); // Á
      else if (code == 0x00C2) out.push_back(0xE5); // Â
      else if (code == 0x00C3) out.push_back(0xCC); // Ã
      else if (code == 0x00C4) out.push_back(0x80); // Ä
      else if (code == 0x00C5) out.push_back(0x81); // Å
      else if (code == 0x00C6) out.push_back(0xAE); // Æ
      else if (code == 0x00C7) out.push_back(0x82); // Ç
      else if (code == 0x00C8) out.push_back(0xE9); // È
      else if (code == 0x00C9) out.push_back(0x83); // É
      else if (code == 0x00CA) out.push_back(0xE6); // Ê
      else if (code == 0x00CB) out.push_back(0xE8); // Ë
      else if (code == 0x00CC) out.push_back(0xED); // Ì
      else if (code == 0x00CD) out.push_back(0xEA); // Í
      else if (code == 0x00CE) out.push_back(0xEB); // Î
      else if (code == 0x00CF) out.push_back(0xEC); // Ï
      else if (code == 0x00D1) out.push_back(0x84); // Ñ
      else if (code == 0x00D2) out.push_back(0xF1); // Ò
      else if (code == 0x00D3) out.push_back(0xEE); // Ó
      else if (code == 0x00D4) out.push_back(0xEF); // Ô
      else if (code == 0x00D5) out.push_back(0xCD); // Õ
      else if (code == 0x00D6) out.push_back(0x85); // Ö
      else if (code == 0x00D8) out.push_back(0xAF); // Ø
      else if (code == 0x00D9) out.push_back(0xF4); // Ù
      else if (code == 0x00DA) out.push_back(0xF2); // Ú
      else if (code == 0x00DB) out.push_back(0xF3); // Û
      else if (code == 0x00DC) out.push_back(0x86); // Ü
      else if (code == 0x00DF) out.push_back(0xA7); // ß
      else if (code == 0x00E0) out.push_back(0x88); // à
      else if (code == 0x00E1) out.push_back(0x87); // á
      else if (code == 0x00E2) out.push_back(0x89); // â
      else if (code == 0x00E3) out.push_back(0x8B); // ã
      else if (code == 0x00E4) out.push_back(0x8A); // ä
      else if (code == 0x00E5) out.push_back(0x8C); // å
      else if (code == 0x00E6) out.push_back(0xBE); // æ
      else if (code == 0x00E7) out.push_back(0x8D); // ç
      else if (code == 0x00E8) out.push_back(0x8F); // è
      else if (code == 0x00E9) out.push_back(0x8E); // é
      else if (code == 0x00EA) out.push_back(0x90); // ê
      else if (code == 0x00EB) out.push_back(0x91); // ë
      else if (code == 0x00EC) out.push_back(0x93); // ì
      else if (code == 0x00ED) out.push_back(0x92); // í
      else if (code == 0x00EE) out.push_back(0x94); // î
      else if (code == 0x00EF) out.push_back(0x95); // ï
      else if (code == 0x00F1) out.push_back(0x96); // ñ
      else if (code == 0x00F2) out.push_back(0x98); // ò
      else if (code == 0x00F3) out.push_back(0x97); // ó
      else if (code == 0x00F4) out.push_back(0x99); // ô
      else if (code == 0x00F5) out.push_back(0x9B); // õ
      else if (code == 0x00F6) out.push_back(0x9A); // ö
      else if (code == 0x00F7) out.push_back(0xD6); // ÷
      else if (code == 0x00F8) out.push_back(0xBF); // ø
      else if (code == 0x00F9) out.push_back(0x9D); // ù
      else if (code == 0x00FA) out.push_back(0x9C); // ú
      else if (code == 0x00FB) out.push_back(0x9E); // û
      else if (code == 0x00FC) out.push_back(0x9F); // ü
      else if (code == 0x00FF) out.push_back(0xD8); // ÿ
      // Latin Extended-A
      else if (code == 0x0131) out.push_back(0xF5); // ı
      else if (code == 0x0152) out.push_back(0xCE); // Œ
      else if (code == 0x0153) out.push_back(0xCF); // œ
      else if (code == 0x0178) out.push_back(0xD9); // Ÿ
      // Latin Extended-B
      else if (code == 0x0192) out.push_back(0xC4); // ƒ
      // Spacing Modifier Letters
      else if (code == 0x02C6) out.push_back(0xF6); // ˆ
      else if (code == 0x02C7) out.push_back(0xFF); // ˇ
      else if (code == 0x02D8) out.push_back(0xF9); // ˘
      else if (code == 0x02D9) out.push_back(0xFA); // ˙
      else if (code == 0x02DA) out.push_back(0xFB); // ˚
      else if (code == 0x02DB) out.push_back(0xFE); // ˛
      else if (code == 0x02DC) out.push_back(0xF7); // ˜
      else if (code == 0x02DD) out.push_back(0xFD); // ˝
      else
        out.push_back('?'); // Unmappable 2-byte character
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.length()) { // 3-byte UTF-8
      unsigned char c2 = (unsigned char)input[i + 1];
      unsigned char c3 = (unsigned char)input[i + 2];
      uint32_t code = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);

      // Greek and Coptic
      if (code == 0x03A9) out.push_back(0xBD); // Ω
      else if (code == 0x03C0) out.push_back(0xB9); // π
      // General Punctuation
      else if (code == 0x2013) out.push_back(0xD0); // –
      else if (code == 0x2014) out.push_back(0xD1); // —
      else if (code == 0x2018) out.push_back(0xD4); // '
      else if (code == 0x2019) out.push_back(0xD5); // '
      else if (code == 0x201A) out.push_back(0xE2); // ‚
      else if (code == 0x201C) out.push_back(0xD2); // "
      else if (code == 0x201D) out.push_back(0xD3); // "
      else if (code == 0x201E) out.push_back(0xE3); // „
      else if (code == 0x2020) out.push_back(0xA0); // †
      else if (code == 0x2021) out.push_back(0xE0); // ‡
      else if (code == 0x2022) out.push_back(0xA5); // • (bullet)
      else if (code == 0x2026) out.push_back(0xC9); // …
      else if (code == 0x2030) out.push_back(0xE4); // ‰
      else if (code == 0x2039) out.push_back(0xDC); // ‹
      else if (code == 0x203A) out.push_back(0xDD); // ›
      else if (code == 0x2044) out.push_back(0xDA); // ⁄
      // Currency Symbols
      else if (code == 0x20AC) out.push_back(0xDB); // €
      // Mathematical Operators
      else if (code == 0x2202) out.push_back(0xB6); // ∂
      else if (code == 0x2206) out.push_back(0xC6); // ∆
      else if (code == 0x220F) out.push_back(0xB8); // ∏
      else if (code == 0x2211) out.push_back(0xB7); // ∑
      else if (code == 0x221A) out.push_back(0xC3); // √
      else if (code == 0x221E) out.push_back(0xB0); // ∞
      else if (code == 0x222B) out.push_back(0xBA); // ∫
      else if (code == 0x2248) out.push_back(0xC5); // ≈
      else if (code == 0x2260) out.push_back(0xAD); // ≠
      else if (code == 0x2264) out.push_back(0xB2); // ≤
      else if (code == 0x2265) out.push_back(0xB3); // ≥
      else if (code == 0x25CA) out.push_back(0xD7); // ◊
      // Alphabetic Presentation Forms
      else if (code == 0xFB01) out.push_back(0xDE); // ﬁ
      else if (code == 0xFB02) out.push_back(0xDF); // ﬂ
      else
        out.push_back('?'); // Unmappable 3-byte character
      i += 3;
    } else {
      out.push_back('?'); // Use ? for invalid/unmappable sequences
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

uint8_t WireProtocol::peekMessageType(const std::vector<uint8_t> &bytes) {
  if (bytes.size() < 1) {
    return 0;
  }
  return bytes[0];
}

ImageData WireProtocol::parseImageData(const std::vector<uint8_t> &bytes) {
  ImageData data;
  size_t offset = 0;

  std::cout << "[WireProtocol] Parsing ImageData: bufferSize=" << bytes.size() << std::endl;

  // Read message type (should be 12)
  if (offset + 1 > bytes.size()) {
    std::cerr << "[WireProtocol] Buffer too small for messageType" << std::endl;
    return data;
  }
  uint8_t messageType = bytes[offset];
  offset += 1;

  if (messageType != 12) {
    std::cerr << "[WireProtocol] Invalid ImageData message type: "
              << static_cast<int>(messageType) << " (expected 12)" << std::endl;
    return data;
  }

  // Read imageId length (uint16 LE)
  if (offset + 2 > bytes.size()) {
    std::cerr << "[WireProtocol] Buffer too small for imageIdLen" << std::endl;
    return data;
  }
  uint16_t imageIdLen = readUInt16LE(&bytes[offset]);
  offset += 2;

  std::cout << "[WireProtocol] imageIdLen=" << imageIdLen << " offset=" << offset << std::endl;

  // Read imageId string
  if (offset + imageIdLen > bytes.size()) {
    std::cerr << "[WireProtocol] ImageId extends beyond buffer: offset=" << offset
              << " imageIdLen=" << imageIdLen << " bufferSize=" << bytes.size() << std::endl;
    return data;
  }
  data.imageId = std::string(bytes.begin() + offset, bytes.begin() + offset + imageIdLen);
  offset += imageIdLen;

  // Read pictBytes length (uint32 LE)
  if (offset + 4 > bytes.size()) {
    std::cerr << "[WireProtocol] Buffer too small for pictLen" << std::endl;
    return data;
  }
  uint32_t pictLen = readUInt32LE(&bytes[offset]);
  offset += 4;

  // Read pictBytes
  if (offset + pictLen > bytes.size()) {
    std::cerr << "[WireProtocol] PictBytes extend beyond buffer" << std::endl;
    return data;
  }
  data.pictBytes.assign(bytes.begin() + offset, bytes.begin() + offset + pictLen);
  offset += pictLen;

  std::cout << "[WireProtocol] Parsed ImageData: id=" << data.imageId
            << " pictBytes=" << data.pictBytes.size() << " bytes" << std::endl;

  return data;
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

      if (offset + 16 > bytes.size()) {  // Updated: need 16 bytes (x, y, width, height, fontId, fontSize, rgba)
        if (i < 3)
          std::cerr << "[WireProtocol] DrawText at index " << i
                    << ": not enough bytes for header (need 16, have "
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

      if (offset + 12 > bytes.size()) {
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

      // Read imageId length and string
      if (offset + 2 > bytes.size()) {
        delete prim;
        break;
      }
      uint16_t imageIdLen = readUInt16LE(&bytes[offset]);
      offset += 2;

      if (imageIdLen > 0) {
        if (offset + imageIdLen > bytes.size()) {
          delete prim;
          break;
        }
        prim->imageId = std::string(bytes.begin() + offset, bytes.begin() + offset + imageIdLen);
        offset += imageIdLen;
      }

      // Read pictBytes length (will be 0 - images come via ImageData)
      if (offset + 4 > bytes.size()) {
        delete prim;
        break;
      }
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

  // Parse optional scrollMetadata (scrollY and scrollX)
  if (offset < bytes.size()) {
    uint8_t hasScrollMetadata = bytes[offset++];
    frame.hasScrollMetadata = (hasScrollMetadata == 1);
    if (hasScrollMetadata) {
      // Read scrollY
      if (offset + 4 <= bytes.size()) {
        frame.scrollY = (int32_t)readUInt32LE(&bytes[offset]);
        offset += 4;
      }

      // Read scrollX
      if (offset + 4 <= bytes.size()) {
        frame.scrollX = (int32_t)readUInt32LE(&bytes[offset]);
        offset += 4;
      }

      // Read viewport and document dimensions
      if (offset + 2 <= bytes.size()) {
        frame.viewportWidth = readUInt16LE(&bytes[offset]);
        offset += 2;
      }
      if (offset + 2 <= bytes.size()) {
        frame.viewportHeight = readUInt16LE(&bytes[offset]);
        offset += 2;
      }
      if (offset + 2 <= bytes.size()) {
        frame.documentWidth = readUInt16LE(&bytes[offset]);
        offset += 2;
      }
      if (offset + 2 <= bytes.size()) {
        frame.documentHeight = readUInt16LE(&bytes[offset]);
        offset += 2;
      }

      std::cout << "[WireProtocol] Parsed scroll metadata: pos=(" << frame.scrollX << "," << frame.scrollY
                << ") viewport=" << frame.viewportWidth << "x" << frame.viewportHeight
                << " document=" << frame.documentWidth << "x" << frame.documentHeight << std::endl;

      // Skip stickyElements array
      if (offset + 2 <= bytes.size()) {
        uint16_t stickyCount = readUInt16LE(&bytes[offset]);
        offset += 2;
        for (int i = 0; i < stickyCount && offset < bytes.size(); i++) {
          if (offset + 2 > bytes.size())
            break;
          uint16_t posLen = readUInt16LE(&bytes[offset]);
          offset += 2;
          offset += posLen;        // Skip position string
          offset += 4 + 4 + 2 + 2; // x, y, width, height
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
        frame.currentUrl.assign(reinterpret_cast<const char *>(&bytes[offset]),
                                urlLen);
        frame.hasCurrentUrl = true;
        offset += urlLen;
        std::cout << "[WireProtocol] Parsed currentUrl=" << frame.currentUrl
                  << std::endl;
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
      std::cout << "[WireProtocol] Parsed lastProcessedScrollSeq="
                << frame.lastProcessedScrollSeq << std::endl;
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

std::vector<uint8_t> WireProtocol::serializeScroll(int32_t scrollX,
                                                   int32_t scrollY,
                                                   uint32_t sequenceNumber) {
  std::vector<uint8_t> result(13); // 1 + 4 + 4 + 4 bytes
  result[0] = 4;                   // MessageType 4 (Scroll)
  writeUInt32LE(&result[1], (uint32_t)scrollX);
  writeUInt32LE(&result[5], (uint32_t)scrollY);
  writeUInt32LE(&result[9], sequenceNumber);
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

std::vector<uint8_t> WireProtocol::serializeResizeViewport(uint16_t width, uint16_t height) {
  std::vector<uint8_t> result(5); // 1 + 2 + 2 bytes
  result[0] = 13; // MessageType 13 (ResizeViewport)
  writeUInt16LE(&result[1], width);
  writeUInt16LE(&result[3], height);
  return result;
}

} // namespace mochila
