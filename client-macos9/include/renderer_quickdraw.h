#ifndef RENDERER_QUICKDRAW_H
#define RENDERER_QUICKDRAW_H

#if defined(__MWERKS__) || defined(macintosh)
#include <Carbon.h>
#else
#include <Carbon/Carbon.h>
#endif

#include "primitive_store.h"
#include <vector>
#include <string>

namespace mochila {

class QuickDrawRenderer {
public:
    static const int ADDRESS_BAR_HEIGHT = 32;

    QuickDrawRenderer(int width, int height, const char* title);
    ~QuickDrawRenderer();

    bool isValid() const { return window_ != NULL && gWorld_ != NULL; }
    WindowRef getWindow() const { return window_; }
    ControlRef getVerticalScrollBar() const { return vScrollBar_; }
    ControlRef getHorizontalScrollBar() const { return hScrollBar_; }

    void renderFrame(PrimitiveStore& store);
    void smartScroll(int newScrollY, PrimitiveStore& store);  // CopyBits + render new strip only!
    void setScrollY(int scrollY) { scrollY_ = scrollY; }
    void setScrollX(int scrollX) { scrollX_ = scrollX; }
    int getScrollY() const { return scrollY_; }
    int getScrollX() const { return scrollX_; }
    int getViewportHeight() const { return height_ - ADDRESS_BAR_HEIGHT; }

    // Scrollbar management
    void updateScrollBarsWithDocumentSize(int docWidth, int docHeight, int viewportWidth, int viewportHeight);

    // Window resize handling
    bool handleResize(int newWidth, int newHeight);
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Direct drawing helper utilities
    void clearScreen();
    void drawRect(int x, int y, int w, int h, const Color& color, int borderRadius = 0);
    void drawText(int x, int y, const std::string& text, int fontId, int fontSize, const Color& color, bool isBold = false, bool isItalic = false, bool isUnderline = false, int targetWidth = 0);
    void drawBorder(int x, int y, int w, int h, int thickness, const Color& color, int borderRadius = 0);
    void drawImage(int x, int y, int w, int h, const std::vector<uint8_t>& pictBytes, PicHandle* cachedPicHandlePtr);
    void drawMaskedImage(int x, int y, int w, int h, const Color& fillColor, const std::vector<uint8_t>& maskData);

    // Custom QuickDraw Address Bar UI & State
    void drawAddressBar();
    void updateAddressBarOnly();  // Efficiently redraw only address bar without touching page content
    void setCurrentUrl(const std::string& url) { currentUrl_ = url; if (!addressBarFocused_) addressBarText_ = url; }
    std::string getCurrentUrl() const { return currentUrl_; }
    std::string getAddressBarText() const { return addressBarText_; }
    bool isAddressBarFocused() const { return addressBarFocused_; }
    void setAddressBarFocused(bool focused);
    void appendToAddressBar(char c);
    void backspaceAddressBar();
    void selectAllAddressBar();
    void setAddressBarText(const std::string& text) { addressBarText_ = text; }

    // Blit offscreen GWorld buffer to Carbon Window
    void copyGWorldToWindow();

    // Hardware accelerated viewport scroll
    void scrollViewport(int deltaY);

private:
    WindowRef window_;
    GWorldPtr gWorld_;
    ControlRef vScrollBar_;  // Vertical scrollbar control
    ControlRef hScrollBar_;  // Horizontal scrollbar control
    int width_;
    int height_;
    int scrollY_;
    int scrollX_;
    int lastRenderedScrollY_;  // Track scroll position of last render
    int lastRenderedScrollX_;  // Track horizontal scroll position of last render
    int lastTextY_;
    int lastPenX_;
    int lastChromiumEnd_;
    bool hasLastText_;

    // Custom Address Bar state
    std::string currentUrl_;
    std::string addressBarText_;
    bool addressBarFocused_;
    unsigned long lastFocusBlinkTicks_;

    // Scrollbar management (private)
    void updateScrollBars();
    void createScrollBars();
};

} // namespace mochila

#endif // RENDERER_QUICKDRAW_H
