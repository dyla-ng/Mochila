#ifndef MACOS9_PRIMITIVE_STORE_H
#define MACOS9_PRIMITIVE_STORE_H

#include "wire_protocol.h"
#include <vector>
#include <map>
#include <string>

namespace mochila {

// Typedefs for CodeWarrior compatibility (doesn't support nested templates)
typedef std::vector<PrimitivePtr> PrimitiveBucket;
typedef std::map<int16_t, PrimitiveBucket> ZIndexMap;
typedef std::map<std::string, PrimitivePtr> IdentityMap;
typedef std::map<std::string, std::vector<uint8_t> > ImageCacheMap;

class PrimitiveStore {
public:
    PrimitiveStore();
    ~PrimitiveStore();

    void applyFrameUpdate(const FrameUpdate& update);
    void clear();

    const std::vector<PrimitivePtr>& getPrimitives() const;
    const ZIndexMap& getPrimitivesByZIndex() const { return byZIndex_; }  // Direct map access for efficient rendering
    size_t size() const;

private:
    void rebuildFlatCache() const;

    // Primitives organized by zIndex for O(n) traversal instead of O(n log n) sorting
    ZIndexMap byZIndex_;

    // Direct lookup by identity (no index indirection needed)
    IdentityMap identityLookup_;

    ImageCacheMap imageCache_;

    // Cached flat list for getPrimitives() compatibility
    mutable std::vector<PrimitivePtr> flatCache_;
    mutable bool flatCacheDirty_;
};

} // namespace mochila

#endif // MACOS9_PRIMITIVE_STORE_H
