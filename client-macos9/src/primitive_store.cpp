#include "primitive_store.h"
#include <algorithm>
#include <iostream>

namespace mochila {

PrimitiveStore::PrimitiveStore() : flatCacheDirty_(true) {}

PrimitiveStore::~PrimitiveStore() { clear(); }

void PrimitiveStore::clear() {
  // Free all primitives across all zIndex buckets
  for (ZIndexMap::iterator it = byZIndex_.begin(); it != byZIndex_.end();
       ++it) {
    for (size_t i = 0; i < it->second.size(); i++) {
      PrimitivePtr prim = it->second[i];
      if (prim) {
        if (prim->type == PrimitiveType_DrawImage) {
          DrawImagePrimitive *img = (DrawImagePrimitive *)prim;
          if (img->hPict != NULL) {
            DisposeHandle((Handle)img->hPict);
            img->hPict = NULL;
          }
        }
        delete prim;
      }
    }
  }
  byZIndex_.clear();
  identityLookup_.clear();
  imageCache_.clear();
  flatCache_.clear();
  flatCacheDirty_ = true;
}

// Sort primitives within a zIndex bucket by treeOrder only
struct TreeOrderSorter {
  bool operator()(const PrimitivePtr &a, const PrimitivePtr &b) const {
    if (!a || !b)
      return a != NULL;
    return a->treeOrder < b->treeOrder;
  }
};

static void freePrimitive(PrimitivePtr p) {
  if (!p)
    return;
  if (p->type == PrimitiveType_DrawImage) {
    DrawImagePrimitive *img = (DrawImagePrimitive *)p;
    if (img->hPict != NULL) {
      DisposeHandle((Handle)img->hPict);
      img->hPict = NULL;
    }
  }
  delete p;
}

void PrimitiveStore::applyFrameUpdate(const FrameUpdate &update) {
  size_t added = 0, changed = 0, removed = 0;
  bool structureChanged = false;

  for (size_t i = 0; i < update.primitives.size(); i++) {
    PrimitivePtr prim = update.primitives[i];
    if (!prim)
      continue;

    if (prim->type == PrimitiveType_RemovePrimitive) {
      // Remove primitive by identity
      IdentityMap::iterator it = identityLookup_.find(prim->identity);
      if (it != identityLookup_.end()) {
        PrimitivePtr existing = it->second;
        int16_t zIndex = existing->zIndex;

        // Remove from zIndex bucket
        PrimitiveBucket &bucket = byZIndex_[zIndex];
        for (size_t j = 0; j < bucket.size(); j++) {
          if (bucket[j] == existing) {
            freePrimitive(bucket[j]);
            bucket.erase(bucket.begin() + j);
            break;
          }
        }

        // Clean up empty bucket
        if (bucket.empty()) {
          byZIndex_.erase(zIndex);
        }

        identityLookup_.erase(it);
        removed++;
        structureChanged = true;
      }
    } else {
      // Handle image caching (PICT bytes only, not decoded handles)
      if (prim->type == PrimitiveType_DrawImage) {
        DrawImagePrimitive *img = (DrawImagePrimitive *)prim;
        if (img->pictBytes.empty()) {
          ImageCacheMap::iterator cacheIt = imageCache_.find(img->identity);
          if (cacheIt != imageCache_.end()) {
            img->pictBytes = cacheIt->second;
          }
        } else {
          imageCache_[img->identity] = img->pictBytes;
        }

        // PicHandle will be lazily decoded at render time (not here)
        // This prevents freezing on large frames with 100+ images
      }

      // Check if primitive already exists
      IdentityMap::iterator it = identityLookup_.find(prim->identity);

      if (it != identityLookup_.end()) {
        // Changed primitive - replace in-place
        PrimitivePtr existing = it->second;
        int16_t oldZIndex = existing->zIndex;
        int16_t newZIndex = prim->zIndex;

        // Remove from old bucket
        PrimitiveBucket &oldBucket = byZIndex_[oldZIndex];
        for (size_t j = 0; j < oldBucket.size(); j++) {
          if (oldBucket[j] == existing) {
            freePrimitive(oldBucket[j]);
            oldBucket.erase(oldBucket.begin() + j);
            break;
          }
        }
        if (oldBucket.empty()) {
          byZIndex_.erase(oldZIndex);
        }

        // Insert into new bucket
        byZIndex_[newZIndex].push_back(prim);
        identityLookup_[prim->identity] = prim;

        changed++;
        if (oldZIndex != newZIndex) {
          structureChanged = true; // zIndex changed, need to re-sort bucket
        }
      } else {
        // New primitive - add to bucket
        byZIndex_[prim->zIndex].push_back(prim);
        identityLookup_[prim->identity] = prim;
        added++;
        structureChanged = true;
      }
    }
  }

  // Sort affected buckets by treeOrder (MUCH faster than sorting entire array!)
  // Only sort if structure changed (add/remove) or if zIndex changed
  if (structureChanged) {
    for (ZIndexMap::iterator it = byZIndex_.begin(); it != byZIndex_.end();
         ++it) {
      std::sort(it->second.begin(), it->second.end(), TreeOrderSorter());
    }
  }

  // Mark flat cache dirty if anything changed
  if (added > 0 || changed > 0 || removed > 0) {
    flatCacheDirty_ = true;
  }

  std::cout << "[PrimitiveStore] Update: +" << added << " ~" << changed << " -"
            << removed << " (sorted "
            << (structureChanged ? byZIndex_.size() : 0) << " buckets)"
            << std::endl;
}

void PrimitiveStore::rebuildFlatCache() const {
  flatCache_.clear();

  // Iterate through zIndex buckets in sorted order (map is already sorted by
  // key)
  for (ZIndexMap::const_iterator it = byZIndex_.begin(); it != byZIndex_.end();
       ++it) {
    // Each bucket is already sorted by treeOrder
    for (size_t i = 0; i < it->second.size(); i++) {
      flatCache_.push_back(it->second[i]);
    }
  }

  flatCacheDirty_ = false;
}

const std::vector<PrimitivePtr> &PrimitiveStore::getPrimitives() const {
  if (flatCacheDirty_) {
    rebuildFlatCache();
  }
  return flatCache_;
}

size_t PrimitiveStore::size() const {
  size_t total = 0;
  for (ZIndexMap::const_iterator it = byZIndex_.begin(); it != byZIndex_.end();
       ++it) {
    total += it->second.size();
  }
  return total;
}

} // namespace mochila
