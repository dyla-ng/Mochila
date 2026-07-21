/**
 * Live server for Step 5.5 client testing
 *
 * Full pipeline integration:
 * - Step 1: Playwright primitive extraction
 * - Step 2: Font substitution
 * - Step 4: Change detection
 * - Step 5: Wire protocol + lockstep ack
 */

import { chromium, Page, Browser } from 'playwright';
import WebSocket from 'ws';
import {
  Primitive,
  PrimitiveType,
} from './types';
import {
  addIdentities,
  diffPrimitives,
  PrimitiveWithIdentity,
} from './change-detection';
import {
  buildFrameUpdate,
  serializeFrameUpdate,
  deserializeFrameAck,
  deserializeClick,
  deserializeNavigateCommand,
  deserializeKeyInput,
  deserializeMouseMove,
  deserializeMouseEnter,
  deserializeMouseLeave,
} from './wire-protocol';
import { PlaywrightBlocker } from '@ghostery/adblocker-playwright';
import fetch from 'cross-fetch';
import { pictCache } from './pict-cache';
import { ExtractionPipeline } from './pipeline';
import { rawBytesCache } from './raw-bytes-cache';


interface ClientEvent {
  type: 'scroll' | 'init' | 'click' | 'navigate' | 'keyInput' | 'mouseEnter' | 'mouseLeave';
  scrollY?: number;
  scrollSeq?: number;  // Sequence number for scroll events
  deltaY?: number;
  x?: number;
  y?: number;
  action?: number;
  isText?: boolean;
  text?: string;
}

export class LiveSession {
  private browser: Browser | null = null;
  private page: Page | null = null;
  private ws: WebSocket;
  private currentFrame: PrimitiveWithIdentity[] = [];
  private frameId: number = 0;
  private waitingForAck: boolean = false;
  private ackTimeout: NodeJS.Timeout | null = null;
  private stats = {
    framesSent: 0,
    acksReceived: 0,
    avgRoundTripMs: 0,
  };
  private frameSendTimes = new Map<number, number>();
  private sentImages = new Set<string>();
  private pendingEvents: ClientEvent[] = [];
  private processingEvent: boolean = false;
  private scrollMetadata: any = null;
  private lastProcessedScrollSeq: number = 0;  // Track last processed client scroll sequence

  constructor(ws: WebSocket) {
    this.ws = ws;
    pictCache.onCacheUpdate = () => {
      this.debouncedTriggerUpdate();
    };
  }

  async init(url: string) {
    console.log(`[Server] Initializing browser for ${url}`);
    this.browser = await chromium.launch({
      headless: true,
      args: ['--disable-web-security', '--disable-features=IsolateOrigins,site-per-process'],
    });
    this.page = await this.browser.newPage({
      viewport: { width: 1024, height: 768 },
    });

    // Clear raw image bytes cache on initialization
    rawBytesCache.clear();

    // Enable uBlock Origin / EasyList & EasyPrivacy ad blocking engine
    try {
      const blocker = await PlaywrightBlocker.fromPrebuiltAdsAndTracking(fetch);
      await blocker.enableBlockingInPage(this.page);
      console.log('[Server] uBlock Origin ad & tracking blocker enabled');
    } catch (err) {
      console.error('[Server] Failed to initialize ad blocker:', err);
    }

    // Intercept response network events to cache image buffers directly from Chromium
    this.page.on('response', async (response) => {
      const responseUrl = response.url();
      const req = response.request();
      if (
        req.resourceType() === 'image' &&
        (responseUrl.startsWith('http') || responseUrl.startsWith('data:'))
      ) {
        try {
          const buffer = await response.body();
          rawBytesCache.set(responseUrl, buffer);
        } catch {
          // ignore error retrieving body (e.g. redirected or from cache)
        }
      }
    });

    // Forward browser console messages to Node console
    this.page.on('console', msg => console.log(`[Browser] ${msg.text()}`));

    await this.page.goto(url, {
      waitUntil: 'networkidle',  // Wait for CSS and other resources to load
      timeout: 60000,
    });

    // Additional wait to ensure CSS variables are fully resolved
    await this.page.waitForTimeout(500);

    // Save actual Chrome screenshot asynchronously in background (non-blocking)
    import('path').then(path => {
      import('fs').then(fs => {
        const outputDir = path.resolve(process.cwd(), 'output');
        if (!fs.existsSync(outputDir)) {
          fs.mkdirSync(outputDir, { recursive: true });
        }
        this.page?.screenshot({ path: path.join(outputDir, 'actual_chrome.png') }).catch(() => {});
      });
    }).catch(() => {});

    // Extract initial frame immediately
    console.log('[Server] Extracting initial frame');
    pictCache.clear();
    const startTime = Date.now();
    const result = await ExtractionPipeline.run(this.page!);
    console.log(`[Server] ExtractionPipeline completed in ${Date.now() - startTime}ms`);
    this.currentFrame = addIdentities(result.primitives);
    this.scrollMetadata = result.scrollMetadata;

    console.log(`[Server] Initial frame: ${this.currentFrame.length} primitives`);

    // Diagnostic: Log first 20 text primitives to debug wrapping
    console.log('[DEBUG] First 20 text primitives:');
    const textPrimitives = result.primitives.filter(p => p.type === PrimitiveType.DrawText).slice(0, 20);
    textPrimitives.forEach((p, idx) => {
      if (p.type === PrimitiveType.DrawText) {
        console.log(`  [${idx}] (${p.x},${p.y}) fontSize=${p.fontSize} maxWidth=${(p as any).maxWidth || 'none'} "${p.text.substring(0, 50)}..."`);
      }
    });

    // Send initial frame (all added, nothing changed/removed)
    await this.sendFrame(this.currentFrame, [], []);
  }

  handleClientEvent(event: ClientEvent) {
    if (event.type === 'scroll') {
      const existingScrollIdx = this.pendingEvents.findIndex(e => e.type === 'scroll');
      if (existingScrollIdx !== -1) {
        this.pendingEvents[existingScrollIdx].scrollY = event.scrollY;
        return;
      }
    }

    this.pendingEvents.push(event);
    this.triggerProcessEvents();
  }

  private async triggerProcessEvents() {
    if (this.processingEvent || this.waitingForAck || this.pendingEvents.length === 0) {
      return;
    }

    this.processingEvent = true;
    const nextEvent = this.pendingEvents.shift()!;

    try {
      await this.processClientEvent(nextEvent);
    } catch (err) {
      console.error('[Server] Error processing client event:', err);
    } finally {
      this.processingEvent = false;
      setTimeout(() => this.triggerProcessEvents(), 0);
    }
  }

  private debounceTimeout: NodeJS.Timeout | null = null;

  private debouncedTriggerUpdate() {
    if (this.debounceTimeout) {
      clearTimeout(this.debounceTimeout);
    }
    this.debounceTimeout = setTimeout(() => {
      // Only push refresh if there isn't already a scroll or refresh pending to avoid redundant frames
      const hasPending = this.pendingEvents.some(
        e => e.type === 'scroll' || (e as any).type === 'refresh'
      );
      if (!hasPending) {
        this.pendingEvents.push({ type: 'refresh' } as any);
        this.triggerProcessEvents();
      }
    }, 300);
  }

  private async processClientEvent(event: ClientEvent) {
    if (event.type === 'scroll') {
      let targetScrollY: number;

      if (typeof event.deltaY === 'number') {
        // Relative scroll - get current position and add delta
        const currentY = await this.page!.evaluate(() => window.scrollY);
        targetScrollY = Math.max(0, currentY + event.deltaY);
        console.log(`[Server] Client scroll delta ${event.deltaY}px (${currentY} -> ${targetScrollY}), re-extracting...`);
      } else if (typeof event.scrollY === 'number') {
        // Absolute scroll
        targetScrollY = event.scrollY;
        console.log(`[Server] Client scroll to ${event.scrollY}px, re-extracting...`);
      } else {
        return;
      }

      // Scroll page
      await this.page!.evaluate((y: number) => window.scrollTo(0, y), targetScrollY);

      // Update last processed scroll sequence
      if (typeof event.scrollSeq === 'number') {
        this.lastProcessedScrollSeq = event.scrollSeq;
        console.log(`[Server] Processed scroll sequence ${event.scrollSeq}`);
      }

      // Extract new frame
      const result = await ExtractionPipeline.run(this.page!);
      const newFrame = addIdentities(result.primitives);
      this.scrollMetadata = result.scrollMetadata;

      // Diff
      const diff = diffPrimitives(this.currentFrame, newFrame);

      console.log(`[Server] Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);

      const previousFrame = this.currentFrame;

      // Update current frame
      this.currentFrame = newFrame;

      // Send update
      await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
    } else if (event.type === 'click') {
      if (typeof event.x === 'number' && typeof event.y === 'number') {
        console.log(`[Server] Client click at doc (${event.x}, ${event.y}), executing page.mouse.click...`);
        const currentScrollY = await this.page!.evaluate(() => window.scrollY);
        const viewportY = event.y - currentScrollY;

        await this.page!.mouse.click(event.x, viewportY);
        await this.page!.waitForTimeout(300);

        // Extract new frame
        const result = await ExtractionPipeline.run(this.page!);
        const newFrame = addIdentities(result.primitives);
        this.scrollMetadata = result.scrollMetadata;

        const diff = diffPrimitives(this.currentFrame, newFrame);
        console.log(`[Server] Click Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);

        const previousFrame = this.currentFrame;
        this.currentFrame = newFrame;
        await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
      }
    } else if (event.type === 'navigate') {
      if (typeof event.action === 'number') {
        const actionName = event.action === 1 ? 'Back' : (event.action === 2 ? 'Forward' : 'Reload');
        console.log(`[Server] Client requested navigation command: ${actionName}`);
        if (event.action === 1) {
          await this.page!.goBack().catch(() => {});
        } else if (event.action === 2) {
          await this.page!.goForward().catch(() => {});
        } else if (event.action === 3) {
          await this.page!.reload().catch(() => {});
        }
        await this.page!.waitForTimeout(300);

        // Extract new frame
        const result = await ExtractionPipeline.run(this.page!);
        const newFrame = addIdentities(result.primitives);
        this.scrollMetadata = result.scrollMetadata;

        const diff = diffPrimitives(this.currentFrame, newFrame);
        console.log(`[Server] Navigate Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);

        const previousFrame = this.currentFrame;
        this.currentFrame = newFrame;
        await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
      }
    } else if (event.type === 'keyInput') {
      if (typeof event.text === 'string') {
        if (event.isText) {
          console.log(`[Server] Typing text '${event.text}' into page...`);
          await this.page!.keyboard.type(event.text).catch(() => {});
        } else {
          console.log(`[Server] Pressing key '${event.text}' on page...`);
          await this.page!.keyboard.press(event.text).catch(() => {});
        }

        // Batch any additional keyInput events queued during lockstep wait
        while (this.pendingEvents.length > 0 && this.pendingEvents[0].type === 'keyInput') {
          const nextKey = this.pendingEvents.shift()!;
          if (typeof nextKey.text === 'string') {
            if (nextKey.isText) {
              console.log(`[Server] Typing batched text '${nextKey.text}'...`);
              await this.page!.keyboard.type(nextKey.text).catch(() => {});
            } else {
              console.log(`[Server] Pressing batched key '${nextKey.text}'...`);
              await this.page!.keyboard.press(nextKey.text).catch(() => {});
            }
          }
        }

        await this.page!.waitForTimeout(100);

        // Extract new frame
        const result = await ExtractionPipeline.run(this.page!);
        const newFrame = addIdentities(result.primitives);
        this.scrollMetadata = result.scrollMetadata;

        const diff = diffPrimitives(this.currentFrame, newFrame);
        console.log(`[Server] KeyInput Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);

        const previousFrame = this.currentFrame;
        this.currentFrame = newFrame;
        await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
      }
    } else if ((event as any).type === 'refresh') {
      console.log('[Server] Cache updated, re-extracting refreshed frame...');
      const result = await ExtractionPipeline.run(this.page!);
      const newFrame = addIdentities(result.primitives);
      this.scrollMetadata = result.scrollMetadata;

      const diff = diffPrimitives(this.currentFrame, newFrame);
      if (diff.added.length > 0 || diff.changed.length > 0 || diff.removed.length > 0) {
        console.log(`[Server] Refresh Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);
        const previousFrame = this.currentFrame;
        this.currentFrame = newFrame;
        await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
      } else {
        console.log('[Server] Refresh extracted frame had no changes, skipping send.');
      }
    } else if (event.type === 'mouseEnter') {
      if (typeof event.x === 'number' && typeof event.y === 'number') {
        console.log(`[Server] Dwell MouseEnter at (${event.x}, ${event.y})`);
        await this.page!.mouse.move(event.x, event.y).catch(() => {});
        const result = await ExtractionPipeline.run(this.page!);
        const newFrame = addIdentities(result.primitives);
        this.scrollMetadata = result.scrollMetadata;

        const diff = diffPrimitives(this.currentFrame, newFrame);
        if (diff.added.length > 0 || diff.changed.length > 0 || diff.removed.length > 0) {
          console.log(`[Server] MouseEnter Hover Diff: +${diff.added.length} -${diff.removed.length} ~${diff.changed.length} =${diff.unchanged.length}`);
          const previousFrame = this.currentFrame;
          this.currentFrame = newFrame;
          await this.sendFrame(diff.added, diff.changed, diff.removed, previousFrame);
        }
      }
    }
  }

  private async sendFrame(
    added: PrimitiveWithIdentity[],
    changed: PrimitiveWithIdentity[],
    removed: PrimitiveWithIdentity[],
    previousFrame?: PrimitiveWithIdentity[]
  ) {
    if (this.waitingForAck) {
      console.warn('[Server] Still waiting for ack, skipping frame');
      return;
    }

    // When images are removed from the sliding window, the C++ client evicts the texture
    // from its cache. We must mirror that eviction here so re-entering images get fresh bytes.
    for (const prim of removed) {
      if (prim.type === PrimitiveType.DrawImage) {
        this.sentImages.delete(prim.identity);
      }
    }

    // Strip image bytes for primitives the client already has cached.
    // Uses a shared helper to avoid duplicating the logic for added vs changed.
    const stripIfCached = (prim: PrimitiveWithIdentity) => {
      if (prim.type !== PrimitiveType.DrawImage) return prim;
      
      const hasBytes = !!prim.pictBytes && prim.pictBytes.length > 0;
      if (!hasBytes) {
        // It is still a placeholder, don't mark as cached on client
        return prim;
      }

      if (this.sentImages.has(prim.identity)) {
        console.log(`[Cache] Client-side Cache Hit for image ${prim.identity} (omitting bytes)`);
        return { ...prim, pictBytes: undefined };
      }
      this.sentImages.add(prim.identity);
      return prim;
    };

    const processedAdded   = added.map(stripIfCached);
    const processedChanged = changed.map(stripIfCached);

    this.frameId++;
    const frameUpdate = buildFrameUpdate(this.frameId, processedAdded, processedChanged, removed, previousFrame);

    // Add scroll metadata for client-side rendering
    if (this.scrollMetadata) {
      frameUpdate.scrollMetadata = this.scrollMetadata;
    }

    // Add current URL
    if (this.page) {
      frameUpdate.currentUrl = this.page.url();
    }

    // Add last processed scroll sequence for client-side reconciliation
    if (this.lastProcessedScrollSeq > 0) {
      frameUpdate.lastProcessedScrollSeq = this.lastProcessedScrollSeq;
    }

    const { bytes } = serializeFrameUpdate(frameUpdate);

    this.waitingForAck = true;
    this.frameSendTimes.set(this.frameId, Date.now());
    this.stats.framesSent++;

    // No disconnect timeout for Mac OS 9 testing so large payloads process completely
    if (this.ackTimeout) clearTimeout(this.ackTimeout);
    this.ackTimeout = null;

    const bytesInMB = (bytes.length / 1024 / 1024).toFixed(2);
    console.log(`[Server] Sending frame ${this.frameId}: ${frameUpdate.primitiveCount} primitives (${bytes.length} bytes = ${bytesInMB} MB)`);

    // Warn if frame is dangerously large for Mac OS 9 (>1MB)
    if (bytes.length > 1024 * 1024) {
      console.warn(`[Server] ⚠️  WARNING: Frame ${this.frameId} is ${bytesInMB} MB - may freeze Mac OS 9 client!`);
    }

    this.ws.send(bytes);
  }

  handleAck(ackBytes: Buffer) {
    const ack = deserializeFrameAck(ackBytes);

    if (!this.waitingForAck) {
      console.warn('[Server] Received unexpected ack');
      return;
    }

    if (ack.frameId !== this.frameId) {
      console.warn(`[Server] Ack frame ID mismatch: expected ${this.frameId}, got ${ack.frameId}`);
      return;
    }

    // Clear timeout
    if (this.ackTimeout) {
      clearTimeout(this.ackTimeout);
      this.ackTimeout = null;
    }

    // Calculate round-trip time
    const sendTime = this.frameSendTimes.get(ack.frameId);
    if (sendTime) {
      const rtt = Date.now() - sendTime;
      this.updateAvgRtt(rtt);
      this.frameSendTimes.delete(ack.frameId);
      console.log(`[Server] Ack received for frame ${ack.frameId} (${rtt}ms RTT)`);
    }

    this.stats.acksReceived++;
    this.waitingForAck = false;
    this.triggerProcessEvents();
  }

  private updateAvgRtt(rtt: number) {
    const n = this.stats.acksReceived;
    const avg = this.stats.avgRoundTripMs;
    this.stats.avgRoundTripMs = (avg * n + rtt) / (n + 1);
  }


  async close() {
    rawBytesCache.clear();
    if (this.debounceTimeout) {
      clearTimeout(this.debounceTimeout);
      this.debounceTimeout = null;
    }
    if (this.browser) {
      await this.browser.close();
    }
  }

  getStats() {
    return {
      ...this.stats,
      currentFramePrimitives: this.currentFrame.length,
    };
  }
}

if (process.env.NODE_ENV !== 'test') {
  // WebSocket server listening on 0.0.0.0 (all network interfaces)
  const wss = new WebSocket.Server({ port: 8080, host: '0.0.0.0' });

  console.log('[Server] WebSocket server listening on ws://0.0.0.0:8080');
  console.log('[Server] Waiting for client connection...\n');

  wss.on('connection', (ws: WebSocket) => {
    console.log('[Server] Client connected');

    let session: LiveSession | null = null;

    ws.on('message', async (data: Buffer) => {
      if (data.length === 0) return;
      const messageType = data.readUInt8(0);

      if (messageType === 3) { // Init
        const urlLen = data.readUInt16LE(1);
        const url = data.toString('utf-8', 3, 3 + urlLen);
        console.log(`[Server] Binary Init request for URL: ${url}`);
        session = new LiveSession(ws);
        await session.init(url);
      } else if (messageType === 2) { // Ack
        if (session) {
          session.handleAck(data);
        }
      } else if (messageType === 4) { // Scroll
        if (session) {
          const scrollY = data.readInt32LE(1);
          const scrollSeq = data.readUInt32LE(5);  // Read sequence number
          session.handleClientEvent({ type: 'scroll', scrollY, scrollSeq });
        }
      } else if (messageType === 5) { // Stats request
        if (session) {
          ws.send(JSON.stringify({
            type: 'stats',
            stats: session.getStats(),
          }));
        }
      } else if (messageType === 6) { // Click
        if (session) {
          const click = deserializeClick(data);
          console.log(`[Server] Binary Click request at (${click.x}, ${click.y})`);
          session.handleClientEvent({ type: 'click', x: click.x, y: click.y });
        }
      } else if (messageType === 7) { // NavigateCommand
        if (session) {
          const nav = deserializeNavigateCommand(data);
          console.log(`[Server] Binary NavigateCommand request: action ${nav.action}`);
          session.handleClientEvent({ type: 'navigate', action: nav.action });
        }
      } else if (messageType === 8) { // KeyInput
        if (session) {
          const keyInput = deserializeKeyInput(data);
          console.log(`[Server] Binary KeyInput request: isText=${keyInput.isText}, text='${keyInput.text}'`);
          session.handleClientEvent({ type: 'keyInput', isText: keyInput.isText, text: keyInput.text });
        }
      } else if (messageType === 10) { // MouseEnter
        if (session) {
          const enter = deserializeMouseEnter(data);
          session.handleClientEvent({ type: 'mouseEnter', x: enter.x, y: enter.y });
        }
      } else if (messageType === 11) { // MouseLeave
        if (session) {
          session.handleClientEvent({ type: 'mouseLeave' });
        }
      }
    });

    ws.on('close', async () => {
      console.log('[Server] Client disconnected');
      if (session) {
        await session.close();
      }
    });

    ws.on('error', (error) => {
      console.error('[Server] WebSocket error:', error);
    });
  });
}
