#pragma once
// A minimal NBT writer (big-endian): tags 0..12, a root compound with a name.
#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstring>
#include "export/format/bytes.h"

namespace Nbt {
enum : uchar { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6,
               ByteArray = 7, String = 8, List = 9, Compound = 10, IntArray = 11, LongArray = 12 };
}

class NbtWriter
{
public:
    QByteArray out;

    void writeName(const char* n) { const QByteArray u(n); putBE16(out, quint16(u.size())); out.append(u); }
    void head(uchar type, const char* name) { out.append(char(type)); if (name) writeName(name); }

    void writeByte(const char* n, qint8 v)   { head(Nbt::Byte, n); out.append(char(v)); }
    void writeShort(const char* n, qint16 v) { head(Nbt::Short, n); putBE16(out, quint16(v)); }
    void writeInt(const char* n, qint32 v)   { head(Nbt::Int, n); putBE32(out, quint32(v)); }
    void writeLong(const char* n, qint64 v)  { head(Nbt::Long, n); putBE64(out, v); }
    void writeFloat(const char* n, float v)  { head(Nbt::Float, n); quint32 u; std::memcpy(&u, &v, 4); putBE32(out, u); }
    void writeString(const char* n, const QString& s)
    {
        const QByteArray u = s.toUtf8();
        head(Nbt::String, n);
        putBE16(out, quint16(u.size()));
        out.append(u);
    }
    void writeByteArray(const char* n, const QByteArray& v)
    {
        head(Nbt::ByteArray, n);
        putBE32(out, quint32(v.size()));
        out.append(v);
    }
    void writeIntArray(const char* n, const QVector<qint32>& v)
    {
        head(Nbt::IntArray, n);
        putBE32(out, quint32(v.size()));
        for (qint32 x : v) putBE32(out, quint32(x));
    }
    void writeLongArray(const char* n, const QVector<qint64>& v)
    {
        head(Nbt::LongArray, n);
        putBE32(out, quint32(v.size()));
        for (qint64 x : v) putBE64(out, x);
    }
    // compound: open (with a name) ... fields ... endCompound(). A LIST ELEMENT (name=nullptr) has
    // neither a type nor a name — for it beginCompound writes NOTHING (the type is declared in the list header,
    // see beginList), only the fields and the terminating End.
    void beginCompound(const char* name)
    {
        if (name) { out.append(char(Nbt::Compound)); writeName(name); }
    }
    void endCompound() { out.append(char(Nbt::End)); }
    // a list of same-type elements: every element is written by the "nameless payload" methods below
    void beginList(const char* name, uchar elemType, int count)
    {
        head(Nbt::List, name);
        out.append(char(elemType));
        putBE32(out, quint32(count));
    }
    // payloads of LIST ELEMENTS (the type is already declared in the list header, the name is absent)
    void writeStringElem(const QString& s)
    {
        const QByteArray u = s.toUtf8();
        putBE16(out, quint16(u.size()));
        out.append(u);
    }
    void appendRaw(const QByteArray& r) { out.append(r); }           // a ready sequence of tags (the compound fields without the terminating End)
    void writeIntElem(qint32 v) { putBE32(out, quint32(v)); }
    void writeFloatElem(float v) { quint32 u; std::memcpy(&u, &v, 4); putBE32(out, u); }
    void writeDoubleElem(double v)   // List<Double> — entity Pos (armor_stand mannequins)
    { quint64 u; std::memcpy(&u, &v, 8); putBE64(out, qint64(u)); }
};
