#pragma once
// The rules that turn OpenStreetMap tags into "virtual tqt layers" (bldpol, vegpol, rdslin ... with the CODE, REL, MAT ... fields).
// The rules live in an external JSON file (osm_rules.json) so that the tag/code mapping can be changed without a rebuild.
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QPair>
#include <QRegularExpression>

//! A single condition on a tag
struct OsmCondition
{
    enum Kind { Any, Absent, Equals, NotEquals, OneOf, NoneOf, Regex, Gt, Ge, Lt, Le };   // Gt..Le — a comparison of the first number from the tag value
    double number = 0;                              //!< threshold for Gt/Ge/Lt/Le
    QString key;
    Kind kind = Any;
    QStringList values;
    QRegularExpression re;
    bool matches(const QHash<QString, QString>& tags) const;
};

//! A rule: if an object of the OSM layer `from` meets all conditions, it goes to the layer `layer` with the fields `fields`
struct OsmRule
{
    QString note;                                   //!< a comment (for the log/people only)
    QString from;                                   //!< the OSM driver layer: multipolygons | lines | points
    QString layer;                                  //!< the tqt layer suffix: bldpol, vegpol, rdslin ...
    QString as;                                     //!< "" — the geometry as is; "polygon" — a closed line -> a polygon; "line" — a polygon boundary -> a line
    QVector<OsmCondition> conds;
    QVector<QPair<QString, QString>> fields;        //!< field name -> value template
    int pass = 1;                                   //!< 1 — an ordinary rule; 2 — a "guess" for objects not recognized in the first pass (sees the nb:* tags about neighbors)
    bool cont = false;                              //!< true — after firing, continue checking the next rules (an object can fall into several layers)
};

//! The result of applying the rules to an object
struct OsmClassified
{
    QString layer;
    QString as;
    QVector<QPair<QString, QString>> fields;        //!< computed field values
};

//! Parameters of the vegetation "top-up" for OSM data (the "greenery" block in osm_rules.json), see TmcProject::synthesizeGreenery()
struct GreeneryOptions
{
    bool enabled = true;
    bool yards = true;              //!< trees in yards between houses (courtyard vegetation), if OSM did not provide it
    bool privateTaiga = true;       //!< forced taiga (spruces) in free places of the private sector
    double forestShare = 0.08;      //!< the share of the data area under forests: if there are fewer forests, free places for new ones are found
    double parksPerKm2 = 1.5;       //!< how many parks (from 6000 m²) there should be per km²: if there are too few, large lawns and free places become parks
    //! Wild forests for the camps and outposts of bandits: forests are added on very large empty areas far from buildings (taiga and mixed ones alternately),
    //! so that the exporter can place abandoned camps and bases there. A margin from buildings is wildBuildingM, from multi-storey (block) development — wildBlockM.
    bool wildForests = true;
    double wildMinAreaM2 = 30000.0; //!< the smallest area of an empty plot (after the margins), m²
    double wildBuildingM = 40.0;    //!< margin from any buildings, m
    double wildBlockM = 150.0;      //!< margin from block (non-private) development, m: such forests do not appear between high-rises
    double wildPatchM2 = 80000.0;   //!< the area of one wild forest (a round patch around the point of the plot farthest from buildings), m²
    double wildApartM = 600.0;      //!< the smallest distance between such forests, m
    int wildMaxCount = 6;          //!< no more than this many such forests
    double cellM = 2.0;            //!< the grid step for searching free places, m (it coarsens by itself for very large data)
};

class OsmRules
{
public:
    //! Load the rules from JSON. false + err on a parse error.
    bool load(const QString& path, QString* err);

    const QVector<OsmRule>& rules() const { return mRules; }
    bool isEmpty() const { return mRules.isEmpty(); }
    QString path() const { return mPath; }
    //! All tags referenced by the conditions and templates (they become columns of osmconf.ini)
    QStringList tagKeys() const;
    //! Tags referenced by the rules with the prefix prefix ("poi:" or "area:") — already without the prefix
    QStringList spatialKeys(const QString& prefix) const;
    static bool isSpatialKey(const QString& key);       //!< a key with the poi: / area: prefix
    static QString baseKey(const QString& key);         //!< a key without the poi: / area: prefix
    //! The OSM layers that need to be read
    QStringList sourceLayers() const;
    //! Names of the tqt layer fields (the union over all rules)
    QStringList fieldNames(const QString& layer) const;
    //! Apply the rules. There may be several results (if the rules have cont=true).
    //! matched (optional) is set to true if any rule fired, including a dropping one (layer "-")
    QVector<OsmClassified> classify(const QString& from, const QHash<QString, QString>& tags, int pass = 1, bool* matched = nullptr) const;

    bool hasPass2() const;
    const GreeneryOptions& greenery() const { return mGreen; }
    double neighborRadius() const { return mNeighborRadius; }               //!< radius for searching recognized neighbors, m ("neighbors" block in the JSON)
    QStringList neighborIgnoreCodes() const { return mNeighborIgnore; }     //!< codes that do not count as neighbors (for example sheds)

    //! Parse the value of the other_tags column (hstore: "k"=>"v","k2"=>"v2")
    static void parseHstore(const QString& s, QHash<QString, QString>& tags);

private:
    static QString evalTemplate(const QString& tpl, const QHash<QString, QString>& tags);
    static QString evalOne(const QString& tpl, const QHash<QString, QString>& tags);
    QVector<OsmRule> mRules;
    QString mPath;
    GreeneryOptions mGreen;
    double mNeighborRadius = 60.0;
    QStringList mNeighborIgnore;
};
