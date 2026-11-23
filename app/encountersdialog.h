#pragma once
// The "City events" dialog (GUI only, docs/city-encounters.md): the list of random city events with a
// checkbox each — everything is on by default, the window stores which ids the player wants OFF (QSettings
// via MainWindow); the exporter writes the chosen set into the world as the "osmc_enc" datapack config.
// The rows repeat the ids of export/encounters.h in the same order and count; the titles are tr() here,
// lupdate does not see strings coming from the exporter table.
#include <QDialog>
#include <QStringList>
#include <QVector>

class QTreeWidget;

class EncountersDialog : public QDialog
{
    Q_OBJECT
public:
    //! disabled — the ids switched off at the last export (empty — all events on)
    EncountersDialog(QWidget* parent, const QStringList& disabled);

    //! the ids the player left unchecked (empty — every event is on)
    QStringList disabled() const;

private:
    QTreeWidget* mTree = nullptr;
};
