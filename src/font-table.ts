/**
 * Font Substitution Table for Mac OS 9
 *
 * Maps web fonts to Mac OS 9 system fonts that will actually be rendered client-side.
 * Per handoff spec Section 3: server must compute layout using these substitute fonts'
 * real metrics, NOT the original web font metrics, to avoid OBML-style text overlap bugs.
 */

import {
  GENEVA_REGULAR_WIDTHS,
  NEW_YORK_REGULAR_WIDTHS,
  MONACO_REGULAR_WIDTHS,
  COURIER_MODERN_WIDTHS,
} from './font-widths';

export enum FontCategory {
  SansSerif = 'sans-serif',
  Serif = 'serif',
  Monospace = 'monospace',
}

export enum FontWeight {
  Regular = 400,
  Bold = 700,
}

export enum FontStyle {
  Normal = 'normal',
  Italic = 'italic',
}

export interface MacOS9Font {
  id: number;           // Font ID for wire protocol
  name: string;         // Mac OS 9 font name
  category: FontCategory;
  weight: FontWeight;
  style: FontStyle;     // normal or italic

  // REAL metrics for server-side layout computation
  // Extracted from original Mac OS 9 bitmap fonts or modern macOS equivalents
  charWidths: { [char: string]: number };  // Per-character widths at 1pt (REQUIRED for accurate layout)
  avgCharWidth: number;  // Average character width at 1pt (for quick estimates only)
  lineHeight: number;    // Line height multiplier (e.g., 1.25)
  ascent: number;        // Ascent ratio
  descent: number;       // Descent ratio
}

/**
 * Mac OS 9 System Font Table
 * REAL METRICS extracted from original Apple bitmap fonts (1984-1985)
 * Source: Macintosh Garden "Early Apple Fonts" archive
 * Extracted via fondu → BDF format
 *
 * Metrics are normalized ratios calculated from 12pt bitmap measurements:
 * - avgCharWidth: average character width in pixels / point size
 * - lineHeight: (ascent + descent) / point size
 * - ascent: ascent in pixels / point size
 * - descent: descent in pixels / point size
 */
export const MAC_OS_9_FONTS: MacOS9Font[] = [
  // Geneva Regular
  {
    id: 1,
    name: 'Geneva',
    category: FontCategory.SansSerif,
    weight: FontWeight.Regular,
    style: FontStyle.Normal,
    charWidths: GENEVA_REGULAR_WIDTHS,
    avgCharWidth: 0.577,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Geneva Bold
  {
    id: 2,
    name: 'Geneva',
    category: FontCategory.SansSerif,
    weight: FontWeight.Bold,
    style: FontStyle.Normal,
    charWidths: GENEVA_REGULAR_WIDTHS,
    avgCharWidth: 0.63,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Geneva Italic
  {
    id: 3,
    name: 'Geneva',
    category: FontCategory.SansSerif,
    weight: FontWeight.Regular,
    style: FontStyle.Italic,
    charWidths: GENEVA_REGULAR_WIDTHS,
    avgCharWidth: 0.577,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Geneva Bold Italic
  {
    id: 4,
    name: 'Geneva',
    category: FontCategory.SansSerif,
    weight: FontWeight.Bold,
    style: FontStyle.Italic,
    charWidths: GENEVA_REGULAR_WIDTHS,
    avgCharWidth: 0.63,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Chicago Regular
  {
    id: 5,
    name: 'Chicago',
    category: FontCategory.SansSerif,
    weight: FontWeight.Regular,
    style: FontStyle.Normal,
    charWidths: GENEVA_REGULAR_WIDTHS,
    avgCharWidth: 0.614,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Monaco Regular
  {
    id: 6,
    name: 'Monaco',
    category: FontCategory.Monospace,
    weight: FontWeight.Regular,
    style: FontStyle.Normal,
    charWidths: MONACO_REGULAR_WIDTHS,
    avgCharWidth: 0.583,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Monaco Bold
  {
    id: 7,
    name: 'Monaco',
    category: FontCategory.Monospace,
    weight: FontWeight.Bold,
    style: FontStyle.Normal,
    charWidths: MONACO_REGULAR_WIDTHS,
    avgCharWidth: 0.583,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // New York Regular
  {
    id: 8,
    name: 'New York',
    category: FontCategory.Serif,
    weight: FontWeight.Regular,
    style: FontStyle.Normal,
    charWidths: NEW_YORK_REGULAR_WIDTHS,
    avgCharWidth: 0.576,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // New York Bold
  {
    id: 9,
    name: 'New York',
    category: FontCategory.Serif,
    weight: FontWeight.Bold,
    style: FontStyle.Normal,
    charWidths: NEW_YORK_REGULAR_WIDTHS,
    avgCharWidth: 0.63,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // New York Italic
  {
    id: 10,
    name: 'New York',
    category: FontCategory.Serif,
    weight: FontWeight.Regular,
    style: FontStyle.Italic,
    charWidths: NEW_YORK_REGULAR_WIDTHS,
    avgCharWidth: 0.576,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // New York Bold Italic
  {
    id: 11,
    name: 'New York',
    category: FontCategory.Serif,
    weight: FontWeight.Bold,
    style: FontStyle.Italic,
    charWidths: NEW_YORK_REGULAR_WIDTHS,
    avgCharWidth: 0.63,
    lineHeight: 1.25,
    ascent: 1.0,
    descent: 0.25,
  },
  // Courier Regular
  {
    id: 12,
    name: 'Courier',
    category: FontCategory.Monospace,
    weight: FontWeight.Regular,
    style: FontStyle.Normal,
    charWidths: COURIER_MODERN_WIDTHS,
    avgCharWidth: 0.600,
    lineHeight: 1.0,
    ascent: 0.754,
    descent: 0.246,
  },
  // Courier Bold
  {
    id: 13,
    name: 'Courier',
    category: FontCategory.Monospace,
    weight: FontWeight.Bold,
    style: FontStyle.Normal,
    charWidths: COURIER_MODERN_WIDTHS,
    avgCharWidth: 0.600,
    lineHeight: 1.0,
    ascent: 0.754,
    descent: 0.246,
  },
  // Courier Italic
  {
    id: 14,
    name: 'Courier',
    category: FontCategory.Monospace,
    weight: FontWeight.Regular,
    style: FontStyle.Italic,
    charWidths: COURIER_MODERN_WIDTHS,
    avgCharWidth: 0.600,
    lineHeight: 1.0,
    ascent: 0.754,
    descent: 0.246,
  },
];

/**
 * Classify a web font-family into serif/sans-serif/monospace
 */
export function classifyFontFamily(fontFamily: string): FontCategory {
  const lower = fontFamily.toLowerCase();

  // Monospace detection — covers modern CSS font stacks used on Wikipedia/BBC
  if (
    lower.includes('mono') ||
    lower.includes('courier') ||
    lower.includes('console') ||
    lower.includes('code') ||
    lower.includes('terminal') ||
    lower.includes('source code') ||
    lower.includes('fira code') ||
    lower.includes('consolas') ||
    lower.includes('menlo') ||
    lower.includes('sfmono') ||
    lower.includes('sf mono') ||
    lower.includes('liberation mono') ||
    lower.includes('roboto mono') ||
    lower.includes('ubuntu mono') ||
    lower.includes('inconsolata') ||
    lower.includes('jetbrains mono')
  ) {
    return FontCategory.Monospace;
  }

  // Serif detection
  if (
    lower.includes('serif') ||
    lower.includes('times') ||
    lower.includes('georgia') ||
    lower.includes('garamond') ||
    lower.includes('palatino') ||
    lower.includes('baskerville')
  ) {
    // But exclude sans-serif
    if (lower.includes('sans')) {
      return FontCategory.SansSerif;
    }
    return FontCategory.Serif;
  }

  // Default to sans-serif (most web fonts)
  return FontCategory.SansSerif;
}

/**
 * Normalize font weight from CSS values to Regular/Bold.
 * Threshold at 600 (semibold+) — medium (500) stays Regular
 * since it's visually close to normal weight on Mac OS 9 bitmap fonts.
 */
export function normalizeFontWeight(cssWeight: string | number): FontWeight {
  if (typeof cssWeight === 'number') {
    return cssWeight >= 600 ? FontWeight.Bold : FontWeight.Regular;
  }

  const lower = cssWeight.toLowerCase();

  if (
    lower === 'bold' ||
    lower === 'bolder' ||
    lower === '700' ||
    lower === '800' ||
    lower === '900' ||
    lower === '600'  // Semibold maps to bold
  ) {
    return FontWeight.Bold;
  }

  return FontWeight.Regular;
}

/**
 * Map a web font to the closest Mac OS 9 font, including italic style.
 */
export function substituteFontForMacOS9(
  fontFamily: string,
  cssWeight: string | number,
  cssStyle: string = 'normal'
): MacOS9Font {
  const category = classifyFontFamily(fontFamily);
  const weight = normalizeFontWeight(cssWeight);
  const style = cssStyle === 'italic' || cssStyle === 'oblique'
    ? FontStyle.Italic
    : FontStyle.Normal;

  // Find exact match (category + weight + style)
  const exactMatch = MAC_OS_9_FONTS.find(
    (font) => font.category === category && font.weight === weight && font.style === style
  );
  if (exactMatch) return exactMatch;

  // Fallback 1: match category + weight, ignore style
  const weightMatch = MAC_OS_9_FONTS.find(
    (font) => font.category === category && font.weight === weight
  );
  if (weightMatch) return weightMatch;

  // Fallback 2: match category only, prefer regular
  const categoryMatch = MAC_OS_9_FONTS.find(
    (font) => font.category === category && font.weight === FontWeight.Regular
  );
  if (categoryMatch) return categoryMatch;

  // Ultimate fallback: Geneva Regular
  return MAC_OS_9_FONTS[0];
}

/**
 * Calculate text width using substitute font metrics
 * This is critical per handoff spec Section 3: layout must be computed
 * using the ACTUAL metrics of the font the client will render with
 */
export function calculateTextWidth(
  text: string,
  fontSize: number,
  font: MacOS9Font
): number {
  // Simple approximation: avgCharWidth * fontSize * text.length
  // Real implementation would need per-character widths and kerning
  return text.length * font.avgCharWidth * fontSize;
}

/**
 * Calculate text height using substitute font metrics
 */
export function calculateTextHeight(
  fontSize: number,
  font: MacOS9Font
): number {
  return fontSize * font.lineHeight;
}
