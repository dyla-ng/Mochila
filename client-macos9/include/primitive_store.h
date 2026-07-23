#ifndef MACOS9_PRIMITIVE_STORE_H
#define MACOS9_PRIMITIVE_STORE_H

#include "wire_protocol.h"
#include <vector>
#include <map>
#include <string>

namespace mochila {

// Typedefs for CodeWarrior compatibility (doesn't support nested templates)
typedef std::map<std::string, PrimitivePtr> IdentityMap;
typedef std::map<std::string, std::vector<uint8_t> > ImageCacheMap;

class PrimitiveStore {
public:
    PrimitiveStore();
    ~PrimitiveStore();

    void applyFrameUpdate(const FrameUpdate& update);
    void clear();

    const std::vector<PrimitivePtr>& getPrimitives() const;
    size_t size() const;

private:
    // Simple vector of all primitives, sorted by treeOrder (document order)
    std::vector<PrimitivePtr> primitives_;

    // Direct lookup by identity for updates
    IdentityMap identityLookup_;

    // Image cache (PICT bytes only, not decoded handles)
    ImageCacheMap imageCache_;
};

} // namespace mochila

#endif // MACOS9_PRIMITIVE_STORE_H
