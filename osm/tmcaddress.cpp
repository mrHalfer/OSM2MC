#include "osm/tmcaddress.h"
#include <QRegularExpression>
#include <QStringList>

namespace
{
//! A street type word (lower case, without a trailing dot) -> the canonical full word (the Russian/Ukrainian words are data: they are what real addresses contain)
const QHash<QString, QString>& typeWords()
{
    static const QHash<QString, QString> m = []
    {
        QHash<QString, QString> h;
        auto add = [&](const char* canon, std::initializer_list<const char*> forms)
        {
            const QString c = QString::fromUtf8(canon);
            h.insert(c, c);
            for (const char* f : forms) h.insert(QString::fromUtf8(f), c);
        };
        add("улица", { "ул", "улица", "ул-ца" });
        add("проспект", { "пр-кт", "пр-т", "просп", "пр", "пркт", "пспт" });
        add("переулок", { "пер", "пер-к", "пр-к" });
        add("проезд", { "пр-д", "пр-зд", "прз" });
        add("площадь", { "пл", "площ" });
        add("бульвар", { "б-р", "бул", "бульв" });
        add("шоссе", { "ш" });
        add("набережная", { "наб", "наб-я" });
        add("тупик", { "туп" });
        add("аллея", { "ал", "алл" });
        add("линия", {});
        add("просека", { "просека" });
        add("микрорайон", { "мкр", "мкрн", "м-н" });
        add("квартал", { "кв-л" });
        add("дорога", { "дор" });
        add("спуск", {});
        add("въезд", {});
        add("переезд", {});
        add("тракт", {});
        add("вулиця", { "вул" });
        add("провулок", { "пров" });
        add("площа", {});
        add("деревня", { "дер" });
        add("село", {});
        add("посёлок", { "поселок", "пос" });
        add("хутор", {});
        add("станица", {});
        add("узвіз", {});
        add("проспект", {});
        return h;
    }();
    return m;
}

QString lowerNoDot(QString w)
{
    while (w.endsWith(QLatin1Char('.')) || w.endsWith(QLatin1Char(','))) w.chop(1);
    return w.toLower();
}

QString canonType(const QString& word)
{
    return typeWords().value(lowerNoDot(word));
}

QString clean(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}
}

void tmcSplitStreet(const QString& streetIn, QString& type, QString& name)
{
    type.clear();
    name.clear();
    const QString street = clean(streetIn);
    if (street.isEmpty()) return;
    QStringList words = street.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() >= 2)
    {
        const QString first = canonType(words.first());
        // "улица Ленина", "ул. 8 Марта", "пр-кт Мира"; but not "Шоссе Энтузиастов" -> a type as well, that is fine
        if (!first.isEmpty()) { type = first; words.removeFirst(); name = words.join(QLatin1Char(' ')); return; }
        const QString last = canonType(words.last());
        // "Ленинский проспект", "2-й Южный проезд", "Садовая улица"
        if (!last.isEmpty()) { type = last; words.removeLast(); name = words.join(QLatin1Char(' ')); return; }
    }
    name = street;       // a single word ("Бульвар"), Latin script and other languages: the type is not separated
}

QString tmcNormalizeHouseNumber(const QString& in)
{
    QString s = clean(in);
    if (s.isEmpty()) return s;
    static const QRegularExpression lead(QStringLiteral("^(?:дом|д)\\.?\\s*"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    s.remove(lead);
    // building and structure: "12 к 3", "12к3", "12 корп. 3", "12 корпус 3" -> "12К.3"; "12 стр 2", "12с2", "12 строение 2" -> "12С.2"
    static const QRegularExpression korp(QStringLiteral("(?<=\\d|\\s)(?:корпус|корп|к|k)\\.?\\s*(?=\\d)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression stro(QStringLiteral("(?<=\\d|\\s|\\.)(?:строение|стр|с|s)\\.?\\s*(?=\\d)"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    s.replace(korp, QStringLiteral("К."));
    s.replace(stro, QStringLiteral("С."));
    s.replace(QRegularExpression(QStringLiteral("\\s*[;,]\\s*")), QStringLiteral(";"));
    s.remove(QRegularExpression(QStringLiteral("\\s+(?=К\\.|С\\.)")));
    // a single lowercase letter after digits: "26а" -> "26А"
    static const QRegularExpression letter(QStringLiteral("^(\\d+)\\s*([^\\W\\d_])$"), QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch m = letter.match(s);
    if (m.hasMatch()) s = m.captured(1) + m.captured(2).toUpper();
    return s;
}

TmcAddress tmcBuildingAddress(const QHash<QString, QString>& tags)
{
    auto tag = [&](const char* k) { return clean(tags.value(QString::fromLatin1(k))); };
    TmcAddress a;
    QString street = tag("addr:street");
    QString number = tag("addr:housenumber");
    if (street.isEmpty()) street = tag("addr:place");           // villages and garden partnerships without streets: "деревня Ивановка", a house number
    if (street.isEmpty() || number.isEmpty())
    {   // addr:full: "улица Ленина, 5", "Москва, Ленинский проспект, 12к1"
        const QString full = tag("addr:full");
        if (!full.isEmpty())
        {
            const QStringList parts = full.split(QLatin1Char(','), Qt::SkipEmptyParts);
            static const QRegularExpression hasDigit(QStringLiteral("\\d"));
            for (int i = parts.size() - 1; i >= 0; --i)
            {
                const QString p = clean(parts[i]);
                if (number.isEmpty() && p.contains(hasDigit) && p.size() <= 12 && !p.contains(QLatin1Char(' ')))
                { number = p; continue; }
                if (street.isEmpty() && !p.isEmpty()
                    && (!canonType(p.section(QLatin1Char(' '), 0, 0)).isEmpty() || !canonType(p.section(QLatin1Char(' '), -1)).isEmpty()))
                    street = p;
            }
        }
    }
    tmcSplitStreet(street, a.type, a.name);
    a.number = tmcNormalizeHouseNumber(number);
    if (a.name.isEmpty() && a.number.isEmpty()) a.name = tag("addr:housename");   // the house has only a name
    return a;
}

TmcAddress tmcRoadName(const QHash<QString, QString>& tags)
{
    TmcAddress a;
    for (const char* k : { "name:ru", "name", "official_name", "alt_name" })
    {
        const QString v = clean(tags.value(QString::fromLatin1(k)));
        if (!v.isEmpty()) { tmcSplitStreet(v, a.type, a.name); break; }
    }
    return a;
}
