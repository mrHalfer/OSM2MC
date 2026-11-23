#pragma once
// Random city events over the exported world (docs/city-encounters.md). THE single list of event ids,
// shared by the exporter (the "osmc_enc" datapack: config scoreboard, tick dispatcher, per-event
// on/off functions, see exportrun.cpp) and by the "City events" window (app/encountersdialog.cpp),
// which repeats the ids in the same order and count — the window holds the translatable titles
// (lupdate only extracts tr() literals, not strings coming from this table). Adding an event means
// adding an id here AND a row in the dialog; a mismatch is reported by the dialog at open time.
extern const char* const kEncIds[];
extern const int kEncCount;
