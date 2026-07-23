#include "ot_websocket.h"
#include "primitive_store.h"
#include "renderer_quickdraw.h"
#include "wire_protocol.h"
#include "preferences.h"

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#if defined(__MWERKS__) || defined(macintosh)
#include <SIOUX.h>
#endif

using namespace mochila;

int main(int argc, char *argv[]) {
  // Configure CodeWarrior SIOUX console settings so it stays open and doesn't
  // auto-quit
#if defined(__MWERKS__) || defined(macintosh)
  SIOUXSettings.asktosaveonclose = false;
  SIOUXSettings.autocloseonquit = false;
#endif

  // Initialize Mac OS 9 Cursor
  InitCursor();

  // Load preferences (or prompt for configuration on first launch)
  MochilaPreferences prefs;

  if (!PreferencesManager::load(prefs)) {
    std::cout << "[Mochila] No saved preferences found" << std::endl;

    // Show configuration dialog
    if (!PreferencesManager::showConfigDialog(prefs)) {
      std::cerr << "[Mochila] Configuration cancelled, exiting" << std::endl;
      return 1;
    }

    // Save preferences for next time
    PreferencesManager::save(prefs);
  }

  // Command-line arguments can override saved preferences
  std::string host = prefs.serverHost;
  int port = prefs.serverPort;
  std::string targetUrl = prefs.lastUrl;

  if (argc > 1)
    host = argv[1];
  if (argc > 2)
    port = atoi(argv[2]);
  if (argc > 3)
    targetUrl = argv[3];

  // Calculate initial window size based on screen resolution (85% of screen)
  BitMap screenBits;
  GetQDGlobalsScreenBits(&screenBits);
  Rect screenRect = screenBits.bounds;

  int screenWidth = screenRect.right - screenRect.left;
  int screenHeight = screenRect.bottom - screenRect.top;

  // Use 85% of screen size, with sensible min/max bounds
  int initialWidth = static_cast<int>(screenWidth * 0.85);
  int initialHeight = static_cast<int>((screenHeight - 40) * 0.85); // -40 for menu bar

  // Clamp to reasonable bounds
  if (initialWidth < 800) initialWidth = 800;
  if (initialWidth > 1600) initialWidth = 1600;
  if (initialHeight < 600) initialHeight = 600;
  if (initialHeight > 1200) initialHeight = 1200;

  std::cout << "[Mac OS 9] Screen: " << screenWidth << "x" << screenHeight
            << ", Window: " << initialWidth << "x" << initialHeight << " (85%)" << std::endl;

  // Create & Show Window FIRST so the user sees the Carbon UI immediately
  QuickDrawRenderer renderer(initialWidth, initialHeight, "Mochila");
  if (!renderer.isValid()) {
    std::cerr << "[Mac OS 9] QuickDraw initialization failed!" << std::endl;
    return 1;
  }

  renderer.drawRect(0, 0, renderer.getWidth(), renderer.getHeight(), Color(240, 240, 245, 255));
  renderer.drawText(50, 50, "Mochila - Connecting to server...", 3, 18,
                    Color(30, 30, 30, 255), true, false, false);
  renderer.copyGWorldToWindow();

  OpenTransportWebSocket ws;
  std::cout << "[Mac OS 9] Connecting to " << host << ":" << port << "..."
            << std::endl;

  if (!ws.connect(host, port, targetUrl)) {
    std::cerr << "[Mac OS 9] Failed to connect to host live server!"
              << std::endl;
    char portBuf[16];
    sprintf(portBuf, "%d", port);
    renderer.drawText(
        50, 80, "Failed to connect to " + host + ":" + std::string(portBuf), 3,
        14, Color(200, 0, 0, 255), false, false, false);
    renderer.copyGWorldToWindow();
  } else {
    // Send binary Init request (MessageType 3) with viewport dimensions
    // Format: [type][urlLen:2][url...][width:2][height:2]
    uint16_t urlLen = static_cast<uint16_t>(targetUrl.length());
    std::vector<uint8_t> initMsg(1 + 2 + urlLen + 2 + 2);
    initMsg[0] = 3;                    // MessageType = 3 (Init)
    initMsg[1] = urlLen & 0xFF;        // Little-Endian low byte
    initMsg[2] = (urlLen >> 8) & 0xFF; // Little-Endian high byte
    memcpy(&initMsg[3], targetUrl.data(), urlLen);

    // Append viewport dimensions
    uint16_t viewportWidth = static_cast<uint16_t>(renderer.getWidth());
    uint16_t viewportHeight = static_cast<uint16_t>(renderer.getHeight());
    size_t offset = 3 + urlLen;
    initMsg[offset] = viewportWidth & 0xFF;
    initMsg[offset + 1] = (viewportWidth >> 8) & 0xFF;
    initMsg[offset + 2] = viewportHeight & 0xFF;
    initMsg[offset + 3] = (viewportHeight >> 8) & 0xFF;

    std::cout << "[Mac OS 9] Sending Init with viewport: " << viewportWidth << "x" << viewportHeight << std::endl;
    ws.sendBinary(initMsg);
  }

  // Set initial URL in custom address bar
  renderer.setCurrentUrl(targetUrl);

  PrimitiveStore store;
  bool shouldQuit = false;

  // Bring Mochila Carbon Browser window to front
  SelectWindow(renderer.getWindow());

  bool scrollPending = false;
  int pendingScrollY = 0;
  unsigned long scrollStoppedTicks = 0;
  uint32_t localScrollSeq =
      0; // Client-side scroll sequence number for reconciliation

  // Non-blocking Classic Mac Event & Network Loop
  while (!shouldQuit) {
    EventRecord event;
    // Sleep 1 tick (~16ms) to allow network I/O polling without burning CPU
    if (WaitNextEvent(everyEvent, &event, 1, nil)) {
      switch (event.what) {
      case mouseDown: {
        WindowRef hitWin = NULL;
        short part = FindWindow(event.where, &hitWin);
        if (part == inContent && hitWin == renderer.getWindow()) {
          SetPortWindowPort(hitWin);
          Point localPt = event.where;
          GlobalToLocal(&localPt);

          // Check if click is in a scrollbar control
          ControlRef hitControl;
          short controlPart = FindControl(localPt, hitWin, &hitControl);
          if (controlPart != 0 && hitControl != NULL) {
            // Handle scrollbar tracking
            short trackResult = TrackControl(hitControl, localPt, NULL);
            if (trackResult != 0) {
              // Get new scrollbar value
              int newValue = GetControlValue(hitControl);

              // Determine which scrollbar was clicked
              if (hitControl == renderer.getVerticalScrollBar()) {
                renderer.setScrollY(newValue);
                scrollPending = true;
                pendingScrollY = newValue;
                scrollStoppedTicks = TickCount();
                renderer.renderFrame(store);  // Re-render with new scroll position
              } else if (hitControl == renderer.getHorizontalScrollBar()) {
                renderer.setScrollX(newValue);
                scrollPending = true;
                pendingScrollY = renderer.getScrollY();  // Keep Y unchanged
                scrollStoppedTicks = TickCount();
                renderer.renderFrame(store);  // Re-render with new scroll position
              }
            }
          } else if (localPt.v < QuickDrawRenderer::ADDRESS_BAR_HEIGHT) {
            if (localPt.h >= 8 && localPt.h <= 38) { // Back [<]
              if (ws.isConnected()) {
                std::vector<uint8_t> navMsg =
                    WireProtocol::serializeNavigateCommand(1);
                ws.sendBinary(navMsg);
              }
            } else if (localPt.h >= 44 && localPt.h <= 74) { // Forward [>]
              if (ws.isConnected()) {
                std::vector<uint8_t> navMsg =
                    WireProtocol::serializeNavigateCommand(2);
                ws.sendBinary(navMsg);
              }
            } else if (localPt.h >= 80 && localPt.h <= 110) { // Reload [R]
              if (ws.isConnected()) {
                std::vector<uint8_t> navMsg =
                    WireProtocol::serializeNavigateCommand(3);
                ws.sendBinary(navMsg);
              }
            } else if (localPt.h >= 118) { // URL Input Box
              renderer.selectAllAddressBar();
              renderer.updateAddressBarOnly(); // Only update address bar
            }
          } else { // Click inside web viewport
            renderer.setAddressBarFocused(false);
            renderer.updateAddressBarOnly(); // Only update address bar

            int clickX = localPt.h;
            int clickY = localPt.v - QuickDrawRenderer::ADDRESS_BAR_HEIGHT +
                         renderer.getScrollY();
            if (ws.isConnected()) {
              std::vector<uint8_t> clickMsg =
                  WireProtocol::serializeClick(clickX, clickY);
              ws.sendBinary(clickMsg);
            }
          }
        } else if (part == inDrag) {
          BitMap screenBits;
          GetQDGlobalsScreenBits(&screenBits);
          DragWindow(hitWin, event.where, &screenBits.bounds);
        } else if (part == inGrow) {
          // Handle window resize via grow box (resize handle)
          Rect sizeRect;
          sizeRect.top = 480;     // Minimum height (640x480)
          sizeRect.left = 640;    // Minimum width
          sizeRect.bottom = 1200; // Maximum height (1600x1200)
          sizeRect.right = 1600;  // Maximum width

          long growResult = GrowWindow(hitWin, event.where, &sizeRect);
          if (growResult != 0) {
            short newWidth = LoWord(growResult);
            short newHeight = HiWord(growResult);

            std::cout << "[Mac OS 9] User resized window via grow box to "
                      << newWidth << "x" << newHeight << std::endl;

            // Resize the actual window
            SizeWindow(hitWin, newWidth, newHeight, true);

            // updateEvt will fire automatically and handle the rest
            // (GWorld reallocation, server notification, etc.)
          }
        } else if (part == inZoomIn || part == inZoomOut) {
          // Handle zoom box (maximize/restore button)
          if (TrackBox(hitWin, event.where, part)) {
            // Get screen dimensions for maximize
            BitMap screenBits;
            GetQDGlobalsScreenBits(&screenBits);
            Rect screenRect = screenBits.bounds;

            if (part == inZoomIn) {
              // Maximize: use full screen minus menu bar and some margin
              short maxWidth = screenRect.right - screenRect.left - 10;
              short maxHeight = screenRect.bottom - screenRect.top - 40; // Leave room for menu bar

              std::cout << "[Mac OS 9] Zooming in (maximize) to " << maxWidth << "x" << maxHeight << std::endl;
              ZoomWindow(hitWin, part, false);
            } else {
              // Restore to standard size
              std::cout << "[Mac OS 9] Zooming out (restore)" << std::endl;
              ZoomWindow(hitWin, part, false);
            }

            // updateEvt will fire and handle GWorld reallocation
          }
        } else if (part == inGoAway) {
          if (TrackGoAway(hitWin, event.where)) {
            shouldQuit = true;
          }
        }
        break;
      }
      case keyDown:
      case autoKey: {
        unsigned char charCode = event.message & charCodeMask;
        unsigned char keyCode = (event.message & keyCodeMask) >> 8;
        bool isCmdPressed = (event.modifiers & cmdKey) != 0;

        if (isCmdPressed &&
            (charCode == 'l' || charCode == 'L')) { // Cmd+L (Focus Address Bar)
          renderer.selectAllAddressBar();
          renderer.updateAddressBarOnly(); // Only update address bar
        } else if (isCmdPressed &&
                   (charCode == 'r' || charCode == 'R')) { // Cmd+R (Reload)
          if (ws.isConnected()) {
            std::vector<uint8_t> navMsg =
                WireProtocol::serializeNavigateCommand(3);
            ws.sendBinary(navMsg);
          }
        } else if (isCmdPressed &&
                   (charCode == '[' ||
                    keyCode == 0x7B)) { // Cmd+[ or Cmd+Left (Back)
          if (ws.isConnected()) {
            std::vector<uint8_t> navMsg =
                WireProtocol::serializeNavigateCommand(1);
            ws.sendBinary(navMsg);
          }
        } else if (isCmdPressed &&
                   (charCode == ']' ||
                    keyCode == 0x7C)) { // Cmd+] or Cmd+Right (Forward)
          if (ws.isConnected()) {
            std::vector<uint8_t> navMsg =
                WireProtocol::serializeNavigateCommand(2);
            ws.sendBinary(navMsg);
          }
        } else if (renderer.isAddressBarFocused()) { // Custom Address Bar Input
                                                     // Handling
          if (charCode == '\r' || charCode == 3) { // Return / Enter (Navigate)
            std::string typedUrl = renderer.getAddressBarText();
            if (!typedUrl.empty()) {
              if (typedUrl.find("://") == std::string::npos) {
                typedUrl = "https://" + typedUrl;
              }
              renderer.setCurrentUrl(typedUrl);
              renderer.setAddressBarFocused(false);

              // Re-init session for new URL
              store.clear();
              renderer.setScrollY(0);

              // Send Init with viewport dimensions (same format as initial load)
              uint16_t urlLen = static_cast<uint16_t>(typedUrl.length());
              std::vector<uint8_t> initMsg(1 + 2 + urlLen + 2 + 2);
              initMsg[0] = 3; // MessageType 3 (Init)
              initMsg[1] = urlLen & 0xFF;
              initMsg[2] = (urlLen >> 8) & 0xFF;
              memcpy(&initMsg[3], typedUrl.data(), urlLen);

              // Append viewport dimensions
              uint16_t viewportWidth = static_cast<uint16_t>(renderer.getWidth());
              uint16_t viewportHeight = static_cast<uint16_t>(renderer.getHeight());
              size_t offset = 3 + urlLen;
              initMsg[offset] = viewportWidth & 0xFF;
              initMsg[offset + 1] = (viewportWidth >> 8) & 0xFF;
              initMsg[offset + 2] = viewportHeight & 0xFF;
              initMsg[offset + 3] = (viewportHeight >> 8) & 0xFF;

              std::cout << "[Mac OS 9] Navigating to: " << typedUrl
                        << " (viewport: " << viewportWidth << "x" << viewportHeight << ")" << std::endl;
              ws.sendBinary(initMsg);
            }
          } else if (charCode == 0x08 ||
                     charCode == 0x7F) { // Backspace / Delete
            renderer.backspaceAddressBar();
            renderer.updateAddressBarOnly(); // Only redraw address bar, not
                                             // entire page
          } else if (charCode >= 32 &&
                     charCode <= 126) { // Printable characters
            renderer.appendToAddressBar((char)charCode);
            renderer.updateAddressBarOnly(); // Only redraw address bar, not
                                             // entire page
          }
        } else if (charCode == 0x1F || keyCode == 0x7D) { // Down Arrow
          int newScroll = renderer.getScrollY() + 60;
          // SMART SCROLL: CopyBits + render only new strip - 100x faster!
          renderer.smartScroll(newScroll, store);
          scrollPending = true;
          pendingScrollY = newScroll;
          scrollStoppedTicks = TickCount();
        } else if (charCode == 0x1E || keyCode == 0x7E) { // Up Arrow
          int newScroll = renderer.getScrollY() - 60;
          if (newScroll < 0)
            newScroll = 0;
          // SMART SCROLL: CopyBits + render only new strip - 100x faster!
          renderer.smartScroll(newScroll, store);
          scrollPending = true;
          pendingScrollY = newScroll;
          scrollStoppedTicks = TickCount();
        } else if (!isCmdPressed && keyCode == 0x7B) { // Left Arrow (horizontal scroll left)
          int newScrollX = renderer.getScrollX() - 60;
          if (newScrollX < 0)
            newScrollX = 0;
          renderer.setScrollX(newScrollX);
          renderer.renderFrame(store); // Full re-render for horizontal scroll
          scrollPending = true;
          pendingScrollY = renderer.getScrollY(); // Keep Y unchanged
          scrollStoppedTicks = TickCount();
        } else if (!isCmdPressed && keyCode == 0x7C) { // Right Arrow (horizontal scroll right)
          int newScrollX = renderer.getScrollX() + 60;
          renderer.setScrollX(newScrollX);
          renderer.renderFrame(store); // Full re-render for horizontal scroll
          scrollPending = true;
          pendingScrollY = renderer.getScrollY(); // Keep Y unchanged
          scrollStoppedTicks = TickCount();
        } else {
          char c = charCode;
          std::string textStr(1, c);
          if (ws.isConnected()) {
            std::vector<uint8_t> keyMsg =
                WireProtocol::serializeKeyInput(true, textStr);
            ws.sendBinary(keyMsg);
          }
        }
        break;
      }
      case updateEvt: {
        WindowRef updateWin = (WindowRef)event.message;
        if (updateWin == renderer.getWindow()) {
          // Check if window was resized
          Rect portRect;
          GetPortBounds(GetWindowPort(updateWin), &portRect);
          int currentWidth = portRect.right - portRect.left;
          int currentHeight = portRect.bottom - portRect.top;

          if (currentWidth != renderer.getWidth() || currentHeight != renderer.getHeight()) {
            std::cout << "[Mac OS 9] Window resized to " << currentWidth << "x" << currentHeight << std::endl;

            // Handle the resize (reallocate GWorld, reposition scrollbars)
            if (renderer.handleResize(currentWidth, currentHeight)) {
              // Send resize message to server
              if (ws.isConnected()) {
                std::vector<uint8_t> resizeMsg = WireProtocol::serializeResizeViewport(
                  static_cast<uint16_t>(currentWidth),
                  static_cast<uint16_t>(currentHeight)
                );
                ws.sendBinary(resizeMsg);
                std::cout << "[Mac OS 9] Sent ResizeViewport: " << currentWidth << "x" << currentHeight << std::endl;
              }

              // Re-render with new dimensions
              renderer.renderFrame(store);
            }
          }

          BeginUpdate(updateWin);
          renderer.copyGWorldToWindow(); // Instant GWorld blit for uncovered
                                         // update region
          EndUpdate(updateWin);
        }
        break;
      }
      }
    }

    // Send debounced scroll position to server after user stops scrolling for
    // 300ms (~18 ticks)
    if (scrollPending && (TickCount() - scrollStoppedTicks) >= 18) {
      if (ws.isConnected()) {
        localScrollSeq++; // Increment sequence number for each scroll event
        std::vector<uint8_t> scrollMsg =
            WireProtocol::serializeScroll(renderer.getScrollX(), pendingScrollY, localScrollSeq);
        ws.sendBinary(scrollMsg);
        std::cout << "[Mac OS 9] Sent scroll seq=" << localScrollSeq
                  << " scroll=(" << renderer.getScrollX() << "," << pendingScrollY << ")px" << std::endl;
      }
      scrollPending = false;
    }

    // Poll OpenTransport TCP socket for incoming messages
    // IMPORTANT: Drain all pending messages in a loop (server sends multiple ImageData messages)
    if (ws.isConnected()) {
      std::vector<uint8_t> packetData;
      while (ws.pollData(packetData)) {
        // Peek at message type to determine how to parse
        uint8_t msgType = WireProtocol::peekMessageType(packetData);

        if (msgType == 1) {
          // FrameUpdate message
          FrameUpdate update = WireProtocol::parseFrameUpdate(packetData);

          // DEBUG: Log received primitives
          std::cout << "[DEBUG] FrameUpdate #" << update.frameId << " received "
                    << update.primitives.size() << " primitives" << std::endl;

        store.applyFrameUpdate(update);

        // DEBUG: Log store state
        std::cout << "[DEBUG] Store now has " << store.getPrimitives().size()
                  << " total primitives" << std::endl;

        // Update address bar with current URL from server (when navigating via
        // links)
        if (update.hasCurrentUrl && !update.currentUrl.empty()) {
          std::string oldUrl = renderer.getCurrentUrl();
          if (oldUrl != update.currentUrl) {
            renderer.setCurrentUrl(update.currentUrl);
            renderer
                .updateAddressBarOnly(); // Redraw address bar to show new URL
            std::cout << "[Mac OS 9] Updated address bar to: "
                      << update.currentUrl << std::endl;
          }
        }

        // Apply scroll position from server using sequence number
        // reconciliation Only apply if server has processed all our scroll
        // events
        if (update.hasLastProcessedScrollSeq &&
            update.lastProcessedScrollSeq >= localScrollSeq) {
          // Server has processed all our scrolls, trust server position
          renderer.setScrollY(update.scrollY);
          renderer.setScrollX(update.scrollX);
          std::cout << "[Mac OS 9] Applied server scroll=(" << update.scrollX << "," << update.scrollY << ")"
                    << " (server processed seq="
                    << update.lastProcessedScrollSeq << ")" << std::endl;
        } else if (!update.hasLastProcessedScrollSeq && !scrollPending) {
          // Old server without sequence numbers, fall back to scrollPending
          // check
          renderer.setScrollY(update.scrollY);
          renderer.setScrollX(update.scrollX);
        } else {
          // Server hasn't processed our latest scroll yet, keep local position
          std::cout << "[Mac OS 9] Ignoring server scroll (local prediction "
                       "newer, localSeq="
                    << localScrollSeq
                    << " serverSeq=" << update.lastProcessedScrollSeq << ")"
                    << std::endl;
        }

        // Update scrollbar ranges with document size (if metadata present)
        if (update.hasScrollMetadata) {
          renderer.updateScrollBarsWithDocumentSize(
            update.documentWidth, update.documentHeight,
            update.viewportWidth, update.viewportHeight
          );
        }

        // Send FrameAck (MessageType 2)
        FrameAck ack;
        ack.messageType = "FrameAck";
        ack.frameId = update.frameId;
        std::vector<uint8_t> ackBytes = WireProtocol::serializeFrameAck(ack);
        ws.sendBinary(ackBytes);

          // Render the frame (full redraw is fast on QuickDraw!)
          // Viewport culling prevents off-screen image decoding that would freeze UI
          renderer.renderFrame(store);
        } else if (msgType == 12) {
          // ImageData message
          ImageData imgData = WireProtocol::parseImageData(packetData);

          std::cout << "[Mac OS 9] Received ImageData: " << imgData.imageId
                    << " (" << imgData.pictBytes.size() << " bytes)" << std::endl;

          // Find ALL matching DrawImage primitives and populate pictBytes
          // (same image may appear multiple times on page)
          const std::vector<PrimitivePtr> &prims = store.getPrimitives();
          int populatedCount = 0;
          for (size_t i = 0; i < prims.size(); i++) {
            if (prims[i]->type == PrimitiveType_DrawImage) {
              DrawImagePrimitive *img = (DrawImagePrimitive *)prims[i];
              if (img->imageId == imgData.imageId && img->pictBytes.empty()) {
                img->pictBytes = imgData.pictBytes;
                populatedCount++;
                std::cout << "[Mac OS 9] Populated image " << imgData.imageId
                          << " at (" << img->x << "," << img->y << ")" << std::endl;
              }
            }
          }

          // Re-render if we populated any images
          if (populatedCount > 0) {
            std::cout << "[Mac OS 9] Populated " << populatedCount
                      << " instances of image " << imgData.imageId << ", re-rendering" << std::endl;
            renderer.renderFrame(store);
          }
        } else {
          std::cerr << "[Mac OS 9] Unknown message type: " << (int)msgType << std::endl;
        }
      }
    }
  }

  ws.disconnect();
  std::cout << "[Mac OS 9] Mochila Carbon Client exited cleanly." << std::endl;
  return 0;
}
