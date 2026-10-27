#pragma once
// A minimal zip writer (deflate with a fallback to store): local headers + the central directory + EOCD, everything is assembled in memory.
#include <QByteArray>
#include <QDate>
#include <QString>
#include <QTime>
#include <QVector>
#include "export/format/bytes.h"

class MiniZip
{
public:
    //! The total uncompressed/compressed size of all entries — Export checks it against the classic zip limits.
    quint64 totalRaw = 0;
    quint64 totalBlob = 0;

    void add(const QString& name, const QByteArray& data)
    {
        Item it;
        it.name = name.toUtf8();
        it.crc = crc32Of(data);
        it.rawSize = quint32(data.size());
        totalRaw += quint64(data.size());
        // .mca files are already zlib containers (entropy): a level-9 deflate would burn a full pass
        // just to prove that compression is impossible (the result was thrown away anyway on the
        // fallback to store) — we go straight to store, byte for byte the same archive.
        if (name.endsWith(QLatin1String(".mca")))
        {
            it.method = 0;
            it.blob = data;
        }
        else
        {
            const QByteArray deflated = rawDeflate(data);
            if (!deflated.isEmpty() && deflated.size() < data.size())
            {
                it.method = 8;
                it.blob = deflated;
            }
            else
            {
                it.method = 0;
                it.blob = data;
            }
        }
        totalBlob += quint64(it.blob.size());
        items.append(it);
    }

    QByteArray finish()
    {
        // DOS date/time "now"
        const QDate d = QDate::currentDate();
        const QTime t = QTime::currentTime();
        const quint16 dosDate = quint16((qMax(1980, d.year()) - 1980) << 9 | d.month() << 5 | d.day());
        const quint16 dosTime = quint16(t.hour() << 11 | t.minute() << 5 | t.second() / 2);

        QByteArray zip;
        QVector<quint32> offsets(items.size());
        // local headers + data
        for (int i = 0; i < items.size(); ++i)
        {
            const Item& it = items[i];
            offsets[i] = quint32(zip.size());
            putLE32(zip, 0x04034b50);
            putLE16(zip, 20);                 // version needed
            putLE16(zip, 0);                  // flags
            putLE16(zip, it.method);
            putLE16(zip, dosTime);
            putLE16(zip, dosDate);
            putLE32(zip, it.crc);
            putLE32(zip, quint32(it.blob.size()));
            putLE32(zip, it.rawSize);
            putLE16(zip, quint16(it.name.size()));
            putLE16(zip, 0);                  // extra
            zip.append(it.name);
            zip.append(it.blob);
        }
        const quint32 cdStart = quint32(zip.size());
        quint32 cdSize = 0;
        for (int i = 0; i < items.size(); ++i)
        {
            const Item& it = items[i];
            putLE32(zip, 0x02014b50);
            putLE16(zip, 20);                 // version made by
            putLE16(zip, 20);                 // version needed
            putLE16(zip, 0);                  // flags
            putLE16(zip, it.method);
            putLE16(zip, dosTime);
            putLE16(zip, dosDate);
            putLE32(zip, it.crc);
            putLE32(zip, quint32(it.blob.size()));
            putLE32(zip, it.rawSize);
            putLE16(zip, quint16(it.name.size()));
            putLE16(zip, 0);                  // extra
            putLE16(zip, 0);                  // comment
            putLE16(zip, 0);                  // disk
            putLE16(zip, 0);                  // int attrs
            putLE32(zip, 0);                  // ext attrs
            putLE32(zip, offsets[i]);
            zip.append(it.name);
            cdSize += quint32(46 + it.name.size());
        }
        putLE32(zip, 0x06054b50);
        putLE16(zip, 0); putLE16(zip, 0);
        putLE16(zip, quint16(items.size()));
        putLE16(zip, quint16(items.size()));
        putLE32(zip, cdSize);
        putLE32(zip, cdStart);
        putLE16(zip, 0);
        return zip;
    }

private:
    struct Item
    {
        QByteArray name;
        QByteArray blob;
        quint32 crc = 0;
        quint32 rawSize = 0;
        quint16 method = 8;
    };
    QVector<Item> items;
};
