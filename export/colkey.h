#pragma once
// The hash keys shared by all "column/chunk -> data" maps of the exporter.
#include <QtGlobal>

inline qint64 pairKey(int a, int b)
{
    return (qint64(quint32(a)) << 32) | quint32(b);
}
inline int keyFirst(qint64 k)  { return qint32(quint32(quint32(quint64(k) >> 32))); }
inline int keySecond(qint64 k) { return qint32(quint32(quint32(quint64(k) & 0xFFFFFFFFull))); }

//! The hash key for the "column/chunk -> data" maps. pairKey(x, z) has the standard qHash(qint64) = low32(k ^ (k >> 31)) = z ^ (x << 1): for coordinates
//! within a few thousand that is only a few thousand distinct values for millions of keys — chains of hundreds of elements, every insertion
//! and lookup took tens of microseconds (a large reservoir — 125 s for 4 million columns). ColKey behaves like a qint64 (implicit conversions),
//! but hashes the value by mixing (the MurmurHash3 finalizer), so all column and chunk maps work in O(1).
struct ColKey
{
    qint64 v = 0;
    Q_DECL_CONSTEXPR ColKey() = default;
    Q_DECL_CONSTEXPR ColKey(qint64 k) : v(k) {}
    Q_DECL_CONSTEXPR operator qint64() const { return v; }
};
inline uint qHash(const ColKey& k, uint seed = 0) noexcept
{
    quint64 h = quint64(k.v) ^ quint64(seed);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return uint(h);
}

//! The key (x&15, z&15, y&15) inside a section — the same indexing order that Minecraft expects:
//! index = (y<<8) | (z<<4) | x. A bitwise & with negatives gives a correct modulo 16.
inline int sectionIndex(int x, int y, int z)
{
    return ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
}
