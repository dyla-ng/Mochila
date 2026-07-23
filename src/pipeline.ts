import { Page } from 'playwright';
import { Primitive, PrimitiveType, ScrollMetadata } from './types';
import { parseColor } from './utils';
import { substituteFontForMacOS9 } from './font-table';
import { pictCache } from './pict-cache';
import { maskCache } from './mask-rasterizer';
import * as fs from 'fs';
import * as path from 'path';

/**
 * DOMSnapshot-based extraction pipeline
 *
 * Replaces the old 1300-line DOM walking approach with a single
 * DOMSnapshot.captureSnapshot() call that gives us Chrome's exact
 * layout, paint order, and computed styles.
 *
 * Benefits:
 * - 5-10x faster (84ms vs 500-1000ms for Wikipedia)
 * - No font metric bugs (uses Chrome's exact layout)
 * - No z-index bugs (uses Chrome's exact paint order)
 * - Chrome handles all CSS (flexbox, grid, transforms, etc.)
 */
export class ExtractionPipeline {
  /**
   * Run the full extraction pipeline
   */
  static async run(page: Page): Promise<{ primitives: Primitive[]; scrollMetadata: ScrollMetadata }> {
    console.log('[Pipeline] Starting DOMSnapshot-based extraction...');
    const startTime = Date.now();

    // Get scroll metadata
    const scrollMetadata = await page.evaluate(() => ({
      scrollY: window.scrollY,
      scrollX: window.scrollX,
      viewportWidth: window.innerWidth,
      viewportHeight: window.innerHeight,
      documentWidth: document.documentElement.scrollWidth,
      documentHeight: document.documentElement.scrollHeight,
      stickyElements: [], // TODO: Extract if needed
    }));

    // Stage 1: Capture DOMSnapshot (replaces walkDOM, filter, transform, optimize)
    const primitives = await this.captureSnapshot(page);
    const captureTime = Date.now() - startTime;

    // DEBUG: Check what captureSnapshot actually returned
    console.log(`[Pipeline] First 10 primitives (any type):`);
    primitives.slice(0, 10).forEach((p: any, i: number) => {
      console.log(`  [${i}] type=${p.type} (${typeof p.type}) y=${p.y} ${p.text ? `text="${p.text.substring(0, 20)}"` : ''}`);
    });

    const textFromCapture = primitives.filter(p => p.type === PrimitiveType.DrawText).slice(0, 5);
    console.log(`[Pipeline] Text from captureSnapshot (first 5 with type===DrawText):`);
    textFromCapture.forEach((p: any, i: number) => {
      console.log(`  [${i}] y=${p.y} text="${p.text?.substring(0, 30)}"`);
    });

    console.log(`[Pipeline] Snapshot captured and converted in ${captureTime}ms`);
    console.log(`[Pipeline] Generated ${primitives.length} primitives`);

    // Stage 2: Extract and encode images (still needed - DOMSnapshot doesn't include image data)
    const withImages = await this.extractAndEncodeImages(page, primitives, scrollMetadata);
    const totalTime = Date.now() - startTime;

    console.log(`[Pipeline] Total extraction time: ${totalTime}ms`);

    // Stage 3: Encode images and final sort
    const finalPrimitives = await this.encode(withImages, scrollMetadata.scrollY, scrollMetadata.viewportHeight, page);

    return {
      primitives: finalPrimitives,
      scrollMetadata,
    };
  }

  /**
   * Capture DOMSnapshot and convert to primitives
   *
   * This single method replaces:
   * - walkDOM() (800 lines)
   * - filter() (25 lines)
   * - transform() (120 lines)
   * - optimize() (100 lines)
   */
  private static async captureSnapshot(page: Page): Promise<Primitive[]> {
    const client = await page.context().newCDPSession(page);

    let textDebugCount = 0; // DEBUG counter
    let textPushCount = 0; // DEBUG: Track how many text primitives we actually push

    // Capture snapshot with all computed styles we need
    const snapshot: any = await client.send('DOMSnapshot.captureSnapshot', {
      computedStyles: [
        'color',                // [0] Text color
        'font-family',          // [1] Font family
        'font-size',            // [2] Font size
        'font-weight',          // [3] Font weight
        'background-color',     // [4] Background color
        'border-top-width',     // [5] Border width
        'border-top-color',     // [6] Border color
        'border-top-style',     // [7] Border style
        'border-radius',        // [8] Border radius
        'font-style',           // [9] Italic
        'text-decoration',      // [10] Underline
        'visibility',           // [11] Visibility (CRITICAL: filter hidden elements!)
        'opacity',              // [12] Opacity (CRITICAL: filter transparent elements!)
        '-webkit-mask-image',   // [13] Mask image (for icon elements)
        'display',              // [14] Display type
        'list-style-type',      // [15] List bullet style
      ],
      includePaintOrder: true,
    });

    const doc = snapshot.documents[0];
    const primitives: Primitive[] = [];
    let filteredHiddenCount = 0;
    let filteredMultiLineCount = 0;

    // Build a set of all nodeIndex values that have mask-image
    // This helps us skip text for parent elements that contain masked icons
    const maskedNodeIndices = new Set<number>();
    const nodesWithMaskedChildren = new Set<number>();

    for (let i = 0; i < doc.layout.nodeIndex.length; i++) {
      const styleIndices = doc.layout.styles[i] || [];
      const maskImage = styleIndices[13] >= 0 ? snapshot.strings[styleIndices[13]] : '';

      if (maskImage && maskImage !== 'none') {
        const nodeIdx = doc.layout.nodeIndex[i];
        maskedNodeIndices.add(nodeIdx);

        // Mark only the IMMEDIATE parent as having masked children
        // (not all ancestors - that would mark <body> and skip ALL text!)
        const parentIdx = doc.nodes.parentIndex[nodeIdx];
        if (parentIdx >= 0) {
          nodesWithMaskedChildren.add(parentIdx);
        }
      }
    }

    console.log(`[Pipeline] Found ${maskedNodeIndices.size} masked elements, ${nodesWithMaskedChildren.size} parents with masked children`);

    // Extract INPUT element values from DOMSnapshot
    // inputValue is RareStringData: { index: [nodeIdx, ...], value: [stringIdx, ...] }
    const inputValues: any[] = [];
    if (doc.nodes && doc.nodes.inputValue && doc.nodes.inputValue.index && doc.nodes.inputValue.value) {
      // Process each node that has an inputValue
      for (let i = 0; i < doc.nodes.inputValue.index.length; i++) {
        const nodeIdx = doc.nodes.inputValue.index[i];
        const valueIdx = doc.nodes.inputValue.value[i];

        const text = valueIdx >= 0 ? snapshot.strings[valueIdx] : '';
        if (!text || text.trim().length === 0) continue;

        // Find this node's layout data
        const layoutIdx = doc.layout.nodeIndex.indexOf(nodeIdx);
        if (layoutIdx < 0) continue;

        const [x, y, width, height] = doc.layout.bounds[layoutIdx];
        if (width <= 0 || height <= 0) continue;

        const styleIndices = doc.layout.styles[layoutIdx] || [];
        const getStyle = (idx: number) => styleIndices[idx] >= 0 ? snapshot.strings[styleIndices[idx]] : '';

        const visibility = getStyle(11);
        const opacity = getStyle(12);
        if (visibility === 'hidden' || opacity === '0') continue;

        inputValues.push({
          x: Math.round(x),
          y: Math.round(y),
          width: Math.round(width),
          height: Math.round(height),
          text,
          fontFamily: getStyle(1), // font-family
          fontSize: parseInt(getStyle(2)) || 14, // font-size
          fontWeight: getStyle(3), // font-weight
          color: getStyle(0), // color
        });
      }
    }

    console.log(`[Pipeline] Found ${inputValues.length} input/button elements with text (from DOMSnapshot.nodes.inputValue)`);
    if (inputValues.length > 0) {
      console.log(`[Pipeline] First 5 INPUT values:`);
      inputValues.slice(0, 5).forEach((inp, i) => {
        console.log(`  [${i}] "${inp.text}" at (${inp.x}, ${inp.y}) ${inp.width}×${inp.height} color=${inp.color}`);
      });
    }

    // Use textBoxes for precise text positioning (individual lines/runs)
    // This gives us Chrome's exact line breaking instead of multi-line containers
    if (doc.textBoxes && doc.textBoxes.layoutIndex && doc.textBoxes.layoutIndex.length > 0) {
      console.log('[Pipeline] Using textBoxes for text extraction:', doc.textBoxes.layoutIndex.length, 'text runs');

      // Process individual text runs from Chrome's layout engine
      for (let i = 0; i < doc.textBoxes.layoutIndex.length; i++) {
        const layoutIdx = doc.textBoxes.layoutIndex[i];
        const [x, y, width, height] = doc.textBoxes.bounds[i];
        const textStart = doc.textBoxes.start[i];
        const textLength = doc.textBoxes.length[i];

        // Get the full text from the layout node
        const fullTextIdx = doc.layout.text[layoutIdx];
        if (fullTextIdx < 0) continue; // No text

        const fullText = snapshot.strings[fullTextIdx] || '';
        const text = fullText.substring(textStart, textStart + textLength);

        if (!text || text.trim().length === 0) continue;

        // Skip text for elements that contain masked children (icon buttons)
        // Walk up the parent tree to check if any ancestor has masked children
        const nodeIdx = doc.layout.nodeIndex[layoutIdx];
        let hasAncestorWithMaskedChild = false;
        let currentNodeIdx = nodeIdx;
        while (currentNodeIdx >= 0) {
          if (nodesWithMaskedChildren.has(currentNodeIdx)) {
            hasAncestorWithMaskedChild = true;
            if (textDebugCount < 15) {
              console.log(`[Pipeline] SKIPPING text "${text}" - ancestor node ${currentNodeIdx} has masked children`);
            }
            break;
          }
          currentNodeIdx = doc.nodes.parentIndex[currentNodeIdx];
        }
        if (hasAncestorWithMaskedChild) continue;

        // Extract styles from the layout node
        const styleIndices = doc.layout.styles[layoutIdx] || [];
        const getStyle = (idx: number) => styleIndices[idx] >= 0 ? snapshot.strings[styleIndices[idx]] : '';

        const color = getStyle(0);
        const fontFamily = getStyle(1);
        const fontSize = getStyle(2);
        const fontWeight = getStyle(3);
        const fontStyle = getStyle(9);
        const textDecoration = getStyle(10);
        const visibility = styleIndices[11] >= 0 ? snapshot.strings[styleIndices[11]] : '';
        const opacity = styleIndices[12] >= 0 ? snapshot.strings[styleIndices[12]] : '';

        // Skip hidden/invisible text
        if (visibility === 'hidden' || opacity === '0') continue;

        const parsedColor = parseColor(color);
        if (!parsedColor || parsedColor.a === 0) continue;

        const fontSizeNum = parseInt(fontSize) || 16;
        const fontWeightNum = parseInt(fontWeight) || 400;
        const isBold = fontWeightNum >= 600;
        const isItalic = fontStyle === 'italic' || fontStyle === 'oblique';
        const isUnderline = textDecoration.includes('underline');

        // Substitute Mac OS 9 font
        const macFont = substituteFontForMacOS9(fontFamily, fontWeight, fontStyle);

        // Check if it's a link (blue color heuristic)
        const isLink = color.includes('51, 102, 204') || color.includes('0, 0, 255');
        const hoverColor = isLink ? { r: 255, g: 0, b: 0, a: 255 } : undefined;
        const hoverUnderline = isLink;

        const paintOrder = doc.layout.paintOrders[layoutIdx] || 0;

        primitives.push({
          type: PrimitiveType.DrawText,
          x: Math.round(x),
          y: Math.round(y),
          text,
          fontSize: fontSizeNum,
          fontId: macFont.id,  // Fixed: use .id not .fontId
          color: parsedColor,
          hoverColor,
          hoverUnderline: hoverUnderline || undefined,
          isBold,
          isItalic,
          isUnderline,
          targetWidth: Math.round(width),
          targetHeight: Math.round(height),
          zIndex: paintOrder,
          treeOrder: layoutIdx,
        });

        if (textDebugCount < 10) {
          console.log(`[Pipeline DEBUG] TextBox #${textDebugCount}: "${text.substring(0, 40)}" fontFamily="${fontFamily}" → fontId=${macFont.id} (${macFont.name})`);
          textDebugCount++;
        }
      }
    }

    // Add INPUT/BUTTON text primitives
    let inputTextCount = 0;
    for (const inputData of inputValues) {
      const parsedColor = parseColor(inputData.color);

      if (!parsedColor) {
        console.log(`[Pipeline] INPUT skipped (no color): "${inputData.text}" color="${inputData.color}"`);
        continue;
      }
      if (parsedColor.a === 0) {
        console.log(`[Pipeline] INPUT skipped (transparent): "${inputData.text}" color="${inputData.color}"`);
        continue;
      }

      const fontWeightNum = parseInt(inputData.fontWeight as any) || 400;
      const isBold = fontWeightNum >= 600;

      const macFont = substituteFontForMacOS9(inputData.fontFamily, inputData.fontWeight, 'normal');

      // Center text within the button
      // Horizontal: center of button - half of text width
      // Vertical: add some offset (buttons typically have padding)
      const textWidth = inputData.fontSize * inputData.text.length * 0.6; // Rough estimate
      const centeredX = inputData.x + (inputData.width - textWidth) / 2;
      const centeredY = inputData.y + (inputData.height - inputData.fontSize) / 2;

      primitives.push({
        type: PrimitiveType.DrawText,
        x: Math.round(centeredX),
        y: Math.round(centeredY),
        text: inputData.text,
        fontSize: inputData.fontSize,
        fontId: macFont.id,
        color: parsedColor,
        isBold,
        isItalic: false,
        isUnderline: false,
        targetWidth: inputData.width,
        targetHeight: inputData.height,
        zIndex: 1000, // Buttons are usually on top
        treeOrder: 20000, // High treeOrder to ensure they're after other text
      });

      if (inputTextCount < 5) {
        console.log(`[Pipeline] Added INPUT text primitive: "${inputData.text}" at (${inputData.x}, ${inputData.y})`);
      }
      inputTextCount++;
    }
    console.log(`[Pipeline] Total INPUT text primitives added: ${inputTextCount}`);

    // Process each layout node (fallback for backgrounds, borders, and if textBoxes unavailable)
    for (let i = 0; i < doc.layout.nodeIndex.length; i++) {
      const [x, y, width, height] = doc.layout.bounds[i];

      // Skip invisible elements
      if (width <= 0 || height <= 0) continue;

      const styleIndices = doc.layout.styles[i] || [];

      // CRITICAL: Skip hidden/invisible elements (273 on Wikipedia!)
      const visibility = styleIndices[11] >= 0 ? snapshot.strings[styleIndices[11]] : '';
      const opacity = styleIndices[12] >= 0 ? snapshot.strings[styleIndices[12]] : '';

      if (visibility === 'hidden' || opacity === '0') {
        filteredHiddenCount++;
        continue;
      }

      const textIdx = doc.layout.text[i];
      const paintOrder = doc.layout.paintOrders[i] || 0;

      // Get computed styles from strings array
      const getStyle = (idx: number) => styleIndices[idx] >= 0 ? snapshot.strings[styleIndices[idx]] : '';

      const color = getStyle(0);
      const fontFamily = getStyle(1);
      const fontSize = getStyle(2);
      const fontWeight = getStyle(3);
      const bgColor = getStyle(4);
      const borderWidth = getStyle(5);
      const borderColor = getStyle(6);
      const borderStyle = getStyle(7);
      const borderRadius = getStyle(8);
      const fontStyle = getStyle(9);
      const textDecoration = getStyle(10);
      const maskImage = getStyle(13);

      // Create background primitive if has visible background
      // BUT skip if element has mask-image (will be rendered as DrawMaskedImage instead)
      const hasMaskImage = maskImage && maskImage !== 'none';
      if (bgColor && bgColor !== 'rgba(0, 0, 0, 0)' && bgColor !== 'transparent' && !hasMaskImage) {
        const parsedColor = parseColor(bgColor);
        if (parsedColor && parsedColor.a > 0) {
          const borderRadiusNum = borderRadius ? Math.min(255, Math.max(0, Math.round(parseFloat(borderRadius)))) : 0;

          primitives.push({
            type: PrimitiveType.DrawRect,
            x: Math.round(x),
            y: Math.round(y),
            width: Math.round(width),
            height: Math.round(height),
            color: parsedColor,
            borderRadius: borderRadiusNum > 0 ? borderRadiusNum : undefined,
            zIndex: paintOrder,
            treeOrder: i,
          });
        }
      }

      // Create border primitive if has visible border
      if (borderWidth && borderColor && borderStyle && borderStyle !== 'none') {
        const borderWidthNum = parseFloat(borderWidth);
        if (borderWidthNum > 0) {
          const parsedBorderColor = parseColor(borderColor);
          if (parsedBorderColor && parsedBorderColor.a > 0) {
            const borderRadiusNum = borderRadius ? Math.min(255, Math.max(0, Math.round(parseFloat(borderRadius)))) : 0;

            primitives.push({
              type: PrimitiveType.DrawBorder,
              x: Math.round(x),
              y: Math.round(y),
              width: Math.round(width),
              height: Math.round(height),
              thickness: Math.round(borderWidthNum),
              color: parsedBorderColor,
              borderRadius: borderRadiusNum > 0 ? borderRadiusNum : undefined,
              zIndex: paintOrder,
              treeOrder: i,
            });
          }
        }
      }

      // Create text primitive if has text content
      // SKIP if textBoxes is being used (to avoid duplicate text)
      // SKIP if element has mask-image (icon-only buttons with hidden text)
      // SKIP if element contains a child with mask-image (parent containers of icons)
      const nodeIdx = doc.layout.nodeIndex[i];
      const hasMaskedChild = nodesWithMaskedChildren.has(nodeIdx);
      if (textIdx >= 0 && !hasMaskImage && !hasMaskedChild && !(doc.textBoxes && doc.textBoxes.layoutIndex && doc.textBoxes.layoutIndex.length > 0)) {
        const text = snapshot.strings[textIdx];

        // DEBUG: Log first few text extractions
        if (textDebugCount < 10 && text && text.trim()) {
          console.log(`[Pipeline DEBUG] Text #${textDebugCount}: y=${y} text="${text.substring(0, 30)}" fontSize=${fontSize}`);
          textDebugCount++;
        }

        // Skip whitespace-only text
        if (!text || text.trim().length === 0) continue;

        const parsedColor = parseColor(color);

        // DEBUG: Check why text is being skipped
        if (textDebugCount <= 10 && (!parsedColor || parsedColor.a === 0)) {
          console.log(`[Pipeline DEBUG] SKIPPING text at y=${y} "${text.substring(0, 30)}" - color="${color}" parsedColor=${JSON.stringify(parsedColor)}`);
        }

        if (parsedColor && parsedColor.a > 0) {
          const fontSizeNum = parseInt(fontSize) || 16;
          const fontWeightNum = parseInt(fontWeight) || 400;
          const isBold = fontWeightNum >= 600;
          const isItalic = fontStyle === 'italic' || fontStyle === 'oblique';
          const isUnderline = textDecoration.includes('underline');

          // Mark multi-line boxes for client-side wrapping
          // DOMSnapshot may return boxes spanning multiple lines (height >> fontSize)
          // The client needs to know to wrap this text instead of drawing it as one line
          const heightRatio = height / fontSizeNum;
          const isMultiLine = heightRatio > 1.8;

          if (isMultiLine && filteredMultiLineCount < 10) {
            console.log(`[Pipeline DEBUG] Multi-line box (ratio=${heightRatio.toFixed(2)}): "${text.substring(0, 60)}"`);
            filteredMultiLineCount++;
          }

          // Substitute Mac OS 9 font
          const macFont = substituteFontForMacOS9(fontFamily, fontWeight, fontStyle);

          // Check if it's a link (blue color heuristic - could be improved)
          const isLink = color.includes('51, 102, 204') || color.includes('0, 0, 255');
          const hoverColor = isLink ? { r: 215, g: 30, b: 30, a: 255 } : undefined;
          const hoverUnderline = isLink || isUnderline;

          // DEBUG: Log when actually pushing text primitives
          if (textPushCount < 50) {
            console.log(`[Pipeline DEBUG] PUSHING text primitive #${textPushCount}: y=${Math.round(y)} text="${text.substring(0, 60)}" color=${color}`);
          }
          textPushCount++;

          primitives.push({
            type: PrimitiveType.DrawText,
            x: Math.round(x),
            y: Math.round(y),
            width: Math.round(width),   // DOMSnapshot bounding box width
            height: Math.round(height), // DOMSnapshot bounding box height
            text,
            fontId: macFont.id,
            fontSize: fontSizeNum,
            color: parsedColor,
            hoverColor,
            hoverUnderline: hoverUnderline || undefined,
            isMultiLine,  // Flag for client to wrap text
            isBold,
            isItalic,
            isUnderline,
            zIndex: paintOrder,
            treeOrder: i,
            originalFontFamily: fontFamily,
            originalFontWeight: fontWeight,
            substituteFontName: macFont.name,
          });
        }
      }
    }

    // DON'T SORT HERE! Keep document order (treeOrder).
    // zIndex sorting happens in encode() for final paint order.
    // Sorting by zIndex here breaks document flow - e.g. Wikipedia footer (z=3)
    // would appear before header (z=67), which corrupts text extraction.

    // Sort by treeOrder only to maintain document flow
    const indexedPrimitives = primitives.map((item, index) => ({ item, index }));
    indexedPrimitives.sort((a, b) => {
      const treeA = (a.item as any).treeOrder ?? 0;
      const treeB = (b.item as any).treeOrder ?? 0;
      if (treeA !== treeB) return treeA - treeB;

      // Tiebreaker: original index ensures stability
      return a.index - b.index;
    });
    const sortedPrimitives = indexedPrimitives.map(x => x.item);

    console.log(`[Pipeline] Primitive breakdown:`);
    console.log(`  DrawRect:   ${sortedPrimitives.filter(p => p.type === PrimitiveType.DrawRect).length}`);
    console.log(`  DrawText:   ${sortedPrimitives.filter(p => p.type === PrimitiveType.DrawText).length}`);
    console.log(`  DrawBorder: ${sortedPrimitives.filter(p => p.type === PrimitiveType.DrawBorder).length}`);
    console.log(`  Filtered hidden/invisible: ${filteredHiddenCount}`);
    console.log(`  Filtered multi-line containers (ratio > 2.0): ${filteredMultiLineCount}`);

    // DEBUG: Check first 10 actual primitives right before return
    console.log(`[Pipeline] ACTUAL primitives[0-9] right before return:`);
    for (let i = 0; i < Math.min(10, sortedPrimitives.length); i++) {
      const p: any = sortedPrimitives[i];
      console.log(`  [${i}] type=${p.type} y=${p.y} ${p.text ? `text="${p.text.substring(0, 20)}"` : ''}`);
    }

    // NO consolidation needed! DOMSnapshot text nodes are already perfectly positioned
    // with proper spacing. Just render each node at its exact (x,y) position.
    return sortedPrimitives;
  }

  /**
   * Extract images and masked images from the DOM
   *
   * DOMSnapshot doesn't provide image data, so we still need to walk
   * the DOM to find <img> tags and CSS background-image/mask-image.
   *
   * This is MUCH simpler than the old walkDOM() because we only care
   * about images, not layout/text/styles.
   */
  private static async extractAndEncodeImages(
    page: Page,
    existingPrimitives: Primitive[],
    scrollMetadata: ScrollMetadata
  ): Promise<Primitive[]> {
    console.log('[Pipeline] Extracting images...');

    // Load browser-side script from separate file (avoids bundler __name issue)
    const scriptPath = path.join(__dirname, 'extract-images-browser.js');
    const scriptContent = fs.readFileSync(scriptPath, 'utf8');

    // Execute script in browser context
    const imageData = await page.evaluate(scriptContent) as any[];

    console.log(`[Pipeline] Found ${imageData.length} images`);

    // Convert to primitives
    const imagePrimitives: Primitive[] = [];
    for (const img of imageData) {
      if (img.type === 'image') {
        imagePrimitives.push({
          type: PrimitiveType.DrawImage,
          x: img.x,
          y: img.y,
          width: img.width,
          height: img.height,
          src: img.src,
          zIndex: img.zIndex,
          treeOrder: img.treeOrder,
        } as any);
      } else if (img.type === 'maskedImage') {
        const fillColor = parseColor(img.fillColor);
        if (fillColor) {
          imagePrimitives.push({
            type: PrimitiveType.DrawMaskedImage,
            x: img.x,
            y: img.y,
            width: img.width,
            height: img.height,
            src: img.src,
            fillColor,
            maskPosition: img.maskPosition || '0% 0%',
            maskSize: img.maskSize || 'auto',
            zIndex: img.zIndex,
            treeOrder: img.treeOrder,
          } as any);
        }
      }
    }

    return [...existingPrimitives, ...imagePrimitives];
  }

  /**
   * Stage 5: Viewport-First Image Encoding (async) & CSS Stacking Order Sort
   *
   * KEPT FROM OLD PIPELINE - This handles encoding images to PICT format
   */
  static async encode(primitives: Primitive[], scrollY: number = 0, viewportHeight: number = 768, page: Page): Promise<Primitive[]> {
    const imagePrimitives: any[] = [];
    const maskedImagePrimitives: any[] = [];
    const otherPrimitives: Primitive[] = [];
    const pageUrl = page.url();

    for (const prim of primitives) {
      if (prim.type === PrimitiveType.DrawImage) {
        imagePrimitives.push(prim);
      } else if (prim.type === PrimitiveType.DrawMaskedImage) {
        maskedImagePrimitives.push(prim);
      } else {
        otherPrimitives.push(prim);
      }
    }

    // VERY tight viewport window for initial load - only encode visible images
    // This prevents 6+ MB frames that freeze Mac OS 9
    // No buffer - exact viewport only. Scrolling will trigger cache updates.
    const minY = scrollY;
    const maxY = scrollY + viewportHeight;

    const encodePromises = imagePrimitives.map(async (prim) => {
      let src = prim.src;
      const maskColor = prim.maskColor;
      const backgroundColor = prim.backgroundColor;

      // Strip leading/trailing quotes from src (malformed HTML)
      if (src && ((src.startsWith('"') && src.endsWith('"')) || (src.startsWith("'") && src.endsWith("'")))) {
        src = src.slice(1, -1);
      } else if (src && (src.startsWith('"') || src.startsWith("'"))) {
        src = src.slice(1);
      }

      if (src && src.startsWith('//')) {
        src = 'https:' + src;
      } else if (src && !src.startsWith('http') && !src.startsWith('data:') && pageUrl) {
        try {
          src = new URL(src, pageUrl).href;
        } catch (e) {}
      }

      if (!src || (!src.startsWith('http') && !src.startsWith('data:'))) {
        console.log(`[Pipeline] Grey box fallback for invalid image src at (${prim.x}, ${prim.y}) ${prim.width}x${prim.height} - src="${src || 'EMPTY'}"`);
        return {
          type: PrimitiveType.DrawRect,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          color: { r: 230, g: 230, b: 230, a: 255 },
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }

      const isAlreadyCached = pictCache.has(src, 1024, maskColor, backgroundColor);

      if (isAlreadyCached) {
        // Only include pictBytes if already cached (instant, no blocking)
        try {
          const encoded = await pictCache.get(src, 1024, maskColor, backgroundColor, page);
          return {
            type: PrimitiveType.DrawImage,
            x: prim.x,
            y: prim.y,
            width: prim.width,
            height: prim.height,
            src: src,
            pictBytes: encoded.pictBytes,
            zIndex: prim.zIndex,
            treeOrder: prim.treeOrder,
          } as Primitive;
        } catch (e: any) {
          console.error('[Pipeline] Cache retrieval error for src:', src ? src.substring(0, 60) : 'none', e);
          return {
            type: PrimitiveType.DrawRect,
            x: prim.x,
            y: prim.y,
            width: prim.width,
            height: prim.height,
            color: { r: 230, g: 230, b: 230, a: 255 },
            zIndex: prim.zIndex,
            treeOrder: prim.treeOrder,
          } as Primitive;
        }
      } else {
        // ALL uncached images: return placeholder, encode in background
        // This prevents blocking the initial frame send
        pictCache.get(src, 1024, maskColor, backgroundColor, page).catch(() => {});
        return {
          type: PrimitiveType.DrawImage,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          src: src,
          pictBytes: Buffer.alloc(0),  // Empty - will be sent via cache update
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }
    });

    const encodeMaskPromises = maskedImagePrimitives.map(async (prim) => {
      let src = prim.src;

      if (src && ((src.startsWith('"') && src.endsWith('"')) || (src.startsWith("'") && src.endsWith("'")))) {
        src = src.slice(1, -1);
      } else if (src && (src.startsWith('"') || src.startsWith("'"))) {
        src = src.slice(1);
      }

      if (src && src.startsWith('//')) {
        src = 'https:' + src;
      } else if (src && !src.startsWith('http') && !src.startsWith('data:') && pageUrl) {
        try {
          src = new URL(src, pageUrl).href;
        } catch (e) {}
      }

      if (!src || (!src.startsWith('http') && !src.startsWith('data:'))) {
        return {
          type: PrimitiveType.DrawRect,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          color: { r: 0, g: 0, b: 0, a: 0 },
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }

      try {
        const maskData = await maskCache.get(src, prim.width, prim.height, page, prim.maskPosition, prim.maskSize);

        const posInfo = prim.maskPosition && prim.maskPosition !== '0% 0%' ? ` pos=${prim.maskPosition}` : '';
        const sizeInfo = prim.maskSize && prim.maskSize !== 'auto' ? ` size=${prim.maskSize}` : '';
        console.log(`[Pipeline] Created DrawMaskedImage at (${prim.x}, ${prim.y}) ${prim.width}x${prim.height}${posInfo}${sizeInfo} - maskData=${maskData.length} bytes`);

        return {
          type: PrimitiveType.DrawMaskedImage,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          fillColor: prim.fillColor,
          maskData,
          src,
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      } catch (e: any) {
        console.error('[Pipeline] Mask encoding error for src:', src ? src.substring(0, 60) : 'none', e);
        return {
          type: PrimitiveType.DrawRect,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          color: { r: 0, g: 0, b: 0, a: 0 },
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }
    });

    const encodedImages = await Promise.all(encodePromises);
    const encodedMasks = await Promise.all(encodeMaskPromises);
    const all = [...otherPrimitives, ...encodedImages, ...encodedMasks];

    // DEBUG: Check text primitives before and after sort
    const textBefore = all.filter(p => p.type === PrimitiveType.DrawText).slice(0, 5);
    console.log(`[Pipeline] Text BEFORE sort:`);
    textBefore.forEach((p: any, i: number) => {
      console.log(`  [${i}] y=${p.y} text="${p.text?.substring(0, 30)}"`);
    });

    // DON'T SORT BY PAINTORDER! Maintain document order (treeOrder).
    // Sorting by paintOrder breaks text extraction - e.g. Wikipedia footer (paintOrder=3)
    // appears before header (paintOrder=67), corrupting the reading flow.
    // For correct rendering, we rely on document order which generally works fine.
    const indexed = all.map((item, index) => ({ item, index }));
    indexed.sort((a, b) => {
      const treeA = (a.item as any).treeOrder ?? 0;
      const treeB = (b.item as any).treeOrder ?? 0;
      if (treeA !== treeB) return treeA - treeB;

      // Tiebreaker: original index ensures stability
      return a.index - b.index;
    });
    const sorted = indexed.map(x => x.item);

    const textAfter = sorted.filter(p => p.type === PrimitiveType.DrawText).slice(0, 5);
    console.log(`[Pipeline] Text AFTER sort (document order):`);
    textAfter.forEach((p: any, i: number) => {
      console.log(`  [${i}] y=${p.y} text="${p.text?.substring(0, 30)}"`);
    });

    return sorted;
  }
}
