import { Page } from 'playwright';
import { ExtractedElement, Primitive, PrimitiveType, ScrollMetadata } from './types';
import { isElementVisible, parseColor } from './utils';
import { substituteFontForMacOS9, MAC_OS_9_FONTS } from './font-table';
import { pictCache } from './pict-cache';
import { maskCache } from './mask-rasterizer';
import {
  GENEVA_REGULAR_WIDTHS,
  MONACO_REGULAR_WIDTHS,
  NEW_YORK_REGULAR_WIDTHS,
  COURIER_MODERN_WIDTHS
} from './font-widths';

export class ExtractionPipeline {
  /**
   * Run the full extraction pipeline stages
   */
  static async run(page: Page): Promise<{ primitives: Primitive[]; scrollMetadata: ScrollMetadata }> {
    // Stage 1: DOM walk (Browser context)
    const { elements, scrollMetadata } = await this.walkDOM(page);

    // Stage 2: Filter (Node context)
    const filtered = this.filter(elements);

    // Stage 3: Transform (Node context)
    const rawPrimitives = this.transform(filtered);

    // Stage 4: Optimize (Node context)
    // Re-enabled with fixed space insertion logic
    const optimized = this.optimize(rawPrimitives);

    // Stage 5: Encode & Sort (Node context, viewport-first)
    const finalPrimitives = await this.encode(optimized, scrollMetadata.scrollY, 768, page);

    return {
      primitives: finalPrimitives,
      scrollMetadata,
    };
  }

  /**
   * Stage 1: Walk the DOM and gather all visible elements
   */
  static async walkDOM(page: Page): Promise<{ elements: ExtractedElement[]; scrollMetadata: ScrollMetadata }> {
    function walkDOMBrowserScript() {
      const results: any[] = [];
      let treeOrderCounter = 0;

      function getEffectiveZIndex(element: HTMLElement) {
        let current: HTMLElement | null = element;
        let effectiveZ = 0;

        while (current && current !== document.body) {
          const style = window.getComputedStyle(current);
          const position = style.position;
          const zIndexVal = parseInt(style.zIndex, 10);

          if (!isNaN(zIndexVal)) {
            effectiveZ = zIndexVal;
            break;
          }

          if (position === 'sticky' || position === 'fixed') {
            effectiveZ = 100;
            break;
          }

          current = current.parentElement;
        }

        return effectiveZ;
      }

      function isElementHidden(element: HTMLElement) {
        const elStyle = window.getComputedStyle(element);
        const elRect = element.getBoundingClientRect();
        const isSrOnly = (elRect.width <= 1 && elRect.height <= 1) ||
          (elStyle.clipPath && elStyle.clipPath !== 'none' && elStyle.clipPath.includes('inset(50%)')) ||
          (elStyle.clip && elStyle.clip !== 'auto' && elStyle.clip !== 'rect(auto, auto, auto, auto)' && elStyle.clip.includes('1px'));
        if (isSrOnly) return true;

        let ancestor: HTMLElement | null = element;
        while (ancestor && ancestor !== document.body) {
          const rect = ancestor.getBoundingClientRect();
          const style = window.getComputedStyle(ancestor);
          const isOverflowClipped = style.overflow === 'hidden' || style.overflow === 'clip' ||
                                    style.overflowX === 'hidden' || style.overflowY === 'hidden';
          if ((rect.width === 0 || rect.height === 0) && isOverflowClipped) {
            return true;
          }

          if (style.display === 'none' ||
              style.visibility === 'hidden' ||
              style.opacity === '0') {
            return true;
          }

          ancestor = ancestor.parentElement;
        }
        return false;
      }

      const scrollMetadata = {
        scrollY: window.scrollY,
        scrollX: window.scrollX,
        viewportWidth: window.innerWidth,
        viewportHeight: window.innerHeight,
        documentWidth: document.documentElement.scrollWidth,
        documentHeight: document.documentElement.scrollHeight,
        stickyElements: [] as any[],
      };

      function getEffectiveBackgroundColor(element: HTMLElement) {
        let current = element.parentElement;
        while (current) {
          const bg = window.getComputedStyle(current).backgroundColor;
          if (bg && bg !== 'rgba(0, 0, 0, 0)' && bg !== 'transparent') {
            return bg;
          }
          current = current.parentElement;
        }
        return 'white';
      }

      function extractUrlValue(str: string) {
        if (!str || typeof str !== 'string' || !str.includes('url(')) return '';
        const start = str.indexOf('url(') + 4;

        // Find the matching closing paren for url(...), handling quotes
        let end = start;
        let inQuotes = false;
        let quoteChar = '';

        for (let i = start; i < str.length; i++) {
          const c = str[i];
          if (!inQuotes && (c === '"' || c === "'")) {
            inQuotes = true;
            quoteChar = c;
          } else if (inQuotes && c === quoteChar) {
            inQuotes = false;
          } else if (!inQuotes && c === ')') {
            end = i;
            break;
          }
        }

        if (start < end) {
          let u = str.substring(start, end).trim();
          if ((u.startsWith('"') && u.endsWith('"')) || (u.startsWith("'") && u.endsWith("'"))) {
            u = u.slice(1, -1);
          }
          return u;
        }
        return '';
      }

      // Extract root document/body background color
      const bodyBg = window.getComputedStyle(document.body).backgroundColor;
      const htmlBg = window.getComputedStyle(document.documentElement).backgroundColor;
      let rootBg = bodyBg;
      if (!rootBg || rootBg === 'rgba(0, 0, 0, 0)' || rootBg === 'transparent') {
        rootBg = htmlBg;
      }
      if (!rootBg || rootBg === 'rgba(0, 0, 0, 0)' || rootBg === 'transparent') {
        rootBg = 'white';
      }

      // Push root page canvas background primitive at lowest zIndex (-999)
      results.push({
        type: 'background',
        x: 0,
        y: 0,
        width: Math.max(scrollMetadata.viewportWidth, scrollMetadata.documentWidth),
        height: Math.max(scrollMetadata.viewportHeight, scrollMetadata.documentHeight),
        zIndex: -999,
        treeOrder: treeOrderCounter++,
        computedStyle: {
          backgroundColor: rootBg,
        }
      });

      // TreeWalker for processing elements (images, backgrounds, borders, etc.)
      // Text extraction now handled separately using element-based approach
      const walker = document.createTreeWalker(
        document.body,
        NodeFilter.SHOW_ELEMENT,  // Only elements, not text nodes
        {
          acceptNode: (node) => {
            const element = node as HTMLElement;
            const tagName = element.tagName.toLowerCase();
            if (['script', 'style', 'noscript', 'meta', 'link', 'head'].includes(tagName)) {
              return NodeFilter.FILTER_REJECT;
            }
            if (isElementHidden(element)) {
              return NodeFilter.FILTER_SKIP;
            }
            return NodeFilter.FILTER_ACCEPT;
          }
        }
      );

      function getElementDataUrl(element: HTMLElement, targetW: number, targetH: number) {
        try {
          const canvas = document.createElement('canvas');
          const maxDim = 240;
          let w = targetW || (element as any).naturalWidth || (element as any).width || 100;
          let h = targetH || (element as any).naturalHeight || (element as any).height || 100;

          if (w > maxDim || h > maxDim) {
            const ratio = Math.min(maxDim / w, maxDim / h);
            w = Math.round(w * ratio);
            h = Math.round(h * ratio);
          }

          canvas.width = Math.max(1, w);
          canvas.height = Math.max(1, h);
          const ctx = canvas.getContext('2d');
          if (!ctx) return null;

          const tagName = element.tagName.toLowerCase();

          if (element instanceof HTMLImageElement || element instanceof HTMLCanvasElement || element instanceof HTMLVideoElement) {
            ctx.drawImage(element, 0, 0, canvas.width, canvas.height);
            return canvas.toDataURL('image/png');
          } else if (tagName === 'svg') {
            const xml = new XMLSerializer().serializeToString(element);
            const svgImg = new Image();
            svgImg.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(xml);
            ctx.drawImage(svgImg, 0, 0, canvas.width, canvas.height);
            return canvas.toDataURL('image/png');
          }
          return null;
        } catch (e) {
          return null;
        }
      }

      let node: Node | null;
      while ((node = walker.nextNode())) {
        if (node.nodeType === Node.ELEMENT_NODE) {
          const el = node as HTMLElement;
          const rect = el.getBoundingClientRect();
          if (rect.width <= 0 || rect.height <= 0) continue;
          const style = window.getComputedStyle(el);
          const tagName = el.tagName.toLowerCase();

          // Check if this element is an image / video / canvas / svg
          const isImgTag = ['img', 'video', 'canvas', 'svg'].includes(tagName);
          if (isImgTag) {
            let src = getElementDataUrl(el, rect.width, rect.height);
            if (!src && el instanceof HTMLImageElement) {
              src = el.currentSrc || el.src || el.getAttribute('data-src') || el.getAttribute('src') || '';
            }

            if (src) {
              let parentBg = getEffectiveBackgroundColor(el);
              const isFixed = style.position === 'fixed';

              results.push({
                type: 'image',
                x: Math.round(isFixed ? rect.x : rect.x + window.scrollX),
                y: Math.round(isFixed ? rect.y : rect.y + window.scrollY),
                width: Math.round(rect.width),
                height: Math.round(rect.height),
                tagName: tagName,
                src: src,
                maskColor: undefined,
                backgroundColor: parentBg,
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
            }
          }

          // Check if element has CSS background-image: url(...)
          const bgImageStyle = style.backgroundImage || '';
          if (bgImageStyle && bgImageStyle !== 'none' && !isImgTag && bgImageStyle.includes('url(')) {
            const bgUrl = extractUrlValue(bgImageStyle);
            if (bgUrl && !bgUrl.startsWith('data:image/svg+xml')) {
              let parentBg = getEffectiveBackgroundColor(el);
              const isFixed = style.position === 'fixed';

              results.push({
                type: 'image',
                x: Math.round(isFixed ? rect.x : rect.x + window.scrollX),
                y: Math.round(isFixed ? rect.y : rect.y + window.scrollY),
                width: Math.round(rect.width),
                height: Math.round(rect.height),
                tagName: 'bg-' + tagName,
                src: bgUrl,
                maskColor: undefined,
                backgroundColor: parentBg,
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
            }
          }

          // Check for input / textarea / button text
          const isFormTag = ['input', 'textarea', 'button'].includes(tagName);
          if (isFormTag) {
            const inputEl = el as HTMLInputElement;
            const inputType = (inputEl.type || 'text').toLowerCase();
            const isTextInput = ['text', 'search', 'email', 'password', 'number', 'url', 'tel'].includes(inputType) || tagName === 'textarea';
            const isButtonInput = ['submit', 'button', 'reset'].includes(inputType) || tagName === 'button';

            let rawVal = '';
            let isPlaceholder = false;

            if (isTextInput) {
              rawVal = inputEl.value;
              if (!rawVal && inputEl.placeholder) {
                rawVal = inputEl.placeholder;
                isPlaceholder = true;
              }
            } else if (isButtonInput) {
              rawVal = inputEl.value || inputEl.textContent?.trim() || (inputType === 'submit' ? 'Submit' : '');
            }

            if (rawVal) {
              const displayVal = (inputType === 'password' && !isPlaceholder) ? '•'.repeat(rawVal.length) : rawVal;
              const computedColor = isPlaceholder ? 'rgb(128, 128, 128)' : style.color;
              const fontSize = parseFloat(style.fontSize) || 13;
              const isFixed = style.position === 'fixed';

              const paddingLeft = Math.max(6, parseFloat(style.paddingLeft) || 6);
              const textY = Math.round((isFixed ? rect.y : rect.y + window.scrollY) + Math.max(1, (rect.height - fontSize) / 2));
              const textX = Math.round((isFixed ? rect.x : rect.x + window.scrollX) + paddingLeft);

              results.push({
                type: 'text',
                x: textX,
                y: textY,
                width: Math.round(rect.width - paddingLeft),
                height: Math.round(rect.height),
                text: displayVal,
                zIndex: getEffectiveZIndex(el) + 2,
                treeOrder: treeOrderCounter++,
                computedStyle: {
                  color: computedColor,
                  fontFamily: style.fontFamily,
                  fontSize: style.fontSize,
                  fontWeight: style.fontWeight || (isButtonInput ? 'bold' : 'normal'),
                  fontStyle: style.fontStyle || 'normal',
                  textDecoration: style.textDecoration || 'none',
                }
              });
            }
          }

          // Check for background color
          const maskImage = style.maskImage || '';
          if (maskImage === 'none' || !maskImage.includes('url(')) {
            const bgColor = style.backgroundColor;
            if (bgColor && bgColor !== 'rgba(0, 0, 0, 0)' && bgColor !== 'transparent') {
              // Check if element is fixed/sticky to avoid incorrect scroll offset
              const isFixed = style.position === 'fixed';

              results.push({
                type: 'background',
                x: Math.round(isFixed ? rect.x : rect.x + window.scrollX),
                y: Math.round(isFixed ? rect.y : rect.y + window.scrollY),
                width: Math.round(rect.width),
                height: Math.round(rect.height),
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
                computedStyle: {
                  backgroundColor: style.backgroundColor,
                  borderRadius: style.borderRadius || style.borderTopLeftRadius || '0px',  // Fallback to individual corner
                  color: style.color,
                  fontFamily: style.fontFamily,
                  fontSize: style.fontSize,
                  fontWeight: style.fontWeight,
                  display: style.display,
                  visibility: style.visibility,
                }
              });
            }
          }
        } else if (node.nodeType === Node.TEXT_NODE) {
          // Skip text node processing - we'll extract text from elements instead
          // This ensures the browser's whitespace handling is preserved
          continue;
        }
      }

      // IMPROVED APPROACH: Extract text runs using range API
      // Walk all text nodes but use Range to get accurate bounding boxes
      // Whitespace preserved by not trimming until after extraction
      try {
        const textNodes: Node[] = [];
        const textWalker = document.createTreeWalker(
          document.body,
          NodeFilter.SHOW_TEXT,
          {
            acceptNode: (node) => {
              const text = node.textContent || '';
              if (text.length === 0) return NodeFilter.FILTER_REJECT;
              const parent = node.parentElement;
              if (!parent || isElementHidden(parent)) return NodeFilter.FILTER_REJECT;
              // Accept all text nodes
              return NodeFilter.FILTER_ACCEPT;
            }
          }
        );

        let textNode: Node | null;
        while ((textNode = textWalker.nextNode())) {
          textNodes.push(textNode);
        }

        for (const node of textNodes) {
          const parent = node.parentElement;
          if (!parent) continue;

          const text = node.textContent || '';
          if (text.length === 0) continue;

          try {
            // Word-range baseline grouping:
            // Tokenize text into words, measure each word's bounding box via Range,
            // and group words sharing the same visual line baseline Y into single-line primitives.
            const tokens: { text: string; start: number; end: number }[] = [];
            const tokenRegex = /\S+\s*|\s+/g;
            let match: RegExpExecArray | null;
            while ((match = tokenRegex.exec(text)) !== null) {
              tokens.push({
                text: match[0],
                start: match.index,
                end: match.index + match[0].length,
              });
            }

            interface LineBucket {
              y: number;
              minX: number;
              maxX: number;
              height: number;
              text: string;
            }
            const lineBuckets: LineBucket[] = [];

            for (const token of tokens) {
              if (token.text.trim().length === 0 && tokens.length > 1) {
                if (lineBuckets.length > 0) {
                  const lastLine = lineBuckets[lineBuckets.length - 1];
                  if (!lastLine.text.endsWith(' ')) {
                    lastLine.text += ' ';
                  }
                }
                continue;
              }

              try {
                const range = document.createRange();
                range.setStart(node, token.start);
                range.setEnd(node, token.end);
                const rect = range.getBoundingClientRect();

                if (rect.width === 0 || rect.height === 0) continue;

                // Check if parent element is fixed (NOT sticky - sticky scrolls normally)
                const parentStyle = window.getComputedStyle(parent);
                const isFixed = parentStyle.position === 'fixed';

                const absY = Math.round(isFixed ? rect.y : rect.y + window.scrollY);
                const absX = Math.round(isFixed ? rect.x : rect.x + window.scrollX);
                const rectW = Math.round(rect.width);
                const rectH = Math.round(rect.height);

                let matchedBucket: LineBucket | null = null;
                for (const bucket of lineBuckets) {
                  // Only group tokens into the same line bucket if they share baseline Y
                  // AND are horizontally adjacent (not jumping across columns)
                  if (Math.abs(bucket.y - absY) <= 3 && (absX - bucket.maxX) < 40) {
                    matchedBucket = bucket;
                    break;
                  }
                }

                if (matchedBucket) {
                  if (!matchedBucket.text.endsWith(' ') && !token.text.startsWith(' ')) {
                    matchedBucket.text += ' ';
                  }
                  matchedBucket.text += token.text.trim();
                  if (token.text.endsWith(' ')) matchedBucket.text += ' ';
                  matchedBucket.maxX = Math.max(matchedBucket.maxX, absX + rectW);
                  matchedBucket.height = Math.max(matchedBucket.height, rectH);
                } else {
                  lineBuckets.push({
                    y: absY,
                    minX: absX,
                    maxX: absX + rectW,
                    height: rectH,
                    text: token.text,
                  });
                }
              } catch (e) {
                // Range measurement guard
              }
            }

            const style = window.getComputedStyle(parent);
            for (const bucket of lineBuckets) {
              // Normalize newlines/tabs to spaces and collapse duplicate spaces,
              // preserving leading/trailing spaces that separate adjacent DOM elements
              let lineText = bucket.text.replace(/[\r\n\t]+/g, ' ').replace(/  +/g, ' ');
              if (lineText.trim().length === 0) continue;

              const lineMaxWidth = Math.round(bucket.maxX - bucket.minX);


              results.push({
                type: 'text',
                x: bucket.minX,
                y: bucket.y,
                width: bucket.maxX - bucket.minX,
                height: bucket.height,
                text: lineText,
                maxWidth: lineMaxWidth,
                zIndex: getEffectiveZIndex(parent),
                treeOrder: treeOrderCounter++,
                computedStyle: {
                  color: style.color,
                  fontFamily: style.fontFamily,
                  fontSize: style.fontSize,
                  fontWeight: style.fontWeight,
                  fontStyle: style.fontStyle,
                  lineHeight: style.lineHeight,
                  textAlign: style.textAlign,
                  textDecoration: style.textDecoration,
                  cursor: style.cursor,
                  isLink: !!parent.closest('a'),
                },
              });
            }
          } catch (err) {
            // Skip text node on error
          }
        }
      } catch (err) {
        console.error('[Pipeline] Text extraction error:', err);
      }

      // Extract mask icons & pseudo-elements
      try {
        const allElements = document.querySelectorAll('*');
        for (let i = 0; i < allElements.length; i++) {
          const el = allElements[i] as HTMLElement;
          const rect = el.getBoundingClientRect();
          const style = window.getComputedStyle(el);
          const isFixed = style.position === 'fixed';

          if (rect.width === 0 || rect.height === 0) continue;

          const elementMaskImage = style.maskImage || '';
          const svgUrl = extractUrlValue(elementMaskImage);

          if (svgUrl) {
            const width = rect.width;
            const height = rect.height;
            if (width >= 8 && height >= 8) {
              let finalSrc = svgUrl;
              if (svgUrl.startsWith('data:') && svgUrl.includes('%')) {
                try { finalSrc = decodeURIComponent(svgUrl); } catch (e) {}
              }

              const x = Math.round(isFixed ? rect.x : rect.x + window.scrollX);
              const y = Math.round(isFixed ? rect.y : rect.y + window.scrollY);
              const bgColor = style.backgroundColor;

              results.push({
                type: 'image',
                x: x,
                y: y,
                width: Math.round(width),
                height: Math.round(height),
                tagName: 'mask-' + el.tagName.toLowerCase(),
                src: finalSrc,
                maskColor: bgColor,
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
              continue;
            }
          }

          // DEBUG: Log XenForo navigation pseudo-elements
          const className = el.className || '';
          if (typeof className === 'string' && (className.includes('p-navEl-splitTrigger') || className.includes('hScroller-action'))) {
            const rect = el.getBoundingClientRect();
            for (const pseudoType of ['::before', '::after']) {
              const pseudoStyle = window.getComputedStyle(el, pseudoType);
              const maskImg = pseudoStyle.maskImage || 'none';
              const bgImg = pseudoStyle.backgroundImage || 'none';
              const maskUrl = maskImg.includes('url(') ? extractUrlValue(maskImg) : 'none';
              const bgUrl = bgImg.includes('url(') ? extractUrlValue(bgImg) : 'none';

              if (maskUrl !== 'none' || bgUrl !== 'none') {
                console.log('🎯 ICON FOUND:', {
                  element: el.tagName + '.' + className.substring(0, 30),
                  pseudo: pseudoType,
                  width: pseudoStyle.width,
                  height: pseudoStyle.height,
                  bgColor: pseudoStyle.backgroundColor,
                  pos: `(${Math.round(rect.x)}, ${Math.round(rect.y)})`,
                  size: `${Math.round(rect.width)}x${Math.round(rect.height)}`
                });
                console.log('   MASK URL:', maskUrl.substring(0, 100));
                console.log('   BG URL:', bgUrl.substring(0, 100));
              }
            }
          }

          for (const pseudoType of ['::before', '::after']) {
            const pseudoStyle = window.getComputedStyle(el, pseudoType);
            const pseudoWidth = parseFloat(pseudoStyle.width);
            const pseudoHeight = parseFloat(pseudoStyle.height);

            if (pseudoWidth < 8 || pseudoHeight < 8) continue;

            const maskImage = pseudoStyle.maskImage || '';
            const bgImage = pseudoStyle.backgroundImage || '';
            const maskUrl = extractUrlValue(maskImage);
            const bgUrl = extractUrlValue(bgImage);

            const isFixedPseudo = style.position === 'fixed';
            const pseudoX = Math.round(isFixedPseudo ? rect.x : rect.x + window.scrollX);
            const pseudoY = Math.round(isFixedPseudo ? rect.y : rect.y + window.scrollY);
            const pseudoW = Math.round(Math.min(pseudoWidth, rect.width));
            const pseudoH = Math.round(Math.min(pseudoHeight, rect.height));

            // Prioritize mask-image over background-image (icon fonts use mask-image)
            if (maskUrl) {
              // This is a masked icon (FontAwesome, Material Icons, etc.)
              let finalMaskUrl = maskUrl;
              if (maskUrl.startsWith('data:') && maskUrl.includes('%')) {
                try { finalMaskUrl = decodeURIComponent(maskUrl); } catch (e) {}
              }

              // DEBUG: Log position to see if overlapping text
              const className = el.className || '';
              if (typeof className === 'string' && className.includes('navEl')) {
                console.log(`[Pipeline] Masked icon extraction: (${pseudoX}, ${pseudoY}) ${pseudoW}x${pseudoH} from ${className.substring(0, 40)}`);
              }

              results.push({
                type: 'maskedImage',
                x: pseudoX,
                y: pseudoY,
                width: pseudoW,
                height: pseudoH,
                maskUrl: finalMaskUrl,
                fillColor: pseudoStyle.backgroundColor,
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
            } else if (bgUrl) {
              // Regular background image
              let finalBgUrl = bgUrl;
              if (bgUrl.startsWith('data:') && bgUrl.includes('%')) {
                try { finalBgUrl = decodeURIComponent(bgUrl); } catch (e) {}
              }

              results.push({
                type: 'image',
                x: pseudoX,
                y: pseudoY,
                width: pseudoW,
                height: pseudoH,
                tagName: 'pseudo-' + pseudoType.replace('::', ''),
                src: finalBgUrl,
                maskColor: pseudoStyle.backgroundColor,
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
            }
          }
        }
      } catch (err) {}

      // Extract borders
      try {
        const allElements = document.querySelectorAll('*');
        for (let i = 0; i < allElements.length; i++) {
          const el = allElements[i] as HTMLElement;
          const rect = el.getBoundingClientRect();
          const style = window.getComputedStyle(el);

          if (rect.width > 0 && rect.height > 0 && !isElementHidden(el)) {
            const borderTopWidth = parseFloat(style.borderTopWidth) || 0;
            const borderRightWidth = parseFloat(style.borderRightWidth) || 0;
            const borderBottomWidth = parseFloat(style.borderBottomWidth) || 0;
            const borderLeftWidth = parseFloat(style.borderLeftWidth) || 0;

            const hasTop = borderTopWidth > 0 && style.borderTopStyle !== 'none';
            const hasRight = borderRightWidth > 0 && style.borderRightStyle !== 'none';
            const hasBottom = borderBottomWidth > 0 && style.borderBottomStyle !== 'none';
            const hasLeft = borderLeftWidth > 0 && style.borderLeftStyle !== 'none';

            if (
              hasTop && hasRight && hasBottom && hasLeft &&
              borderTopWidth === borderRightWidth &&
              borderTopWidth === borderBottomWidth &&
              borderTopWidth === borderLeftWidth
            ) {
              results.push({
                type: 'border',
                x: Math.round(rect.x + window.scrollX),
                y: Math.round(rect.y + window.scrollY),
                width: Math.round(rect.width),
                height: Math.round(rect.height),
                borderTopWidth,
                borderRightWidth,
                borderBottomWidth,
                borderLeftWidth,
                borderTopColor: style.borderTopColor,
                borderTopStyle: style.borderTopStyle,
                borderRadius: style.borderRadius || style.borderTopLeftRadius || '0px',  // Fallback to individual corner
                zIndex: getEffectiveZIndex(el),
                treeOrder: treeOrderCounter++,
              });
            } else {
              const isFixed = style.position === 'fixed';
              const scrollX = isFixed ? 0 : window.scrollX;
              const scrollY = isFixed ? 0 : window.scrollY;

              if (hasBottom) {
                results.push({
                  type: 'background',
                  x: Math.round(rect.x + scrollX),
                  y: Math.round(rect.y + scrollY + rect.height - borderBottomWidth),
                  width: Math.round(rect.width),
                  height: Math.round(borderBottomWidth),
                  zIndex: getEffectiveZIndex(el),
                  treeOrder: treeOrderCounter++,
                  computedStyle: { backgroundColor: style.borderBottomColor }
                });
              }
              if (hasTop) {
                results.push({
                  type: 'background',
                  x: Math.round(rect.x + scrollX),
                  y: Math.round(rect.y + scrollY),
                  width: Math.round(rect.width),
                  height: Math.round(borderTopWidth),
                  zIndex: getEffectiveZIndex(el),
                  treeOrder: treeOrderCounter++,
                  computedStyle: { backgroundColor: style.borderTopColor }
                });
              }
              if (hasLeft) {
                results.push({
                  type: 'background',
                  x: Math.round(rect.x + scrollX),
                  y: Math.round(rect.y + scrollY),
                  width: Math.round(borderLeftWidth),
                  height: Math.round(rect.height),
                  zIndex: getEffectiveZIndex(el),
                  treeOrder: treeOrderCounter++,
                  computedStyle: { backgroundColor: style.borderLeftColor }
                });
              }
              if (hasRight) {
                results.push({
                  type: 'background',
                  x: Math.round(rect.x + scrollX + rect.width - borderRightWidth),
                  y: Math.round(rect.y + scrollY),
                  width: Math.round(borderRightWidth),
                  height: Math.round(rect.height),
                  zIndex: getEffectiveZIndex(el),
                  treeOrder: treeOrderCounter++,
                  computedStyle: { backgroundColor: style.borderRightColor }
                });
              }
            }
          }
        }
      } catch (err) {}

      // Sticky elements
      const allElems = document.querySelectorAll('*');
      for (let i = 0; i < allElems.length; i++) {
        const el = allElems[i] as HTMLElement;
        const style = window.getComputedStyle(el);

        if (style.position === 'fixed') {
          const rect = el.getBoundingClientRect();
          if (rect.width > 0 && rect.height > 0) {
            scrollMetadata.stickyElements.push({
              position: style.position,
              x: Math.round(rect.x),
              y: Math.round(rect.y),
              width: Math.round(rect.width),
              height: Math.round(rect.height),
            });
          }
        }
      }

      return { elements: results, scrollMetadata };
    }

    return await page.evaluate(walkDOMBrowserScript);
  }

  /**
   * Stage 2: Filter elements based on bounds, dimensions, and visibility
   */
  static filter(elements: ExtractedElement[]): ExtractedElement[] {
    const VIEWPORT_WIDTH = 1024;
    return elements.filter((elem) => {
      if (elem.x + elem.width < 0 || elem.x > VIEWPORT_WIDTH) {
        return false;
      }
      if (elem.width <= 0 || elem.height <= 0) {
        return false;
      }
      if (elem.type === 'background') {
        if (!isElementVisible(elem.computedStyle, { width: elem.width, height: elem.height })) {
          return false;
        }
        const bgColor = elem.computedStyle.backgroundColor;
        if (bgColor === 'rgba(0, 0, 0, 0)' || bgColor === 'transparent') {
          return false;
        }
      } else if (elem.type === 'text') {
        // Don't trim - whitespace-only text elements are valid (e.g., space between inline elements)
        // Example: "<a>article</a> <span>relies</span>" has a space text node between them
        if (!elem.text || elem.text.length === 0) {
          return false;
        }
      }
      return true;
    });
  }

  /**
   * Stage 3: Transform filtered elements into primitives (DrawRect, DrawText, etc.)
   */
  static transform(elements: ExtractedElement[]): Primitive[] {
    const primitives: Primitive[] = [];

    for (const elem of elements) {
      switch (elem.type) {
        case 'background': {
          const bgColor = parseColor(elem.computedStyle.backgroundColor);
          if (bgColor && bgColor.a > 0) {
            // Extract borderRadius (e.g., "8px" -> 8)
            const borderRadiusStr = elem.computedStyle.borderRadius || '0px';
            const borderRadius = Math.min(255, Math.max(0, Math.round(parseFloat(borderRadiusStr))));

            primitives.push({
              type: PrimitiveType.DrawRect,
              x: elem.x,
              y: elem.y,
              width: elem.width,
              height: elem.height,
              color: bgColor,
              borderRadius: borderRadius > 0 ? borderRadius : undefined,
              zIndex: elem.zIndex || 0,
              treeOrder: elem.treeOrder || 0,
            } as Primitive);
          }
          break;
        }
        case 'text': {
          const textColor = parseColor(elem.computedStyle.color);
          if (textColor && textColor.a > 0 && elem.text.length > 0) {
            const fontSize = parseInt(elem.computedStyle.fontSize) || 12;
            const fontWeight = elem.computedStyle.fontWeight;
            const fontFamily = elem.computedStyle.fontFamily;
            const fontStyle = elem.computedStyle.fontStyle || 'normal';
            const textDecoration = elem.computedStyle.textDecoration || '';
            const isBold = (typeof fontWeight === 'number' && fontWeight >= 600) ||
                           fontWeight === 'bold' || fontWeight === '700' || fontWeight === '600' ||
                           fontWeight === '800' || fontWeight === '900';
            const isItalic = fontStyle === 'italic' || fontStyle === 'oblique';
            const isUnderline = textDecoration.includes('underline');
            const isLink = !!(elem.computedStyle as any).isLink || (elem.computedStyle as any).cursor === 'pointer';
            const hoverUnderline = isLink || isUnderline;
            const hoverColor = isLink ? { r: 215, g: 30, b: 30, a: 255 } : undefined;

            const macFont = substituteFontForMacOS9(fontFamily, fontWeight, fontStyle);

            primitives.push({
              type: PrimitiveType.DrawText,
              x: elem.x,
              y: elem.y,
              text: elem.text,
              fontId: macFont.id,
              fontSize: fontSize,
              color: textColor,
              hoverColor,
              hoverUnderline: hoverUnderline || undefined,
              maxWidth: elem.maxWidth,
              isBold,
              isItalic,
              isUnderline,
              zIndex: elem.zIndex || 0,
              treeOrder: elem.treeOrder || 0,
              originalFontFamily: fontFamily,
              originalFontWeight: fontWeight,
              substituteFontName: macFont.name,
            } as Primitive);
          }
          break;
        }
        case 'border': {
          const borderColor = parseColor(elem.borderTopColor);
          // Extract borderRadius (e.g., "8px" -> 8)
          const borderRadiusStr = elem.borderRadius || '0px';
          const borderRadius = Math.min(255, Math.max(0, Math.round(parseFloat(borderRadiusStr))));

          primitives.push({
            type: PrimitiveType.DrawBorder,
            x: elem.x,
            y: elem.y,
            width: elem.width,
            height: elem.height,
            thickness: Math.round(elem.borderTopWidth),
            color: borderColor,
            borderRadius: borderRadius > 0 ? borderRadius : undefined,
            zIndex: elem.zIndex || 0,
            treeOrder: elem.treeOrder || 0,
          } as Primitive);
          break;
        }
        case 'image': {
          primitives.push({
            type: PrimitiveType.DrawImage,
            x: elem.x,
            y: elem.y,
            width: elem.width,
            height: elem.height,
            src: elem.src,
            zIndex: (elem.zIndex || 0) + 1,
            treeOrder: (elem.treeOrder || 0) + 10,
            maskColor: elem.maskColor,
            backgroundColor: elem.backgroundColor,
          } as Primitive);
          break;
        }
        case 'maskedImage': {
          const fillColor = parseColor(elem.fillColor);
          primitives.push({
            type: PrimitiveType.DrawMaskedImage,
            x: elem.x,
            y: elem.y,
            width: elem.width,
            height: elem.height,
            fillColor,
            src: elem.maskUrl,  // Store mask URL for later rasterization
            zIndex: (elem.zIndex || 0) + 1,
            treeOrder: (elem.treeOrder || 0) + 10,
          } as Primitive);
          break;
        }
      }
    }

    return primitives;
  }

  /**
   * Helper: Calculate accurate text width using real Mac OS 9 font metrics
   * Uses actual character widths from font-widths.ts instead of approximations
   */
  private static calculateTextWidth(text: string, fontId: number, fontSize: number): number {
    // Get the font's character width table
    const font = MAC_OS_9_FONTS.find(f => f.id === fontId);
    if (!font) {
      // Fallback to 0.55 approximation if font not found
      return text.length * fontSize * 0.55;
    }

    let totalWidth = 0;
    for (const char of text) {
      const charWidth = font.charWidths[char] || font.avgCharWidth;
      totalWidth += charWidth * fontSize;
    }
    return totalWidth;
  }

  /**
   * Stage 4: Text Run Consolidation
   *
   * Merges adjacent DrawText primitives that are on the same visual line and share
   * the same font/size/color/style/zIndex into a single DrawText run. Reduces primitive
   * counts on rich pages (Wikipedia, BBC) by ~60%, cutting wire payload and C++ render work.
   *
   * Merge conditions (ALL must be true):
   *  1. Both are DrawText primitives
   *  2. Same Y baseline (within 1px — tight to preserve superscripts at different Y)
   *  3. Same fontId AND fontSize (different sizes = superscript vs body)
   *  4. Same RGBA color (blue link vs black body must NOT merge)
   *  5. Same isItalic AND isUnderline (link underlines must NOT bleed into plain text)
   *  6. Same zIndex
   *  7. The next primitive starts within 8px of where the current run ends
   *     (gap tolerance covers letter-spacing and small inline padding)
   *
   * Non-text primitives (rects, borders) between two text runs do NOT break
   * the sweep — they are emitted in-place, and we only stop merging when we
   * encounter a text primitive that fails the above checks.
   */
  static optimize(primitives: Primitive[]): Primitive[] {
    // Sort by (zIndex, treeOrder) so text appears in correct DOM render order
    const sorted = [...primitives].sort((a, b) => {
      const aZ = (a as any).zIndex ?? 0;
      const bZ = (b as any).zIndex ?? 0;
      if (aZ !== bZ) return aZ - bZ;
      return ((a as any).treeOrder ?? 0) - ((b as any).treeOrder ?? 0);
    });

    const result: Primitive[] = [];
    let i = 0;

    while (i < sorted.length) {
      const prim = sorted[i];

      // Non-text primitives pass through unchanged
      if (prim.type !== PrimitiveType.DrawText) {
        result.push(prim);
        i++;
        continue;
      }

      // Start a candidate run
      let run = { ...prim } as typeof prim;
      let j = i + 1;

      while (j < sorted.length) {
        const next = sorted[j];

        // Non-text primitive in the middle: emit it in-place and continue
        // scanning for more text that can be appended to the current run.
        // (e.g. a DrawRect background behind an inline element)
        if (next.type !== PrimitiveType.DrawText) {
          result.push(next);
          j++;
          continue;
        }

        // --- All merge guards ---

        // 1. Y baseline must match within 1px
        //    Tight tolerance so superscripts (raised ~4–6px) stay separate.
        const sameY = Math.abs(next.y - run.y) <= 1;

        // 2. Font identity: fontId encodes family+weight+style, fontSize is point size.
        //    Both must match — different sizes = sub/superscript context.
        const sameFont = next.fontId === run.fontId && next.fontSize === run.fontSize;

        // 3. Color: blue link text must never merge with black body text.
        const sameColor = next.color.r === run.color.r &&
                          next.color.g === run.color.g &&
                          next.color.b === run.color.b &&
                          next.color.a === run.color.a;

        // 4. Text decoration: underlined link spans must not merge with plain text.
        //    This was the primary cause of the spacing gaps seen in the comparison.
        const sameStyle = (!!next.isItalic === !!run.isItalic) &&
                          (!!next.isUnderline === !!run.isUnderline);

        // 5. Stacking layer
        const sameZ = (next.zIndex ?? 0) === (run.zIndex ?? 0);

        // 6. Horizontal proximity: Calculate ACTUAL text width using font metrics
        //    Use real character widths from font-widths.ts for accurate positioning
        const textWidth = this.calculateTextWidth(run.text, run.fontId, run.fontSize);
        const estimatedRunEndX = run.x + textWidth;
        const horizontalGap   = next.x - estimatedRunEndX;
        const touches         = horizontalGap >= -4 && horizontalGap <= 8;

        if (sameY && sameFont && sameColor && sameStyle && sameZ && touches) {
          // ALWAYS insert a space between merged runs unless they already have one
          // This is the safest approach since CSS collapses whitespace-only text nodes
          const needsSpace = !run.text.endsWith(' ') && !next.text.startsWith(' ');
          const spacer = needsSpace ? ' ' : '';

          run = {
            ...run,
            text: run.text + spacer + next.text,
            maxWidth: run.maxWidth != null && next.maxWidth != null
              ? Math.max(run.maxWidth, next.maxWidth)
              : (run.maxWidth ?? next.maxWidth),
          };
          j++;
        } else {
          // Text style/position mismatch — stop extending this run
          break;
        }
      }

      result.push(run);
      // Skip over any non-text primitives we already emitted inside the inner loop
      i = j;
    }

    const before = primitives.filter(p => p.type === PrimitiveType.DrawText).length;
    const after  = result.filter(p => p.type === PrimitiveType.DrawText).length;
    if (before > 0) {
      console.log(
        `[Pipeline] Text consolidation: ${before} → ${after} text primitives ` +
        `(${Math.round((1 - after / before) * 100)}% reduction)`
      );
    }

    return result;
  }


  /**
   * Stage 5: Viewport-First Image Encoding (async) & CSS Stacking Order Sort
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

    const minY = scrollY - 300;
    const maxY = scrollY + viewportHeight + 300;

    const encodePromises = imagePrimitives.map(async (prim) => {
      let src = prim.src;
      const maskColor = prim.maskColor;
      const backgroundColor = prim.backgroundColor;

      // Strip leading/trailing quotes from src (malformed HTML)
      if (src && ((src.startsWith('"') && src.endsWith('"')) || (src.startsWith("'") && src.endsWith("'")))) {
        src = src.slice(1, -1);
      } else if (src && (src.startsWith('"') || src.startsWith("'"))) {
        src = src.slice(1);  // Strip leading quote even if no trailing quote
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
          color: { r: 230, g: 230, b: 230, a: 255 },
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }

      const isOnScreen = prim.y >= minY && prim.y <= maxY;
      const isAlreadyCached = pictCache.has(src, 240, maskColor, backgroundColor);

      if (isOnScreen || isAlreadyCached) {
        // High priority: encode immediately for visible viewport
        try {
          const encoded = await pictCache.get(src, 240, maskColor, backgroundColor);
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
          console.error('[Pipeline] Image encoding error for src:', src ? src.substring(0, 60) : 'none', e);
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
        // Low priority off-screen: Trigger background pre-encoding without blocking Frame 1
        pictCache.get(src, 240, maskColor, backgroundColor).catch(() => {});
        return {
          type: PrimitiveType.DrawImage,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          src: src,
          pictBytes: Buffer.alloc(0),
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }
    });

    // Encode masked images (FontAwesome icons, etc.) to 1-bit monochrome masks
    const encodeMaskPromises = maskedImagePrimitives.map(async (prim) => {
      let src = prim.src;

      // Strip leading/trailing quotes from src (malformed HTML)
      if (src && ((src.startsWith('"') && src.endsWith('"')) || (src.startsWith("'") && src.endsWith("'")))) {
        src = src.slice(1, -1);
      } else if (src && (src.startsWith('"') || src.startsWith("'"))) {
        src = src.slice(1);  // Strip leading quote even if no trailing quote
      }

      if (src && src.startsWith('//')) {
        src = 'https:' + src;
      } else if (src && !src.startsWith('http') && !src.startsWith('data:') && pageUrl) {
        try {
          src = new URL(src, pageUrl).href;
        } catch (e) {}
      }

      if (!src || (!src.startsWith('http') && !src.startsWith('data:'))) {
        // Invalid mask URL - render as empty rect
        return {
          type: PrimitiveType.DrawRect,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          color: { r: 0, g: 0, b: 0, a: 0 }, // Transparent
          zIndex: prim.zIndex,
          treeOrder: prim.treeOrder,
        } as Primitive;
      }

      try {
        // Rasterize SVG mask to 1-bit monochrome
        const maskData = await maskCache.get(src, prim.width, prim.height, page);

        console.log(`[Pipeline] DrawMaskedImage created: (${prim.x}, ${prim.y}) ${prim.width}x${prim.height} ` +
                    `color=(${prim.fillColor.r},${prim.fillColor.g},${prim.fillColor.b}) ` +
                    `maskBytes=${maskData.length}`);

        return {
          type: PrimitiveType.DrawMaskedImage,
          x: prim.x,
          y: prim.y,
          width: prim.width,
          height: prim.height,
          fillColor: prim.fillColor,
          maskData,
          src, // Keep for debugging
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

    all.sort((a, b) => {
      const zA = (a as any).zIndex || 0;
      const zB = (b as any).zIndex || 0;
      if (zA !== zB) return zA - zB;
      return ((a as any).treeOrder || 0) - ((b as any).treeOrder || 0);
    });

    // NOTE: Server-side viewport culling was removed because it conflicts with
    // identity-based change detection. When primitives scroll off-screen and get
    // culled, the change detector marks them as "removed" and sends delete
    // instructions to the client, causing layout issues when scrolling back.
    //
    // Current approach: Send all primitives, rely on:
    // 1. Client-side viewport culling (renderer skips drawing off-screen)
    // 2. Lazy image loading (off-screen images have empty pictBytes)
    // 3. Change detection (only send diffs, not full re-renders)

    return all;
  }
}
