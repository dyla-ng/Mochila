/**
 * PICT encoder for Step 3 - converts images to PICT format entirely in memory
 */

import { spawn } from 'child_process';
import { rawBytesCache } from './raw-bytes-cache';

/**
 * Semaphore: limits the number of concurrent async operations.
 * Prevents hammering remote CDNs (e.g. Wikipedia returns 429 under load).
 */
class Semaphore {
  private queue: Array<() => void> = [];
  private running = 0;

  constructor(private readonly limit: number) {}

  async acquire(): Promise<() => void> {
    if (this.running < this.limit) {
      this.running++;
      return this.release.bind(this);
    }
    return new Promise(resolve => {
      this.queue.push(() => { this.running++; resolve(this.release.bind(this)); });
    });
  }

  private release() {
    this.running--;
    const next = this.queue.shift();
    if (next) next();
  }
}

// Global concurrency limit: max 2 simultaneous fetch+encode jobs.
// Wikipedia rate limits aggressively - keep it low to avoid 429s.
const encodeSemaphore = new Semaphore(2);

export interface PictEncodeResult {
  pictBytes: Buffer;
  originalWidth: number;
  originalHeight: number;
  pictWidth: number;
  pictHeight: number;
  encodeTimeMs: number;
}

const UA = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36';

/**
 * Fetch an image URL with automatic 429 retry.
 * Reads the Retry-After header (defaults to 2s) and waits before retrying.
 * Throws on non-retryable errors or after maxAttempts exhausted.
 */
async function fetchWithRetry(url: string, maxAttempts = 3): Promise<Buffer> {
  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    const response = await fetch(url, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Accept': 'image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8',
        'Referer': 'https://en.wikipedia.org/',
      }
    });

    if (response.status === 429) {
      const retryAfter = parseInt(response.headers.get('Retry-After') ?? '2', 10);
      const waitMs = (isNaN(retryAfter) ? 2 : retryAfter) * 1000;
      // Only log on final attempt to reduce spam
      if (attempt === maxAttempts) {
        console.warn(`[Fetch] 429 for ${url.substring(0, 60)}... — gave up after ${maxAttempts} attempts`);
      }
      if (attempt < maxAttempts) {
        await new Promise(r => setTimeout(r, waitMs));
        continue;
      }
      throw new Error(`HTTP 429 after ${maxAttempts} attempts: ${url}`);
    }

    if (!response.ok) {
      throw new Error(`Failed to download image: ${response.status} ${response.statusText}`);
    }

    return Buffer.from(await response.arrayBuffer());
  }
  throw new Error(`fetchWithRetry: exhausted attempts for ${url}`);
}

import os from 'os';

// Global ImageMagick pool semaphore limited to hardware CPU thread count
const numCpus = os.cpus().length || 4;
const magickSemaphore = new Semaphore(numCpus);

/**
 * Helper function to run an ImageMagick command by piping input to stdin
 * and returning the stdout buffer.
 */
async function runMagick(args: string[], inputBuffer: Buffer): Promise<Buffer> {
  const release = await magickSemaphore.acquire();
  try {
    return await new Promise((resolve, reject) => {
      const child = spawn('magick', args);
      const stdoutChunks: Buffer[] = [];
      const stderrChunks: Buffer[] = [];

      child.stdout.on('data', (chunk) => stdoutChunks.push(chunk));
      child.stderr.on('data', (chunk) => stderrChunks.push(chunk));

      child.on('close', (code) => {
        if (code !== 0) {
          const stderr = Buffer.concat(stderrChunks).toString('utf-8');
          reject(new Error(`magick ${args.join(' ')} failed with code ${code}: ${stderr}`));
        } else {
          resolve(Buffer.concat(stdoutChunks));
        }
      });

      child.on('error', (err) => {
        reject(err);
      });

      child.stdin.write(inputBuffer);
      child.stdin.end();
    });
  } finally {
    release();
  }
}

/**
 * Render an SVG data URL to PNG using Playwright
 * This handles CSS variables and other browser-specific SVG features
 */
async function renderSvgToPng(svgDataUrl: string, page: any, width: number, height: number): Promise<Buffer> {
  // Render SVG to PNG using the browser
  const pngDataUrl = await page.evaluate(
    ({ svgUrl, w, h }: { svgUrl: string; w: number; h: number }) => {
      return new Promise<string>((resolve, reject) => {
        const img = new Image();
        img.onload = () => {
          const canvas = document.createElement('canvas');
          canvas.width = w;
          canvas.height = h;
          const ctx = canvas.getContext('2d');
          if (!ctx) {
            reject(new Error('Could not get canvas context'));
            return;
          }
          ctx.drawImage(img, 0, 0, w, h);
          resolve(canvas.toDataURL('image/png'));
        };
        img.onerror = () => reject(new Error('Failed to load SVG'));
        img.src = svgUrl;
      });
    },
    { svgUrl: svgDataUrl, w: width, h: height }
  );

  // Convert data URL to buffer
  const base64Data = pngDataUrl.replace(/^data:image\/png;base64,/, '');
  return Buffer.from(base64Data, 'base64');
}

/**
 * Encode an image URL to PICT format entirely in memory
 * Uses ImageMagick via streams (stdin/stdout) to handle the conversion
 *
 * @param imageUrl - URL or data URL of the image to encode
 * @param maxPhotoDimension - Maximum dimension for photos (default 240)
 * @param maskColor - Optional CSS color for mask-image compositing (e.g. 'rgb(64, 66, 68)')
 * @param page - Optional Playwright page for rendering SVGs with CSS variables
 */
export async function encodeImageToPict(
  imageUrl: string,
  maxPhotoDimension: number = 240,
  maskColor?: string,
  backgroundColor?: string,
  page?: any // Optional Playwright page for rendering SVGs
): Promise<PictEncodeResult> {
  const startTime = Date.now();

  let inputBuffer: Buffer;
  let isSvgRendered = false;

  const cachedBuffer = rawBytesCache.get(imageUrl);
  if (cachedBuffer) {
    inputBuffer = cachedBuffer;
  } else if (imageUrl.startsWith('data:')) {
    const commaIndex = imageUrl.indexOf(',');
    if (commaIndex === -1) {
      throw new Error('Invalid data URL');
    }
    const metadata = imageUrl.substring(0, commaIndex);
    const dataPart = imageUrl.substring(commaIndex + 1);

    // Check if this is an SVG data URL with a page available
    const isSvg = metadata.includes('image/svg+xml');
    if (isSvg && page) {
      // Render SVG to PNG using the browser to handle CSS variables
      // Cap SVG rendering at 256x256 to prevent huge encodes
      // (4096x4096 takes 59+ seconds! Most SVGs are small icons anyway)
      const svgRenderSize = Math.min(maxPhotoDimension, 256);
      console.log(`[PICT] Rendering SVG at ${svgRenderSize}x${svgRenderSize} (capped for performance)`);
      inputBuffer = await renderSvgToPng(imageUrl, page, svgRenderSize, svgRenderSize);
      isSvgRendered = true;
    } else if (metadata.includes('base64')) {
      inputBuffer = Buffer.from(dataPart, 'base64');
    } else {
      let decoded = dataPart;
      try { decoded = decodeURIComponent(dataPart); } catch (e) {}
      decoded = decoded.replace(/\\"/g, '"').replace(/\\'/g, "'");
      inputBuffer = Buffer.from(decoded, 'utf-8');
    }
  } else {
    // Acquire concurrency slot before fetching — limits parallel CDN requests.
    const release = await encodeSemaphore.acquire();
    try {
      inputBuffer = await fetchWithRetry(imageUrl);
    } finally {
      release();
    }
  }

  // In Memory: Determine image format using magic bytes first, falling back to URL/MIME inspection
  let format = 'png'; // default fallback
  let detected = false;

  // If we rendered SVG to PNG, treat it as PNG
  if (isSvgRendered) {
    format = 'png';
    detected = true;
  }

  // Check data URL MIME type first (highest priority for data: URLs)
  if (!detected && imageUrl.startsWith('data:')) {
    const mimeEnd = imageUrl.indexOf(';');
    const mime = mimeEnd !== -1 ? imageUrl.substring(5, mimeEnd) : imageUrl.substring(5, imageUrl.indexOf(','));
    if (mime === 'image/svg+xml' || mime === 'image/svg') {
      format = 'svg';
      detected = true;
    } else if (mime === 'image/png') {
      format = 'png'; detected = true;
    } else if (mime === 'image/jpeg' || mime === 'image/jpg') {
      format = 'jpeg'; detected = true;
    } else if (mime === 'image/gif') {
      format = 'gif'; detected = true;
    } else if (mime === 'image/webp') {
      format = 'webp'; detected = true;
    }
  }

  if (!detected && inputBuffer.length >= 4) {
    const hex = inputBuffer.toString('hex', 0, 4);
    if (hex === '89504e47') {
      format = 'png';
      detected = true;
    } else if (hex.startsWith('ffd8')) {
      format = 'jpeg';
      detected = true;
    } else if (hex === '47494638') {
      format = 'gif';
      detected = true;
    } else if (hex === '52494646') { // RIFF
      if (inputBuffer.length >= 12 && inputBuffer.toString('hex', 8, 12) === '57454250') { // WEBP
        format = 'webp';
        detected = true;
      }
    } else {
      // SVG: check up to 512 bytes for <svg tag (may be preceded by <?xml declaration)
      const header = inputBuffer.toString('utf-8', 0, Math.min(inputBuffer.length, 512)).toLowerCase();
      if (header.includes('<svg')) {
        format = 'svg';
        detected = true;
      }
    }
  }

  if (!detected) {
    // Fall back to URL extension matching
    const lowerUrl = imageUrl.toLowerCase();
    if (lowerUrl.includes('.webp')) {
      format = 'webp';
    } else if (lowerUrl.includes('.jpg') || lowerUrl.includes('.jpeg')) {
      format = 'jpeg';
    } else if (lowerUrl.includes('.gif')) {
      format = 'gif';
    } else if (lowerUrl.includes('.png')) {
      format = 'png';
    } else if (lowerUrl.includes('.svg')) {
      format = 'svg';
    }
  }

  // Get original dimensions using ImageMagick identify from stdin with format hint (e.g. png:-)
  let originalWidth: number;
  let originalHeight: number;

  if (format === 'svg') {
    let svgStr = inputBuffer.toString('utf-8');
    if (svgStr.includes('currentColor')) {
      const activeColor = maskColor || 'black';
      svgStr = svgStr.replace(/currentColor/g, activeColor);
      inputBuffer = Buffer.from(svgStr, 'utf-8');
    }
    const svgContent = svgStr;
    const viewBoxMatch = svgContent.match(/viewBox=["']([^"']+)["']/);

    if (viewBoxMatch) {
      // viewBox="0 0 width height"
      const values = viewBoxMatch[1].split(/\s+/);
      if (values.length === 4) {
        originalWidth = Math.round(parseFloat(values[2]));
        originalHeight = Math.round(parseFloat(values[3]));
      } else {
        // Fallback: use default size
        originalWidth = 100;
        originalHeight = 100;
      }
    } else {
      // Try width/height attributes
      const widthMatch = svgContent.match(/width=["']?(\d+(?:\.\d+)?)/);
      const heightMatch = svgContent.match(/height=["']?(\d+(?:\.\d+)?)/);

      if (widthMatch && heightMatch) {
        originalWidth = Math.round(parseFloat(widthMatch[1]));
        originalHeight = Math.round(parseFloat(heightMatch[1]));
      } else {
        // Fallback: use default size
        originalWidth = 100;
        originalHeight = 100;
      }
    }
  } else {
    // For non-SVG formats, use ImageMagick identify
    const identifyOutput = await runMagick(['identify', '-format', '%wx%h', `${format}:-`], inputBuffer);
    const [widthStr, heightStr] = identifyOutput.toString('utf-8').trim().split('x');
    originalWidth = parseInt(widthStr);
    originalHeight = parseInt(heightStr);

    if (isNaN(originalWidth) || isNaN(originalHeight)) {
      throw new Error(`Failed to parse image dimensions from identify output: "${identifyOutput.toString('utf-8')}"`);
    }
  }

  // Note: We now resize ALL images, not just photos, to prevent
  // massive PNG/SVG diagrams from freezing the Mac OS 9 client

  // Build ImageMagick args
  const convertArgs: string[] = [];

  // For SVGs, provide size hint to ImageMagick
  if (format === 'svg') {
    convertArgs.push('-size', `${originalWidth}x${originalHeight}`);
  }

  // Start with 'format:-' to read format correctly from stdin
  convertArgs.push(`${format}:-`);

  // Calculate final PICT dimensions and configure resize command
  // CRITICAL FIX: Apply size limit to ALL images (not just photos)
  // to prevent massive PNGs/SVGs from creating 3+ MB PICT files that freeze Mac OS 9
  let pictWidth = originalWidth;
  let pictHeight = originalHeight;

  if (originalWidth > maxPhotoDimension || originalHeight > maxPhotoDimension) {
    convertArgs.push('-resize', `${maxPhotoDimension}x${maxPhotoDimension}`);
    const ratio = Math.min(maxPhotoDimension / originalWidth, maxPhotoDimension / originalHeight);
    pictWidth = Math.round(originalWidth * ratio);
    pictHeight = Math.round(originalHeight * ratio);
  }

  // Mask-image color compositing
  // For SVG icons used as masks, composite with the background color (if visible)
  const hasVisibleMaskColor = maskColor && 
                              maskColor !== 'transparent' && 
                              maskColor !== 'rgba(0, 0, 0, 0)' && 
                              !maskColor.endsWith(', 0)');

  if (hasVisibleMaskColor && format === 'svg') {
    // Fill the shape with maskColor preserving the alpha channel
    convertArgs.push('-background', maskColor);
    convertArgs.push('-alpha', 'shape');
  }

  // Flatten transparency onto parent background color (fallback to white) since PICT has no native alpha transparency
  const bgToUse = (backgroundColor && 
                   backgroundColor !== 'transparent' && 
                   backgroundColor !== 'rgba(0, 0, 0, 0)') 
                  ? backgroundColor 
                  : 'white';

  convertArgs.push('-background', bgToUse, '-flatten');

  // Output as 24-bit Direct Color PICT to stdout (DirectBitsRect opcode 0x009A)
  // Avoids 8-bit palette mapping bugs in QuickDraw
  convertArgs.push('pict:-');

  // Run conversion in memory
  const pictBytes = await runMagick(convertArgs, inputBuffer);

  const encodeTimeMs = Date.now() - startTime;

  console.log(`[PICT] Encoded ${imageUrl.substring(0, 60)}... (${originalWidth}x${originalHeight} -> ${pictWidth}x${pictHeight}, ${pictBytes.length} bytes, ${encodeTimeMs}ms)`);

  return {
    pictBytes,
    originalWidth,
    originalHeight,
    pictWidth,
    pictHeight,
    encodeTimeMs,
  };
}
