#include "primitive_store.h"
#include <algorithm>
#include <iostream>

namespace mochila {

PrimitiveStore::PrimitiveStore() {}

PrimitiveStore::~PrimitiveStore() { clear(); }

void PrimitiveStore::clear() {
  // Free all primitives
  for (size_t i = 0; i < primitives_.size(); i++) {
    PrimitivePtr prim = primitives_[i];
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
  primitives_.clear();
  identityLookup_.clear();
  imageCache_.clear();
}

// Sort primitives by treeOrder (document order)
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

  for (size_t i = 0; i < update.primitives.size(); i++) {
    PrimitivePtr prim = update.primitives[i];
    if (!prim)
      continue;

    if (prim->type == PrimitiveType_RemovePrimitive) {
      // Remove primitive by identity
      IdentityMap::iterator it = identityLookup_.find(prim->identity);
      if (it != identityLookup_.end()) {
        PrimitivePtr existing = it->second;

        // Remove from vector
        for (size_t j = 0; j < primitives_.size(); j++) {
          if (primitives_[j] == existing) {
            freePrimitive(primitives_[j]);
            primitives_.erase(primitives_.begin() + j);
            break;
          }
        }

        identityLookup_.erase(it);
        removed++;
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

        // SPECIAL CASE: For DrawImage, preserve client-side pictBytes and cached PicHandle
        // The server sends empty pictBytes (images come via ImageData messages)
        // We must not lose the client-side populated data when primitive is updated!
        if (prim->type == PrimitiveType_DrawImage && existing->type == PrimitiveType_DrawImage) {
          DrawImagePrimitive *newImg = (DrawImagePrimitive *)prim;
          DrawImagePrimitive *existingImg = (DrawImagePrimitive *)existing;

          // Preserve client-populated pictBytes and cached PicHandle
          if (!existingImg->pictBytes.empty() && newImg->pictBytes.empty()) {
            newImg->pictBytes = existingImg->pictBytes;
            newImg->hPict = existingImg->hPict;
            existingImg->hPict = NULL;  // Transfer ownership, don't dispose

            std::cout << "[PrimitiveStore] Preserved pictBytes and PicHandle for image "
                      << existingImg->imageId << std::endl;
          }
        }

        // Remove from vector
        for (size_t j = 0; j < primitives_.size(); j++) {
          if (primitives_[j] == existing) {
            freePrimitive(primitives_[j]);
            primitives_[j] = prim;
            break;
          }
        }

        identityLookup_[prim->identity] = prim;
        changed++;
      } else {
        // New primitive - add to vector
        primitives_.push_back(prim);
        identityLookup_[prim->identity] = prim;
        added++;
      }
    }
  }

  // Sort by treeOrder to maintain document order
  // This is fast even with 2000 primitives (~1ms on G4)
  std::sort(primitives_.begin(), primitives_.end(), TreeOrderSorter());

  std::cout << "[PrimitiveStore] Update: +" << added << " ~" << changed << " -"
            << removed << " (total: " << primitives_.size() << " primitives)"
            << std::endl;
}

const std::vector<PrimitivePtr> &PrimitiveStore::getPrimitives() const {
  return primitives_;
}

size_t PrimitiveStore::size() const {
  return primitives_.size();
}

} // namespace mochila
