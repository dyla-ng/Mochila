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

// Sort primitives by zIndex first (layering), then treeOrder (document order)
// This ensures backgrounds render before text (low zIndex) while maintaining
// correct document flow within each layer.
struct ZIndexThenTreeOrderSorter {
  bool operator()(const PrimitivePtr &a, const PrimitivePtr &b) const {
    if (!a || !b)
      return a != NULL;

    // First sort by zIndex (backgrounds before text)
    if (a->zIndex != b->zIndex)
      return a->zIndex < b->zIndex;

    // Within same zIndex, sort by treeOrder (document order)
    return a->treeOrder < b->treeOrder;
  }
};

// Helper: Convert UTF-8 to UniChar (UTF-16) for ATSUI
static std::vector<UniChar> utf8ToUniChar(const std::string &input) {
  std::vector<UniChar> result;
  result.reserve(input.length());

  size_t i = 0;
  while (i < input.length()) {
    unsigned char c = (unsigned char)input[i];

    if (c < 0x80) {
      // ASCII
      result.push_back((UniChar)c);
      i++;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < input.length()) {
      // 2-byte UTF-8
      unsigned char c2 = (unsigned char)input[i + 1];
      UniChar code = ((c & 0x1F) << 6) | (c2 & 0x3F);
      result.push_back(code);
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.length()) {
      // 3-byte UTF-8
      unsigned char c2 = (unsigned char)input[i + 1];
      unsigned char c3 = (unsigned char)input[i + 2];
      UniChar code = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
      result.push_back(code);
      i += 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < input.length()) {
      // 4-byte UTF-8 (surrogate pairs for ATSUI)
      // For simplicity, replace with replacement character
      result.push_back(0xFFFD);
      i += 4;
    } else {
      // Invalid UTF-8, skip
      result.push_back(0x003F); // '?'
      i++;
    }
  }

  return result;
}

// REMOVED: utf8ToMacRoman() is now only in wire_protocol.cpp
// Text conversion happens once during deserialization, not during rendering
// Keeping this comment to explain why the function was removed
//
// Old function signature was:
//   static std::string utf8ToMacRoman(const std::string &input)
//
// This caused double-conversion corruption! Now conversion happens once
// in wire_protocol.cpp when deserializing text primitives.

QuickDrawRenderer::QuickDrawRenderer(int width, int height, const char *title)
    : window_(NULL), gWorld_(NULL), vScrollBar_(NULL), hScrollBar_(NULL),
      width_(width), height_(height), scrollY_(0), scrollX_(0),
      lastRenderedScrollY_(0), lastRenderedScrollX_(0), lastTextY_(0), lastPenX_(0), lastChromiumEnd_(0),
      hasLastText_(false) {

  // Enable fractional character widths & font kerning tables in QuickDraw
  SetFractEnable(true);

  // Enable TrueType outline fonts for smooth, sharp vector typography on Mac OS
  // 9
  SetOutlinePreferred(true);

  // Center window on screen
  BitMap screenBits;
  GetQDGlobalsScreenBits(&screenBits);
  int screenWidth = screenBits.bounds.right - screenBits.bounds.left;
  int screenHeight = screenBits.bounds.bottom - screenBits.bounds.top;

  Rect wRect;
  wRect.left = (screenWidth - width) / 2;
  wRect.top = ((screenHeight - height) / 2) + 20; // +20 for menu bar
  wRect.right = wRect.left + width;
  wRect.bottom = wRect.top + height;

  Str255 pTitle;
  size_t len = strlen(title);
  if (len > 255)
    len = 255;
  pTitle[0] = (unsigned char)len;
  memcpy(&pTitle[1], title, len);

  // Use zoomDocProc to enable both zoom box and grow box (resize handle)
  window_ = NewCWindow(NULL, &wRect, pTitle, true, zoomDocProc, (WindowRef)-1L,
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

  // Use depth 0 (screen native depth) - this is what worked in the original version
  QDErr err = NewGWorld(&gWorld_, 0, &gRect, NULL, NULL, 0);
  if (err != noErr || !gWorld_) {
    std::cerr << "[QuickDraw] NewGWorld failed with error: " << err << std::endl;
    return;
  }

  std::cout << "[QuickDraw] Created GWorld (screen native depth)" << std::endl;

  PixMapHandle gwPixMap = GetGWorldPixMap(gWorld_);
  if (!LockPixels(gwPixMap)) {
    std::cerr << "[QuickDraw] LockPixels failed!" << std::endl;
    return;
  }

  std::cout << "[QuickDraw] GWorld: depth=" << (**gwPixMap).pixelSize
            << " pixelType=" << (**gwPixMap).pixelType << std::endl;

  UnlockPixels(gwPixMap);

  // Set up drawing context
  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  // Initialize GWorld with proper drawing state
  PenNormal(); // Set pen to normal mode (copy, not blend)
  RGBColor black = {0x0000, 0x0000, 0x0000};
  RGBForeColor(&black);
  RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
  RGBBackColor(&white);

  // Create native scrollbars
  createScrollBars();

  clearScreen();
  SetGWorld(origPort, origDev);
  copyGWorldToWindow();
}

QuickDrawRenderer::~QuickDrawRenderer() {
  // Dispose of scrollbar controls
  if (vScrollBar_) {
    DisposeControl(vScrollBar_);
    vScrollBar_ = NULL;
  }
  if (hScrollBar_) {
    DisposeControl(hScrollBar_);
    hScrollBar_ = NULL;
  }
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

  // Text is already converted to Mac Roman by wire_protocol.cpp
  // Do NOT convert again or it will corrupt the text!
  const std::string& macText = text;
  if (macText.empty()) {
    static int emptyCount = 0;
    emptyCount++;
    if (emptyCount <= 20) {
      std::cout << "[DEBUG] EMPTY TEXT #" << emptyCount << " after conversion: \""
                << text.substr(0, 50) << "\" (skipping)" << std::endl;
    }
    return;
  }

  // Check if converted text is all spaces/whitespace (invisible when rendered)
  bool allSpaces = true;
  for (size_t i = 0; i < macText.length(); i++) {
    if (macText[i] != ' ' && macText[i] != '\t' && macText[i] != '\n' && macText[i] != '\r') {
      allSpaces = false;
      break;
    }
  }
  if (allSpaces) {
    static int spacesCount = 0;
    spacesCount++;
    if (spacesCount <= 20) {
      std::cout << "[DEBUG] ALL-SPACES TEXT #" << spacesCount << " after conversion: \""
                << text.substr(0, 50) << "\" -> \"" << macText << "\" (skipping)" << std::endl;
    }
    return;
  }

  // DEBUG: Check for white text (would be invisible on white background)
  static int whiteTextCount = 0;
  if (color.r > 240 && color.g > 240 && color.b > 240) {
    whiteTextCount++;
    if (whiteTextCount <= 10) {
      std::cout << "[DEBUG] WHITE TEXT #" << whiteTextCount << ": \""
                << macText.substr(0, 30) << "\" at (" << x << "," << y << ")"
                << " color=(" << (int)color.r << "," << (int)color.g << "," << (int)color.b << ")"
                << std::endl;
    }
  }

  // DEBUG: Log first 50 text draws to find overlapping text
  static int drawCount = 0;
  static int lastY = -1000;

  if (drawCount < 50) {
    int yDiff = y - lastY;
    bool possibleOverlap = (yDiff >= 0 && yDiff < fontSize);

    std::cout << "[DEBUG] Text #" << drawCount << ": y=" << y
              << " (diff=" << yDiff << ")"
              << (possibleOverlap ? " [OVERLAP?]" : "")
              << " \"" << macText.substr(0, 40) << "\""
              << std::endl;
  }

  lastY = y;
  drawCount++;

  // Map fontId to QuickDraw font ID
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
    qdFont = kFontIDTimes; // Times (Serif)
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

  // Set QuickDraw font and style
  TextFont(qdFont);

  // Scale Geneva down by 8% to compensate for it being ~11% wider than Arial
  // This reduces the amount of CharExtra adjustment needed
  int effectiveFontSize = fontSize > 0 ? fontSize : 12;
  if (qdFont == kFontIDGeneva && effectiveFontSize > 8) {
    effectiveFontSize = (int)(effectiveFontSize * 0.92);
  }
  TextSize(effectiveFontSize);

  Style face = normal;
  if (isBold)
    face |= bold;
  if (isItalic)
    face |= italic;
  if (isUnderline)
    face |= underline;
  TextFace(face);

  // Set text mode to srcOr (transparent background)
  TextMode(srcOr);

  // Set text color
  RGBColor qdColor = {(unsigned short)(color.r * 257),
                      (unsigned short)(color.g * 257),
                      (unsigned short)(color.b * 257)};
  RGBForeColor(&qdColor);

  // Get font metrics for baseline calculation
  FontInfo finfo;
  GetFontInfo(&finfo);
  int ascent = finfo.ascent;

  // DEBUG: Log ascent value for first few draws
  if (drawCount <= 3) {
    std::cout << "[DEBUG] FontInfo: ascent=" << ascent
              << " descent=" << finfo.descent
              << " leading=" << finfo.leading
              << " -> final baseline y=" << (y + ascent) << std::endl;
  }

  // Measure actual rendered width and adjust spacing to fit targetWidth
  bool spacingAdjusted = false;
  if (targetWidth > 0 && macText.length() > 0) {
    // Measure how wide QuickDraw will render this text
    int actualWidth = TextWidth(macText.data(), 0, macText.length());
    int widthDiff = targetWidth - actualWidth;

    // Only adjust if difference is significant (more than 2 pixels)
    if (abs(widthDiff) > 2) {
      // Distribute spacing adjustment across all characters
      // Use Fixed-point math (16.16 format): shift left 16 bits
      Fixed charExtraAmount = ((long)widthDiff << 16) / (long)macText.length();
      CharExtra(charExtraAmount);
      spacingAdjusted = true;

      // DEBUG: Log spacing adjustments for first few
      static int adjustCount = 0;
      if (adjustCount < 10) {
        std::cout << "[DEBUG] Text spacing: actual=" << actualWidth
                  << " target=" << targetWidth
                  << " diff=" << widthDiff
                  << " charExtra=" << (charExtraAmount >> 16) << "." << (charExtraAmount & 0xFFFF)
                  << " text=\"" << text.substr(0, 30) << "\"" << std::endl;
        adjustCount++;
      }
    }
  }

  // Clip text to its bounding box to prevent overflow (as final safeguard)
  if (targetWidth > 0) {
    Rect clipRect;
    clipRect.left = x;
    clipRect.top = y;
    clipRect.right = x + targetWidth;
    clipRect.bottom = y + fontSize + 10; // Add some padding for descenders
    ClipRect(&clipRect);
  }

  // Position pen at baseline (DOMSnapshot gives top-left, we need baseline)
  MoveTo(x, y + ascent);

  // Draw text with adjusted spacing
  DrawText(macText.data(), 0, macText.length());

  // Reset spacing adjustment
  if (spacingAdjusted) {
    CharExtra(0);
  }

  // Reset clip region to full viewport
  if (targetWidth > 0) {
    Rect fullClip;
    fullClip.left = 0;
    fullClip.top = ADDRESS_BAR_HEIGHT;
    fullClip.right = 10000;
    fullClip.bottom = 10000;
    ClipRect(&fullClip);
  }

  // DEBUG: Draw red border around text area to visualize what's being drawn
  // Use actual DOMSnapshot bounding box dimensions instead of guessing
  if (targetWidth > 0) {  // Only draw if we have valid dimensions
    RGBColor red = {0xFFFF, 0x0000, 0x0000};
    RGBForeColor(&red);
    PenSize(1, 1);

    Rect debugRect;
    debugRect.left = x;
    debugRect.top = y;
    debugRect.right = x + targetWidth;  // Use actual width from DOMSnapshot
    debugRect.bottom = y + fontSize + 4; // Use fontSize for height (close enough)
    FrameRect(&debugRect);

    // Restore color
    RGBForeColor(&qdColor);
  }
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
                                  PicHandle *cachedPicHandlePtr) {
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

  // Diagnostic logging
  if (maskData.empty()) {
    std::cout << "[DrawMaskedImage] WARNING: Empty maskData at (" << x << "," << y
              << ") size " << w << "x" << h << " - nothing will render!" << std::endl;
    return;
  }

  if (w <= 0 || h <= 0)
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
  // Use PaintRect for reliable pixel rendering in GWorld
  int pixelsDrawn = 0;
  for (int py = 0; py < h; py++) {
    for (int px = 0; px < w; px++) {
      int bitIndex = py * w + px;
      int byteIndex = bitIndex / 8;
      int bitOffset = 7 - (bitIndex % 8); // MSB first

      if (byteIndex < maskData.size()) {
        uint8_t byte = maskData[byteIndex];
        bool pixelOn = (byte & (1 << bitOffset)) != 0;

        if (pixelOn) {
          // Draw this pixel as a 1x1 rectangle
          Rect pixelRect;
          pixelRect.left = x + px;
          pixelRect.top = y + py;
          pixelRect.right = x + px + 1;
          pixelRect.bottom = y + py + 1;
          PaintRect(&pixelRect);
          pixelsDrawn++;
        }
      }
    }
  }

  // Diagnostic: warn if no pixels were drawn
  if (pixelsDrawn == 0) {
    std::cout << "[DrawMaskedImage] WARNING: Mask at (" << x << "," << y
              << ") " << w << "x" << h << " has ALL ZERO bits - invisible!" << std::endl;
    std::cout << "[DrawMaskedImage] MaskData size: " << maskData.size()
              << " bytes, fillColor: RGB(" << (int)fillColor.r << ","
              << (int)fillColor.g << "," << (int)fillColor.b << ")" << std::endl;
  }

  // Reset foreground color to black
  RGBColor black = {0, 0, 0};
  RGBForeColor(&black);
}

void QuickDrawRenderer::drawAddressBar() {
  if (!gWorld_)
    return;

  const int kScrollBarWidth = 16;  // Standard Mac OS scrollbar width

  // 1. Toolbar Background Rect (Light Grey) - exclude scrollbar area
  Color bgBar(235, 235, 238);
  drawRect(0, 0, width_ - kScrollBarWidth, ADDRESS_BAR_HEIGHT, bgBar);

  // Bottom border line
  Color lineBorder(180, 180, 185);
  drawRect(0, ADDRESS_BAR_HEIGHT - 1, width_ - kScrollBarWidth, 1, lineBorder);

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
    displayUrl = " ";
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
  CGrafPtr winPort = GetWindowPort(window_);
  PixMapHandle srcPixMap = GetGWorldPixMap(gWorld_);
  PixMapHandle dstPixMap = GetPortPixMap(winPort);
  LockPixels(srcPixMap);

  const int kScrollBarWidth = 16;  // Standard Mac OS scrollbar width

  Rect addressBarBounds;
  addressBarBounds.left = 0;
  addressBarBounds.top = 0;
  addressBarBounds.right = width_ - kScrollBarWidth;  // Exclude scrollbar area
  addressBarBounds.bottom = ADDRESS_BAR_HEIGHT;

  // Use PixMap for color graphics, not BitMap
  CopyBits((BitMap *)*srcPixMap, (BitMap *)*dstPixMap, &addressBarBounds,
           &addressBarBounds, srcCopy, NULL);
  UnlockPixels(srcPixMap);
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
  PixMapHandle pm = GetGWorldPixMap(gWorld_);

  if (LockPixels(pm)) {
    const int kScrollBarWidth = 16;  // Standard Mac OS scrollbar width

    Rect srcBounds;
    srcBounds.left = 0;
    srcBounds.top = 0;
    srcBounds.right = width_;
    srcBounds.bottom = height_;

    // Destination rect excludes scrollbar areas (16px on right and bottom)
    Rect dstBounds;
    dstBounds.left = 0;
    dstBounds.top = 0;
    dstBounds.right = width_ - kScrollBarWidth;   // Exclude vertical scrollbar
    dstBounds.bottom = height_ - kScrollBarWidth;  // Exclude horizontal scrollbar

    // Use GetPortBitMapForCopyBits - this is what worked in the original version
    CopyBits((BitMap *)*pm, GetPortBitMapForCopyBits(winPort), &srcBounds, &dstBounds,
             srcCopy, NULL);
    UnlockPixels(pm);
  }
}

void QuickDrawRenderer::renderFrame(PrimitiveStore &store) {
  if (!gWorld_ || !window_)
    return;

  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(gWorld_, NULL);

  clearScreen();

  // Set clip region to viewport (classic Mac approach for culling)
  // QuickDraw will automatically skip off-screen drawing operations
  Rect clipRect;
  clipRect.top = ADDRESS_BAR_HEIGHT;
  clipRect.left = 0;
  clipRect.bottom = height_;
  clipRect.right = width_;
  ClipRect(&clipRect);

  // CRITICAL FIX: Render by PRIMITIVE TYPE to ensure proper layering.
  // Backgrounds must render first, then borders, then text on top.
  // Rendering in pure document order causes backgrounds to cover text.
  const std::vector<PrimitivePtr> &allPrimitives = store.getPrimitives();

  int rectCount = 0, textCount = 0, borderCount = 0, imageCount = 0;
  int skippedText = 0;

  std::cout << "[DEBUG] renderFrame: Total primitives to render: " << allPrimitives.size() << std::endl;

  // PASS 1: Render all backgrounds (DrawRect)
  for (size_t i = 0; i < allPrimitives.size(); i++) {
    PrimitivePtr prim = allPrimitives[i];
    if (!prim || prim->type != PrimitiveType_DrawRect)
      continue;

      DrawRectPrimitive *p = (DrawRectPrimitive *)prim;
      int localX = p->x - scrollX_;
      int localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      drawRect(localX, localY, p->width, p->height, color, p->borderRadius);
      rectCount++;
  }

  // PASS 2: Render all borders (DrawBorder)
  for (size_t i = 0; i < allPrimitives.size(); i++) {
    PrimitivePtr prim = allPrimitives[i];
    if (!prim || prim->type != PrimitiveType_DrawBorder)
      continue;

      DrawBorderPrimitive *p = (DrawBorderPrimitive *)prim;
      int localX = p->x - scrollX_;
      int localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      drawBorder(localX, localY, p->width, p->height, p->thickness, p->color,
                 p->borderRadius);
      borderCount++;
  }

  // PASS 3: Render all text (DrawText) - ON TOP
  for (size_t i = 0; i < allPrimitives.size(); i++) {
    PrimitivePtr prim = allPrimitives[i];
    if (!prim || prim->type != PrimitiveType_DrawText)
      continue;

      DrawTextPrimitive *p = (DrawTextPrimitive *)prim;
      int localX = p->x - scrollX_;
      int localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

      Color color =
          (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
      bool underline = p->isUnderline || (p->isHovered && p->hoverUnderline);
      const std::string &textToDraw =
          p->macRomanText.empty() ? p->text : p->macRomanText;
      drawText(localX, localY, textToDraw, p->fontId, p->fontSize, color,
               p->isBold, p->isItalic, underline, p->width);  // Use actual DOMSnapshot width
      textCount++;
  }

  // PASS 4: Render all images (DrawImage, DrawMaskedImage) - ON TOP
  // VIEWPORT CULLING: Skip images outside viewport to avoid blocking PICT decodes
  int skippedImages = 0;
  for (size_t i = 0; i < allPrimitives.size(); i++) {
    PrimitivePtr prim = allPrimitives[i];
    if (!prim)
      continue;

      int localY = 0;
      switch (prim->type) {
      case PrimitiveType_DrawImage: {
        DrawImagePrimitive *p = (DrawImagePrimitive *)prim;
        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        // Skip images outside viewport (with 100px buffer for smooth scrolling)
        // This prevents decoding dozens of off-screen images that would freeze the UI
        if (localY + p->height < ADDRESS_BAR_HEIGHT - 100 || localY > height_ + 100) {
          skippedImages++;
          break;
        }

        drawImage(localX, localY, p->width, p->height, p->pictBytes, &p->hPict);
        imageCount++;
        break;
      }
      case PrimitiveType_DrawMaskedImage: {
        DrawMaskedImagePrimitive *p = (DrawMaskedImagePrimitive *)prim;
        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        // Skip masked images outside viewport too
        if (localY + p->height < ADDRESS_BAR_HEIGHT - 100 || localY > height_ + 100) {
          skippedImages++;
          break;
        }

        std::cout << "[DEBUG] DrawMaskedImage: pos=(" << localX << "," << localY
                  << ") size=" << p->width << "x" << p->height << " color=("
                  << (int)p->fillColor.r << "," << (int)p->fillColor.g << ","
                  << (int)p->fillColor.b << ")"
                  << " maskBytes=" << p->maskData.size() << std::endl;

        drawMaskedImage(localX, localY, p->width, p->height, p->fillColor,
                        p->maskData);
        imageCount++; // Count as image for stats
        break;
      }
      default:
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

  // Draw sticky custom QuickDraw address bar on top of page content inside
  // GWorld
  drawAddressBar();

  std::cout << "[DEBUG] renderFrame complete:" << std::endl;
  std::cout << "  - Total primitives: " << allPrimitives.size() << std::endl;
  std::cout << "  - Rendered: " << rectCount << " rects, " << textCount << " texts, "
            << borderCount << " borders, " << imageCount << " images" << std::endl;
  std::cout << "  - Skipped: " << skippedText << " texts, " << skippedImages << " images (viewport culling)" << std::endl;
  std::cout << "  - NOTE: Check above for EMPTY TEXT or WHITE TEXT warnings" << std::endl;

  lastRenderedScrollY_ = scrollY_; // Track scroll position of this render
  lastRenderedScrollX_ = scrollX_;

  SetGWorld(origPort, origDev);

  // Blit GWorld double-buffer to Carbon Window
  copyGWorldToWindow();

  // Update scrollbar positions to match current scroll
  updateScrollBars();
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
  // Use primitives in server order (document order)
  const std::vector<PrimitivePtr> &allPrimitives = store.getPrimitives();

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

  // Render primitives in document order
  for (size_t i = 0; i < allPrimitives.size(); i++) {
    PrimitivePtr prim = allPrimitives[i];
    if (!prim)
      continue;

      int localY = 0;

      switch (prim->type) {
      case PrimitiveType_DrawRect: {
        DrawRectPrimitive *p = (DrawRectPrimitive *)prim;

        // Check if primitive is in the newly revealed strip
        if (p->y < stripMinY || p->y > stripMaxY)
          break;

        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;
        if (localY + p->height < ADDRESS_BAR_HEIGHT || localY > height_)
          break;

        Color color =
            (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
        drawRect(localX, localY, p->width, p->height, color, p->borderRadius);
        renderedCount++;
        break;
      }
      case PrimitiveType_DrawText: {
        DrawTextPrimitive *p = (DrawTextPrimitive *)prim;

        if (p->y < stripMinY || p->y > stripMaxY)
          break;

        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        Color color =
            (p->isHovered && p->hasHoverColor) ? p->hoverColor : p->color;
        bool underline = p->isUnderline || (p->isHovered && p->hoverUnderline);
        const std::string &textToDraw =
            p->macRomanText.empty() ? p->text : p->macRomanText;
        drawText(localX, localY, textToDraw, p->fontId, p->fontSize, color,
                 p->isBold, p->isItalic, underline, p->width);  // Use actual DOMSnapshot width
        renderedCount++;
        break;
      }
      case PrimitiveType_DrawBorder: {
        DrawBorderPrimitive *p = (DrawBorderPrimitive *)prim;

        if (p->y < stripMinY || p->y > stripMaxY)
          break;

        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        drawBorder(localX, localY, p->width, p->height, p->thickness, p->color,
                   p->borderRadius);
        renderedCount++;
        break;
      }
      case PrimitiveType_DrawImage: {
        DrawImagePrimitive *p = (DrawImagePrimitive *)prim;

        if (p->y < stripMinY || p->y > stripMaxY)
          break;

        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        drawImage(localX, localY, p->width, p->height, p->pictBytes, &p->hPict);
        renderedCount++;
        break;
      }
      case PrimitiveType_DrawMaskedImage: {
        DrawMaskedImagePrimitive *p = (DrawMaskedImagePrimitive *)prim;

        if (p->y < stripMinY || p->y > stripMaxY)
          break;

        int localX = p->x - scrollX_;
        localY = p->y - scrollY_ + ADDRESS_BAR_HEIGHT;

        drawMaskedImage(localX, localY, p->width, p->height, p->fillColor,
                        p->maskData);
        renderedCount++;
        break;
      }
      default:
        break;
      }
    } // End primitive iteration

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
  updateScrollBars();
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

void QuickDrawRenderer::createScrollBars() {
  if (!window_) return;

  const int kScrollBarWidth = 16;  // Standard Mac OS scrollbar width

  // Vertical scrollbar (right side, below address bar, above horizontal scrollbar)
  Rect vScrollRect;
  vScrollRect.left = width_ - kScrollBarWidth;
  vScrollRect.top = ADDRESS_BAR_HEIGHT;
  vScrollRect.right = width_;
  vScrollRect.bottom = height_ - kScrollBarWidth + 1;  // Leave room for horizontal scrollbar

  vScrollBar_ = NewControl(window_, &vScrollRect, "\p", true, 0, 0, 100, scrollBarProc, 0);

  // Horizontal scrollbar (bottom, left of vertical scrollbar)
  Rect hScrollRect;
  hScrollRect.left = 0;
  hScrollRect.top = height_ - kScrollBarWidth;
  hScrollRect.right = width_ - kScrollBarWidth + 1;  // Leave room for vertical scrollbar
  hScrollRect.bottom = height_;

  hScrollBar_ = NewControl(window_, &hScrollRect, "\p", true, 0, 0, 100, scrollBarProc, 0);

  std::cout << "[QuickDraw] Created native scrollbars" << std::endl;
}

void QuickDrawRenderer::updateScrollBars() {
  if (!vScrollBar_ || !hScrollBar_) return;

  // Update scrollbar thumb positions (max values should be set by updateScrollBarsWithDocumentSize)
  SetControlValue(vScrollBar_, scrollY_);
  SetControlValue(hScrollBar_, scrollX_);
}

void QuickDrawRenderer::updateScrollBarsWithDocumentSize(int docWidth, int docHeight, int viewportWidth, int viewportHeight) {
  if (!vScrollBar_ || !hScrollBar_) return;

  // Calculate maximum scrollable distance
  // Max scroll = document size - viewport size (but never negative)
  int maxScrollY = docHeight - viewportHeight;
  if (maxScrollY < 0) maxScrollY = 0;

  int maxScrollX = docWidth - viewportWidth;
  if (maxScrollX < 0) maxScrollX = 0;

  // Update scrollbar ranges
  SetControlMaximum(vScrollBar_, maxScrollY);
  SetControlMaximum(hScrollBar_, maxScrollX);

  // Update current positions
  SetControlValue(vScrollBar_, scrollY_);
  SetControlValue(hScrollBar_, scrollX_);

  std::cout << "[QuickDraw] Updated scrollbar ranges: vMax=" << maxScrollY
            << " hMax=" << maxScrollX << std::endl;
}

bool QuickDrawRenderer::handleResize(int newWidth, int newHeight) {
  if (!window_) {
    std::cerr << "[QuickDraw] handleResize: no window" << std::endl;
    return false;
  }

  // Validate dimensions (minimum 640x480, maximum 1600x1200 for Mac OS 9 memory constraints)
  if (newWidth < 640 || newHeight < 480) {
    std::cout << "[QuickDraw] Resize too small: " << newWidth << "x" << newHeight << " (min 640x480)" << std::endl;
    return false;
  }
  if (newWidth > 1600 || newHeight > 1200) {
    std::cout << "[QuickDraw] Resize too large: " << newWidth << "x" << newHeight << " (max 1600x1200)" << std::endl;
    return false;
  }

  // No change needed
  if (newWidth == width_ && newHeight == height_) {
    return false;
  }

  std::cout << "[QuickDraw] Resizing from " << width_ << "x" << height_
            << " to " << newWidth << "x" << newHeight << std::endl;

  // Step 1: Create new GWorld at new size
  Rect gRect;
  gRect.left = 0;
  gRect.top = 0;
  gRect.right = newWidth;
  gRect.bottom = newHeight;

  GWorldPtr newGWorld;
  QDErr err = NewGWorld(&newGWorld, 0, &gRect, NULL, NULL, 0);
  if (err != noErr || !newGWorld) {
    std::cerr << "[QuickDraw] Failed to create new GWorld: error " << err << std::endl;
    return false;
  }

  std::cout << "[QuickDraw] Created new GWorld successfully" << std::endl;

  // Step 2: Set up new GWorld's drawing context
  CGrafPtr origPort;
  GDHandle origDev;
  GetGWorld(&origPort, &origDev);
  SetGWorld(newGWorld, NULL);

  // Initialize drawing state
  PenNormal();
  RGBColor black = {0x0000, 0x0000, 0x0000};
  RGBForeColor(&black);
  RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
  RGBBackColor(&white);

  // Clear to white
  Rect clearRect;
  clearRect.left = 0;
  clearRect.top = 0;
  clearRect.right = newWidth;
  clearRect.bottom = newHeight;
  EraseRect(&clearRect);

  SetGWorld(origPort, origDev);

  // Step 3: Dispose old GWorld and swap to new one
  if (gWorld_) {
    DisposeGWorld(gWorld_);
  }
  gWorld_ = newGWorld;

  // Step 4: Update dimensions
  width_ = newWidth;
  height_ = newHeight;

  // Step 5: Reposition and resize scrollbars
  const int kScrollBarWidth = 16;

  if (vScrollBar_) {
    MoveControl(vScrollBar_, width_ - kScrollBarWidth, ADDRESS_BAR_HEIGHT);
    SizeControl(vScrollBar_, kScrollBarWidth, height_ - ADDRESS_BAR_HEIGHT - kScrollBarWidth + 1);
  }

  if (hScrollBar_) {
    MoveControl(hScrollBar_, 0, height_ - kScrollBarWidth);
    SizeControl(hScrollBar_, width_ - kScrollBarWidth + 1, kScrollBarWidth);
  }

  std::cout << "[QuickDraw] Resize complete, scrollbars repositioned" << std::endl;

  return true;
}

} // namespace mochila
