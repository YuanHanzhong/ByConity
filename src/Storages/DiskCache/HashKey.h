#pragma once

#include <Functions/FunctionsHashing.h>
#include <Storages/DiskCache/Buffer.h>
#include <common/StringRef.h>

namespace DB::HybridCache
{
// Pairs up key and hash together, reducing the cost of computing hash multiple
// times, and eliminating possibility to modify one of them independently.
class HashedKey
{
public:
    static HashedKey precomputed(StringRef key, UInt64 key_hash) { return HashedKey{key, key_hash}; }

    explicit HashedKey(StringRef key) : HashedKey{key, hashBuffer(key)} { }

    HashedKey(const char * key, size_t size) : HashedKey{{key, size}} { }

    StringRef key() const { return key_; }

    UInt64 keyHash() const { return key_hash_; }

    bool operator==(HashedKey other) const { return key_hash_ == other.keyHash() && key_ == other.key(); }

    bool operator!=(HashedKey other) const { return !(*this == other); }

private:
    HashedKey(StringRef key, UInt64 key_hash) : key_{key}, key_hash_{key_hash} { }

    static UInt64 hashBuffer(StringRef key, UInt64 seed = 0) { return MurmurHash3Impl64WithSeed::apply(key.data, key.size, seed); }

    StringRef key_;
    UInt64 key_hash_{};
};

UInt64 hashBuffer(BufferView key, UInt64 seed = 0);
UInt32 checksum(BufferView data, UInt32 init = 0);

inline HashedKey makeHashKey(const void * ptr, size_t size)
{
    return HashedKey{StringRef{reinterpret_cast<const char *>(ptr), size}};
}

inline HashedKey makeHashKey(BufferView key)
{
    return makeHashKey(key.data(), key.size());
}

inline HashedKey makeHashKey(const Buffer & key)
{
    return makeHashKey(key.view());
}

inline HashedKey makeHashKey(const char * cstr)
{
    return HashedKey{StringRef{cstr}};
}
}
