import { Color } from './types';
import { converter, formatHex } from 'culori';

/**
 * Parse a CSS color string to RGBA components
 */
export function parseColor(colorStr: string): Color {
  // Handle rgb/rgba format
  const rgbaMatch = colorStr.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\)/);
  if (rgbaMatch) {
    return {
      r: parseInt(rgbaMatch[1]),
      g: parseInt(rgbaMatch[2]),
      b: parseInt(rgbaMatch[3]),
      // Convert CSS alpha (0.0-1.0) to byte range (0-255)
      a: rgbaMatch[4] ? Math.round(parseFloat(rgbaMatch[4]) * 255) : 255,
    };
  }

  // Handle hex format
  const hexMatch = colorStr.match(/^#([0-9a-f]{6})$/i);
  if (hexMatch) {
    const hex = hexMatch[1];
    return {
      r: parseInt(hex.substr(0, 2), 16),
      g: parseInt(hex.substr(2, 2), 16),
      b: parseInt(hex.substr(4, 2), 16),
      a: 255,
    };
  }

  // Handle oklch, lab, lch, and other modern CSS color formats
  // Use culori to convert to RGB
  if (colorStr.includes('oklch(') || colorStr.includes('lab(') || colorStr.includes('lch(') || colorStr.includes('color(')) {
    try {
      const rgb = converter('rgb');
      const result = rgb(colorStr);

      if (result && typeof result.r === 'number' && typeof result.g === 'number' && typeof result.b === 'number') {
        return {
          r: Math.round(result.r * 255),
          g: Math.round(result.g * 255),
          b: Math.round(result.b * 255),
          a: result.alpha !== undefined ? Math.round(result.alpha * 255) : 255,
        };
      }
    } catch (e) {
      console.warn('[Utils] Failed to parse modern color format:', colorStr, e);
    }
  }

  // Fallback to black
  return { r: 0, g: 0, b: 0, a: 255 };
}

/**
 * Check if an element is actually visible on the page
 * Based on v1's validated approach: check computed style properties
 */
export function isElementVisible(
  computedStyle: any,
  boundingBox: { width: number; height: number }
): boolean {
  // Check display
  if (computedStyle.display === 'none') {
    return false;
  }

  // Check visibility
  if (computedStyle.visibility === 'hidden') {
    return false;
  }

  // Check opacity
  if (parseFloat(computedStyle.opacity) === 0) {
    return false;
  }

  // Check if element has any size
  if (boundingBox.width === 0 || boundingBox.height === 0) {
    return false;
  }

  return true;
}

/**
 * Calculate statistics for a primitive array
 */
export function calculateStats(primitives: any[]): any {
  const stats = {
    totalCount: primitives.length,
    rectCount: 0,
    textCount: 0,
    borderCount: 0,
    imageCount: 0,
  };

  for (const prim of primitives) {
    switch (prim.type) {
      case 1: // DrawRect
        stats.rectCount++;
        break;
      case 2: // DrawText
        stats.textCount++;
        break;
      case 3: // DrawBorder
        stats.borderCount++;
        break;
      case 4: // DrawImage
        stats.imageCount++;
        break;
    }
  }

  return stats;
}
