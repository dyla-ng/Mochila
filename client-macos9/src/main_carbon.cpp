#include "ot_websocket.h"
#include "primitive_store.h"
#include "renderer_quickdraw.h"
#include "wire_protocol.h"

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

  std::string host = "10.141.28.14"; // Host Mac IP address
  int port = 8080;
  std::string targetUrl =
      "https://en.wikipedia.org/wiki/History_of_the_Internet";

  if (argc > 1)
    host = argv[1];
  if (argc > 2)
    port = atoi(argv[2]);
  if (argc > 3)
    targetUrl = argv[3];

  // Create & Show Window FIRST so the user sees the Carbon UI immediately
  QuickDrawRenderer renderer(1024, 768, "Mochila v2 - Mac OS 9.2.2");
  if (!renderer.isValid()) {
    std::cerr << "[Mac OS 9] QuickDraw initialization failed!" << std::endl;
    return 1;
  }

  renderer.drawRect(0, 0, 1024, 768, Color(240, 240, 245, 255));
  renderer.drawText(50, 50, "Mochila v2 - Connecting to server...", 3, 18,
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
    // Send binary Init request (MessageType 3) with Little-Endian urlLen
    uint16_t urlLen = static_cast<uint16_t>(targetUrl.length());
    std::vector<uint8_t> initMsg(1 + 2 + urlLen);
    initMsg[0] = 3;                    // MessageType = 3 (Init)
    initMsg[1] = urlLen & 0xFF;        // Little-Endian low byte
    initMsg[2] = (urlLen >> 8) & 0xFF; // Little-Endian high byte
    memcpy(&initMsg[3], targetUrl.data(), urlLen);
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
    // Sleep 1 tick (16.6ms) during idle states so Mac OS 9 can sleep CPU
    if (WaitNextEvent(everyEvent, &event, 1, nil)) {
      switch (event.what) {
      case mouseDown: {
        WindowRef hitWin = NULL;
        short part = FindWindow(event.where, &hitWin);
        if (part == inContent && hitWin == renderer.getWindow()) {
          SetPortWindowPort(hitWin);
          Point localPt = event.where;
          GlobalToLocal(&localPt);

          if (localPt.v < QuickDrawRenderer::ADDRESS_BAR_HEIGHT) {
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

              std::vector<uint8_t> initMsg(1 + 2 + typedUrl.length());
              initMsg[0] = 3; // MessageType 3 (Init)
              uint16_t urlLen = typedUrl.length();
              initMsg[1] = urlLen & 0xFF;
              initMsg[2] = (urlLen >> 8) & 0xFF;
              memcpy(&initMsg[3], typedUrl.data(), urlLen);
              std::cout << "[Mac OS 9] Navigating to: " << typedUrl
                        << std::endl;
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
            WireProtocol::serializeScroll(pendingScrollY, localScrollSeq);
        ws.sendBinary(scrollMsg);
        std::cout << "[Mac OS 9] Sent scroll seq=" << localScrollSeq
                  << " scrollY=" << pendingScrollY << "px" << std::endl;
      }
      scrollPending = false;
    }

    // Poll OpenTransport TCP socket for incoming binary FrameUpdates
    if (ws.isConnected()) {
      std::vector<uint8_t> packetData;
      if (ws.pollData(packetData)) {
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
          std::cout << "[Mac OS 9] Applied server scrollY=" << update.scrollY
                    << " (server processed seq="
                    << update.lastProcessedScrollSeq << ")" << std::endl;
        } else if (!update.hasLastProcessedScrollSeq && !scrollPending) {
          // Old server without sequence numbers, fall back to scrollPending
          // check
          renderer.setScrollY(update.scrollY);
        } else {
          // Server hasn't processed our latest scroll yet, keep local position
          std::cout << "[Mac OS 9] Ignoring server scrollY (local prediction "
                       "newer, localSeq="
                    << localScrollSeq
                    << " serverSeq=" << update.lastProcessedScrollSeq << ")"
                    << std::endl;
        }

        // Send FrameAck (MessageType 2)
        FrameAck ack;
        ack.messageType = "FrameAck";
        ack.frameId = update.frameId;
        std::vector<uint8_t> ackBytes = WireProtocol::serializeFrameAck(ack);
        ws.sendBinary(ackBytes);

        // DIFFERENTIAL RENDERING: Only draw what changed!
        // On FIRST frame, use renderFrame() (clears screen, no white erase
        // rects) On subsequent frames, use renderDiff() for 10-100x speedup
        if (update.frameId == 1 || store.size() == 0) {
          renderer.renderFrame(store);
        } else {
          renderer.renderDiff(update, store);
        }
      }
    }
  }

  ws.disconnect();
  std::cout << "[Mac OS 9] Mochila Carbon Client exited cleanly." << std::endl;
  return 0;
}
