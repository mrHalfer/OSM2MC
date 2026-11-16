#pragma once
// The "Export rules" dialog (GUI only): visual editing of the four reference files the exporter reads at run time —
// osm_rules.json (OSM tags -> tqt layers), default.t3d (the 3D visualisation sources), point_models.csv (an object
// code -> a point structure model) and heights.csv (typical building heights by code). The dialog edits the copies
// it found next to the program (or in the current folder — the same order as the loaders), "Save as..." writes
// anywhere. The next export picks the saved files up on its own, no restart needed.
#include <QDialog>
#include <QVector>

class QTabWidget;
class QLabel;
class RulesTabBase;

class RulesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RulesDialog(QWidget* parent = nullptr);

private:
    void refreshBottom();                       //!< the path label and the "unsaved" star in the tab titles
    void saveCurrentTab();
    void saveCurrentTabAs();
    void reloadCurrentTab();
    bool confirmDiscard();                      //!< asks about unsaved tabs; false — the user chose to stay

    QTabWidget* mTabs = nullptr;
    QLabel* mPathLabel = nullptr;
    QVector<RulesTabBase*> mTabList;
};
