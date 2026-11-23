#pragma once
// Random city events over the exported world (docs/city-encounters.md). THE single list of event ids,
// shared by the exporter (the "osmc_enc" datapack, see EncPack::build) and by the "City events" window
// (app/encountersdialog.cpp), which repeats the ids in the same order and count — the window holds the
// translatable titles (lupdate only extracts tr() literals, not strings coming from this table). Adding
// an event means adding an id here AND a row in the dialog; a mismatch is reported by the dialog at
// open time. The dialog also marks the events whose logic has not shipped yet — encImplemented().
#include <QByteArray>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVector>
#include <functional>

extern const char* const kEncIds[];
extern const int kEncCount;

//! true — the event has real logic in the generated pack (false → the window says it is in progress)
bool encImplemented(const char* id);

//! The "osmc_enc" datapack (docs/city-encounters.md). The exporter bakes the exact coordinates of the
//! event places into the generated functions (no macros, no external files: the pack runs as is in
//! 1.21.x and 26.x); the places come from EncPack::Prop collected while the world is built, the site
//! markers (Tags osmc.site / osmc.site_<code>) stay the general anchors the not-yet-generated events
//! will use. The tick function runs one "cycle" every 200 ticks; every event is gated by a scoreboard
//! (#<id> osmc_enc 0|1) that the load function seeds from the exporter's checkboxes and the player may
//! flip at run time with /function osmc_enc:on/<id> or osmc_enc:off/<id>.
struct EncPack
{
    //! One world cell with the block that stands in it at export time (so an event can take the cell
    //! over and give it back exactly as it was). Used by window_wave (the glass of a facade row) and
    //! fresh_grave (the surface under the mound).
    struct Cell
    {
        int x = 0, y = 0, z = 0;
        quint16 id = 0;         // the block id baked for setblock (blockName() of it goes into the pack)
    };
    //! A place one of the baked events can happen at, collected by the build stages (ExportRun::encProps)
    struct Prop
    {
        int x = 0, y = 0, z = 0;        // kind-specific base: a lamp — the base block below the lights
                                        // (the glowstone sits at y+3 and y+6); a bus stop — the spawn
                                        // cell of the node; a playground — the spawn cell of the pad
                                        // centre; chalk/jack — the block cell of the pattern/jack;
                                        // a cross — the mast cell off the roadway; a school — the
                                        // chest cell on the facade; a scarecrow/grave/bell — the
                                        // stand cell / the mound cell / the tower base cell; a window
                                        // wall — unused (all data lives in cells); a car — the plate
                                        // cell by the car door; a bus — the low corner of the strip
                                        // (y the flat ground under the bus); a lurker — the lamp
                                        // base he circles
        int kind = 0;                   // one of the kEp* below
        int dir = 0;                    // 0..3, the kDirNames order (N/E/S/W): a chalk — the arrow
                                        // points this way; a jack — the facing the cemetery baked;
                                        // a scarecrow — the daytime facing (back to the house); a
                                        // school — the facing the chest (and its sign) show; a bell —
                                        // the facing of the bell; a bus — 0 the strip runs east-west,
                                        // 1 it runs north-south
        quint16 blk[5] = { 0 };         // chalk only: the surface blocks of the five cells, so the
                                        // event restores the road exactly as it was
        QVector<Cell> cells;            // a window wall: every glass cell, rows bottom to top (the
                                        // generator splits them by y); a school: the three cells the
                                        // silhouette walks through, nearest to the chest first, then
                                        // optionally the glass cells of "her office" above the
                                        // cabinet; a grave: the mound cell itself (its id = the
                                        // block to give back when the dig is found); a car: the
                                        // garage patch cell first (id = the block under it) and the
                                        // car's core cell second; a bus: the photo point cell; a
                                        // lurker: the four anchor foot cells in the N/E/S/W order
    };
    enum { kEpLamp = 0, kEpBusStop = 1, kEpPlayground = 2, kEpChalk = 3, kEpJack = 4,
           kEpCross = 5, kEpWindows = 6, kEpScarecrow = 7, kEpGrave = 8, kEpBell = 9, kEpSchool = 10,
           kEpCar = 11, kEpBus = 12, kEpLurker = 13 };
    using AddFn = std::function<void (const QString& path, const QByteArray& data)>;   // = writeZip's addEntry
    static void build(const AddFn& add, const QSet<QString>& off, const QVector<Prop>& props);
};

//! The cells of the chalk arrow of a chalk prop in world (x;z) coordinates, the pattern rotated by
//! prop.dir (0=N..3=W, the kDirNames order). Shared by the exporter's bake (it validates the cells)
//! and by EncPack::build (it bakes the draw/restore functions), so the two never disagree.
void encChalkCells(const EncPack::Prop& prop, QPoint out[5]);
