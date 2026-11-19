#pragma once
// The "Export rules" dialog (GUI only): visual editing of the OSM rules of the rules catalog — rules/osmrules.json
// ("OSM tags -> layer and code"). The dialog edits the copy it found in the catalog next to the program (or in the
// current folder — the same order as the loaders), "Save as..." writes anywhere. The next export picks the saved
// file up on its own, no restart needed. A live pane on the right resolves the selected rule's CODE through
// TmcRulesRepo — what the code builds on the layer (family, effect, model, height, material).
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
