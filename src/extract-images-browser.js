/**
 * Browser-side image extraction script
 *
 * This file is executed in the browser context via page.evaluate().
 * It must be plain JavaScript (no TypeScript, no imports, no bundler helpers).
 *
 * Returns array of image data to be converted to primitives on the server.
 */
(function extractImages() {
  const images = [];
  let treeOrderCounter = 10000; // Start high to avoid conflicts with snapshot primitives

  const getEffectiveBackgroundColor = (element) => {
    let current = element.parentElement;
    while (current) {
      const bg = window.getComputedStyle(current).backgroundColor;
      if (bg && bg !== 'rgba(0, 0, 0, 0)' && bg !== 'transparent') {
        return bg;
      }
      current = current.parentElement;
    }
    return 'white';
  };

  const extractUrlValue = (str) => {
    if (!str || typeof str !== 'string' || !str.includes('url(')) return '';
    const start = str.indexOf('url(') + 4;
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
  };

  // Find all <img>, <video>, <canvas>, <svg> elements
  const mediaElements = document.querySelectorAll('img, video, canvas, svg');
  for (const el of Array.from(mediaElements)) {
    const element = el;
    const rect = element.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) continue;

    const style = window.getComputedStyle(element);
    const isFixed = style.position === 'fixed';

    let src = '';
    if (element instanceof HTMLImageElement) {
      src = element.currentSrc || element.src;
    } else if (element instanceof SVGElement) {
      // Convert SVG to data URL for rendering
      const serializer = new XMLSerializer();
      const svgString = serializer.serializeToString(element);
      const encodedSVG = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgString);
      src = encodedSVG;

      // Debug: log small SVGs (likely icons)
      if (rect.width < 50 && rect.height < 50) {
        console.log('[extract-images] Small SVG icon:', rect.width + 'x' + rect.height, 'at (' + Math.round(rect.x) + ',' + Math.round(rect.y) + ')');
      }
    } else if (element instanceof HTMLCanvasElement || element instanceof HTMLVideoElement) {
      // For canvas/video, we'd need to extract data URL - skip for now
      continue;
    }

    if (src) {
      images.push({
        type: 'image',
        x: Math.round(isFixed ? rect.x : rect.x + window.scrollX),
        y: Math.round(isFixed ? rect.y : rect.y + window.scrollY),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
        src: src,
        backgroundColor: getEffectiveBackgroundColor(element),
        zIndex: 0,
        treeOrder: treeOrderCounter++,
      });
    }
  }

  // Find elements with background-image
  const allElements = document.querySelectorAll('*');
  for (const el of Array.from(allElements)) {
    const element = el;
    const rect = element.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) continue;

    const style = window.getComputedStyle(element);
    const bgImage = style.backgroundImage;

    if (bgImage && bgImage !== 'none' && bgImage.includes('url(')) {
      const url = extractUrlValue(bgImage);
      if (url && !url.startsWith('data:image/svg+xml')) {
        const isFixed = style.position === 'fixed';

        images.push({
          type: 'image',
          x: Math.round(isFixed ? rect.x : rect.x + window.scrollX),
          y: Math.round(isFixed ? rect.y : rect.y + window.scrollY),
          width: Math.round(rect.width),
          height: Math.round(rect.height),
          src: url,
          backgroundColor: getEffectiveBackgroundColor(element),
          zIndex: 0,
          treeOrder: treeOrderCounter++,
        });
      }
    }

    // Find mask-image (for icon fonts and sprite sheets)
    const maskImage = style.maskImage || style.webkitMaskImage;
    if (maskImage && maskImage !== 'none' && maskImage.includes('url(')) {
      const url = extractUrlValue(maskImage);
      if (url) {
        // Capture mask-position and mask-size for sprite sheet support
        const maskPosition = style.maskPosition || style.webkitMaskPosition || '0% 0%';
        const maskSize = style.maskSize || style.webkitMaskSize || 'auto';

        images.push({
          type: 'maskedImage',
          x: Math.round(rect.x + window.scrollX),
          y: Math.round(rect.y + window.scrollY),
          width: Math.round(rect.width),
          height: Math.round(rect.height),
          src: url,
          fillColor: style.backgroundColor,
          maskPosition: maskPosition,
          maskSize: maskSize,
          zIndex: 0,
          treeOrder: treeOrderCounter++,
        });
      }
    }
  }

  return images;
})();
