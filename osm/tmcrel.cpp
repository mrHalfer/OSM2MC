#include "osm/tmcrel.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

bool TmcHeightTable::load(const QString& csvPath)
{
    mHigh.clear();
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    in.setCodec("UTF-8");                                                  // the BOM is recognized automatically
    in.readLine();                                                         // the header: CODE;HIGH;OBJNAME50
    while (!in.atEnd())
    {
        const QStringList p = in.readLine().split(QLatin1Char(';'));
        bool okC = false, okH = false;
        const int code = p.value(0).trimmed().toInt(&okC);
        const double h = p.value(1).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&okH);
        if (okC && okH && code > 0) mHigh.insert(code, h);                 // and zero heights (lawns, meadows) too: the code is in the table, the height is 0
    }
    return !mHigh.isEmpty();
}

QString TmcHeightTable::findDefault()
{
    for (const QString& d : { QCoreApplication::applicationDirPath(), QDir::currentPath() })
    {
        const QString p = QDir(d).filePath(QStringLiteral("heights.csv"));
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

static bool inList(int code, std::initializer_list<int> l) { for (int c : l) if (c == code) return true; return false; }

double tmcRelFromFloors(int code, int floorIn, double areaM2, double perimM)
{
    const double fl = floorIn <= 0 ? 1 : floorIn;
    const double len = std::max(perimM, 0.01), area = std::max(areaM2, 0.01);
    // "building width" by the compactness of the outline (as width_calc in SQL)
    const double sqr = area / (len * len);
    const double k = sqr <= 0.032 ? 2.0 : sqr <= 0.042 ? 2.5 : sqr <= 0.052 ? 3.0 : sqr <= 0.062 ? 3.3 : 4.0;
    const double width = len * sqr * k;
    auto frac = [](double v) { return v - std::floor(v); };
    auto pitched = [&](double v) { return v < 4.5 ? 4.5 + frac(v) : v; };            // pitched roofs: not lower than 4.5 + the fractional part
    auto smallBld = [&]()                                                           // sheds and small dachas
    {
        const double t = 4 + frac(width / 4);
        if (t < 4.5) return 4.5 + frac(t);
        if (t > 4.7) return t - 0.2;
        return t;
    };

    if (inList(code, { 44310000, 44311000, 44319000 })) return fl <= 1 ? 4.5 : fl * 4;
    if (code == 44340000) return fl <= 1 ? 5 : fl == 2 ? 9 : fl * 4;
    if (inList(code, { 44316000, 44317000, 44318000 })) return fl * 4.5;
    if (inList(code, { 44312000, 44313000, 44314000, 44315000 })) return fl * 4.2;
    if (code == 44400000)
    {
        if (fl <= 4) return pitched(3 * fl + width / 6);
        if (fl <= 20) return fl * 3.3;
        return 20 * 3.3 + (fl - 20) * 3;
    }
    if (inList(code, { 44400110, 44400002 }))                                       // private sector, cottages
    {
        if (area <= 105) return pitched(2.5 + width / 3);
        if (area <= 205) return 4 + width / 2;
        return 4.5 + width / 3;
    }
    if (code == 44400100)                                                           // dachas
    {
        if (area <= 30) return fl <= 1 ? smallBld() : fl * 3.3;
        return pitched(2.5 + width / 3);
    }
    if (code == 44000000) return fl <= 1 ? smallBld() : fl * 3.3;                   // sheds and booths
    if (code != 44350000) return fl <= 1 ? 4.5 : fl * 3.3;
    return 0;
}

static bool firstNumber(const QString& s, double& out)
{
    static const QRegularExpression re(QStringLiteral("-?\\d+(?:[.,]\\d+)?"));
    const auto m = re.match(s);
    if (!m.hasMatch()) return false;
    out = QString(m.captured(0)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    return true;
}

QString tmcAutoRel(const QHash<QString, QString>& tags, int code, const TmcHeightTable& clutters, double defaultH)
{
    double v = 0;
    if (firstNumber(tags.value(QStringLiteral("height")), v) && v > 0) return QString::number(v, 'f', 1);     // as REL_HAND: set manually
    double lv = 0;
    if (firstNumber(tags.value(QStringLiteral("building:levels")), lv) && lv > 0)
    {
        double area = 0, perim = 0;
        firstNumber(tags.value(QStringLiteral("_area")), area);
        firstNumber(tags.value(QStringLiteral("_perim")), perim);
        const double r = tmcRelFromFloors(code, int(lv + 0.5), area, perim);
        if (r > 0) return QString::number(r, 'f', 1);
    }
    // a fallback before the table: enhancer data (the nearest point from the .dat) — the height, otherwise floors by the formulas
    double dv = 0;
    if (firstNumber(tags.value(QStringLiteral("db:rel")), dv) && dv > 0) return QString::number(dv, 'f', 1);
    if (firstNumber(tags.value(QStringLiteral("db:floor")), dv) && dv > 0)
    {
        double area = 0, perim = 0;
        firstNumber(tags.value(QStringLiteral("_area")), area);
        firstNumber(tags.value(QStringLiteral("_perim")), perim);
        const double r = tmcRelFromFloors(code, int(dv + 0.5), area, perim);
        if (r > 0) return QString::number(r, 'f', 1);
    }
    const double h = clutters.high(code);                                          // -1 — the code is not in the table
    return QString::number(h >= 0 ? h : defaultH, 'f', 1);
}
