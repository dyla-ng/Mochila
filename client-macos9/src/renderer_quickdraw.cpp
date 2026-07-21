#include "renderer_quickdraw.h"
#include <iostream>
#include <stdlib.h>
#include <string.h>

#ifndef pushButProc
#define pushButProc 0
#endif

#ifndef editTextProc
#define editTextProc 16
#endif

#ifndef kControlEditTextPart
#define kControlEditTextPart 5
#endif

#ifndef kControlEditTextTextTag
#define kControlEditTextTextTag 'text'
#endif

#ifndef kControlEditTextSelectionTag
#define kControlEditTextSelectionTag 'sel '
#endif

namespace mochila {

// Helper: Convert UTF-8 encoded text to Mac Roman for native QuickDraw
// DrawString
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

QuickDrawRenderer::QuickDrawRenderer(int width, int height, const char *title)
    : window_(NULL), gWorld_(NULL), width_(width), height_(height), scrollY_(0),
      lastRenderedScrollY_(0), lastTextY_(-9999), lastPenX_(-9999),
      lastChromiumEnd_(-9999), hasLastText_(false) {

  // Enable fractional character widths & font kerning tables in QuickDraw
  SetFractEnable(true);

  // Enable TrueType outline fonts for smooth, sharp vector typography on Mac OS
  // 9
  SetOutlinePreferred(true);

  Rect wRect;
  wRect.left = 50;
  wRect.top = 50;
  wRect.right = 50 + width;
  wRect.bottom = 50 + height;

  Str255 pTitle;
  size_t len = strlen(title);
  if (len > 255)
    len = 255;
  pTitle[0] = (unsigned char)len;
  memcpy(&pTitle[1], title, len);

  window_ = NewCWindow(NULL, &wRect, pTitle, true, documentProc, (WindowRef)-1L,
                       true, 0);
  if (!window_) {
    std::cerr << "[QuickDraw] NewCWindow failed!" << std::endl;
    return;
  }

  ShowWindow(window_);
  SelectWindow(window_);
  SetPortWindowPort(window_);

  Rect gRect;
  gRect.left = 0;
  gRect.top = 0;
  gRect.right = width;
  gRect.bottom = height;

  // Use depth 0 (screen native depth) to save heap memory
  QDErr err = NewGWorld(&gWorld_, 0, &gRect, NULL, NULL, 0);
  if (err != noErr || !gWorld_) {
    std::cerr << "[QuickDraw] NewGWorld failed with error: " << err
              << std::endl;
  }

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);
  clearScreen();
  SetGWorld(origPort, origDev);

  copyGWorldToWindow();
}

QuickDrawRenderer::~QuickDrawRenderer() {
  if (gWorld_) {
    DisposeGWorld(gWorld_);
    gWorld_ = NULL;
  }
  if (window_) {
    DisposeWindow(window_);
    window_ = NULL;
  }
}

void QuickDrawRenderer::clearScreen() {
  if (!gWorld_)
    return;
  // Clears active GWorld buffer without resetting port
  Rect bounds;
  bounds.left = 0;
  bounds.top = 0;
  bounds.right = width_;
  bounds.bottom = height_;
  RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
  RGBBackColor(&white);
  EraseRect(&bounds);
}

void QuickDrawRenderer::drawRect(int x, int y, int w, int h, const Color &color,
                                 int borderRadius) {
  if (!gWorld_)
    return;

  // DEBUG: Log when we try to draw rounded rects
  static int roundedCount = 0;
  if (borderRadius > 0 && roundedCount < 5) {
    std::cout << "[DEBUG] drawRect with borderRadius=" << borderRadius
              << " at (" << x << "," << y << ") " << w << "x" << h << std::endl;
    roundedCount++;
  }

  Rect r;
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  RGBColor qdColor = {(unsigned short)(color.r * 257),
                      (unsigned short)(color.g * 257),
                      (unsigned short)(color.b * 257)};
  RGBForeColor(&qdColor);

  if (borderRadius > 0) {
    // Use PaintRoundRect for rounded corners
    // ovalWidth and ovalHeight are diameter, not radius
    PaintRoundRect(&r, borderRadius * 2, borderRadius * 2);
  } else {
    PaintRect(&r);
  }
}

void QuickDrawRenderer::drawText(int x, int y, const std::string &text,
                                 int fontId, int fontSize, const Color &color,
                                 bool isBold, bool isItalic, bool isUnderline,
                                 int targetWidth) {
  if (!gWorld_ || text.empty())
    return;

  // Convert input string from UTF-8 to Mac Roman
  std::string macText = utf8ToMacRoman(text);
  if (macText.empty())
    return;

  // Map fontId strictly to Mac OS 9 font definitions matching font-table.ts
  short qdFont = kFontIDGeneva;
  switch (fontId) {
  case 1:
  case 2:
  case 3:
  case 4:
    qdFont = kFontIDGeneva; // Geneva (Sans-Serif)
    break;
  case 5:
    qdFont = systemFont; // Chicago (System)
    break;
  case 6:
  case 7:
    qdFont = kFontIDMonaco; // Monaco (Monospace)
    break;
  case 8:
  case 9:
  case 10:
  case 11:
    qdFont = kFontIDTimes; // New York / Times (Serif)
    break;
  case 12:
  case 13:
  case 14:
    qdFont = kFontIDCourier; // Courier (Monospace)
    break;
  default:
    qdFont = kFontIDGeneva;
    break;
  }

  TextFont(qdFont);

  // Reduce font size by ~5% to account for Geneva being wider than web fonts
  // This prevents excessive CharExtra compression for normal text
  int actualSize = fontSize > 0 ? fontSize : 12;
  int adjustedSize = (int)(actualSize * 0.95f);
  if (adjustedSize < 9) adjustedSize = 9;  // Don't go below 9pt
  TextSize(adjustedSize);

  Style face = normal;
  if (isBold)
    face |= bold;
  if (isItalic)
    face |= italic;
  if (isUnderline)
    face |= underline;
  TextFace(face);

  // Set QuickDraw TextMode to srcOr (transparent text background) so background
  // rects are preserved
  TextMode(srcOr);

  RGBColor qdColor = {(unsigned short)(color.r * 257),
                      (unsigned short)(color.g * 257),
                      (unsigned short)(color.b * 257)};
  RGBForeColor(&qdColor);

  // Calculate baseline position for text rendering
  // Range.getBoundingClientRect() gives us the TOP of the text box,
  // and we need to add the ascent to get the baseline.
  const float FONT_TABLE_ASCENT_RATIO = 1.0;
  int ascent = (int)(adjustedSize * FONT_TABLE_ASCENT_RATIO);

  // Check if this text primitive is on the same visual line and adjacent to the
  // previous text primitive
  bool continuePen = false;
  if (hasLastText_ && std::abs(y - lastTextY_) <= 4) {
    int gap = x - lastPenX_;
    int chromiumGap = x - lastChromiumEnd_;

    // CRITICAL: Only continue pen for genuine inline text (paragraph words),
    // NOT for separate UI elements (buttons, nav items, etc.)
    //
    // If chromiumGap > 6px, this indicates separate elements (flexbox gaps,
    // button spacing, etc.) and we should trust the server's absolute position.
    //
    // Only continue for tight inline text where chromiumGap <= 6px (word
    // spacing)
    if (gap < 60 && chromiumGap <= 6) {
      continuePen = true;
    }
  }

  if (continuePen) {
    int targetX = lastPenX_;
    int chromiumGap = x - lastChromiumEnd_;

    // Preserve small word-spacing gaps (1-6px) from Chromium layout
    if (chromiumGap >= 2) {
      targetX += chromiumGap; // Don't cap at 8 - trust the gap for inline text
    }

    MoveTo(targetX, y + ascent);
  } else {
    MoveTo(x, y + ascent);
  }

  Str255 pStr;
  size_t len = macText.length();
  if (len > 255)
    len = 255;
  pStr[0] = (unsigned char)len;
  memcpy(&pStr[1], macText.data(), len);

  // If targetWidth is provided, check if Mac OS 9 text width exceeds target
  // container width Apply CharExtra to squeeze character spacing slightly so
  // text stays strictly inside bounds
  bool appliedCharExtra = false;
  if (targetWidth > 0 && macText.length() > 1) {
    int nativeWidth = TextWidth(macText.data(), 0, macText.length());
    if (nativeWidth > targetWidth) {
      float reducePerChar =
          (float)(targetWidth - nativeWidth) / (float)(macText.length() - 1);
      if (reducePerChar < -1.5f)
        reducePerChar = -1.5f; // Cap max compression
      CharExtra(X2Fix(reducePerChar));
      appliedCharExtra = true;
    }
  }

  DrawString(pStr);

  if (appliedCharExtra) {
    CharExtra(0); // Reset CharExtra after drawing
  }

  // Track pen position after DrawString
  Point endPen;
  GetPen(&endPen);
  int macTextWidth = TextWidth(macText.data(), 0, macText.length());
  lastTextY_ = y;
  lastPenX_ = endPen.h;
  lastChromiumEnd_ = x + (targetWidth > 0 ? targetWidth : macTextWidth);
  hasLastText_ = true;
}

void QuickDrawRenderer::drawBorder(int x, int y, int w, int h, int thickness,
                                   const Color &color, int borderRadius) {
  if (!gWorld_)
    return;

  RGBColor qdColor = {(unsigned short)(color.r * 257),
                      (unsigned short)(color.g * 257),
                      (unsigned short)(color.b * 257)};
  RGBForeColor(&qdColor);

  // IMPROVED: Draw concentric frames instead of single thick pen
  // This avoids the corner overlap artifacts
  for (int i = 0; i < thickness; i++) {
    Rect r;
    r.left = x + i;
    r.top = y + i;
    r.right = x + w - i;
    r.bottom = y + h - i;

    if (borderRadius > 0) {
      // Rounded border - decrease radius slightly for inner frames
      int adjustedRadius = borderRadius - i;
      if (adjustedRadius < 0)
        adjustedRadius = 0;
      FrameRoundRect(&r, adjustedRadius * 2, adjustedRadius * 2);
    } else {
      FrameRect(&r);
    }
  }
}

void QuickDrawRenderer::drawImage(int x, int y, int w, int h,
                                  const std::vector<uint8_t> &pictBytes,
                                  PicHandle* cachedPicHandlePtr) {
  if (!gWorld_)
    return;
  if (w <= 0 || h <= 0)
    return;

  Rect dstRect;
  dstRect.left = x;
  dstRect.top = y;
  dstRect.right = x + w;
  dstRect.bottom = y + h;

  // LAZY DECODING: Only decode PicHandle when actually drawing (browser-style)
  // This prevents freezing on large frames with 100+ images
  if (cachedPicHandlePtr != NULL && *cachedPicHandlePtr != NULL) {
    // Already decoded - use cached handle (fast path)
    DrawPicture(*cachedPicHandlePtr, &dstRect);
    return;
  }

  // Decode PICT bytes into PicHandle on first draw
  if (pictBytes.empty())
    return;

  size_t dataOffset = 0;
  if (pictBytes.size() >= 526) {
    if (pictBytes[522] == 0x00 && pictBytes[523] == 0x11 &&
        pictBytes[524] == 0x02 && pictBytes[525] == 0xFF) {
      dataOffset = 512;
    }
  }

  size_t pictLen = pictBytes.size() - dataOffset;
  if (pictLen < 10)
    return;

  PicHandle hPict = (PicHandle)NewHandle(pictLen);
  if (!hPict)
    return;

  HLock((Handle)hPict);
  memcpy(*hPict, &pictBytes[dataOffset], pictLen);
  HUnlock((Handle)hPict);

  DrawPicture(hPict, &dstRect);

  // Cache the decoded PicHandle in the primitive for next draw
  if (cachedPicHandlePtr != NULL) {
    *cachedPicHandlePtr = hPict;
  } else {
    // No cache location provided - dispose immediately (slow path)
    DisposeHandle((Handle)hPict);
  }
}

void QuickDrawRenderer::drawMaskedImage(int x, int y, int w, int h,
                                        const Color &fillColor,
                                        const std::vector<uint8_t> &maskData) {
  if (!gWorld_)
    return;
  if (w <= 0 || h <= 0 || maskData.empty())
    return;

  // maskData is 1-bit packed sequentially (8 pixels per byte, MSB first)
  // NOT row-aligned - bits are packed continuously
  int totalPixels = w * h;
  int totalBytes = (totalPixels + 7) / 8; // Round up to nearest byte

  if (maskData.size() < totalBytes) {
    std::cout << "[DrawMaskedImage] ERROR: maskData too small: have "
              << maskData.size() << " bytes, need " << totalBytes << std::endl;
    return;
  }

  // Set fill color
  RGBColor qColor;
  qColor.red = fillColor.r * 257;
  qColor.green = fillColor.g * 257;
  qColor.blue = fillColor.b * 257;
  RGBForeColor(&qColor);

  // Draw each pixel where mask is 1
  // This is simple but works correctly for small icons
  for (int py = 0; py < h; py++) {
    for (int px = 0; px < w; px++) {
      int bitIndex = py * w + px;
      int byteIndex = bitIndex / 8;
      int bitOffset = 7 - (bitIndex % 8); // MSB first

      if (byteIndex < maskData.size()) {
        uint8_t byte = maskData[byteIndex];
        bool pixelOn = (byte & (1 << bitOffset)) != 0;

        if (pixelOn) {
          // Draw this pixel
          MoveTo(x + px, y + py);
          LineTo(x + px, y + py);
        }
      }
    }
  }

  // Reset foreground color to black
  RGBColor black = {0, 0, 0};
  RGBForeColor(&black);
}

void QuickDrawRenderer::drawAddressBar() {
  if (!gWorld_)
    return;

  // 1. Toolbar Background Rect (Light Grey)
  Color bgBar(235, 235, 238);
  drawRect(0, 0, width_, ADDRESS_BAR_HEIGHT, bgBar);

  // Bottom border line
  Color lineBorder(180, 180, 185);
  drawRect(0, ADDRESS_BAR_HEIGHT - 1, width_, 1, lineBorder);

  // 2. Buttons: [<] [>] [R]
  Color btnBg(245, 245, 248);
  Color btnBorder(150, 150, 155);
  Color btnText(30, 30, 30);

  // Button 1: [<] Back
  drawRect(8, 4, 30, 24, btnBg);
  drawBorder(8, 4, 30, 24, 1, btnBorder);
  drawText(18, 9, "<", 0, 12, btnText, true);

  // Button 2: [>] Forward
  drawRect(44, 4, 30, 24, btnBg);
  drawBorder(44, 4, 30, 24, 1, btnBorder);
  drawText(54, 9, ">", 0, 12, btnText, true);

  // Button 3: [R] Reload
  drawRect(80, 4, 30, 24, btnBg);
  drawBorder(80, 4, 30, 24, 1, btnBorder);
  drawText(90, 9, "R", 0, 12, btnText, true);

  // 3. URL Text Input Box
  int urlX = 118;
  int urlW = width_ - 130;
  Color inputBg(255, 255, 255);
  Color inputBorder =
      addressBarFocused_ ? Color(50, 120, 240) : Color(170, 170, 175);

  drawRect(urlX, 4, urlW, 24, inputBg);
  drawBorder(urlX, 4, urlW, 24, addressBarFocused_ ? 2 : 1, inputBorder);

  // Display URL text
  std::string displayUrl = addressBarFocused_ ? addressBarText_ : currentUrl_;
  if (displayUrl.empty())
    displayUrl = "https://en.wikipedia.org/wiki/PowerPC_7xx";
  Color urlColor(20, 20, 20);
  drawText(urlX + 8, 8, displayUrl, 0, 12, urlColor);

  // Blinking Caret Cursor when Focused
  if (addressBarFocused_) {
    int textLen = displayUrl.length();
    int cursorX = urlX + 8 + (textLen * 7); // Approx font width
    if (cursorX > urlX + urlW - 6)
      cursorX = urlX + urlW - 6;

    Color cursorColor(0, 100, 240);
    drawRect(cursorX, 7, 2, 18, cursorColor);
  }
}

void QuickDrawRenderer::updateAddressBarOnly() {
  if (!gWorld_ || !window_)
    return;

  // Set up GWorld rendering context
  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  // Draw only the address bar (no page content)
  drawAddressBar();

  SetGWorld(origPort, origDev);

  // Blit only the address bar region to window (not the whole screen!)
  GrafPtr winPort = GetWindowPort(window_);
  PixMapHandle pixMap = GetGWorldPixMap(gWorld_);
  LockPixels(pixMap);

  Rect addressBarBounds;
  addressBarBounds.left = 0;
  addressBarBounds.top = 0;
  addressBarBounds.right = width_;
  addressBarBounds.bottom = ADDRESS_BAR_HEIGHT;

  BitMap *srcMap = (BitMap *)*pixMap;
  BitMap *dstMap = (BitMap *)GetPortBitMapForCopyBits(winPort);

  CopyBits(srcMap, dstMap, &addressBarBounds, &addressBarBounds, srcCopy, NULL);
  UnlockPixels(pixMap);
}

void QuickDrawRenderer::setAddressBarFocused(bool focused) {
  addressBarFocused_ = focused;
  if (focused && addressBarText_.empty()) {
    addressBarText_ = currentUrl_;
  }
}

void QuickDrawRenderer::appendToAddressBar(char c) { addressBarText_ += c; }

void QuickDrawRenderer::backspaceAddressBar() {
  if (!addressBarText_.empty()) {
    addressBarText_.pop_back();
  }
}

void QuickDrawRenderer::selectAllAddressBar() {
  addressBarFocused_ = true;
  addressBarText_ = currentUrl_;
}

void QuickDrawRenderer::copyGWorldToWindow() {
  if (!gWorld_ || !window_)
    return;

  SetPortWindowPort(window_);
  CGrafPtr winPort = GetWindowPort(window_);
  PixMapHandle pixMap = GetGWorldPixMap(gWorld_);

  if (LockPixels(pixMap)) {
    Rect bounds;
    bounds.left = 0;
    bounds.top = 0;
    bounds.right = width_;
    bounds.bottom = height_;

    BitMap *srcMap = (BitMap *)*pixMap;
    BitMap *dstMap = (BitMap *)GetPortBitMapForCopyBits(winPort);

    CopyBits(srcMap, dstMap, &bounds, &bounds, srcCopy, NULL);
    UnlockPixels(pixMap);
  }
}

void QuickDrawRenderer::renderFrame(PrimitiveStore &store) {
  if (!gWorld_ || !window_)
    return;

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  hasLastText_ = false;
  lastTextY_ = -9999;
  lastPenX_ = -9999;
  lastChromiumEnd_ = -9999;

  clearScreen();

  // Set clip region to viewport (classic Mac approach for culling)
  // QuickDraw will automatically skip off-screen drawing operations
  Rect clipRect;
  clipRect.top = ADDRESS_BAR_HEIGHT;
  clipRect.left = 0;
  clipRect.bottom = height_;
  clipRect.right = width_;
  ClipRect(&clipRect);

  // Iterate z-index buckets directly (no allocation overhead)
  const ZIndexMap &primitivesByZ = store.getPrimitivesByZIndex();
  int rectCount = 0, textCount = 0, borderCount = 0, imageCount = 0;

  for (ZIndexMap::const_iterator zIt = primitivesByZ.begin(); zIt != primitivesByZ.end(); ++zIt) {
    const PrimitiveBucket &bucket = zIt->second;
    for (size_t i = 0; i < bucket.size(); i++) {
      PrimitivePtr prim = bucket[i];
      if (!prim)
        continue;

      int localY = 0;
      switch (prim->type) {
    case PrimitiveType_DrawRect: {
      DrawRectPrimitive *p = (DrawRectPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      drawRect(p->x, localY, p->width, p->height, color, p->borderRadius);
      rectCount++;
      break;
    }
    case PrimitiveType_DrawText: {
      DrawTextPrimitive *p = (DrawTextPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      bool underline = p->isUnderline || (p->isHovered && p->hoverUnderline);
      const std::string &textToDraw =
          p->macRomanText.empty() ? p->text : p->macRomanText;
      drawText(p->x, localY, textToDraw, p->fontId, p->fontSize, color,
               p->isBold, p->isItalic, underline, p->maxWidth);
      textCount++;
      break;
    }
    case PrimitiveType_DrawBorder: {
      DrawBorderPrimitive *p = (DrawBorderPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawBorder(p->x, localY, p->width, p->height, p->thickness, p->color,
                 p->borderRadius);
      borderCount++;
      break;
    }
    case PrimitiveType_DrawImage: {
      DrawImagePrimitive *p = (DrawImagePrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawImage(p->x, localY, p->width, p->height, p->pictBytes, &p->hPict);
      imageCount++;
      break;
    }
    case PrimitiveType_DrawMaskedImage: {
      DrawMaskedImagePrimitive *p = (DrawMaskedImagePrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      std::cout << "[DEBUG] DrawMaskedImage: pos=(" << p->x << "," << localY
                << ") size=" << p->width << "x" << p->height << " color=("
                << (int)p->fillColor.r << "," << (int)p->fillColor.g << ","
                << (int)p->fillColor.b << ")"
                << " maskBytes=" << p->maskData.size() << std::endl;

      drawMaskedImage(p->x, localY, p->width, p->height, p->fillColor,
                      p->maskData);
      imageCount++; // Count as image for stats
      break;
    }
    case PrimitiveType_RemovePrimitive:
      break;
    }
    }  // End bucket iteration
  }  // End zIndex iteration

  // Reset clip region to full window for address bar (which is above viewport)
  Rect fullRect;
  fullRect.top = 0;
  fullRect.left = 0;
  fullRect.bottom = height_;
  fullRect.right = width_;
  ClipRect(&fullRect);

  // Draw sticky custom QuickDraw address bar on top of page content inside
  // GWorld
  drawAddressBar();

  std::cout << "[DEBUG] Rendered: " << rectCount << " rects, " << textCount
            << " texts, " << borderCount << " borders, " << imageCount
            << " images" << std::endl;

  lastRenderedScrollY_ = scrollY_; // Track scroll position of this render

  SetGWorld(origPort, origDev);

  // Blit GWorld double-buffer to Carbon Window
  copyGWorldToWindow();
}

// DIFFERENTIAL RENDERING - Only draws what changed!
// This is THE performance optimization that makes Mac OS 9 fast on complex
// pages
void QuickDrawRenderer::renderDiff(const FrameUpdate &update,
                                   PrimitiveStore &store) {
  if (!gWorld_ || !window_)
    return;

  // CRITICAL: If scroll position changed since last render, we MUST do a full
  // redraw Otherwise we'd paint primitives at wrong Y offsets over the
  // locally-scrolled view
  if (scrollY_ != lastRenderedScrollY_) {
    std::cout << "[DEBUG] Scroll changed (" << lastRenderedScrollY_ << " -> "
              << scrollY_ << "), falling back to full renderFrame()"
              << std::endl;
    renderFrame(store);
    return;
  }

  // CRITICAL: If update has 0 primitives, there's nothing to render differentially
  // Fall back to full renderFrame() to ensure screen isn't blank
  if (update.primitiveCount == 0) {
    std::cout << "[DEBUG] Empty diff (0 primitives), falling back to full renderFrame()"
              << std::endl;
    renderFrame(store);
    return;
  }

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  hasLastText_ = false;
  lastTextY_ = -9999;
  lastPenX_ = -9999;
  lastChromiumEnd_ = -9999;

  // DON'T clearScreen() - keep existing pixels intact (differential rendering!)

  // Set clip region to viewport (classic Mac approach for culling)
  Rect clipRect;
  clipRect.top = ADDRESS_BAR_HEIGHT;
  clipRect.left = 0;
  clipRect.bottom = height_;
  clipRect.right = width_;
  ClipRect(&clipRect);

  int addedCount = 0, changedCount = 0, removedCount = 0;

  // Step 1: Erase removed primitives (draw white rectangles over them)
  RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
  for (size_t i = 0; i < update.primitiveCount; i++) {
    PrimitivePtr prim = update.primitives[i];
    if (!prim)
      continue;

    if (prim->type == PrimitiveType_RemovePrimitive) {
      RemovePrimitive *p = (RemovePrimitive *)prim;
      // Erase by drawing white rect
      // Note: We don't have the bounds, so we'd need to track this.
      // For now, skip removal erasing - full redraws will clean them up
      removedCount++;
      continue;
    }
  }

  // Step 2 & 3: Draw added and changed primitives
  for (size_t i = 0; i < update.primitiveCount; i++) {
    PrimitivePtr prim = update.primitives[i];
    if (!prim)
      continue;

    int localY = 0;

    switch (prim->type) {
    case PrimitiveType_DrawRect: {
      DrawRectPrimitive *p = (DrawRectPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      // Erase old version first (for changed primitives)
      RGBForeColor(&white);
      Rect eraseRect;
      eraseRect.left = p->x;
      eraseRect.top = localY;
      eraseRect.right = p->x + p->width;
      eraseRect.bottom = localY + p->height;
      PaintRect(&eraseRect);

      // Draw new version
      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      drawRect(p->x, localY, p->width, p->height, color, p->borderRadius);
      addedCount++;
      break;
    }
    case PrimitiveType_DrawText: {
      DrawTextPrimitive *p = (DrawTextPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      // Erase old text (approximate bounds)
      RGBForeColor(&white);
      Rect eraseRect;
      eraseRect.left = p->x;
      eraseRect.top = localY;
      eraseRect.right = p->x + (p->maxWidth > 0 ? p->maxWidth : 500);
      eraseRect.bottom = localY + p->fontSize + 4;
      PaintRect(&eraseRect);

      // Draw new text
      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      bool underline = p->isUnderline || (p->isHovered && p->hoverUnderline);
      const std::string &textToDraw =
          p->macRomanText.empty() ? p->text : p->macRomanText;
      drawText(p->x, localY, textToDraw, p->fontId, p->fontSize, color,
               p->isBold, p->isItalic, underline, p->maxWidth);
      addedCount++;
      break;
    }
    case PrimitiveType_DrawBorder: {
      DrawBorderPrimitive *p = (DrawBorderPrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawBorder(p->x, localY, p->width, p->height, p->thickness, p->color,
                 p->borderRadius);
      addedCount++;
      break;
    }
    case PrimitiveType_DrawImage: {
      DrawImagePrimitive *p = (DrawImagePrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawImage(p->x, localY, p->width, p->height, p->pictBytes, &p->hPict);
      addedCount++;
      break;
    }
    case PrimitiveType_DrawMaskedImage: {
      DrawMaskedImagePrimitive *p = (DrawMaskedImagePrimitive *)prim;
      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawMaskedImage(p->x, localY, p->width, p->height, p->fillColor,
                      p->maskData);
      addedCount++;
      break;
    }
    case PrimitiveType_RemovePrimitive:
      break;
    }
  }

  // Reset clip region to full window for address bar (which is above viewport)
  Rect fullRect;
  fullRect.top = 0;
  fullRect.left = 0;
  fullRect.bottom = height_;
  fullRect.right = width_;
  ClipRect(&fullRect);

  // Always redraw address bar (it's sticky/fixed)
  drawAddressBar();

  std::cout << "[DEBUG] Diff render: +" << addedCount << " -" << removedCount
            << " (skipped full redraw of " << store.size() << " primitives)"
            << std::endl;

  lastRenderedScrollY_ = scrollY_; // Track scroll position of this render

  SetGWorld(origPort, origDev);

  // Blit GWorld to window
  copyGWorldToWindow();
}

// SMART SCROLLING - Hardware-accelerated CopyBits + render only new strip
// This is 100x faster than full redraws on scroll!
void QuickDrawRenderer::smartScroll(int newScrollY, PrimitiveStore &store) {
  if (!gWorld_ || !window_)
    return;

  int deltaY = newScrollY - scrollY_;
  if (deltaY == 0)
    return;

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  // Step 1: Hardware-accelerated pixel shift (CopyBits under the hood)
  Rect contentBounds;
  contentBounds.left = 0;
  contentBounds.top = ADDRESS_BAR_HEIGHT;
  contentBounds.right = width_;
  contentBounds.bottom = height_;

  RgnHandle updateRgn = NewRgn();
  ScrollRect(&contentBounds, 0, -deltaY, updateRgn);

  // Step 2: Calculate which primitives are in the newly revealed strip
  Rect updateBounds;
  GetRegionBounds(updateRgn, &updateBounds);

  // Clear the new strip
  RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
  RGBForeColor(&white);
  PaintRgn(updateRgn);

  // Step 3: Update scroll position
  scrollY_ = newScrollY;

  // Set clip region to viewport
  Rect clipRect;
  clipRect.top = ADDRESS_BAR_HEIGHT;
  clipRect.left = 0;
  clipRect.bottom = height_;
  clipRect.right = width_;
  ClipRect(&clipRect);

  // Step 4: Render ONLY primitives in the newly revealed strip
  // Iterate z-index buckets directly (no allocation overhead)
  const ZIndexMap &primitivesByZ = store.getPrimitivesByZIndex();
  int renderedCount = 0;

  // Determine the Y range of the newly revealed strip (in document coordinates)
  int stripMinY, stripMaxY;
  if (deltaY > 0) {
    // Scrolled down, new strip at bottom
    stripMinY =
        scrollY_ + (height_ - ADDRESS_BAR_HEIGHT) - deltaY - 50; // Buffer
    stripMaxY = scrollY_ + (height_ - ADDRESS_BAR_HEIGHT) + 50;
  } else {
    // Scrolled up, new strip at top
    stripMinY = scrollY_ - 50;
    stripMaxY = scrollY_ + (-deltaY) + 50;
  }

  for (ZIndexMap::const_iterator zIt = primitivesByZ.begin(); zIt != primitivesByZ.end(); ++zIt) {
    const PrimitiveBucket &bucket = zIt->second;
    for (size_t i = 0; i < bucket.size(); i++) {
      PrimitivePtr prim = bucket[i];
      if (!prim)
        continue;

      int localY = 0;

      switch (prim->type) {
    case PrimitiveType_DrawRect: {
      DrawRectPrimitive *p = (DrawRectPrimitive *)prim;

      // Check if primitive is in the newly revealed strip
      if (p->y < stripMinY || p->y > stripMaxY)
        break;

      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;
      if (localY + p->height < ADDRESS_BAR_HEIGHT || localY > height_)
        break;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      drawRect(p->x, localY, p->width, p->height, color, p->borderRadius);
      renderedCount++;
      break;
    }
    case PrimitiveType_DrawText: {
      DrawTextPrimitive *p = (DrawTextPrimitive *)prim;

      if (p->y < stripMinY || p->y > stripMaxY)
        break;

      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      bool underline = p->isUnderline || (p->isHovered && p->hoverUnderline);
      const std::string &textToDraw =
          p->macRomanText.empty() ? p->text : p->macRomanText;
      drawText(p->x, localY, textToDraw, p->fontId, p->fontSize, color,
               p->isBold, p->isItalic, underline, p->maxWidth);
      renderedCount++;
      break;
    }
    case PrimitiveType_DrawBorder: {
      DrawBorderPrimitive *p = (DrawBorderPrimitive *)prim;

      if (p->y < stripMinY || p->y > stripMaxY)
        break;

      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawBorder(p->x, localY, p->width, p->height, p->thickness, p->color,
                 p->borderRadius);
      renderedCount++;
      break;
    }
    case PrimitiveType_DrawImage: {
      DrawImagePrimitive *p = (DrawImagePrimitive *)prim;

      if (p->y < stripMinY || p->y > stripMaxY)
        break;

      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawImage(p->x, localY, p->width, p->height, p->pictBytes, &p->hPict);
      renderedCount++;
      break;
    }
    case PrimitiveType_DrawMaskedImage: {
      DrawMaskedImagePrimitive *p = (DrawMaskedImagePrimitive *)prim;

      if (p->y < stripMinY || p->y > stripMaxY)
        break;

      localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawMaskedImage(p->x, localY, p->width, p->height, p->fillColor,
                      p->maskData);
      renderedCount++;
      break;
    }
    default:
      break;
    }
    }  // End bucket iteration
  }  // End zIndex iteration

  // Reset clip region to full window for address bar (which is above viewport)
  Rect fullRect;
  fullRect.top = 0;
  fullRect.left = 0;
  fullRect.bottom = height_;
  fullRect.right = width_;
  ClipRect(&fullRect);

  // Redraw address bar (it's sticky)
  drawAddressBar();

  std::cout << "[DEBUG] Smart scroll: shifted " << deltaY << "px, rendered "
            << renderedCount << " primitives in new strip (saved "
            << (store.size() - renderedCount) << " draws!)" << std::endl;

  lastRenderedScrollY_ = scrollY_;

  DisposeRgn(updateRgn);
  SetGWorld(origPort, origDev);
  copyGWorldToWindow();
}

void QuickDrawRenderer::scrollViewport(int deltaY) {
  if (!gWorld_ || !window_ || deltaY == 0)
    return;

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  Rect bounds;
  bounds.left = 0;
  bounds.top = 0;
  bounds.right = width_;
  bounds.bottom = height_;

  RgnHandle updateRgn = NewRgn();
  ScrollRect(&bounds, 0, -deltaY, updateRgn);
  EraseRgn(updateRgn);
  DisposeRgn(updateRgn);

  SetGWorld(origPort, origDev);

  copyGWorldToWindow();
}

} // namespace mochila
