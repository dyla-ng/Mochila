import { encodeImageToPict, PictEncodeResult } from './pict-encoder';

export interface CachedPictImage {
  pictBytes: Buffer;
  originalWidth: number;
  originalHeight: number;
  pictWidth: number;
  pictHeight: number;
  encodeTimeMs: number;
  cachedAt: number; // timestamp
}

export class PictImageCache {
  private cache: Map<string, CachedPictImage> = new Map();
  private pending: Map<string, Promise<PictEncodeResult>> = new Map();
  private maxSize: number = 100;
  private hits: number = 0;
  private misses: number = 0;
  private timeSavedMs: number = 0;
  private encoder: typeof encodeImageToPict;

  public onCacheUpdate?: () => void;

  constructor(encoder: typeof encodeImageToPict = encodeImageToPict) {
    this.encoder = encoder;
  }

  /**
   * Synchronously check if an image is already in cache (without triggering fetch/encode)
   */
  has(url: string, maxPhotoDimension: number, maskColor?: string, backgroundColor?: string): boolean {
    const key = `${url}:${maxPhotoDimension}:${maskColor || 'none'}:${backgroundColor || 'none'}`;
    return this.cache.has(key);
  }

  async get(url: string, maxPhotoDimension: number, maskColor?: string, backgroundColor?: string, page?: any): Promise<PictEncodeResult> {
    const key = `${url}:${maxPhotoDimension}:${maskColor || 'none'}:${backgroundColor || 'none'}`;
    const cached = this.cache.get(key);

    if (cached) {
      // LRU Eviction Logic: move to end of map (most recently used)
      this.cache.delete(key);
      this.cache.set(key, cached);

      this.hits++;
      this.timeSavedMs += cached.encodeTimeMs;
      // console.log(`[Cache] Hit for ${url} (saved ${cached.encodeTimeMs}ms)`);

      return {
        pictBytes: cached.pictBytes,
        originalWidth: cached.originalWidth,
        originalHeight: cached.originalHeight,
        pictWidth: cached.pictWidth,
        pictHeight: cached.pictHeight,
        encodeTimeMs: cached.encodeTimeMs,
      };
    }

    // Coalesce in-flight requests for the same key to prevent redundant encoding
    let pendingPromise = this.pending.get(key);
    if (pendingPromise) {
      try {
        const result = await pendingPromise;
        this.hits++;
        this.timeSavedMs += result.encodeTimeMs;
        // console.log(`[Cache] Hit (coalesced) for ${url} (saved ${result.encodeTimeMs}ms)`);
        return result;
      } catch (err) {
        // Re-throw if the pending promise rejected so caller is aware
        throw err;
      }
    }

    // Cache miss: initiate encoding using the injected encoder
    const promise = this.encoder(url, maxPhotoDimension, maskColor, backgroundColor, page);
    this.pending.set(key, promise);
    this.misses++;

    try {
      const result = await promise;
      const cachedItem: CachedPictImage = {
        pictBytes: result.pictBytes,
        originalWidth: result.originalWidth,
        originalHeight: result.originalHeight,
        pictWidth: result.pictWidth,
        pictHeight: result.pictHeight,
        encodeTimeMs: result.encodeTimeMs,
        cachedAt: Date.now(),
      };

      // LRU Eviction if full
      if (this.cache.size >= this.maxSize) {
        const oldestKey = this.cache.keys().next().value;
        if (oldestKey !== undefined) {
          this.cache.delete(oldestKey);
        }
      }

      this.cache.set(key, cachedItem);
      if (this.onCacheUpdate) {
        this.onCacheUpdate();
      }
      return result;
    } finally {
      this.pending.delete(key);
    }
  }

  clear() {
    this.cache.clear();
    this.pending.clear();
    this.hits = 0;
    this.misses = 0;
    this.timeSavedMs = 0;
  }

  getStats() {
    return {
      hits: this.hits,
      misses: this.misses,
      size: this.cache.size,
      timeSavedMs: this.timeSavedMs,
    };
  }
}

export const pictCache = new PictImageCache();
