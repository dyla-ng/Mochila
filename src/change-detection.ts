/**
 * Change Detection for Primitive Trees
 *
 * Per handoff spec Section 5: this is a NEW problem, not a port of v1's
 * tile-hashing/CopyRect logic. We use DOM-based identity, not pixel comparison.
 *
 * Key principles:
 * - Identity based on content+type+position hash (not CDP node IDs - see CDP-NODE-ID-DECISION.md)
 * - Diff primitives by identity to find: added, removed, changed
 * - Scrolling should be cheap: just y-coordinate changes
 */

import * as crypto from 'crypto';
import {
  Primitive,
  PrimitiveType,
  DrawRectPrimitive,
  DrawTextPrimitive,
  DrawImagePrimitive,
  DrawBorderPrimitive,
} from './types';

export type PrimitiveWithIdentity = Primitive & {
  identity: string;
};

export interface FrameDiff {
  added: PrimitiveWithIdentity[];
  removed: PrimitiveWithIdentity[];  // For analysis/reporting only
  changed: PrimitiveWithIdentity[];
  unchanged: PrimitiveWithIdentity[];
  removalInstructions: Primitive[];  // Actual RemovePrimitive wire instructions
}

export interface ScrollDelta {
  isScroll: boolean;
  deltaY: number;
  affectedPrimitives: number;
}

/**
 * Generate stable identity for a primitive based on its content, not DOM node ID
 *
 * Identity = hash(type + content) — EXCLUDES position for scroll detection!
 * - For text: type + text + font + size + color
 * - For rect: type + color + width + height
 * - For image: type + src + width + height
 *
 * Position is NOT included in identity, so when elements scroll (position changes),
 * they maintain same identity and show up as "changed" (not added/removed).
 * This makes scrolling "nearly free" as required.
 */
export function generatePrimitiveIdentity(primitive: Primitive): string {
  const parts: string[] = [];

  // Type always included
  parts.push(`type:${primitive.type}`);

  // Type-specific CONTENT included — this is what makes identities stable across
  // sliding-window evictions. When the top article scrolls out of the window,
  // the remaining articles must keep their original identities so the diff engine
  // doesn't match them against the wrong predecessors and corrupt client state.
  //
  // Coordinates (x, y) are NOT included - primitives with same content but different
  // positions get the same identity, allowing efficient diff detection of moved elements.
  switch (primitive.type) {
    case PrimitiveType.DrawText: {
      const p = primitive as DrawTextPrimitive;
      parts.push(`text:${p.text}`);
      parts.push(`font:${p.fontId}`);
      parts.push(`size:${p.fontSize}`);
      parts.push(`color:${p.color.r},${p.color.g},${p.color.b},${p.color.a}`);
      break;
    }
    case PrimitiveType.DrawRect: {
      const p = primitive as DrawRectPrimitive;
      parts.push(`color:${p.color.r},${p.color.g},${p.color.b},${p.color.a}`);
      parts.push(`w:${p.width}`);
      parts.push(`h:${p.height}`);
      break;
    }
    case PrimitiveType.DrawImage: {
      const p = primitive as DrawImagePrimitive;
      parts.push(`src:${p.src}`);
      parts.push(`x:${p.x}`);  // Include x: same image at different columns = different identities
      parts.push(`w:${p.width}`);
      parts.push(`h:${p.height}`);
      break;
    }
    case PrimitiveType.DrawBorder: {
      const p = primitive as DrawBorderPrimitive;
      parts.push(`color:${p.color.r},${p.color.g},${p.color.b},${p.color.a}`);
      parts.push(`w:${p.width}`);
      parts.push(`h:${p.height}`);
      parts.push(`t:${p.thickness}`);
      break;
    }
  }

  // Y position NOT included — when elements scroll, same content at a new y
  // shows up as "changed" (position delta only), which is correct and cheap.

  // Hash to fixed length
  const hash = crypto.createHash('sha256')
    .update(parts.join('|'))
    .digest('hex');

  // Use first 16 chars (collision probability: 1 in 2^64, negligible)
  return hash.substring(0, 16);
}

/**
 * Add identity to primitives
 * Handles collision case: if two primitives have same identity, add index suffix
 */
export function addIdentities(primitives: Primitive[]): PrimitiveWithIdentity[] {
  const identityCounts = new Map<string, number>();
  const result: PrimitiveWithIdentity[] = [];

  for (const primitive of primitives) {
    let baseIdentity = generatePrimitiveIdentity(primitive);

    // Check for collision
    const count = identityCounts.get(baseIdentity) || 0;
    const identity = count > 0 ? `${baseIdentity}_${count}` : baseIdentity;

    identityCounts.set(baseIdentity, count + 1);

    result.push({
      ...primitive,
      identity,
    });
  }

  return result;
}

/**
 * Check if a primitive has changed (same identity, different content/position)
 */
export function primitiveChanged(old: PrimitiveWithIdentity, current: PrimitiveWithIdentity): boolean {
  // Position change
  if (old.x !== current.x || old.y !== current.y) {
    return true;
  }

  // Type-specific changes
  switch (current.type) {
    case PrimitiveType.DrawText: {
      const oldP = old as DrawTextPrimitive;
      const curP = current as DrawTextPrimitive;
      return oldP.text !== curP.text ||
             oldP.fontId !== curP.fontId ||
             oldP.fontSize !== curP.fontSize ||
             !colorsEqual(oldP.color, curP.color);
    }

    case PrimitiveType.DrawRect: {
      const oldP = old as DrawRectPrimitive;
      const curP = current as DrawRectPrimitive;
      return oldP.width !== curP.width ||
             oldP.height !== curP.height ||
             !colorsEqual(oldP.color, curP.color);
    }

    case PrimitiveType.DrawImage: {
      const oldP = old as DrawImagePrimitive;
      const curP = current as DrawImagePrimitive;
      const oldBytesLen = oldP.pictBytes ? oldP.pictBytes.length : 0;
      const curBytesLen = curP.pictBytes ? curP.pictBytes.length : 0;
      return oldP.src !== curP.src ||
             oldP.width !== curP.width ||
             oldP.height !== curP.height ||
             oldBytesLen !== curBytesLen ||
             (!!oldP.pictBytes && !!curP.pictBytes && !oldP.pictBytes.equals(curP.pictBytes));
    }

    case PrimitiveType.DrawBorder: {
      const oldP = old as DrawBorderPrimitive;
      const curP = current as DrawBorderPrimitive;
      return oldP.width !== curP.width ||
             oldP.height !== curP.height ||
             oldP.thickness !== curP.thickness ||
             !colorsEqual(oldP.color, curP.color);
    }
  }

  return false;
}

function colorsEqual(a: any, b: any): boolean {
  return a.r === b.r && a.g === b.g && a.b === b.b && a.a === b.a;
}

/**
 * Diff two frames to find changes
 *
 * Returns:
 * - added: Primitives in current but not in previous
 * - removed: Primitives in previous but not in current (for reporting)
 * - changed: Primitives with same identity but different content
 * - unchanged: Primitives that are identical
 * - removalInstructions: Actual RemovePrimitive wire instructions to send to client
 */
export function diffPrimitives(
  previous: PrimitiveWithIdentity[],
  current: PrimitiveWithIdentity[]
): FrameDiff {
  const prevMap = new Map(previous.map(p => [p.identity, p]));
  const currentMap = new Map(current.map(p => [p.identity, p]));

  const added: PrimitiveWithIdentity[] = [];
  const removed: PrimitiveWithIdentity[] = [];
  const changed: PrimitiveWithIdentity[] = [];
  const unchanged: PrimitiveWithIdentity[] = [];
  const removalInstructions: Primitive[] = [];

  // Find added and changed
  for (const curr of current) {
    const prev = prevMap.get(curr.identity);

    if (!prev) {
      added.push(curr);
    } else if (primitiveChanged(prev, curr)) {
      changed.push(curr);
    } else {
      unchanged.push(curr);
    }
  }

  // Find removed and create RemovePrimitive instructions
  for (const prev of previous) {
    if (!currentMap.has(prev.identity)) {
      removed.push(prev);

      // Create RemovePrimitive wire instruction
      removalInstructions.push({
        type: PrimitiveType.RemovePrimitive,
        identity: prev.identity,
        x: 0,  // Dummy values (not used for removal)
        y: 0,
      });
    }
  }

  return { added, removed, changed, unchanged, removalInstructions };
}

/**
 * Detect if a frame diff is primarily a scroll (cheap to encode)
 *
 * Scroll detection:
 * - Most primitives have same identity
 * - Changed primitives mostly have only y-coordinate changes
 * - Y-coordinate changes are uniform (same delta)
 *
 * Returns scroll delta if detected, null otherwise
 */
export function detectScroll(diff: FrameDiff): ScrollDelta | null {
  const totalPrimitives = diff.added.length + diff.removed.length +
                          diff.changed.length + diff.unchanged.length;

  // Most primitives should be unchanged or just moved
  const movedOrUnchanged = diff.changed.length + diff.unchanged.length;
  if (movedOrUnchanged < totalPrimitives * 0.8) {
    return null; // Too many added/removed, not a simple scroll
  }

  // Check if changes are uniform y-deltas
  const yDeltas = new Map<number, number>();

  for (const changed of diff.changed) {
    // Find the old version to compare
    // Note: we'd need to pass previous frame to calculate delta
    // For now, this is a simplified check
    // In real implementation, we'd track old y-positions
  }

  // Simplified: if we have mostly unchanged + some position changes, call it a scroll
  if (diff.changed.length > 0 && diff.added.length === 0 && diff.removed.length === 0) {
    return {
      isScroll: true,
      deltaY: 0, // Would calculate from actual y changes
      affectedPrimitives: diff.changed.length
    };
  }

  return null;
}

/**
 * Calculate y-delta for a primitive (for scroll detection)
 */
export function calculateYDelta(old: PrimitiveWithIdentity, current: PrimitiveWithIdentity): number {
  return current.y - old.y;
}

/**
 * Detect uniform scroll: check if most changed primitives moved by same y-delta
 *
 * Tolerates sticky headers and other fixed elements by checking if the MAJORITY
 * of elements have the same delta (80%+ threshold).
 */
export function detectUniformScroll(
  previous: PrimitiveWithIdentity[],
  current: PrimitiveWithIdentity[]
): { isUniformScroll: boolean; deltaY: number; affectedCount: number } {
  const prevMap = new Map(previous.map(p => [p.identity, p]));

  const yDeltas: number[] = [];

  for (const curr of current) {
    const prev = prevMap.get(curr.identity);
    if (prev && prev.x === curr.x && prev.y !== curr.y) {
      // Same primitive, same x, different y = vertical move
      yDeltas.push(curr.y - prev.y);
    }
  }

  if (yDeltas.length === 0) {
    return { isUniformScroll: false, deltaY: 0, affectedCount: 0 };
  }

  // Find most common delta
  const deltaCounts = new Map<number, number>();
  for (const delta of yDeltas) {
    deltaCounts.set(delta, (deltaCounts.get(delta) || 0) + 1);
  }

  // Get delta with highest count
  let maxCount = 0;
  let mostCommonDelta = 0;
  for (const [delta, count] of deltaCounts) {
    if (count > maxCount) {
      maxCount = count;
      mostCommonDelta = delta;
    }
  }

  // If 80%+ of elements have the same delta, it's a uniform scroll
  const uniformPercentage = maxCount / yDeltas.length;
  if (uniformPercentage >= 0.8) {
    return {
      isUniformScroll: true,
      deltaY: mostCommonDelta,
      affectedCount: maxCount
    };
  }

  return { isUniformScroll: false, deltaY: 0, affectedCount: 0 };
}
