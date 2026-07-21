// Primitive types for the mochila-v2 prototype

export enum PrimitiveType {
  DrawRect = 1,
  DrawText = 2,
  DrawBorder = 3,
  DrawImage = 4,
  RemovePrimitive = 5,  // Instruction to remove a primitive by identity
  DrawMaskedImage = 7,  // Monochrome mask + fill color (for icons)
}

export interface Position {
  x: number;
  y: number;
}

export interface Size {
  width: number;
  height: number;
}

export interface Color {
  r: number;
  g: number;
  b: number;
  a: number;
}

export interface DrawRectPrimitive {
  type: PrimitiveType.DrawRect;
  x: number;
  y: number;
  width: number;
  height: number;
  color: Color;
  hoverColor?: Color;    // Optional hover fill color for local C++ hover
  borderRadius?: number; // Border radius in pixels for rounded corners
  zIndex: number;      // CSS z-index (0 if not positioned)
  treeOrder: number;   // DOM traversal order for tiebreaking
}

export interface DrawTextPrimitive {
  type: PrimitiveType.DrawText;
  x: number;
  y: number;
  text: string;
  fontId: number;         // Mac OS 9 font ID from substitution table
  fontSize: number;
  color: Color;
  hoverColor?: Color;     // Optional hover text color for local C++ hover
  hoverUnderline?: boolean;// Optional hover underline flag for local C++ hover
  maxWidth?: number;      // Container width for client-side wrapping
  isBold?: boolean;       // CSS font-weight: >= 600 (semibold+)
  isItalic?: boolean;     // CSS font-style: italic
  isUnderline?: boolean;  // CSS text-decoration: underline (links)
  zIndex: number;         // CSS z-index (0 if not positioned)
  treeOrder: number;      // DOM traversal order for tiebreaking
  // For debugging/comparison - original web font info
  originalFontFamily?: string;
  originalFontWeight?: string;
  substituteFontName?: string;
}

export interface DrawBorderPrimitive {
  type: PrimitiveType.DrawBorder;
  x: number;
  y: number;
  width: number;
  height: number;
  thickness: number;
  color: Color;
  borderRadius?: number; // Border radius in pixels for rounded corners
  zIndex: number;      // CSS z-index (0 if not positioned)
  treeOrder: number;   // DOM traversal order for tiebreaking
}

export interface DrawImagePrimitive {
  type: PrimitiveType.DrawImage;
  x: number;
  y: number;
  width: number;
  height: number;
  src?: string;  // Optional: original source URL (not sent over wire)
  pictBytes?: Buffer;  // Raw binary PICT data (sent over wire)
  zIndex: number;      // CSS z-index (0 if not positioned)
  treeOrder: number;   // DOM traversal order for tiebreaking
}

/**
 * DrawMaskedImage - 1-bit monochrome mask with fill color
 *
 * Optimized for icon fonts (FontAwesome, Material Icons) rendered via CSS mask-image.
 * The mask is a 1-bit bitmap (black = transparent, white = opaque) that gets filled
 * with the specified color. Much more efficient than sending full-color PNGs for icons.
 *
 * Rendered on Mac OS 9 using QuickDraw's CopyMask() function.
 */
export interface DrawMaskedImagePrimitive {
  type: PrimitiveType.DrawMaskedImage;
  x: number;
  y: number;
  width: number;
  height: number;
  fillColor: Color;           // Color to fill through the mask
  maskData?: Buffer;          // 1-bit monochrome mask (1 bit per pixel, packed)
  src?: string;               // Optional: original mask URL (not sent over wire)
  zIndex: number;
  treeOrder: number;
}

/**
 * RemovePrimitive instruction - tells client to remove a primitive by identity
 *
 * This is a wire format instruction, not a drawable primitive.
 * When sent to a client, it means "delete the primitive with this identity from your state".
 *
 * Note: x,y are NOT used for rendering (this isn't drawn), but are included
 * for compatibility with PrimitiveWithIdentity intersection type.
 */
export interface RemovePrimitive {
  type: PrimitiveType.RemovePrimitive;
  identity: string;  // Identity of primitive to remove
  x: number;  // Dummy values for type compatibility
  y: number;  // Dummy values for type compatibility
}

export type Primitive =
  | DrawRectPrimitive
  | DrawTextPrimitive
  | DrawBorderPrimitive
  | DrawImagePrimitive
  | DrawMaskedImagePrimitive
  | RemovePrimitive;

export interface PrimitiveStats {
  totalCount: number;
  rectCount: number;
  textCount: number;
  borderCount: number;
  imageCount: number;
  maskedImageCount: number;
}

// Extracted element types representing the raw data returned from DOM walk
export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface ExtractedStyle {
  display: string;
  visibility: string;
  opacity: string;
  backgroundColor: string;
  color: string;
  fontFamily: string;
  fontSize: string;
  fontWeight: string;
  borderTopWidth?: string;
  borderRightWidth?: string;
  borderBottomWidth?: string;
  borderLeftWidth?: string;
  borderTopColor?: string;
  borderRightColor?: string;
  borderBottomColor?: string;
  borderLeftColor?: string;
  borderTopStyle?: string;
  borderRightStyle?: string;
  borderBottomStyle?: string;
  borderLeftStyle?: string;
  maskImage?: string;
}

export interface BaseExtractedElement {
  x: number;
  y: number;
  width: number;
  height: number;
  zIndex: number;
  treeOrder: number;
}

export interface ExtractedBackgroundElement extends BaseExtractedElement {
  type: 'background';
  computedStyle: {
    backgroundColor: string;
    borderRadius: string;
    color: string;
    fontFamily: string;
    fontSize: string;
    fontWeight: string;
    display: string;
    visibility: string;
  };
}

export interface ExtractedTextElement extends BaseExtractedElement {
  type: 'text';
  text: string;
  maxWidth?: number;
  computedStyle: {
    color: string;
    fontFamily: string;
    fontSize: string;
    fontWeight: string;
    fontStyle: string;         // 'normal' | 'italic' | 'oblique'
    textDecoration: string;    // e.g. 'underline solid rgb(...)'
  };
}

export interface ExtractedImageElement extends BaseExtractedElement {
  type: 'image';
  tagName: string;
  src?: string;
  maskColor?: string;
  backgroundColor?: string;
}

export interface ExtractedMaskedImageElement extends BaseExtractedElement {
  type: 'maskedImage';
  maskUrl: string;           // URL of SVG mask
  fillColor: string;         // CSS color to fill through mask
}

export interface ExtractedBorderElement extends BaseExtractedElement {
  type: 'border';
  borderTopWidth: number;
  borderRightWidth: number;
  borderBottomWidth: number;
  borderLeftWidth: number;
  borderTopColor: string;
  borderTopStyle: string;
  borderRadius?: string;
}

export type ExtractedElement =
  | ExtractedBackgroundElement
  | ExtractedTextElement
  | ExtractedImageElement
  | ExtractedMaskedImageElement
  | ExtractedBorderElement;

export interface ScrollMetadata {
  scrollY: number;
  scrollX: number;
  viewportWidth: number;
  viewportHeight: number;
  documentWidth: number;
  documentHeight: number;
  stickyElements: any[];
}

