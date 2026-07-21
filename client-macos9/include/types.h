#ifndef MACOS9_TYPES_H
#define MACOS9_TYPES_H

#include <string>
#include <vector>

#if defined(__MWERKS__) || defined(macintosh)
#include <MacTypes.h>
#include <stdlib.h>
typedef UInt8   uint8_t;
typedef UInt16  uint16_t;
typedef UInt32  uint32_t;
typedef SInt16  int16_t;
typedef SInt32  int32_t;
#else
#include <stdint.h>
#endif

namespace mochila {

struct Color {
    uint8_t r, g, b, a;
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

enum PrimitiveType {
    PrimitiveType_DrawRect = 1,
    PrimitiveType_DrawText = 2,
    PrimitiveType_DrawBorder = 3,
    PrimitiveType_DrawImage = 4,
    PrimitiveType_RemovePrimitive = 5,
    PrimitiveType_DrawMaskedImage = 7
};

struct Primitive {
    PrimitiveType type;
    std::string identity;
    int16_t zIndex;
    uint16_t treeOrder;
    bool isHovered;

    Primitive() : type(PrimitiveType_DrawRect), zIndex(0), treeOrder(0), isHovered(false) {}
    virtual ~Primitive() {}
};

typedef Primitive* PrimitivePtr;

struct DrawRectPrimitive : public Primitive {
    int32_t x, y;
    uint16_t width, height;
    Color color;
    bool hasHoverColor;
    Color hoverColor;
    uint8_t borderRadius;  // Border radius in pixels (0 = sharp corners)

    DrawRectPrimitive()
        : x(0), y(0), width(0), height(0), hasHoverColor(false), borderRadius(0) {
        type = PrimitiveType_DrawRect;
    }
};

struct DrawTextPrimitive : public Primitive {
    int32_t x, y;
    std::string text;
    std::string macRomanText;
    uint16_t fontId;
    uint16_t fontSize;
    uint16_t maxWidth;
    Color color;
    bool isBold, isItalic, isUnderline;

    bool hasHoverColor;
    Color hoverColor;
    bool hoverUnderline;

    DrawTextPrimitive()
        : x(0), y(0), fontId(1), fontSize(12), maxWidth(0),
          isBold(false), isItalic(false), isUnderline(false),
          hasHoverColor(false), hoverUnderline(false) {
        type = PrimitiveType_DrawText;
    }
};

struct DrawBorderPrimitive : public Primitive {
    int32_t x, y;
    uint16_t width, height, thickness;
    Color color;
    uint8_t borderRadius;  // Border radius in pixels (0 = sharp corners)

    DrawBorderPrimitive()
        : x(0), y(0), width(0), height(0), thickness(1), borderRadius(0) {
        type = PrimitiveType_DrawBorder;
    }
};

struct DrawImagePrimitive : public Primitive {
    int32_t x, y;
    uint16_t width, height;
    std::vector<uint8_t> pictBytes;
    PicHandle hPict;

    DrawImagePrimitive()
        : x(0), y(0), width(0), height(0), hPict(NULL) {
        type = PrimitiveType_DrawImage;
    }
};

struct DrawMaskedImagePrimitive : public Primitive {
    int32_t x, y;
    uint16_t width, height;
    Color fillColor;
    std::vector<uint8_t> maskData;  // 1-bit packed monochrome mask
    BitMap* maskBitmap;              // QuickDraw BitMap for CopyMask()

    DrawMaskedImagePrimitive()
        : x(0), y(0), width(0), height(0), maskBitmap(NULL) {
        type = PrimitiveType_DrawMaskedImage;
    }

    ~DrawMaskedImagePrimitive() {
        if (maskBitmap) {
            if (maskBitmap->baseAddr) {
                free(maskBitmap->baseAddr);
            }
            delete maskBitmap;
        }
    }
};

struct RemovePrimitive : public Primitive {
    RemovePrimitive() {
        type = PrimitiveType_RemovePrimitive;
    }
};

} // namespace mochila

#endif // MACOS9_TYPES_H
