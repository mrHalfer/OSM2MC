#pragma once
// Byte utilities of the Minecraft format: CRC32, deflate/zlib/gzip on top of qCompress(), writing integers into a QByteArray (little/big endian).
#include <QByteArray>

quint32 crc32Of(const QByteArray& data);
//! A pure deflate (without the zlib header and adler32): for the gzip wrapper and method 8 in zip
QByteArray rawDeflate(const QByteArray& data);
//! A full zlib stream (CMF/FLG + deflate + adler32) — for region files (compression id 2)
QByteArray zlibStream(const QByteArray& data);
//! A GZIP wrapper (for level.dat)
QByteArray gzipWrap(const QByteArray& data);

inline void putLE16(QByteArray& b, quint16 v) { b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF)); }
inline void putLE32(QByteArray& b, quint32 v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF)); }
inline void putBE32(QByteArray& b, quint32 v) { for (int i = 3; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF)); }
inline void putBE16(QByteArray& b, quint16 v) { for (int i = 1; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF)); }
inline void putBE64(QByteArray& b, qint64 v) { const quint64 u = quint64(v); for (int i = 7; i >= 0; --i) b.append(char((u >> (8 * i)) & 0xFF)); }
