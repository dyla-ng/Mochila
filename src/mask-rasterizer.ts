/**
 * mask-rasterizer.ts
 *
 * Rasterizes SVG mask images to 1-bit monochrome bitmaps for QuickDraw CopyMask()
 *
 * SVG masks (like FontAwesome icons) are fetched, rendered to canvas at target size,
 * converted to 1-bit monochrome (threshold at 50% alpha), and packed into bytes.
 */

import { Page } from 'playwright';
// @ts-ignore
import fetch from 'node-fetch';

/**
 * Rasterize an SVG mask URL to 1-bit monochrome bitmap
 *
 * @param maskUrl - URL of SVG mask (e.g., FontAwesome caret-down.svg)
 * @param width - Target width in pixels
 * @param height - Target height in pixels
 * @param page - Playwright page for canvas rendering
 * @returns Buffer containing 1-bit packed monochrome mask data
 */
export async function rasterizeMaskTo1Bit(
  maskUrl: string,
  width: number,
  height: number,
  page: Page
): Promise<Buffer> {
  try {
    // Fetch the SVG content
    let svgContent: string;

    if (maskUrl.startsWith('data:')) {
      // Data URL - extract SVG content
      const base64Match = maskUrl.match(/^data:image\/svg\+xml;base64,(.+)$/);
      if (base64Match) {
        svgContent = Buffer.from(base64Match[1], 'base64').toString('utf-8');
      } else {
        // URL-encoded SVG
        const urlMatch = maskUrl.match(/^data:image\/svg\+xml[^,]*,(.+)$/);
        svgContent = urlMatch ? decodeURIComponent(urlMatch[1]) : maskUrl;
      }
    } else {
      // HTTP/HTTPS URL - fetch it
      const response = await fetch(maskUrl);
      svgContent = await response.text();
    }

    // Render SVG to canvas and extract alpha channel
    const imageData = await page.evaluate(
      ({ svg, w, h }) => {
        const canvas = document.createElement('canvas');
        canvas.width = w;
        canvas.height = h;
        const ctx = canvas.getContext('2d');
        if (!ctx) return null;

        // Create image from SVG
        const img = new Image();
        const svgBlob = new Blob([svg], { type: 'image/svg+xml' });
        const url = URL.createObjectURL(svgBlob);

        return new Promise<number[] | null>((resolve) => {
          img.onload = () => {
            // Draw SVG to canvas at target size
            ctx.drawImage(img, 0, 0, w, h);
            URL.revokeObjectURL(url);

            // Get RGBA pixel data
            const imgData = ctx.getImageData(0, 0, w, h);
            const pixels: number[] = [];

            // Extract alpha channel only (SVG masks use alpha as the mask value)
            for (let i = 0; i < imgData.data.length; i += 4) {
              pixels.push(imgData.data[i + 3]); // Alpha channel
            }

            resolve(pixels);
          };

          img.onerror = () => {
            URL.revokeObjectURL(url);
            resolve(null);
          };

          img.src = url;
        });
      },
      { svg: svgContent, w: width, h: height }
    );

    if (!imageData) {
      throw new Error('Failed to render SVG mask');
    }

    // Convert to 1-bit monochrome (threshold at 50% = 128)
    // Pack bits: 8 pixels per byte, MSB first
    const totalPixels = width * height;
    const byteCount = Math.ceil(totalPixels / 8);
    const maskData = Buffer.alloc(byteCount);

    for (let i = 0; i < totalPixels; i++) {
      const alpha = imageData[i];
      const bit = alpha >= 128 ? 1 : 0; // Threshold at 50%

      const byteIndex = Math.floor(i / 8);
      const bitIndex = 7 - (i % 8); // MSB first

      if (bit) {
        maskData[byteIndex] |= (1 << bitIndex);
      }
    }

    return maskData;
  } catch (error) {
    console.error('[MaskRasterizer] Error rasterizing mask:', maskUrl, error);
    // Return empty mask on error (all transparent)
    const byteCount = Math.ceil((width * height) / 8);
    return Buffer.alloc(byteCount);
  }
}

/**
 * Cache for rasterized masks to avoid re-rendering the same icon
 */
class MaskCache {
  private cache = new Map<string, Buffer>();

  getCacheKey(url: string, width: number, height: number): string {
    return `${url}|${width}|${height}`;
  }

  async get(url: string, width: number, height: number, page: Page): Promise<Buffer> {
    const key = this.getCacheKey(url, width, height);

    if (this.cache.has(key)) {
      return this.cache.get(key)!;
    }

    const maskData = await rasterizeMaskTo1Bit(url, width, height, page);
    this.cache.set(key, maskData);

    console.log(`[MaskCache] Rasterized ${url.substring(0, 60)}... (${width}x${height} -> ${maskData.length} bytes)`);

    return maskData;
  }

  has(url: string, width: number, height: number): boolean {
    return this.cache.has(this.getCacheKey(url, width, height));
  }

  clear(): void {
    this.cache.clear();
  }
}

export const maskCache = new MaskCache();
