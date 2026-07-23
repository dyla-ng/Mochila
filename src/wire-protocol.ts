/**
 * Wire Protocol for mochila-v2
 *
 * Based on v1's lockstep model (handoff spec Section 2):
 * - Server sends FrameUpdate
 * - Server blocks waiting for FrameAck
 * - 5-second timeout, disconnect on timeout
 * - Client sends ack after processing
 */

import {
  Primitive,
  PrimitiveType,
  DrawRectPrimitive,
  DrawTextPrimitive,
  DrawBorderPrimitive,
  DrawImagePrimitive,
  DrawMaskedImagePrimitive,
  RemovePrimitive,
  Color,
} from './types';

/**
 * Wire format for a single frame update
 *
 * Contains primitives from Step 4's diff:
 * - Added primitives (full payload)
 * - Changed primitives (full payload)
 * - RemovePrimitive instructions (identity only)
 */
export interface ScrollMetadata {
  scrollY: number;
  scrollX: number;
  viewportWidth: number;
  viewportHeight: number;
  documentWidth: number;
  documentHeight: number;
  stickyElements: StickyElement[];
}

export interface StickyElement {
  position: string;  // 'fixed' or 'sticky'
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface FrameUpdate {
  messageType: 'FrameUpdate';
  frameId: number;
  primitiveCount: number;
  primitives: WirePrimitive[];
  scrollMetadata?: ScrollMetadata;
  currentUrl?: string;
  lastProcessedScrollSeq?: number;  // Sequence number of last processed client scroll
}

/**
 * Separate message for image data (async, no ack required)
 *
 * Images are sent separately from layout to avoid bloating FrameUpdates.
 * Client receives these asynchronously and populates cached images by ID.
 */
export interface ImageData {
  messageType: 'ImageData';
  imageId: string;      // SHA256 hash of source URL
  pictBytes: Buffer;    // PICT encoded image data
}

/**
 * Wire representation of primitives
 *
 * Each primitive has a type-specific payload:
 * - DrawText: text, fontId, fontSize, color, x, y
 * - DrawRect: x, y, width, height, color
 * - DrawBorder: x, y, width, height, thickness, color
 * - DrawImage: x, y, width, height, pictBytes (base64)
 * - RemovePrimitive: identity only (NO dummy x/y - cleaned up from Step 4)
 */
export type WirePrimitive =
  | WireDrawText
  | WireDrawRect
  | WireDrawBorder
  | WireDrawImage
  | WireDrawMaskedImage
  | WireRemovePrimitive;

export interface WireDrawText {
  type: 'DrawText';
  identity: string;
  x: number;
  y: number;
  width: number;
  height: number;
  text: string;
  fontId: number;
  fontSize: number;
  color: Color;
  hoverColor?: Color;
  hoverUnderline?: boolean;
  maxWidth?: number;  // Container width for client-side wrapping
  isBold?: boolean;
  isItalic?: boolean;
  isUnderline?: boolean;
  zIndex: number;
  treeOrder: number;
}

export interface WireDrawRect {
  type: 'DrawRect';
  identity: string;
  x: number;
  y: number;
  width: number;
  height: number;
  color: Color;
  hoverColor?: Color;
  borderRadius?: number;
  zIndex: number;
  treeOrder: number;
}

export interface WireDrawBorder {
  type: 'DrawBorder';
  identity: string;
  x: number;
  y: number;
  width: number;
  height: number;
  thickness: number;
  color: Color;
  borderRadius?: number;
  zIndex: number;
  treeOrder: number;
}

export interface WireDrawImage {
  type: 'DrawImage';
  identity: string;
  imageId?: string;    // Hash of source URL - if present, client waits for ImageData message
  x: number;
  y: number;
  width: number;
  height: number;
  pictBytes?: Buffer;  // Raw binary PICT data (optional for backward compat, deprecated)
  zIndex: number;
  treeOrder: number;
}

export interface WireDrawMaskedImage {
  type: 'DrawMaskedImage';
  identity: string;
  x: number;
  y: number;
  width: number;
  height: number;
  fillColor: Color;    // Color to fill through the mask
  maskData?: Buffer;   // 1-bit monochrome mask (packed, 8 pixels per byte)
  zIndex: number;
  treeOrder: number;
}

/**
 * RemovePrimitive wire instruction
 *
 * CLEAN: No dummy x/y values (removed from Step 4 prototype)
 * Only includes what's needed: type and identity
 */
export interface WireRemovePrimitive {
  type: 'RemovePrimitive';
  identity: string;  // Identity of primitive to remove from client state
}

/**
 * Client acknowledgment message
 *
 * Sent after client finishes processing a frame.
 * Server blocks waiting for this before computing next frame.
 */
export interface FrameAck {
  messageType: 'FrameAck';
  frameId: number;  // Acknowledging this frame
}

/**
 * Message types that can be sent over the wire
 */
export type WireMessage = FrameUpdate | FrameAck;

/**
 * Serialize a FrameUpdate to wire format (JSON for now, binary later)
 *
 * Returns both the structured message and the actual bytes that would be sent.
 */
function calculateFrameUpdateSize(update: FrameUpdate): number {
  let size = 1 + 4 + 2; // messageType + frameId + primitiveCount

  for (const prim of update.primitives) {
    size += 1 + 2; // primitiveType + identityLen
    const identity = prim.type === 'RemovePrimitive' ? prim.identity : (prim as any).identity;
    const identityBytes = Buffer.byteLength(identity, 'utf-8');
    size += identityBytes;

    if (prim.type === 'DrawRect') {
      size += 4 + 4 + 2 + 2 + 4; // x, y, width, height, rgba
      size += 1; // rectFlags (hasHoverColor)
      if (prim.hoverColor) size += 4;
      size += 1; // borderRadius
      size += 2 + 2; // zIndex, treeOrder
    } else if (prim.type === 'DrawText') {
      size += 4 + 4 + 2 + 2 + 2 + 2 + 4 + 2; // x, y, width, height, fontId, fontSize, rgba, textLen
      size += Buffer.byteLength(prim.text, 'utf-8');
      size += 2; // maxWidth
      size += 1; // fontFlags byte (italic | underline | bold | hasHoverColor | hoverUnderline)
      if (prim.hoverColor) size += 4;
      size += 2 + 2; // zIndex, treeOrder
    } else if (prim.type === 'DrawBorder') {
      size += 4 + 4 + 2 + 2 + 2 + 4; // x, y, width, height, thickness, rgba
      size += 1; // borderRadius
      size += 2 + 2; // zIndex, treeOrder
    } else if (prim.type === 'DrawImage') {
      size += 4 + 4 + 2 + 2; // x, y, width, height
      size += 2; // imageId length field
      if (prim.imageId) {
        size += Buffer.from(prim.imageId, 'utf8').length;
      }
      size += 4; // pictBytes length (always 0 now)
      size += 2 + 2; // zIndex, treeOrder
    } else if (prim.type === 'DrawMaskedImage') {
      size += 4 + 4 + 2 + 2 + 4 + 4; // x, y, width, height, rgba, maskDataLen
      if (prim.maskData) {
        size += prim.maskData.length;
      }
      size += 2 + 2; // zIndex, treeOrder
    } else if (prim.type === 'RemovePrimitive') {
      // no extra payload
    }
  }

  // Scroll metadata (optional)
  size += 1; // hasScrollMetadata flag
  if (update.scrollMetadata) {
    size += 4 + 4 + 2 + 2 + 2 + 2; // scrollY, scrollX, viewportWidth, viewportHeight, documentWidth, documentHeight
    size += 2; // stickyElementCount
    for (const sticky of update.scrollMetadata.stickyElements) {
      size += 2; // positionLen
      size += Buffer.byteLength(sticky.position, 'utf-8');
      size += 4 + 4 + 2 + 2; // x, y, width, height
    }
  }

  // Current URL (optional)
  size += 1; // hasCurrentUrl flag
  if (update.currentUrl) {
    size += 2; // urlLen
    size += Buffer.byteLength(update.currentUrl, 'utf-8');
  }

  // Last processed scroll sequence (optional)
  size += 1; // hasScrollSeq flag
  if (update.lastProcessedScrollSeq !== undefined) {
    size += 4; // uint32 sequence number
  }

  return size;
}

/**
 * Serialize a FrameUpdate to structured binary format.
 */
export function serializeFrameUpdate(update: FrameUpdate): {
  structured: FrameUpdate;
  bytes: Buffer;
  byteLength: number;
} {
  const size = calculateFrameUpdateSize(update);
  const bytes = Buffer.alloc(size);
  let offset = 0;

  // Header
  bytes.writeUInt8(1, offset); offset += 1;
  bytes.writeUInt32LE(update.frameId, offset); offset += 4;
  bytes.writeUInt16LE(update.primitiveCount, offset); offset += 2;

  for (const prim of update.primitives) {
    let typeVal = 1;
    if (prim.type === 'DrawRect') typeVal = 1;
    else if (prim.type === 'DrawText') typeVal = 2;
    else if (prim.type === 'DrawBorder') typeVal = 3;
    else if (prim.type === 'DrawImage') typeVal = 4;
    else if (prim.type === 'RemovePrimitive') typeVal = 5;
    else if (prim.type === 'DrawMaskedImage') typeVal = 7;

    bytes.writeUInt8(typeVal, offset); offset += 1;

    const identity = prim.type === 'RemovePrimitive' ? prim.identity : (prim as any).identity;
    const identityBytes = Buffer.byteLength(identity, 'utf-8');
    bytes.writeUInt16LE(identityBytes, offset); offset += 2;
    if (identityBytes > 0) {
      bytes.write(identity, offset, identityBytes, 'utf-8'); offset += identityBytes;
    }

    if (prim.type === 'DrawRect') {
      bytes.writeInt32LE(prim.x, offset); offset += 4;
      bytes.writeInt32LE(prim.y, offset); offset += 4;
      bytes.writeUInt16LE(prim.width, offset); offset += 2;
      bytes.writeUInt16LE(prim.height, offset); offset += 2;
      bytes.writeUInt8(prim.color.r, offset); offset += 1;
      bytes.writeUInt8(prim.color.g, offset); offset += 1;
      bytes.writeUInt8(prim.color.b, offset); offset += 1;
      bytes.writeUInt8(prim.color.a, offset); offset += 1;

      const rectFlags = prim.hoverColor ? 0x01 : 0x00;
      bytes.writeUInt8(rectFlags, offset); offset += 1;
      if (prim.hoverColor) {
        bytes.writeUInt8(prim.hoverColor.r, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.g, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.b, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.a, offset); offset += 1;
      }

      // Write borderRadius for rounded corners (clamped to UInt8 range)
      const borderRadiusValue = Math.min(255, Math.max(0, prim.borderRadius || 0));
      bytes.writeUInt8(borderRadiusValue, offset); offset += 1;

      bytes.writeInt16LE(prim.zIndex, offset); offset += 2;
      bytes.writeUInt16LE(prim.treeOrder, offset); offset += 2;
    } else if (prim.type === 'DrawText') {
      bytes.writeInt32LE(prim.x, offset); offset += 4;
      bytes.writeInt32LE(prim.y, offset); offset += 4;
      bytes.writeUInt16LE(prim.width, offset); offset += 2;
      bytes.writeUInt16LE(prim.height, offset); offset += 2;
      bytes.writeUInt16LE(prim.fontId, offset); offset += 2;
      bytes.writeUInt16LE(prim.fontSize, offset); offset += 2;
      bytes.writeUInt8(prim.color.r, offset); offset += 1;
      bytes.writeUInt8(prim.color.g, offset); offset += 1;
      bytes.writeUInt8(prim.color.b, offset); offset += 1;
      bytes.writeUInt8(prim.color.a, offset); offset += 1;

      const textBytes = Buffer.byteLength(prim.text, 'utf-8');
      bytes.writeUInt16LE(textBytes, offset); offset += 2;
      bytes.write(prim.text, offset, textBytes, 'utf-8'); offset += textBytes;

      bytes.writeUInt16LE(prim.maxWidth || 0, offset); offset += 2;
      // Font style flags byte: bit 0 = italic, bit 1 = underline, bit 2 = bold, bit 3 = hoverColor, bit 4 = hoverUnderline
      const fontFlags = (prim.isItalic ? 0x01 : 0x00) |
                        (prim.isUnderline ? 0x02 : 0x00) |
                        (prim.isBold ? 0x04 : 0x00) |
                        (prim.hoverColor ? 0x08 : 0x00) |
                        (prim.hoverUnderline ? 0x10 : 0x00);
      bytes.writeUInt8(fontFlags, offset); offset += 1;
      if (prim.hoverColor) {
        bytes.writeUInt8(prim.hoverColor.r, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.g, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.b, offset); offset += 1;
        bytes.writeUInt8(prim.hoverColor.a, offset); offset += 1;
      }
      bytes.writeInt16LE(prim.zIndex, offset); offset += 2;
      bytes.writeUInt16LE(prim.treeOrder, offset); offset += 2;
    } else if (prim.type === 'DrawBorder') {
      bytes.writeInt32LE(prim.x, offset); offset += 4;
      bytes.writeInt32LE(prim.y, offset); offset += 4;
      bytes.writeUInt16LE(prim.width, offset); offset += 2;
      bytes.writeUInt16LE(prim.height, offset); offset += 2;
      bytes.writeUInt16LE(prim.thickness, offset); offset += 2;
      bytes.writeUInt8(prim.color.r, offset); offset += 1;
      bytes.writeUInt8(prim.color.g, offset); offset += 1;
      bytes.writeUInt8(prim.color.b, offset); offset += 1;
      bytes.writeUInt8(prim.color.a, offset); offset += 1;

      // Write borderRadius for rounded corners (clamped to UInt8 range)
      const borderRadiusValue = Math.min(255, Math.max(0, prim.borderRadius || 0));
      bytes.writeUInt8(borderRadiusValue, offset); offset += 1;

      bytes.writeInt16LE(prim.zIndex, offset); offset += 2;
      bytes.writeUInt16LE(prim.treeOrder, offset); offset += 2;
    } else if (prim.type === 'DrawImage') {
      bytes.writeInt32LE(prim.x, offset); offset += 4;
      bytes.writeInt32LE(prim.y, offset); offset += 4;
      bytes.writeUInt16LE(prim.width, offset); offset += 2;
      bytes.writeUInt16LE(prim.height, offset); offset += 2;

      // NEW: Write imageId for async image loading
      const imageIdStr = prim.imageId || '';
      const imageIdBuf = Buffer.from(imageIdStr, 'utf8');
      bytes.writeUInt16LE(imageIdBuf.length, offset); offset += 2;
      if (imageIdBuf.length > 0) {
        imageIdBuf.copy(bytes, offset);
        offset += imageIdBuf.length;
      }

      // pictBytes length - always 0 now (sent via ImageData message)
      bytes.writeUInt32LE(0, offset); offset += 4;

      bytes.writeInt16LE(prim.zIndex, offset); offset += 2;
      bytes.writeUInt16LE(prim.treeOrder, offset); offset += 2;
    } else if (prim.type === 'DrawMaskedImage') {
      bytes.writeInt32LE(prim.x, offset); offset += 4;
      bytes.writeInt32LE(prim.y, offset); offset += 4;
      bytes.writeUInt16LE(prim.width, offset); offset += 2;
      bytes.writeUInt16LE(prim.height, offset); offset += 2;

      // Fill color (RGBA)
      bytes.writeUInt8(prim.fillColor.r, offset); offset += 1;
      bytes.writeUInt8(prim.fillColor.g, offset); offset += 1;
      bytes.writeUInt8(prim.fillColor.b, offset); offset += 1;
      bytes.writeUInt8(prim.fillColor.a, offset); offset += 1;

      // 1-bit mask data (length + bytes)
      const maskLen = prim.maskData ? prim.maskData.length : 0;
      bytes.writeUInt32LE(maskLen, offset); offset += 4;
      if (prim.maskData && maskLen > 0) {
        prim.maskData.copy(bytes, offset);
        offset += maskLen;
      }

      bytes.writeInt16LE(prim.zIndex, offset); offset += 2;
      bytes.writeUInt16LE(prim.treeOrder, offset); offset += 2;
    } else if (prim.type === 'RemovePrimitive') {
      // no extra payload
    }
  }

  // Scroll metadata (optional)
  bytes.writeUInt8(update.scrollMetadata ? 1 : 0, offset); offset += 1;
  if (update.scrollMetadata) {
    bytes.writeInt32LE(update.scrollMetadata.scrollY, offset); offset += 4;
    bytes.writeInt32LE(update.scrollMetadata.scrollX, offset); offset += 4;
    bytes.writeUInt16LE(update.scrollMetadata.viewportWidth, offset); offset += 2;
    bytes.writeUInt16LE(update.scrollMetadata.viewportHeight, offset); offset += 2;
    bytes.writeUInt16LE(update.scrollMetadata.documentWidth, offset); offset += 2;
    bytes.writeUInt16LE(update.scrollMetadata.documentHeight, offset); offset += 2;
    bytes.writeUInt16LE(update.scrollMetadata.stickyElements.length, offset); offset += 2;

    for (const sticky of update.scrollMetadata.stickyElements) {
      const posBytes = Buffer.byteLength(sticky.position, 'utf-8');
      bytes.writeUInt16LE(posBytes, offset); offset += 2;
      bytes.write(sticky.position, offset, posBytes, 'utf-8'); offset += posBytes;
      bytes.writeInt32LE(sticky.x, offset); offset += 4;
      bytes.writeInt32LE(sticky.y, offset); offset += 4;
      bytes.writeUInt16LE(sticky.width, offset); offset += 2;
      bytes.writeUInt16LE(sticky.height, offset); offset += 2;
    }
  }

  // Current URL (optional)
  bytes.writeUInt8(update.currentUrl ? 1 : 0, offset); offset += 1;
  if (update.currentUrl) {
    const urlBytes = Buffer.byteLength(update.currentUrl, 'utf-8');
    bytes.writeUInt16LE(urlBytes, offset); offset += 2;
    bytes.write(update.currentUrl, offset, urlBytes, 'utf-8'); offset += urlBytes;
  }

  // Last processed scroll sequence (optional)
  bytes.writeUInt8(update.lastProcessedScrollSeq !== undefined ? 1 : 0, offset); offset += 1;
  if (update.lastProcessedScrollSeq !== undefined) {
    bytes.writeUInt32LE(update.lastProcessedScrollSeq, offset); offset += 4;
  }

  return {
    structured: update,
    bytes,
    byteLength: size,
  };
}

/**
 * Deserialize a FrameUpdate from structured binary wire bytes.
 */
export function deserializeFrameUpdate(bytes: Buffer): FrameUpdate {
  let offset = 0;

  const messageType = bytes.readUInt8(offset); offset += 1;
  if (messageType !== 1) {
    throw new Error(`Expected FrameUpdate message type 1, got ${messageType}`);
  }

  const frameId = bytes.readUInt32LE(offset); offset += 4;
  const primitiveCount = bytes.readUInt16LE(offset); offset += 2;

  const primitives: WirePrimitive[] = [];

  for (let i = 0; i < primitiveCount; i++) {
    const typeVal = bytes.readUInt8(offset); offset += 1;

    const identityLen = bytes.readUInt16LE(offset); offset += 2;
    const identity = bytes.toString('utf-8', offset, offset + identityLen); offset += identityLen;

    if (typeVal === 1) { // DrawRect
      const x = bytes.readInt16LE(offset); offset += 2;
      const y = bytes.readInt16LE(offset); offset += 2;
      const width = bytes.readUInt16LE(offset); offset += 2;
      const height = bytes.readUInt16LE(offset); offset += 2;
      const r = bytes.readUInt8(offset); offset += 1;
      const g = bytes.readUInt8(offset); offset += 1;
      const b = bytes.readUInt8(offset); offset += 1;
      const a = bytes.readUInt8(offset); offset += 1;

      const rectFlags = bytes.readUInt8(offset); offset += 1;
      let hoverColor: Color | undefined = undefined;
      if ((rectFlags & 0x01) !== 0) {
        const hr = bytes.readUInt8(offset); offset += 1;
        const hg = bytes.readUInt8(offset); offset += 1;
        const hb = bytes.readUInt8(offset); offset += 1;
        const ha = bytes.readUInt8(offset); offset += 1;
        hoverColor = { r: hr, g: hg, b: hb, a: ha };
      }

      const zIndex = bytes.readInt16LE(offset); offset += 2;
      const treeOrder = bytes.readUInt16LE(offset); offset += 2;

      primitives.push({
        type: 'DrawRect',
        identity,
        x,
        y,
        width,
        height,
        color: { r, g, b, a },
        hoverColor,
        zIndex,
        treeOrder,
      });
    } else if (typeVal === 2) { // DrawText
      const x = bytes.readInt32LE(offset); offset += 4;  // Fixed: was reading 2 bytes, should be 4
      const y = bytes.readInt32LE(offset); offset += 4;  // Fixed: was reading 2 bytes, should be 4
      const width = bytes.readUInt16LE(offset); offset += 2;
      const height = bytes.readUInt16LE(offset); offset += 2;
      const fontId = bytes.readUInt16LE(offset); offset += 2;
      const fontSize = bytes.readUInt16LE(offset); offset += 2;
      const r = bytes.readUInt8(offset); offset += 1;
      const g = bytes.readUInt8(offset); offset += 1;
      const b = bytes.readUInt8(offset); offset += 1;
      const a = bytes.readUInt8(offset); offset += 1;

      const textLen = bytes.readUInt16LE(offset); offset += 2;
      const text = bytes.toString('utf-8', offset, offset + textLen); offset += textLen;

      const maxWidth = bytes.readUInt16LE(offset); offset += 2;
      const fontFlags = bytes.readUInt8(offset); offset += 1;
      const isItalic = (fontFlags & 0x01) !== 0;
      const isUnderline = (fontFlags & 0x02) !== 0;
      const isBold = (fontFlags & 0x04) !== 0;
      const hasHoverColor = (fontFlags & 0x08) !== 0;
      const hoverUnderline = (fontFlags & 0x10) !== 0;

      let hoverColor: Color | undefined = undefined;
      if (hasHoverColor) {
        const hr = bytes.readUInt8(offset); offset += 1;
        const hg = bytes.readUInt8(offset); offset += 1;
        const hb = bytes.readUInt8(offset); offset += 1;
        const ha = bytes.readUInt8(offset); offset += 1;
        hoverColor = { r: hr, g: hg, b: hb, a: ha };
      }

      const zIndex = bytes.readInt16LE(offset); offset += 2;
      const treeOrder = bytes.readUInt16LE(offset); offset += 2;

      primitives.push({
        type: 'DrawText',
        identity,
        x,
        y,
        width,
        height,
        text,
        fontId,
        fontSize,
        color: { r, g, b, a },
        hoverColor,
        hoverUnderline: hoverUnderline || undefined,
        maxWidth: maxWidth || undefined,
        isBold: isBold || undefined,
        isItalic: isItalic || undefined,
        isUnderline: isUnderline || undefined,
        zIndex,
        treeOrder,
      });
    } else if (typeVal === 3) { // DrawBorder
      const x = bytes.readInt16LE(offset); offset += 2;
      const y = bytes.readInt16LE(offset); offset += 2;
      const width = bytes.readUInt16LE(offset); offset += 2;
      const height = bytes.readUInt16LE(offset); offset += 2;
      const thickness = bytes.readUInt16LE(offset); offset += 2;
      const r = bytes.readUInt8(offset); offset += 1;
      const g = bytes.readUInt8(offset); offset += 1;
      const b = bytes.readUInt8(offset); offset += 1;
      const a = bytes.readUInt8(offset); offset += 1;
      const zIndex = bytes.readInt16LE(offset); offset += 2;
      const treeOrder = bytes.readUInt16LE(offset); offset += 2;

      primitives.push({
        type: 'DrawBorder',
        identity,
        x,
        y,
        width,
        height,
        thickness,
        color: { r, g, b, a },
        zIndex,
        treeOrder,
      });
    } else if (typeVal === 4) { // DrawImage
      const x = bytes.readInt16LE(offset); offset += 2;
      const y = bytes.readInt16LE(offset); offset += 2;
      const width = bytes.readUInt16LE(offset); offset += 2;
      const height = bytes.readUInt16LE(offset); offset += 2;

      const imageBytesLen = bytes.readUInt32LE(offset); offset += 4;
      let pictBytes: Buffer | undefined = undefined;
      if (imageBytesLen > 0) {
        pictBytes = bytes.subarray(offset, offset + imageBytesLen);
        offset += imageBytesLen;
      }

      const zIndex = bytes.readInt16LE(offset); offset += 2;
      const treeOrder = bytes.readUInt16LE(offset); offset += 2;

      primitives.push({
        type: 'DrawImage',
        identity,
        x,
        y,
        width,
        height,
        pictBytes,
        zIndex,
        treeOrder,
      });
    } else if (typeVal === 5) { // RemovePrimitive
      primitives.push({
        type: 'RemovePrimitive',
        identity,
      });
    }
  }

  // Scroll metadata (optional)
  let scrollMetadata: ScrollMetadata | undefined = undefined;
  if (offset < bytes.length) {
    const hasScrollMetadata = bytes.readUInt8(offset); offset += 1;
    if (hasScrollMetadata) {
      const scrollY = bytes.readInt32LE(offset); offset += 4;
      const scrollX = bytes.readInt32LE(offset); offset += 4;
      const viewportWidth = bytes.readUInt16LE(offset); offset += 2;
      const viewportHeight = bytes.readUInt16LE(offset); offset += 2;
      const documentWidth = bytes.readUInt16LE(offset); offset += 2;
      const documentHeight = bytes.readUInt16LE(offset); offset += 2;
      const stickyElementCount = bytes.readUInt16LE(offset); offset += 2;

      const stickyElements: StickyElement[] = [];
      for (let i = 0; i < stickyElementCount; i++) {
        const posLen = bytes.readUInt16LE(offset); offset += 2;
        const position = bytes.toString('utf-8', offset, offset + posLen); offset += posLen;
        const x = bytes.readInt16LE(offset); offset += 2;
        const y = bytes.readInt16LE(offset); offset += 2;
        const width = bytes.readUInt16LE(offset); offset += 2;
        const height = bytes.readUInt16LE(offset); offset += 2;
        stickyElements.push({ position, x, y, width, height });
      }

      scrollMetadata = {
        scrollY,
        scrollX,
        viewportWidth,
        viewportHeight,
        documentWidth,
        documentHeight,
        stickyElements,
      };
    }
  }

  // Current URL (optional)
  let currentUrl: string | undefined = undefined;
  if (offset < bytes.length) {
    const hasCurrentUrl = bytes.readUInt8(offset); offset += 1;
    if (hasCurrentUrl) {
      const urlLen = bytes.readUInt16LE(offset); offset += 2;
      currentUrl = bytes.toString('utf-8', offset, offset + urlLen); offset += urlLen;
    }
  }

  // Last processed scroll sequence (optional)
  let lastProcessedScrollSeq: number | undefined = undefined;
  if (offset < bytes.length) {
    const hasScrollSeq = bytes.readUInt8(offset); offset += 1;
    if (hasScrollSeq) {
      lastProcessedScrollSeq = bytes.readUInt32LE(offset); offset += 4;
    }
  }

  return {
    messageType: 'FrameUpdate',
    frameId,
    primitiveCount,
    primitives,
    scrollMetadata,
    currentUrl,
    lastProcessedScrollSeq,
  };
}

/**
 * Serialize a FrameAck to structured binary format.
 */
export function serializeFrameAck(ack: FrameAck): {
  structured: FrameAck;
  bytes: Buffer;
  byteLength: number;
} {
  const bytes = Buffer.alloc(1 + 4);
  bytes.writeUInt8(2, 0); // messageType = 2 (FrameAck)
  bytes.writeUInt32LE(ack.frameId, 1);

  return {
    structured: ack,
    bytes,
    byteLength: 5,
  };
}

/**
 * Deserialize a FrameAck from structured binary wire bytes.
 */
export function deserializeFrameAck(bytes: Buffer): FrameAck {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 2) {
    throw new Error(`Expected FrameAck message type 2, got ${messageType}`);
  }
  const frameId = bytes.readUInt32LE(1);
  return {
    messageType: 'FrameAck',
    frameId,
  };
}

/**
 * Serialize ImageData message (MessageType 12)
 *
 * Format:
 * - 1 byte: messageType (12)
 * - 2 bytes: imageId length (uint16 LE)
 * - N bytes: imageId (UTF-8)
 * - 4 bytes: pictBytes length (uint32 LE)
 * - M bytes: pictBytes
 */
export function serializeImageData(imageData: ImageData): {
  structured: ImageData;
  bytes: Buffer;
  byteLength: number;
} {
  const imageIdBuf = Buffer.from(imageData.imageId, 'utf8');
  const imageIdLen = imageIdBuf.length;
  const pictLen = imageData.pictBytes.length;

  const totalSize = 1 + 2 + imageIdLen + 4 + pictLen;
  const bytes = Buffer.alloc(totalSize);

  let offset = 0;
  bytes.writeUInt8(12, offset); offset += 1;  // messageType = 12
  bytes.writeUInt16LE(imageIdLen, offset); offset += 2;
  imageIdBuf.copy(bytes, offset); offset += imageIdLen;
  bytes.writeUInt32LE(pictLen, offset); offset += 4;
  imageData.pictBytes.copy(bytes, offset);

  return {
    structured: imageData,
    bytes,
    byteLength: totalSize,
  };
}

export interface ClientClick {
  messageType: 'Click';
  x: number;
  y: number;
}

/**
 * Deserialize a Click event from structured binary wire bytes (MessageType 6).
 */
export function deserializeClick(bytes: Buffer): ClientClick {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 6) {
    throw new Error(`Expected Click message type 6, got ${messageType}`);
  }
  const x = bytes.readInt32LE(1);
  const y = bytes.readInt32LE(5);
  return {
    messageType: 'Click',
    x,
    y,
  };
}

export interface ClientNavigateCommand {
  messageType: 'NavigateCommand';
  action: number; // 1 = Back, 2 = Forward, 3 = Reload
}

/**
 * Deserialize a NavigateCommand from structured binary wire bytes (MessageType 7).
 */
export function deserializeNavigateCommand(bytes: Buffer): ClientNavigateCommand {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 7) {
    throw new Error(`Expected NavigateCommand message type 7, got ${messageType}`);
  }
  const action = bytes.readUInt8(1);
  return {
    messageType: 'NavigateCommand',
    action,
  };
}

export interface ClientKeyInput {
  messageType: 'KeyInput';
  isText: boolean;
  text: string;
}

/**
 * Deserialize a KeyInput event from structured binary wire bytes (MessageType 8).
 */
export function deserializeKeyInput(bytes: Buffer): ClientKeyInput {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 8) {
    throw new Error(`Expected KeyInput message type 8, got ${messageType}`);
  }
  const isText = bytes.readUInt8(1) === 1;
  const len = bytes.readUInt16LE(2);
  const text = bytes.toString('utf-8', 4, 4 + len);
  return {
    messageType: 'KeyInput',
    isText,
    text,
  };
}

export interface ClientMouseMove {
  messageType: 'MouseMove';
  x: number;
  y: number;
}

/**
 * Deserialize a MouseMove event from structured binary wire bytes (MessageType 9).
 */
export function deserializeMouseMove(bytes: Buffer): ClientMouseMove {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 9) {
    throw new Error(`Expected MouseMove message type 9, got ${messageType}`);
  }
  const x = bytes.readInt32LE(1);
  const y = bytes.readInt32LE(5);
  return {
    messageType: 'MouseMove',
    x,
    y,
  };
}

export interface ClientMouseEnter {
  messageType: 'MouseEnter';
  x: number;
  y: number;
}

export function deserializeMouseEnter(bytes: Buffer): ClientMouseEnter {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 10) {
    throw new Error(`Expected MouseEnter message type 10, got ${messageType}`);
  }
  const x = bytes.readInt32LE(1);
  const y = bytes.readInt32LE(5);
  return {
    messageType: 'MouseEnter',
    x,
    y,
  };
}

export interface ClientMouseLeave {
  messageType: 'MouseLeave';
}

export function deserializeMouseLeave(bytes: Buffer): ClientMouseLeave {
  const messageType = bytes.readUInt8(0);
  if (messageType !== 11) {
    throw new Error(`Expected MouseLeave message type 11, got ${messageType}`);
  }
  return {
    messageType: 'MouseLeave',
  };
}

/**
 * Convert internal Primitive to WirePrimitive
 *
 * Uses Step 4's primitives with identity and converts to clean wire format.
 * RemovePrimitive gets cleaned up: no dummy x/y in wire format.
 */
export function primitiveToWire(prim: Primitive & { identity: string }): WirePrimitive {
  switch (prim.type) {
    case PrimitiveType.DrawText:
      return {
        type: 'DrawText',
        identity: prim.identity,
        x: prim.x,
        y: prim.y,
        width: prim.width,
        height: prim.height,
        text: prim.text,
        fontId: prim.fontId,
        fontSize: prim.fontSize,
        color: prim.color,
        hoverColor: prim.hoverColor,
        hoverUnderline: prim.hoverUnderline,
        maxWidth: prim.maxWidth,
        isBold: prim.isBold,
        isItalic: prim.isItalic,
        isUnderline: prim.isUnderline,
        zIndex: prim.zIndex,
        treeOrder: prim.treeOrder,
      };

    case PrimitiveType.DrawRect:
      return {
        type: 'DrawRect',
        identity: prim.identity,
        x: prim.x,
        y: prim.y,
        width: prim.width,
        height: prim.height,
        color: prim.color,
        hoverColor: prim.hoverColor,
        borderRadius: prim.borderRadius,
        zIndex: prim.zIndex,
        treeOrder: prim.treeOrder,
      };

    case PrimitiveType.DrawBorder:
      return {
        type: 'DrawBorder',
        identity: prim.identity,
        x: prim.x,
        y: prim.y,
        width: prim.width,
        height: prim.height,
        thickness: prim.thickness,
        color: prim.color,
        borderRadius: prim.borderRadius,
        zIndex: prim.zIndex,
        treeOrder: prim.treeOrder,
      };

    case PrimitiveType.DrawImage: {
      // NEW: Use imageId instead of pictBytes for async image loading
      // imageId is a hash of the src URL - client will wait for ImageData message
      const imageId = (prim as any).src
        ? require('crypto').createHash('sha256').update((prim as any).src).digest('hex').substring(0, 16)
        : undefined;

      return {
        type: 'DrawImage',
        identity: prim.identity,
        imageId: imageId,  // NEW: Image identifier for async loading
        x: prim.x,
        y: prim.y,
        width: prim.width,
        height: prim.height,
        pictBytes: prim.pictBytes,  // Keep pictBytes here for serialization stage
        zIndex: prim.zIndex,
        treeOrder: prim.treeOrder,
      };
    }

    case PrimitiveType.DrawMaskedImage:
      return {
        type: 'DrawMaskedImage',
        identity: prim.identity,
        x: prim.x,
        y: prim.y,
        width: prim.width,
        height: prim.height,
        fillColor: (prim as any).fillColor,
        maskData: (prim as any).maskData,
        zIndex: prim.zIndex,
        treeOrder: prim.treeOrder,
      };

    case PrimitiveType.RemovePrimitive:
      // CLEAN: Only identity, no dummy x/y
      return {
        type: 'RemovePrimitive',
        identity: prim.identity,
      };

    default:
      throw new Error(`Unknown primitive type: ${(prim as any).type}`);
  }
}

/**
 * Build a FrameUpdate from Step 4's diff output
 *
 * Includes:
 * - Added primitives (full payload)
 * - Changed primitives (full payload)
 * - RemovePrimitive instructions (identity only)
 */
export function buildFrameUpdate(
  frameId: number,
  added: (Primitive & { identity: string })[],
  changed: (Primitive & { identity: string })[],
  removed: (Primitive & { identity: string })[],
  previousFrame?: (Primitive & { identity: string })[]
): FrameUpdate {
  const primitives: WirePrimitive[] = [];

  // Add all "added" primitives (full payload)
  for (const prim of added) {
    primitives.push(primitiveToWire(prim));
  }

  // Add all "changed" primitives (full payload with updated coordinates)
  for (const prim of changed) {
    primitives.push(primitiveToWire(prim));
  }

  // Add RemovePrimitive instructions for removed primitives
  for (const prim of removed) {
    primitives.push({
      type: 'RemovePrimitive',
      identity: prim.identity,
    });
  }

  return {
    messageType: 'FrameUpdate',
    frameId,
    primitiveCount: primitives.length,
    primitives,
  };
}
