/**
 * Font Injector - Inject Mac OS 9 fonts into Chrome
 *
 * This makes Chrome use Geneva/Monaco for layout calculations,
 * so its measured positions exactly match what Mac OS 9 will render.
 *
 * Solves the font metrics mismatch problem!
 */

import { Page } from 'playwright';
import * as fs from 'fs';
import * as path from 'path';

interface FontDefinition {
  name: string;
  file: string;
  weight?: number;
  style?: 'normal' | 'italic';
}

const MAC_OS_9_FONTS: FontDefinition[] = [
  // Geneva (sans-serif) - Primary UI font
  // Using smaller Geneva Normal.ttf which is closer to original Mac OS 9 Geneva (31KB vs 719KB)
  // This should match QuickDraw's Geneva metrics better than Geneva-Apple.ttf
  { name: 'Geneva', file: 'Geneva Normal.ttf', weight: 400, style: 'normal' },
  { name: 'Geneva', file: 'Geneva Normal-Italic.ttf', weight: 400, style: 'italic' },
  // Note: No separate Geneva Bold.ttf - Chrome will synthesize bold from Normal.ttf
  // Geneva Bold-Italic exists but we'll use it for bold+italic only
  { name: 'Geneva', file: 'Geneva Bold-Italic.ttf', weight: 700, style: 'italic' },

  // Note: We'll add Monaco (monospace) when we get it
  // { name: 'Monaco', file: 'Monaco.woff', weight: 400, style: 'normal' },
];

/**
 * Convert font file to base64 data URL (supports WOFF, TTF, OTF)
 */
function fontToDataUrl(fontPath: string): string {
  const fontData = fs.readFileSync(fontPath);
  const base64 = fontData.toString('base64');

  // Detect format from file extension
  const ext = path.extname(fontPath).toLowerCase();
  let mimeType = 'font/woff';

  if (ext === '.ttf') mimeType = 'font/ttf';
  else if (ext === '.otf') mimeType = 'font/otf';
  else if (ext === '.woff2') mimeType = 'font/woff2';

  return `data:${mimeType};charset=utf-8;base64,${base64}`;
}

/**
 * Generate @font-face CSS for Mac OS 9 fonts
 */
function generateFontFaceCSS(): string {
  const fontDir = path.join(__dirname, '../font_files');
  const fontFaces: string[] = [];

  for (const font of MAC_OS_9_FONTS) {
    const fontPath = path.join(fontDir, font.file);

    if (!fs.existsSync(fontPath)) {
      console.warn(`[FontInjector] Font file not found: ${font.file}`);
      continue;
    }

    const dataUrl = fontToDataUrl(fontPath);

    // Detect format for src declaration
    const ext = path.extname(font.file).toLowerCase();
    let format = 'woff';
    if (ext === '.ttf') format = 'truetype';
    else if (ext === '.otf') format = 'opentype';
    else if (ext === '.woff2') format = 'woff2';

    const fontFace = `
      @font-face {
        font-family: '${font.name}';
        src: url('${dataUrl}') format('${format}');
        font-weight: ${font.weight || 400};
        font-style: ${font.style || 'normal'};
        font-display: block;
      }
    `;

    fontFaces.push(fontFace);
    console.log(`[FontInjector] Loaded ${font.name} (${font.weight} ${font.style})`);
  }

  return fontFaces.join('\n');
}

/**
 * Generate CSS to make Mac OS 9 fonts available but DON'T force them
 *
 * IMPORTANT: We DON'T use !important here because we want DOMSnapshot
 * to capture the ORIGINAL font-family from the page, so the server can
 * classify it (Arial→sans, Times→serif, Monaco→mono) and map to the
 * appropriate Mac OS 9 font.
 *
 * The fonts are available if the page explicitly requests them, but
 * we preserve the original font stack for measurement purposes.
 */
function generateFontOverrideCSS(): string {
  return `
    /* Make Mac OS 9 fonts available but don't force them */
    /* The server will do font classification and mapping */
  `;
}

/**
 * Inject Mac OS 9 fonts into a Playwright page
 *
 * MUST be called AFTER page.goto() completes, so the page DOM is ready.
 */
export async function injectMacOS9Fonts(page: Page): Promise<void> {
  console.log('[FontInjector] Injecting Mac OS 9 fonts into Chrome...');

  const fontFaceCSS = generateFontFaceCSS();
  const overrideCSS = generateFontOverrideCSS();

  const fullCSS = `
    ${fontFaceCSS}
    ${overrideCSS}
  `;

  // Inject style tag with very high specificity to override page CSS
  await page.evaluate((css: string) => {
    const style = document.createElement('style');
    style.textContent = css;
    style.id = 'mac-os-9-fonts';

    // Insert at end of head to have highest priority
    if (document.head) {
      document.head.appendChild(style);
    } else {
      document.documentElement.insertBefore(style, document.documentElement.firstChild);
    }
  }, fullCSS);

  console.log('[FontInjector] ✅ Mac OS 9 fonts injected!');
}

/**
 * Test if fonts are working by checking computed styles
 */
export async function testFontInjection(page: Page): Promise<void> {
  const testResults = await page.evaluate(() => {
    const testDiv = document.createElement('div');
    testDiv.textContent = 'Test';
    document.body.appendChild(testDiv);

    const computedFont = window.getComputedStyle(testDiv).fontFamily;
    document.body.removeChild(testDiv);

    return {
      computedFont,
      hasGeneva: computedFont.includes('Geneva'),
    };
  });

  console.log('[FontInjector] Test results:', testResults);

  if (!testResults.hasGeneva) {
    console.warn('[FontInjector] ⚠️  WARNING: Geneva not detected in computed styles!');
    console.warn('[FontInjector] Font injection may not be working correctly.');
  } else {
    console.log('[FontInjector] ✅ Geneva detected! Font injection working.');
  }
}
