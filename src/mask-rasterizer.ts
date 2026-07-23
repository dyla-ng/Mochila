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
 * Supports sprite sheets via mask-position and mask-size CSS properties
 *
 * @param maskUrl - URL of SVG mask (e.g., FontAwesome caret-down.svg or sprite sheet)
 * @param width - Target width in pixels (final icon size)
 * @param height - Target height in pixels (final icon size)
 * @param page - Playwright page for canvas rendering
 * @param maskPosition - CSS mask-position (e.g., "50% 0%", "0px 10px")
 * @param maskSize - CSS mask-size (e.g., "300%", "60px 20px", "auto")
 * @returns Buffer containing 1-bit packed monochrome mask data
 */
export async function rasterizeMaskTo1Bit(
  maskUrl: string,
  width: number,
  height: number,
  page: Page,
  maskPosition: string = '0% 0%',
  maskSize: string = 'auto'
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
        const rawSvg = urlMatch ? urlMatch[1] : maskUrl;
        // Decode URL encoding (converts %23 to #, etc.) and unescape quotes
        svgContent = decodeURIComponent(rawSvg)
          .replace(/\\"/g, '"')
          .replace(/\\'/g, "'");
      }
    } else {
      // HTTP/HTTPS URL - fetch it
      const response = await fetch(maskUrl);
      svgContent = await response.text();
    }

    // Render SVG to canvas and extract alpha channel
    // Support sprite sheets via mask-position and mask-size
    // NOTE: Using string-based evaluate to avoid TypeScript transpilation issues (__name)
    const imageData = await page.evaluate(`
      (function({ svg, w, h, maskPos, maskSz }) {
        // Parse mask-size
        var parseMaskSize = function(size, iconWidth, iconHeight) {
          if (size === 'auto' || size === 'contain' || size === 'cover') {
            return { width: iconWidth, height: iconHeight };
          }
          var parts = size.split(' ');
          var widthPart = parts[0];
          var heightPart = parts[1] || parts[0];

          var parseValue = function(val, iconDim) {
            if (val.includes('%')) {
              return iconDim * parseFloat(val) / 100;
            }
            return parseFloat(val) || iconDim;
          };

          return {
            width: parseValue(widthPart, iconWidth),
            height: parseValue(heightPart, iconHeight)
          };
        };

        // Parse mask-position
        var parseMaskPosition = function(pos, spriteWidth, iconWidth) {
          var parts = pos.split(' ');
          var xPart = parts[0] || '0%';
          var yPart = parts[1] || '0%';

          var parseValue = function(val, spriteDim, iconDim) {
            if (val.includes('%')) {
              var percent = parseFloat(val) / 100;
              return -(spriteDim - iconDim) * percent;
            }
            return parseFloat(val) || 0;
          };

          return {
            x: parseValue(xPart, spriteWidth, iconWidth),
            y: parseValue(yPart, spriteWidth, iconWidth)
          };
        };

        var canvas = document.createElement('canvas');
        canvas.width = w;
        canvas.height = h;
        var ctx = canvas.getContext('2d');
        if (!ctx) return null;

        var spriteSize = parseMaskSize(maskSz, w, h);
        var offset = parseMaskPosition(maskPos, spriteSize.width, w);

        var img = new Image();
        var svgBlob = new Blob([svg], { type: 'image/svg+xml;charset=utf-8' });
        var url = URL.createObjectURL(svgBlob);

        return new Promise(function(resolve) {
          img.onload = function() {
            if (spriteSize.width !== w || spriteSize.height !== h || offset.x !== 0 || offset.y !== 0) {
              var tempCanvas = document.createElement('canvas');
              tempCanvas.width = spriteSize.width;
              tempCanvas.height = spriteSize.height;
              var tempCtx = tempCanvas.getContext('2d');

              if (tempCtx) {
                tempCtx.drawImage(img, 0, 0, spriteSize.width, spriteSize.height);
                ctx.drawImage(tempCanvas, -offset.x, -offset.y, w, h, 0, 0, w, h);
              }
            } else {
              ctx.drawImage(img, 0, 0, w, h);
            }

            URL.revokeObjectURL(url);

            var imgData = ctx.getImageData(0, 0, w, h);
            var pixels = [];

            for (var i = 0; i < imgData.data.length; i += 4) {
              pixels.push(imgData.data[i + 3]);
            }

            resolve(pixels);
          };

          img.onerror = function(e) {
            console.error('[MaskRasterizer Browser] Failed to load SVG image');
            console.error('[MaskRasterizer Browser] SVG sample:', svg.substring(0, 100));
            URL.revokeObjectURL(url);
            resolve(null);
          };

          setTimeout(function() {
            URL.revokeObjectURL(url);
            resolve(null);
          }, 5000);

          img.src = url;
        });
      })(${JSON.stringify({ svg: svgContent, w: width, h: height, maskPos: maskPosition, maskSz: maskSize })})
    `);

    if (!imageData) {
      console.error('[MaskRasterizer] Failed to render SVG mask');
      console.error('[MaskRasterizer] SVG content:', svgContent.substring(0, 200));
      console.error('[MaskRasterizer] Size:', width, 'x', height);
      throw new Error('Failed to render SVG mask');
    }

    // Convert to 1-bit monochrome (threshold at 50% = 128)
    // Pack bits: 8 pixels per byte, MSB first
    const totalPixels = width * height;
    const byteCount = Math.ceil(totalPixels / 8);
    const maskData = Buffer.alloc(byteCount);

    let opaquePixelCount = 0;
    for (let i = 0; i < totalPixels; i++) {
      const alpha = imageData[i];
      const bit = alpha >= 128 ? 1 : 0; // Threshold at 50%

      if (bit) {
        opaquePixelCount++;
      }

      const byteIndex = Math.floor(i / 8);
      const bitIndex = 7 - (i % 8); // MSB first

      if (bit) {
        maskData[byteIndex] |= (1 << bitIndex);
      }
    }

    // Log diagnostic info
    const percentOpaque = ((opaquePixelCount / totalPixels) * 100).toFixed(1);
    console.log(`[MaskRasterizer] Rendered ${width}x${height} mask: ${opaquePixelCount}/${totalPixels} opaque pixels (${percentOpaque}%)`);

    if (opaquePixelCount === 0) {
      console.warn('[MaskRasterizer] WARNING: Mask has ZERO opaque pixels - will render as invisible!');
      console.warn('[MaskRasterizer] URL:', maskUrl.substring(0, 100));
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
 * Includes mask-position and mask-size in cache key for sprite sheet support
 */
class MaskCache {
  private cache = new Map<string, Buffer>();

  getCacheKey(url: string, width: number, height: number, maskPosition?: string, maskSize?: string): string {
    const pos = maskPosition || '0% 0%';
    const size = maskSize || 'auto';
    return `${url}|${width}|${height}|${pos}|${size}`;
  }

  async get(
    url: string,
    width: number,
    height: number,
    page: Page,
    maskPosition?: string,
    maskSize?: string
  ): Promise<Buffer> {
    const key = this.getCacheKey(url, width, height, maskPosition, maskSize);

    if (this.cache.has(key)) {
      return this.cache.get(key)!;
    }

    const maskData = await rasterizeMaskTo1Bit(url, width, height, page, maskPosition, maskSize);
    this.cache.set(key, maskData);

    const posInfo = maskPosition && maskPosition !== '0% 0%' ? ` pos=${maskPosition}` : '';
    const sizeInfo = maskSize && maskSize !== 'auto' ? ` size=${maskSize}` : '';
    console.log(`[MaskCache] Rasterized ${url.substring(0, 60)}... (${width}x${height}${posInfo}${sizeInfo} -> ${maskData.length} bytes)`);

    return maskData;
  }

  has(url: string, width: number, height: number, maskPosition?: string, maskSize?: string): boolean {
    return this.cache.has(this.getCacheKey(url, width, height, maskPosition, maskSize));
  }

  clear(): void {
    this.cache.clear();
  }
}

export const maskCache = new MaskCache();
