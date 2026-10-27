#pragma once
// Address plates and street names for OSM data: from the addr:* and name tags we get the same three strings that the tqt layer fields hold —
// the street type (TYSTR: "улица"), the name (STREET: "Ялагина") and the house number (NUMBR: "26А", "9К.6").
#include <QHash>
#include <QString>

struct TmcAddress
{
    QString type;      //!< "улица", "проспект", "переулок" ... Empty — the type is not recognized (then the whole name is in name)
    QString name;      //!< the name without the type: "Ялагина", "Южный", "60-летия Победы"
    QString number;    //!< the house number: "26А", "9К.6", "12С.2"
    bool isEmpty() const { return type.isEmpty() && name.isEmpty() && number.isEmpty(); }
};

//! Split a street string into a type and a name. Understands Russian (and Ukrainian) types at the beginning and at the end of the string, abbreviations
//! ("ул.", "пр-кт", "наб.", "Южный пр."); for other languages the type is not separated (the whole name goes to name).
void tmcSplitStreet(const QString& street, QString& type, QString& name);

//! The house number from addr:housenumber: removes "д.", turns "к", "корп.", "корпус" into "К.", "стр.", "строение" into "С.", and a lowercase letter after digits into an uppercase one.
QString tmcNormalizeHouseNumber(const QString& housenumber);

//! A building address: addr:street (otherwise addr:place, otherwise the street from addr:full) + addr:housenumber (otherwise the number from addr:full, otherwise addr:housename).
TmcAddress tmcBuildingAddress(const QHash<QString, QString>& tags);

//! A road name for the street sign: name:ru, name, official_name. Service numbers (ref) are not used.
TmcAddress tmcRoadName(const QHash<QString, QString>& tags);
