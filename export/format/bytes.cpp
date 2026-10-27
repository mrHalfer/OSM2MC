#include "export/format/bytes.h"

#include <QByteArray>

quint32 crc32Of(const QByteArray& data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init)
    {
        for (quint32 i = 0; i < 256; ++i)
        {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    for (int i = 0; i < data.size(); ++i)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

//! A qCompress() stream = [4 bytes of the original size BE][the zlib CMF/FLG header][deflate][adler32].
//! Here we return a PURE deflate: mid(6, size-10) strips both the Qt size prefix (4 bytes) and
//! the zlib header (2 bytes) in front and the adler32 (4 bytes) at the back. Exactly such a deflate is expected by the gzip wrapper
//! and by zip method 8. For region files (compression id 2 = zlib), on the contrary, a zlib stream WITH the CMF/FLG
//! header and adler is needed, but WITHOUT the Qt size prefix — see zlibStream().
QByteArray rawDeflate(const QByteArray& data)
{
    if (data.isEmpty())
        return QByteArray();
    const QByteArray z = qCompress(data, 6);   // 6, not 9: on chunks level 9 is ~7 times slower for a gain of ~1% in size
    return z.mid(6, z.size() - 10);
}

//! A full zlib stream (CMF/FLG + deflate + adler32) from qCompress() — for region files
//! (compression id 2). We only strip the 4-byte Qt prefix of the uncompressed size.
QByteArray zlibStream(const QByteArray& data)
{
    if (data.isEmpty())
        return QByteArray();
    const QByteArray z = qCompress(data, 6);   // 6, not 9: on chunks level 9 is ~7 times slower for a gain of ~1% in size
    return z.mid(4);
}

//! A GZIP wrapper around data (for level.dat).
QByteArray gzipWrap(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size() / 2 + 64);
    // gzip header: magic, the deflate method, flags 0, mtime 0, XFL 0, OS=Unix
    out.append(char(0x1F)); out.append(char(0x8B)); out.append(char(0x08)); out.append(char(0x00));
    out.append(4, char(0x00));           // mtime = 0
    out.append(char(0x00));              // XFL
    out.append(char(0x03));              // OS: Unix
    const QByteArray deflated = rawDeflate(data);
    if (deflated.isEmpty())              // empty input: a correct empty final deflate block
        out.append("\x03\x00", 2);
    else
        out.append(deflated);
    const quint32 crc = crc32Of(data);
    const quint32 size = quint32(data.size());
    for (int i = 0; i < 4; ++i) out.append(char((crc >> (8 * i)) & 0xFF));
    for (int i = 0; i < 4; ++i) out.append(char((size >> (8 * i)) & 0xFF));
    return out;
}
