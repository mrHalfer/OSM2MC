#include "rulesdialog.h"
#include <QtWidgets>
#include <functional>
#include "osm/tmcrulesrepo.h"
#include "modelpreview.h"

// The one reference file the dialog edits is the OSM rules of the rules catalog (rules/osmrules.json). The tab keeps
// the whole file as a QJsonObject and re-emit it on save: unknown keys ("_help", "greenery", "neighbors" and any
// future ones) survive untouched; the rule order is kept, the keys inside one object are sorted alphabetically by Qt.
// Condition and field values are shown as compact JSON text, so any form the parser accepts stays editable verbatim.

// ---------------------------------------------------------------- shared helpers

static QString resolveDataFile(const QString& name)
{   // the rules catalog folder: next to the exe, then the current folder; the exe's catalog is the default for "Save as"
    const QString app = QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("rules"))).filePath(name);
    if (QFile::exists(app)) return app;
    const QString cwd = QDir(QDir::current().filePath(QStringLiteral("rules"))).filePath(name);
    if (QFile::exists(cwd)) return cwd;
    return app;
}

static QString jsonToText(const QJsonValue& v)             //!< a value as compact JSON text for a table cell
{
    if (v.isNull()) return QStringLiteral("null");
    // scalars do not form a document on their own — wrap them in an array and strip the brackets
    const QByteArray t = QJsonDocument(QJsonArray() << v).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(t.mid(1, t.size() - 2));
}

static QJsonValue jsonValueFromText(const QString& text)   //!< the reverse: parse, a bare word stays a plain string
{
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray("[") + text.trimmed().toUtf8() + QByteArray("]"), &pe);
    if (pe.error == QJsonParseError::NoError && d.isArray() && !d.array().isEmpty())
        return d.array().at(0);
    return text;
}

static QString cellToText(const QJsonValue& v)     //!< as jsonToText, but a plain string shows without the quotes (jsonValueFromText reads it back)
{
    return v.isString() ? v.toString() : jsonToText(v);
}

static bool writeTextFile(const QString& path, const QByteArray& data, QString* err)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
    f.write(data);
    if (!f.commit()) { if (err) *err = f.errorString(); return false; }
    return true;
}

// Qt's combo boxes change their value on the mouse wheel even without focus, so scrolling a table with the
// cursor over one of its combo cells silently edits the data. The wheel is ignored here and the ignored event
// travels to the parent, i.e. the table underneath scrolls as expected; the value changes by click or keyboard only.
class NoWheelComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

class NoWheelSpinBox : public QSpinBox                              // the same trap: the wheel bumps the value
{
public:
    using QSpinBox::QSpinBox;
protected:
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};

static QTableWidget* makeTable(const QStringList& headers)
{
    QTableWidget* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    return t;
}

static QString cellText(QTableWidget* t, int row, int col)
{
    const QTableWidgetItem* it = t->item(row, col);
    return it ? it->text() : QString();
}

static QHBoxLayout* toolRow(const QList<QPushButton*>& buttons)
{
    QHBoxLayout* l = new QHBoxLayout;
    l->setContentsMargins(0, 0, 0, 0);
    for (QPushButton* b : buttons) l->addWidget(b);
    l->addStretch(1);
    return l;
}

static void swapJson(QJsonArray& a, int i, int j)
{
    const QJsonValue t = a.at(i);
    a[i] = a.at(j);
    a[j] = t;
}

// The figure a ModelPoint number builds (1..9, step 3.575 of export/exportrun.cpp). The dialog captions
// spot codes with it; the ModelPreview widget draws it. Empty for the numbers outside the table.
static QString spotFigureName(int id)
{
    switch (id)
    {
    case 1: return QCoreApplication::translate("RulesDialog", "memorial statue");
    case 2: return QCoreApplication::translate("RulesDialog", "monument stele");
    case 3: return QCoreApplication::translate("RulesDialog", "fountain");
    case 4: return QCoreApplication::translate("RulesDialog", "communication tower");
    case 5: return QCoreApplication::translate("RulesDialog", "composter");
    case 6: return QCoreApplication::translate("RulesDialog", "lighting mast");
    case 7: return QCoreApplication::translate("RulesDialog", "street bench");
    case 8: return QCoreApplication::translate("RulesDialog", "small sculpture");
    case 9: return QCoreApplication::translate("RulesDialog", "bus stop shelter");
    default: return QString();
    }
}

// The layer fields the exporter reads (scene/mcscene.cpp, input/tmcproject.cpp). Both JSON tabs offer them in
// their combos: the "Fields of the layer" table of the rules tab, the model/material columns of the t3d tab.
static QStringList knownLayerFields()
{
    return { QStringLiteral("CODE"), QStringLiteral("REL"), QStringLiteral("MAT"), QStringLiteral("PART"),
             QStringLiteral("PH"), QStringLiteral("MINH"), QStringLiteral("MINL"), QStringLiteral("ROOF"),
             QStringLiteral("ROOFH"), QStringLiteral("ROOFD"), QStringLiteral("ROOFC"), QStringLiteral("ROOFM"),
             QStringLiteral("LIFT"), QStringLiteral("BRG"), QStringLiteral("TUN"), QStringLiteral("LVL"),
             QStringLiteral("LYR"), QStringLiteral("ELEC"), QStringLiteral("STREET"), QStringLiteral("TYSTR"),
             QStringLiteral("NUMBR"), QStringLiteral("ADDNUM") };
}

// ---------------------------------------------------------------- tab base

class RulesTabBase : public QWidget
{
public:
    RulesTabBase(const QString& fileName, const QString& title, std::function<void()> changed)
        : mFileName(fileName), mTitle(title), mChanged(std::move(changed)) {}
    virtual ~RulesTabBase() {}

    virtual bool load(QString* err) = 0;
    virtual bool saveTo(const QString& path, QString* err) = 0;

    QString fileName() const { return mFileName; }
    QString title() const { return mTitle; }
    QString path() const { return mPath; }
    void setPath(const QString& p) { mPath = p; }
    bool loaded() const { return mLoaded; }
    bool dirty() const { return mDirty; }

    void markDirty() { if (!mDirty) { mDirty = true; mChanged(); } }
    void clearDirty() { mDirty = false; mChanged(); }

protected:

    QString mFileName;
    QString mTitle;
    QString mPath;
    bool mLoaded = false;
    bool mDirty = false;
    std::function<void()> mChanged;
    QByteArray mEol = "\n";
};

// ---------------------------------------------------------------- spot structure picker

// Lists every catalog point code whose figure the exporter knows (ModelPoint 1..9, internal codes left
// out) and previews the figure; the rules tab writes the chosen code into the rule's CODE field.
class SpotPickDialog : public QDialog
{
public:
    SpotPickDialog(const QString& curLayer, const QString& curCode, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QCoreApplication::translate("SpotPickDialog", "Spot structures"));
        setModal(true);
        resize(700, 460);
        QHBoxLayout* lay = new QHBoxLayout(this);
        mList = new QListWidget;
        lay->addWidget(mList, 1);
        QVBoxLayout* col = new QVBoxLayout;
        mPreview = new ModelPreview(this);
        col->addWidget(mPreview, 1);
        QHBoxLayout* btns = new QHBoxLayout;
        btns->addStretch(1);
        QPushButton* ok = new QPushButton(QCoreApplication::translate("SpotPickDialog", "Choose"));
        QPushButton* cancel = new QPushButton(QCoreApplication::translate("SpotPickDialog", "Cancel"));
        btns->addWidget(ok);
        btns->addWidget(cancel);
        col->addLayout(btns);
        lay->addLayout(col);

        TmcRulesRepo& repo = TmcRulesRepo::instance();
        for (const QString& name : repo.order())
        {
            const TmcRulesRepo::Layer* la = repo.layer(name);
            if (!la || la->family != TmcRulesRepo::FamilyPoint) continue;
            for (const TmcRulesRepo::Object& ob : la->objects)
            {
                if (ob.internal || ob.model < 1 || ob.model > 9) continue;
                QListWidgetItem* it = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(ob.name).arg(ob.code));
                it->setData(Qt::UserRole, la->name);
                it->setData(Qt::UserRole + 1, ob.model);
                it->setData(Qt::UserRole + 2, ob.name);
                it->setData(Qt::UserRole + 3, ob.code);
                it->setToolTip(QStringLiteral("%1 - %2").arg(spotFigureName(ob.model), la->name));
                mList->addItem(it);
            }
        }
        connect(ok, &QPushButton::clicked, this, [this] { accept(); });
        connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
        connect(mList, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem* it, QListWidgetItem*) { showCurrent(it); });
        connect(mList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { accept(); });
        for (int i = 0; i < mList->count(); ++i)
        {
            QListWidgetItem* it = mList->item(i);
            if (it->data(Qt::UserRole).toString() == curLayer
                && QString::number(it->data(Qt::UserRole + 3).toInt()) == curCode)
            { mList->setCurrentItem(it); break; }
        }
        if (!mList->currentItem() && mList->count())
            mList->setCurrentRow(0);
        showCurrent(mList->currentItem());
        ok->setEnabled(mList->count() > 0);
    }

    QString pickedLayer() const { return mList->currentItem() ? mList->currentItem()->data(Qt::UserRole).toString() : QString(); }
    int pickedCode() const { return mList->currentItem() ? mList->currentItem()->data(Qt::UserRole + 3).toInt() : 0; }

private:
    void showCurrent(QListWidgetItem* it)
    {
        if (!it) return;
        const int model = it->data(Qt::UserRole + 1).toInt();
        mPreview->showModel(model, QStringLiteral("%1 - %2").arg(it->data(Qt::UserRole + 2).toString(),
                                                                 spotFigureName(model)));
    }

    QListWidget* mList = nullptr;
    ModelPreview* mPreview = nullptr;
};


// ---------------------------------------------------------------- rules/osmrules.json


// Qt compares tree items by QTreeWidgetItem::operator<, which takes the DisplayRole strings ("10" before "9").
// The rule list wants the # column numeric; the text columns compare case-insensitively.
class RuleTreeItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (col == 0)
            return data(0, Qt::UserRole).toLongLong() < other.data(0, Qt::UserRole).toLongLong();
        return data(col, Qt::DisplayRole).toString().compare(other.data(col, Qt::DisplayRole).toString(), Qt::CaseInsensitive) < 0;
    }
};

class OsmRulesTab : public RulesTabBase
{
public:
    explicit OsmRulesTab(std::function<void()> changed)
        : RulesTabBase(QStringLiteral("osmrules.json"), QCoreApplication::translate("RulesDialog", "OSM rules"), std::move(changed))
    {
        QSplitter* split = new QSplitter(Qt::Horizontal);

        // the rules table: number, the OSM source, the export layer, the comment
        QWidget* left = new QWidget;
        QVBoxLayout* leftLay = new QVBoxLayout(left);
        leftLay->setContentsMargins(0, 0, 0, 0);
        mFilter = new QLineEdit;
        mFilter->setPlaceholderText(QCoreApplication::translate("RulesDialog", "Filter (a word from a line)"));
        mFilter->setClearButtonEnabled(true);
        leftLay->addWidget(mFilter);
        mTree = new QTreeWidget;
        mTree->setHeaderLabels({ QStringLiteral("#"), QCoreApplication::translate("RulesDialog", "Layer OSM"), QCoreApplication::translate("RulesDialog", "Layer OSMC"), QCoreApplication::translate("RulesDialog", "Comment") });
        mTree->setRootIsDecorated(false);
        mTree->setSelectionBehavior(QAbstractItemView::SelectRows);
        mTree->setSelectionMode(QAbstractItemView::SingleSelection);
        mTree->setUniformRowHeights(true);
        mTree->setSortingEnabled(true);                        // the header sorts only the view; the rule ORDER (priority) is kept in mRules
        mTree->header()->setSortIndicator(0, Qt::AscendingOrder);   // the # column starts ascending (a click on the header may change it)
        mTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        mTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        mTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        leftLay->addWidget(mTree, 1);
        mAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add"));
        mDup = new QPushButton(QCoreApplication::translate("RulesDialog", "Duplicate"));
        mDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Delete"));
        mUp = new QPushButton(QCoreApplication::translate("RulesDialog", "Up"));
        mDown = new QPushButton(QCoreApplication::translate("RulesDialog", "Down"));
        leftLay->addLayout(toolRow({ mAdd, mDup, mDel, mUp, mDown }));
        split->addWidget(left);

        // the detail of the selected rule
        QWidget* right = new QWidget;
        QVBoxLayout* rightLay = new QVBoxLayout(right);
        rightLay->setContentsMargins(0, 0, 0, 0);
        QGridLayout* form = new QGridLayout;
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Comment")), 0, 0, Qt::AlignTop);
        mNote = new QPlainTextEdit;
        mNote->setFixedHeight(58);
        form->addWidget(mNote, 0, 1, 1, 3);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "From")), 1, 0);
        mFrom = new NoWheelComboBox;
        mFrom->addItems({ QStringLiteral("multipolygons"), QStringLiteral("lines"), QStringLiteral("points") });
        form->addWidget(mFrom, 1, 1);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Layer")), 1, 2);
        mLayer = new NoWheelComboBox;
        mLayer->setEditable(true);
        mLayer->addItems({ QStringLiteral("hdrlin"), QStringLiteral("rdslin"), QStringLiteral("rrslin"),
                           QStringLiteral("bldpol"), QStringLiteral("blkpol"), QStringLiteral("csrpol"),
                           QStringLiteral("csrpnt"), QStringLiteral("csrlin"), QStringLiteral("hdtpnt"),
                           QStringLiteral("rdcpnt"), QStringLiteral("rdcpol"), QStringLiteral("hdrpol"),
                           QStringLiteral("vegpol"), QStringLiteral("mrspol"), QStringLiteral("lcapol"),
                           QStringLiteral("terpol"), QStringLiteral("vgbpol"), QStringLiteral("-") });
        form->addWidget(mLayer, 1, 3);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "As")), 2, 0);
        mAs = new NoWheelComboBox;
        mAs->setToolTip(QCoreApplication::translate("RulesDialog", "What the exporter builds from the matched object; the numbers are meters"));
        fillAsChoices(QStringLiteral("multipolygons"), QString());
        QHBoxLayout* asRow = new QHBoxLayout;
        asRow->setContentsMargins(0, 0, 0, 0);
        asRow->addWidget(mAs, 1);
        mAsSize = new NoWheelSpinBox;
        mAsSize->setRange(0, 200);
        mAsSize->setSpecialValueText(QCoreApplication::translate("RulesDialog", "no footprint"));
        mAsSize->setSuffix(QStringLiteral(" m"));
        mAsSize->setToolTip(QCoreApplication::translate("RulesDialog", "Size in meters: the side of the structure footprint (\"no footprint\" leaves the bare point), the length of the crosswalk"));
        mAsSize->setVisible(false);
        asRow->addWidget(mAsSize);
        form->addLayout(asRow, 2, 1);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Pass")), 2, 2);
        mPass = new NoWheelSpinBox;
        mPass->setRange(1, 2);
        mPass->setToolTip(QCoreApplication::translate("RulesDialog", "2 — a guess for objects not recognized on pass 1 (they see the nb:* tags about neighbors)"));
        form->addWidget(mPass, 2, 3);
        mCont = new QCheckBox(QCoreApplication::translate("RulesDialog", "Continue (the object may also match the rules below)"));
        form->addWidget(mCont, 3, 0, 1, 3);
        mSpot = new QPushButton(QCoreApplication::translate("RulesDialog", "Model..."));
        mSpot->setToolTip(QCoreApplication::translate("RulesDialog", "Choose a catalog spot structure for this point rule - the CODE is set (and the layer too when the structure lives on another one)"));
        mSpot->setEnabled(false);
        form->addWidget(mSpot, 3, 3);
        rightLay->addLayout(form);

        rightLay->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Conditions — every tag must match; an empty condition means \"the tag exists\"")));
        mWhen = makeTable({ QCoreApplication::translate("RulesDialog", "Tag"), QCoreApplication::translate("RulesDialog", "Condition (JSON: \"yes\", [\"a\",\"b\"], \"!a\", \">=3\", \"~regex\", \"*\", \"-\")") });
        rightLay->addWidget(mWhen, 1);
        mWhenAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add row"));
        mWhenDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Remove row"));
        rightLay->addLayout(toolRow({ mWhenAdd, mWhenDel }));

        rightLay->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Fields of the layer — a value or a {tag} template")));
        mFields = makeTable({ QCoreApplication::translate("RulesDialog", "Field"), QCoreApplication::translate("RulesDialog", "Value or template") });
        rightLay->addWidget(mFields, 1);
        mFieldHelp = new QPlainTextEdit;                       // the purpose of the clicked field, roomy enough to read
        mFieldHelp->setReadOnly(true);
        mFieldHelp->setFixedHeight(58);
        mFieldHelp->setTabChangesFocus(true);
        rightLay->addWidget(mFieldHelp);
        mFieldsAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add row"));
        mFieldsDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Remove row"));
        mRelAuto = new QPushButton(QCoreApplication::translate("RulesDialog", "REL = \"auto\""));
        mRelAuto->setToolTip(QCoreApplication::translate("RulesDialog", "auto - the height is taken from the OSM tags (height, building:levels, min_height), "
                               "then from the typical height of the code in the rules catalog; a fixed floor count can be written instead"));
        rightLay->addLayout(toolRow({ mFieldsAdd, mFieldsDel, mRelAuto }));
        split->addWidget(right);

        // what the selected rule builds: the CODE resolved in the rules catalog, live
        QWidget* cat = new QWidget;
        QVBoxLayout* catLay = new QVBoxLayout(cat);
        catLay->setContentsMargins(0, 0, 0, 0);
        catLay->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Resolution in the rules catalog")));
        mCatalog = new QTextBrowser;
        mCatalog->setOpenExternalLinks(false);
        mPanePreview = new ModelPreview(cat, 150, 190);          // the figure of a spot code, live
        mPanePreview->setVisible(false);
        catLay->addWidget(mPanePreview);
        catLay->addWidget(mCatalog, 1);
        split->addWidget(cat);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 3);
        split->setStretchFactor(2, 2);
        split->setSizes({ 460, 640, 300 });

        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        lay->addWidget(split, 1);

        connect(mTree, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem* it, QTreeWidgetItem*) { selectRule(it ? it->data(0, Qt::UserRole).toInt() : -1); });
        connect(mFilter, &QLineEdit::textChanged, this, [this] { applyFilter(); });
        connect(mAdd, &QPushButton::clicked, this, [this] { addRule(false); });
        connect(mDup, &QPushButton::clicked, this, [this] { addRule(true); });
        connect(mDel, &QPushButton::clicked, this, [this] { deleteRule(); });
        connect(mUp, &QPushButton::clicked, this, [this] { moveRule(-1); });
        connect(mDown, &QPushButton::clicked, this, [this] { moveRule(1); });
        connect(mNote, &QPlainTextEdit::textChanged, this, [this] { commitWidgets(); });   // selectRule fills it under mFilling
        connect(mFrom, QOverload<int>::of(&QComboBox::activated), this, [this]
                {
                    commitWidgets();
                    refillAsChoices();                           // the offered "as" values follow the source geometry
                    commitWidgets();
                });
        connect(mLayer, &QComboBox::editTextChanged, this, [this] { if (!mFilling) commitWidgets(); });
        connect(mAs, QOverload<int>::of(&QComboBox::activated), this, [this] { commitWidgets(); updateAsSizeVisible(); });
        connect(mAsSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { if (!mFilling) commitWidgets(); });
        connect(mSpot, &QPushButton::clicked, this, [this] { pickSpot(); });
        connect(mPass, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { if (!mFilling) commitWidgets(); });
        connect(mCont, &QCheckBox::toggled, this, [this] { if (!mFilling) commitWidgets(); });
        connect(mWhen, &QTableWidget::cellChanged, this, [this] { if (!mFilling) commitTables(); });
        connect(mWhen->model(), &QAbstractItemModel::rowsRemoved, this, [this] { if (!mFilling) commitTables(); });
        connect(mFields, &QTableWidget::cellChanged, this, [this] { if (!mFilling) commitTables(); });
        connect(mFields, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int, int) { if (!mFilling) showFieldHelp(row); });
        connect(mFields->model(), &QAbstractItemModel::rowsRemoved, this, [this]
                {
                    if (mFilling) return;
                    commitTables();
                    mFilling = true;
                    fillFields(ruleAt(mCur));                   // reindex the combo boxes after a row removal
                    mFilling = false;
                    showFieldHelp(mFields->currentRow());
                });
        connect(mWhenAdd, &QPushButton::clicked, this, [this] { mWhen->insertRow(mWhen->rowCount()); });
        connect(mWhenDel, &QPushButton::clicked, this, [this] { if (mWhen->currentRow() >= 0) mWhen->removeRow(mWhen->currentRow()); });
        connect(mFieldsAdd, &QPushButton::clicked, this, [this] { addFieldRow(); });
        connect(mFieldsDel, &QPushButton::clicked, this, [this] { if (mFields->currentRow() >= 0) mFields->removeRow(mFields->currentRow()); });
        connect(mRelAuto, &QPushButton::clicked, this, [this] { setRelAuto(); });

        setEnabledDepth(false);
    }

    bool load(QString* err) override
    {
        QFile f(mPath);
        if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); mLoaded = false; return false; }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (doc.isNull() || !doc.isObject())
        {
            if (err) *err = QCoreApplication::translate("RulesDialog", "JSON error (%1, position %2)").arg(pe.errorString()).arg(pe.offset);
            mLoaded = false;
            return false;
        }
        mRoot = doc.object();
        mRules = mRoot.value(QStringLiteral("rules")).toArray();
        mLoaded = true;
        rebuildList(0);
        return true;
    }

    bool saveTo(const QString& path, QString* err) override
    {
        if (!mLoaded) { if (err) *err = QCoreApplication::translate("RulesDialog", "the file was not loaded — nothing to save"); return false; }
        mRoot[QStringLiteral("rules")] = mRules;
        return writeTextFile(path, QJsonDocument(mRoot).toJson(QJsonDocument::Indented), err);
    }

private:
    QJsonObject ruleAt(int i) const { return mRules.at(i).toObject(); }

    static QString fieldPurpose(const QString& name)
    {
        if (name == QLatin1String("CODE")) return QCoreApplication::translate("RulesDialog", "the classifier code: what the object is (drives blocks, roads, vegetation)");
        if (name == QLatin1String("REL")) return QCoreApplication::translate("RulesDialog", "the building height in floors; \"auto\" - from the OSM tags, then from the typical height of the code in the rules catalog");
        if (name == QLatin1String("MAT")) return QCoreApplication::translate("RulesDialog", "the building facade material");
        if (name == QLatin1String("PART")) return QCoreApplication::translate("RulesDialog", "the building:part marker (the outline is a fragment of a building)");
        if (name == QLatin1String("PH")) return QCoreApplication::translate("RulesDialog", "helper: the part's own height ({height}||{building:levels}); an empty PH lets the part inherit the host building height");
        if (name == QLatin1String("MINH")) return QCoreApplication::translate("RulesDialog", "the lowest height of a building part, meters (min_height)");
        if (name == QLatin1String("MINL")) return QCoreApplication::translate("RulesDialog", "the lowest floor of a building part (min_level)");
        if (name == QLatin1String("ROOF")) return QCoreApplication::translate("RulesDialog", "the roof kind: \"onion\" (a church dome) or \"skillion\" (one-slope)");
        if (name == QLatin1String("ROOFH")) return QCoreApplication::translate("RulesDialog", "the onion/skillion roof height, meters");
        if (name == QLatin1String("ROOFD")) return QCoreApplication::translate("RulesDialog", "the skillion roof direction, degrees");
        if (name == QLatin1String("ROOFC")) return QCoreApplication::translate("RulesDialog", "the church dome color (a name known to the exporter)");
        if (name == QLatin1String("ROOFM")) return QCoreApplication::translate("RulesDialog", "the church dome material");
        if (name == QLatin1String("LIFT")) return QCoreApplication::translate("RulesDialog", "1 - the building has an elevator shaft (1-block inner walls)");
        if (name == QLatin1String("BRG")) return QCoreApplication::translate("RulesDialog", "the OSM bridge: not empty and not \"no\" - an elevated roadway");
        if (name == QLatin1String("TUN")) return QCoreApplication::translate("RulesDialog", "the OSM tunnel: not empty and not \"no\" - an underground roadway");
        if (name == QLatin1String("LVL")) return QCoreApplication::translate("RulesDialog", "the OSM level - a junction height level");
        if (name == QLatin1String("LYR")) return QCoreApplication::translate("RulesDialog", "the OSM layer - which of the overlapping lines stands on top");
        if (name == QLatin1String("ELEC")) return QCoreApplication::translate("RulesDialog", "not empty and not \"no\" - the railway has a contact wire");
        if (name == QLatin1String("STREET")) return QCoreApplication::translate("RulesDialog", "the street name of the address plate (usually filled automatically from addr:*)");
        if (name == QLatin1String("TYSTR")) return QCoreApplication::translate("RulesDialog", "the street type word of the address (\"street\", \"avenue\", ...)");
        if (name == QLatin1String("NUMBR")) return QCoreApplication::translate("RulesDialog", "the house number of the address plate (usually filled automatically from addr:*)");
        if (name == QLatin1String("ADDNUM")) return QCoreApplication::translate("RulesDialog", "the second half of the house number (the plate shows NUMBR+ADDNUM)");
        return QCoreApplication::translate("RulesDialog", "a custom column - it can be read by the 3D settings (their Field/Lift field/Absorption columns)");
    }

    int currentRule() const
    {
        QTreeWidgetItem* it = mTree->currentItem();
        return it ? it->data(0, Qt::UserRole).toInt() : -1;
    }

    QTreeWidgetItem* itemForRule(int idx) const
    {
        if (idx < 0) return nullptr;
        for (int r = 0; r < mTree->topLevelItemCount(); ++r)
            if (mTree->topLevelItem(r)->data(0, Qt::UserRole).toInt() == idx) return mTree->topLevelItem(r);
        return nullptr;
    }

    void rebuildList(int selectRuleIdx)
    {
        const QString filter = mFilter->text().trimmed();
        const int keep = currentRule();
        mTree->setSortingEnabled(false);                       // fill without a resort per item, one sort at the end
        mTree->blockSignals(true);
        mTree->clear();
        for (int i = 0; i < mRules.size(); ++i)
        {
            const QJsonObject o = ruleAt(i);
            QTreeWidgetItem* it = new RuleTreeItem(QStringList()
                            << QString::number(i + 1)
                            << o.value(QStringLiteral("from")).toString(QStringLiteral("multipolygons"))
                            << o.value(QStringLiteral("layer")).toString()
                            << o.value(QStringLiteral("note")).toString());
            it->setData(0, Qt::UserRole, i);                   // the numeric key of the # column (RuleTreeItem::operator<)
            it->setHidden(!filter.isEmpty() && !itemMatches(it, filter));
            mTree->addTopLevelItem(it);
        }
        mTree->blockSignals(false);
        mTree->sortByColumn(0, Qt::AscendingOrder);                // the # column always ascends after a rebuild (a header click may set any order)
        mTree->setSortingEnabled(true);
        selectRuleIdx = selectRuleIdx >= 0 ? selectRuleIdx : keep;
        if (QTreeWidgetItem* it = itemForRule(selectRuleIdx))
            mTree->setCurrentItem(it);                           // fires currentItemChanged -> selectRule
        else
            selectRule(-1);
    }

    static bool itemMatches(QTreeWidgetItem* it, const QString& filter)
    {
        for (int c = 1; c <= 3; ++c)
            if (it->text(c).contains(filter, Qt::CaseInsensitive)) return true;
        return false;
    }

    void applyFilter()
    {
        const QString filter = mFilter->text().trimmed();
        for (int r = 0; r < mTree->topLevelItemCount(); ++r)
        {
            QTreeWidgetItem* it = mTree->topLevelItem(r);
            it->setHidden(!filter.isEmpty() && !itemMatches(it, filter));
        }
    }

    void setEnabledDepth(bool on)
    {
        QWidget* ws[] = { mNote, mFrom, mLayer, mAs, mAsSize, mPass, mCont, mWhen, mFields,
                          mWhenAdd, mWhenDel, mFieldsAdd, mFieldsDel, mRelAuto };
        for (QWidget* w : ws) w->setEnabled(on);
    }

    void selectRule(int idx)
    {
        mCur = idx;
        setEnabledDepth(idx >= 0);
        mFilling = true;
        if (idx >= 0)
        {
            const QJsonObject o = ruleAt(idx);
            mNote->setPlainText(o.value(QStringLiteral("note")).toString());
            mFrom->setCurrentText(o.value(QStringLiteral("from")).toString(QStringLiteral("multipolygons")));
            mLayer->setEditText(o.value(QStringLiteral("layer")).toString());
            fillAsChoices(o.value(QStringLiteral("from")).toString(QStringLiteral("multipolygons")),
                          o.value(QStringLiteral("as")).toString());
            mPass->setValue(o.value(QStringLiteral("pass")).toInt(1));
            mCont->setChecked(o.value(QStringLiteral("continue")).toBool(false));
            fillTable(mWhen, o.value(QStringLiteral("when")).toObject());
            fillFields(o.value(QStringLiteral("fields")).toObject());
        }
        else
        {
            fillTable(mWhen, QJsonObject());
            fillFields(QJsonObject());
        }
        mFilling = false;
        showFieldHelp(mFields->currentRow());
        updateCatalogHint();
    }

    static void fillTable(QTableWidget* t, const QJsonObject& o)
    {
        t->blockSignals(true);
        t->setRowCount(0);
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        {
            const int r = t->rowCount();
            t->insertRow(r);
            t->setItem(r, 0, new QTableWidgetItem(it.key()));
            t->setItem(r, 1, new QTableWidgetItem(cellToText(it.value())));
        }
        t->blockSignals(false);
    }

    void fillFields(const QJsonObject& o)          //!< the caller sets mFilling around this
    {
        mFields->blockSignals(true);
        mFields->setRowCount(0);
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        {
            const int r = mFields->rowCount();
            mFields->insertRow(r);
            QComboBox* cb = makeFieldCombo();
            cb->setEditText(it.key());
            cb->setToolTip(fieldPurpose(it.key()));
            mFields->setCellWidget(r, 0, cb);
            mFields->setItem(r, 1, new QTableWidgetItem(cellToText(it.value())));
        }
        mFields->blockSignals(false);
    }

    QComboBox* makeFieldCombo()
    {
        QComboBox* cb = new NoWheelComboBox;
        cb->setEditable(true);
        cb->addItems(knownLayerFields());
        connect(cb, &QComboBox::currentTextChanged, this,
                [this](const QString&) { fieldEdited(qobject_cast<QComboBox*>(sender())); });
        return cb;
    }

    void showFieldHelp(int r)                      //!< the purpose pane under the Fields table
    {
        const QComboBox* cb = r >= 0 ? qobject_cast<const QComboBox*>(mFields->cellWidget(r, 0)) : nullptr;
        const QString f = cb ? cb->currentText().trimmed().toUpper() : QString();
        mFieldHelp->setPlainText(f.isEmpty()
            ? QCoreApplication::translate("RulesDialog", "Click a field row - what the field does shows here")
            : fieldPurpose(f));
        mFieldHelp->moveCursor(QTextCursor::Start);
    }

    void fieldEdited(QComboBox* cb)
    {
        if (mFilling || !cb) return;
        int r = -1;
        for (int i = 0; i < mFields->rowCount(); ++i)           // Qt5 has no QTableWidget::indexFromWidget
            if (mFields->cellWidget(i, 0) == cb) { r = i; break; }
        if (r < 0) return;
        const QString f = cb->currentText().trimmed().toUpper();
        cb->setToolTip(fieldPurpose(f));
        showFieldHelp(r);
        if (f == QLatin1String("REL"))
        {   // "auto" is the default height for buildings
            if (!mFields->item(r, 1)) mFields->setItem(r, 1, new QTableWidgetItem);
            if (mFields->item(r, 1)->text().trimmed().isEmpty())
                mFields->item(r, 1)->setText(QStringLiteral("auto"));
        }
        commitTables();
    }

    void addFieldRow()
    {
        const int r = mFields->rowCount();
        mFields->insertRow(r);
        mFields->setCellWidget(r, 0, makeFieldCombo());
        mFields->setCurrentCell(r, 0);
    }

    void setRelAuto()
    {
        if (mCur < 0) return;
        int r = -1;
        for (int i = 0; i < mFields->rowCount(); ++i)
        {
            QComboBox* cb = qobject_cast<QComboBox*>(mFields->cellWidget(i, 0));
            if (cb && cb->currentText().trimmed().toUpper() == QLatin1String("REL")) { r = i; break; }
        }
        if (r < 0)
        {
            addFieldRow();
            r = mFields->rowCount() - 1;
            QComboBox* cb = qobject_cast<QComboBox*>(mFields->cellWidget(r, 0));
            mFilling = true;                                     // the row is being filled, setRelAuto commits once below
            if (cb) cb->setEditText(QStringLiteral("REL"));
            mFilling = false;
            showFieldHelp(r);
        }
        if (!mFields->item(r, 1)) mFields->setItem(r, 1, new QTableWidgetItem);
        mFields->item(r, 1)->setText(QStringLiteral("auto"));    // cellChanged -> commitTables
    }

    void commitWidgets()                                 //!< the top form -> the rule object
    {
        if (mFilling || mCur < 0) return;
        QJsonObject o = ruleAt(mCur);
        putKey(o, QStringLiteral("note"), mNote->toPlainText().replace(QLatin1Char('\n'), QLatin1Char(' ')).trimmed());   // the note stays a single line in the JSON
        o[QStringLiteral("from")] = mFrom->currentText().trimmed().toLower();
        o[QStringLiteral("layer")] = mLayer->currentText().trimmed().toLower();
        const QString as = asValue();
        if (as.isEmpty()) o.remove(QStringLiteral("as")); else o[QStringLiteral("as")] = as;
        if (mPass->value() == 1) o.remove(QStringLiteral("pass")); else o[QStringLiteral("pass")] = mPass->value();
        if (!mCont->isChecked()) o.remove(QStringLiteral("continue")); else o[QStringLiteral("continue")] = true;
        storeRule(o);
    }

    void commitTables()                                  //!< the two tables -> the rule object
    {
        if (mFilling || mCur < 0) return;
        QJsonObject o = ruleAt(mCur);
        QJsonObject when;
        for (int r = 0; r < mWhen->rowCount(); ++r)
        {
            const QString key = cellText(mWhen, r, 0).trimmed();
            if (key.isEmpty()) continue;
            const QString cond = cellText(mWhen, r, 1).trimmed();
            when[key] = cond.isEmpty() ? QJsonValue(QStringLiteral("*")) : jsonValueFromText(cond);
        }
        if (when.isEmpty()) o.remove(QStringLiteral("when")); else o[QStringLiteral("when")] = when;
        QJsonObject fields;
        for (int r = 0; r < mFields->rowCount(); ++r)
        {
            QComboBox* cb = qobject_cast<QComboBox*>(mFields->cellWidget(r, 0));
            const QString key = (cb ? cb->currentText() : cellText(mFields, r, 0)).trimmed().toUpper();
            if (key.isEmpty()) continue;
            fields[key] = jsonValueFromText(cellText(mFields, r, 1));
        }
        if (fields.isEmpty()) o.remove(QStringLiteral("fields")); else o[QStringLiteral("fields")] = fields;
        storeRule(o);
    }

    static void putKey(QJsonObject& o, const QString& key, const QString& value)
    {
        if (value.isEmpty()) o.remove(key); else o[key] = value;
    }

    void storeRule(const QJsonObject& o)
    {
        mRules[mCur] = o;
        if (QTreeWidgetItem* it = itemForRule(mCur))
        {
            mTree->setSortingEnabled(false);                   // one resort per commit, not one per keystroke
            it->setText(1, o.value(QStringLiteral("from")).toString(QStringLiteral("multipolygons")));
            it->setText(2, o.value(QStringLiteral("layer")).toString());
            it->setText(3, o.value(QStringLiteral("note")).toString());
            mTree->setSortingEnabled(true);
        }
        updateCatalogHint();
        markDirty();
    }

    void addRule(bool duplicate)
    {
        QJsonObject o;
        if (duplicate && mCur >= 0) o = ruleAt(mCur);
        else
        {
            o[QStringLiteral("from")] = QStringLiteral("multipolygons");
            o[QStringLiteral("layer")] = QStringLiteral("bldpol");
            QJsonObject w;
            w[QStringLiteral("building")] = QStringLiteral("*");
            o[QStringLiteral("when")] = w;
        }
        const int at = mCur >= 0 ? mCur + 1 : mRules.size();
        mRules.insert(at, o);
        rebuildList(at);
        markDirty();
    }

    void deleteRule()
    {
        if (mCur < 0) return;
        const int next = mCur >= mRules.size() - 1 ? mCur - 1 : mCur;
        mRules.removeAt(mCur);
        rebuildList(next);
        markDirty();
    }

    void moveRule(int dir)
    {
        if (mCur < 0) return;
        const int to = mCur + dir;
        if (to < 0 || to >= mRules.size()) return;
        swapJson(mRules, mCur, to);
        rebuildList(to);
        markDirty();
    }

    // The "As" combo is not editable and offers only what the source geometry can build. The item data
    // holds the kind ("struct"/"crosswalk" add the meters of mAsSize, 0 m leaves a bare point; "" = as
    // is, "polygon", "line"). A point is always a small structure or a crossing - the old "square:N"
    // form is only the footprint size of a structure, so the two share one item.
    void fillAsChoices(const QString& from, const QString& curAs)
    {
        QString kind;                                              // the item to select
        int size = 0;
        bool ok = false;
        if (curAs.isEmpty())
            kind = QStringLiteral("struct");
        else if (curAs.startsWith(QLatin1String("square:")))
            { kind = QStringLiteral("struct"); size = curAs.mid(7).toInt(&ok); }
        else if (curAs.startsWith(QLatin1String("crosswalk:")))
            { kind = QStringLiteral("crosswalk"); size = curAs.mid(10).toInt(&ok); }
        else
            kind = curAs;                                          // "polygon", "line" or an odd legacy value
        mAs->blockSignals(true);
        mAs->clear();
        if (from == QLatin1String("points"))
        {
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Structure"), QStringLiteral("struct"));
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Crosswalk"), QStringLiteral("crosswalk"));
        }
        else if (from == QLatin1String("lines"))
        {
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Line (as is)"), QString());
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Polygon (a closed line)"), QStringLiteral("polygon"));
        }
        else
        {
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Polygon (as is)"), QString());
            mAs->addItem(QCoreApplication::translate("RulesDialog", "Line (a polygon boundary)"), QStringLiteral("line"));
        }
        int at = mAs->findData(kind);
        if (at < 0 && !curAs.isEmpty())
        {   // something the old editable combo could type - keep it verbatim, the rule must not lose it
            mAs->addItem(QStringLiteral("\"%1\"").arg(curAs), curAs);
            at = mAs->count() - 1;
        }
        mAs->setCurrentIndex(qMax(0, at));
        if (mAsSize && at >= 0)
            mAsSize->setValue(ok ? qBound(mAsSize->minimum(), size, mAsSize->maximum()) : 0);
        mAs->blockSignals(false);
        updateAsSizeVisible();
    }

    void refillAsChoices()                                 //!< the offered values follow the "From" geometry
    {
        fillAsChoices(mFrom->currentText().trimmed().toLower(), asValue());
    }

    QString asValue() const                                //!< the combo + the size spin -> the JSON "as"
    {
        const QString kind = mAs->currentData().toString();
        if (kind == QLatin1String("struct"))
            return mAsSize->value() <= 0 ? QString() : QStringLiteral("square:%1").arg(mAsSize->value());
        if (kind == QLatin1String("crosswalk"))
            return QStringLiteral("crosswalk:%1").arg(qMax(1, mAsSize->value()));
        return kind;
    }

    void updateAsSizeVisible()
    {
        if (!mAsSize) return;                                  // the first fillAsChoices runs before the spin exists
        const QString kind = mAs->currentData().toString();
        mAsSize->setVisible(kind == QLatin1String("struct") || kind == QLatin1String("crosswalk"));
    }

    void pickSpot()                                          //!< the "Model..." button: the catalog spot zoo
    {
        SpotPickDialog dlg(mLayer->currentText().trimmed().toLower(), currentFieldCode(), this);
        if (dlg.exec() != QDialog::Accepted) return;
        const int code = dlg.pickedCode();
        if (!code) return;
        const QString layer = dlg.pickedLayer();
        if (!layer.isEmpty() && layer != mLayer->currentText().trimmed().toLower())
            mLayer->setEditText(layer);                        // editTextChanged -> commitWidgets
        setFieldCode(QString::number(code));                   // cellChanged -> commitTables
        updateCatalogHint();                                   // the CODE may have stayed the same -> no cellChanged
    }

    void setFieldCode(const QString& value)                   //!< the CODE row of the fields table, added when absent
    {
        int r = -1;
        for (int i = 0; i < mFields->rowCount(); ++i)
        {
            QComboBox* cb = qobject_cast<QComboBox*>(mFields->cellWidget(i, 0));
            if (cb && cb->currentText().trimmed().toUpper() == QLatin1String("CODE")) { r = i; break; }
        }
        if (r < 0)
        {
            addFieldRow();
            r = mFields->rowCount() - 1;
            QComboBox* cb = qobject_cast<QComboBox*>(mFields->cellWidget(r, 0));
            mFilling = true;                                   // the row is being filled, commitTables runs once below
            if (cb) cb->setEditText(QStringLiteral("CODE"));
            mFilling = false;
            showFieldHelp(r);
        }
        if (!mFields->item(r, 1)) mFields->setItem(r, 1, new QTableWidgetItem);
        mFields->item(r, 1)->setText(value);
    }

    QString currentFieldCode() const
    {
        for (int r = 0; r < mFields->rowCount(); ++r)
        {
            const QComboBox* cb = qobject_cast<const QComboBox*>(mFields->cellWidget(r, 0));
            if (cb && cb->currentText().trimmed().toUpper() == QLatin1String("CODE"))
                return cellText(mFields, r, 1).trimmed();
        }
        return QString();
    }

    void updateCatalogHint()                               //!< the live "what does this code build" pane
    {
        TmcRulesRepo& repo = TmcRulesRepo::instance();
        if (!repo.isLoaded())
        {   // the dialog may open before the first export or preview — nobody has read the catalog yet
            const QString dir = TmcRulesRepo::discover();
            if (!dir.isEmpty())
            {
                QString e;
                repo.load(dir, &e);
            }
        }
        QString html;
        mPanePreview->setVisible(false);
        const QJsonObject o = mCur >= 0 ? ruleAt(mCur) : QJsonObject();
        const QString layer = o.value(QStringLiteral("layer")).toString().trimmed().toLower();
        if (mCur < 0)
            html = QCoreApplication::translate("RulesDialog", "Select a rule - what its code builds on the layer shows here");
        else if (layer.isEmpty())
            html = QCoreApplication::translate("RulesDialog", "The rule has no layer - it matches nothing");
        else if (layer == QLatin1String("-"))
            html = QCoreApplication::translate("RulesDialog", "The object is dropped by this rule (the rules below are not checked)");
        else if (!repo.isLoaded())
            html = QCoreApplication::translate("RulesDialog", "The rules catalog was not found next to the program");
        else
        {   // the CODE being typed lives in the fields table, not in the committed rule yet
            QString code;
            for (int r = 0; r < mFields->rowCount(); ++r)
            {
                const QComboBox* cb = qobject_cast<const QComboBox*>(mFields->cellWidget(r, 0));
                if (cb && cb->currentText().trimmed().toUpper() == QLatin1String("CODE"))
                { code = cellText(mFields, r, 1).trimmed(); break; }
            }
            if (code.isEmpty())
                html = QCoreApplication::translate("RulesDialog", "The rule sets no CODE - the exporter falls back to the generic behavior of the layer");
            else if (code.contains(QLatin1Char('{')))
                html = QCoreApplication::translate("RulesDialog", "The code comes from the tags - it resolves per object, not per rule");
            else
            {
                bool ok = false;
                const int value = code.toInt(&ok);
                const TmcRulesRepo::Layer* lay = repo.layer(layer);
                if (!ok)
                    html = QCoreApplication::translate("RulesDialog", "\"%1\" is not a code number").arg(code.toHtmlEscaped());
                else if (!lay)
                    html = QCoreApplication::translate("RulesDialog", "Layer \"%1\" is not in the catalog - the exporter warns and falls back to the generic behavior").arg(layer.toHtmlEscaped());
                else if (const TmcRulesRepo::Object* ob = repo.object(layer, value))
                {
                    html = catalogHtml(*lay, *ob);
                    if (lay->family == TmcRulesRepo::FamilyPoint && ob->model >= 1 && ob->model <= 9)
                    {
                        mPanePreview->setVisible(true);
                        mPanePreview->showModel(ob->model, ob->name);
                    }
                }
                else if (const TmcRulesRepo::Object* other = repo.objectByCode(value))
                    html = QCoreApplication::translate("RulesDialog", "This code is cataloged as \"%1\" on another layer - here it is unknown (generic behavior + a warning)").arg(other->name.toHtmlEscaped());
                else
                    html = QCoreApplication::translate("RulesDialog", "Unknown code - the exporter warns and the object keeps the generic behavior of \"%1\"").arg(layer.toHtmlEscaped());
            }
        }
        mCatalog->setHtml(QStringLiteral("<div>%1</div>").arg(html));
        const QString asVal = o.value(QStringLiteral("as")).toString();
        mSpot->setEnabled(mCur >= 0 && repo.isLoaded()
                          && o.value(QStringLiteral("from")).toString() == QLatin1String("points")
                          && !asVal.startsWith(QLatin1String("crosswalk:"))
                          && repo.layer(layer) && repo.layer(layer)->family == TmcRulesRepo::FamilyPoint);
    }

    static QString catalogHtml(const TmcRulesRepo::Layer& lay, const TmcRulesRepo::Object& ob)
    {
        QString html = QStringLiteral("<b>%1</b><br>code %2").arg(ob.name.toHtmlEscaped()).arg(ob.code);
        const char* family = lay.family == TmcRulesRepo::FamilyLine ? "line"
                           : lay.family == TmcRulesRepo::FamilyPoint ? "point" : "polygon";
        html += QStringLiteral("<br>%1 layer").arg(QCoreApplication::translate("RulesDialog", family));
        if (!ob.effect.isEmpty())
            html += QStringLiteral("<br>%1 effect").arg(ob.effect.toHtmlEscaped());
        if (lay.family == TmcRulesRepo::FamilyPoint)
        {
            const QString figure = spotFigureName(ob.model);
            const QString name = figure.isEmpty()
                               ? QCoreApplication::translate("RulesDialog", "unknown figure %1").arg(ob.model)
                               : figure;
            html += QStringLiteral("<br>model: %1 (No.%2)").arg(name).arg(ob.model);
        }
        switch (ob.relMode)
        {
        case TmcRulesRepo::RelAuto:
            if (!ob.relColumn.isEmpty())
                html += ob.relHasDefault
                    ? QStringLiteral("<br>height: column %1, the code's Default %2 m").arg(ob.relColumn.toHtmlEscaped()).arg(ob.relDefault)
                    : QStringLiteral("<br>height: column %1, no Default for the code - the 6-floor fallback").arg(ob.relColumn.toHtmlEscaped());
            else
                html += ob.relHasDefault
                    ? QStringLiteral("<br>height: the code's Default %1 m").arg(ob.relDefault)
                    : QStringLiteral("<br>height: no Default for the code - the 6-floor fallback");
            break;
        case TmcRulesRepo::RelFixed:
            html += QStringLiteral("<br>height: strict %1 m").arg(ob.relValue);
            break;
        default:
            html += QStringLiteral("<br>%1").arg(QCoreApplication::translate("RulesDialog", "flat - the height is not read"));
            break;
        }
        if (!lay.materialColumn.isEmpty())
            html += QStringLiteral("<br>material: column %1").arg(lay.materialColumn.toHtmlEscaped());
        const double ws = TmcRulesRepo::widthScale(ob);
        if (!qFuzzyCompare(ws, 1.0))
            html += QStringLiteral("<br>width x%1").arg(ws);
        if (TmcRulesRepo::marking(ob))
            html += QStringLiteral("<br>%1").arg(QCoreApplication::translate("RulesDialog", "road markings"));
        if (ob.internal)
            html += QStringLiteral("<br><i>%1</i>").arg(QCoreApplication::translate("RulesDialog", "internal code - the exporter synthesizes such objects, the OSM data never carries it"));
        return html;
    }

    QJsonObject mRoot;
    QJsonArray mRules;
    QTreeWidget* mTree = nullptr;
    QLineEdit* mFilter = nullptr;
    QPushButton *mAdd = nullptr, *mDup = nullptr, *mDel = nullptr, *mUp = nullptr, *mDown = nullptr;
    QPlainTextEdit* mNote = nullptr;
    QComboBox *mFrom = nullptr, *mLayer = nullptr, *mAs = nullptr;
    QSpinBox *mPass = nullptr, *mAsSize = nullptr;
    QPushButton* mSpot = nullptr;
    QCheckBox* mCont = nullptr;
    QTableWidget *mWhen = nullptr, *mFields = nullptr;
    QPlainTextEdit* mFieldHelp = nullptr;
    QTextBrowser* mCatalog = nullptr;
    ModelPreview* mPanePreview = nullptr;
    QPushButton *mWhenAdd = nullptr, *mWhenDel = nullptr, *mFieldsAdd = nullptr, *mFieldsDel = nullptr, *mRelAuto = nullptr;
    int mCur = -1;
    bool mFilling = false;
};


// ---------------------------------------------------------------- the dialog

RulesDialog::RulesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Export rules"));
    setModal(true);
    setWindowFlag(Qt::WindowMaximizeButtonHint);               // the dialog is meant to be worked in full screen too
    resize(1150, 700);

    QVBoxLayout* lay = new QVBoxLayout(this);
    mTabs = new QTabWidget;
    lay->addWidget(mTabs, 1);
    QHBoxLayout* row = new QHBoxLayout;
    mPathLabel = new QLabel;
    row->addWidget(mPathLabel, 1);
    QPushButton* saveBtn = new QPushButton(tr("Save"));
    QPushButton* saveAsBtn = new QPushButton(tr("Save as..."));
    QPushButton* reloadBtn = new QPushButton(tr("Reload"));
    QPushButton* closeBtn = new QPushButton(tr("Close"));
    row->addWidget(saveBtn);
    row->addWidget(saveAsBtn);
    row->addWidget(reloadBtn);
    row->addWidget(closeBtn);
    lay->addLayout(row);

    const std::function<void()> changed = [this] { refreshBottom(); };
    mTabList << new OsmRulesTab(changed);
    for (RulesTabBase* t : mTabList) mTabs->addTab(t, t->title());

    connect(mTabs, &QTabWidget::currentChanged, this, [this] { refreshBottom(); });
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveCurrentTab(); });
    connect(saveAsBtn, &QPushButton::clicked, this, [this] { saveCurrentTabAs(); });
    connect(reloadBtn, &QPushButton::clicked, this, [this] { reloadCurrentTab(); });
    connect(closeBtn, &QPushButton::clicked, this, [this] { if (confirmDiscard()) accept(); });

    QString firstErr;
    for (RulesTabBase* t : mTabList)
    {
        t->setPath(resolveDataFile(t->fileName()));
        QString err;
        if (!t->load(&err) && firstErr.isEmpty()) firstErr = t->fileName() + QStringLiteral(": ") + err;
    }
    refreshBottom();
    if (!firstErr.isEmpty())
        QMessageBox::warning(this, tr("Export rules"), tr("Could not read the settings files:\n%1").arg(firstErr));
}

void RulesDialog::refreshBottom()
{
    for (int i = 0; i < mTabList.size(); ++i)
        mTabs->setTabText(i, mTabList[i]->title() + (mTabList[i]->dirty() ? QStringLiteral("  *") : QString()));
    RulesTabBase* t = mTabList.value(mTabs->currentIndex());
    if (!t) return;
    QString s = t->path();
    if (!t->loaded()) s += QStringLiteral("  (") + tr("not found") + QStringLiteral(")");
    else if (t->dirty()) s += QStringLiteral("  (") + tr("not saved") + QStringLiteral(")");
    mPathLabel->setText(s);
}

void RulesDialog::saveCurrentTab()
{
    RulesTabBase* t = mTabList.value(mTabs->currentIndex());
    if (!t) return;
    if (!t->loaded()) { saveCurrentTabAs(); return; }
    QString err;
    if (t->saveTo(t->path(), &err))
        t->clearDirty();
    else
        QMessageBox::warning(this, tr("Export rules"), tr("Could not save %1:\n%2").arg(t->fileName(), err));
}

void RulesDialog::saveCurrentTabAs()
{
    RulesTabBase* t = mTabList.value(mTabs->currentIndex());
    if (!t) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save %1 as").arg(t->fileName()),
                                                      t->path(), t->fileName());
    if (path.isEmpty()) return;
    QString err;
    if (t->saveTo(path, &err))
    {
        t->setPath(path);
        t->clearDirty();
    }
    else
        QMessageBox::warning(this, tr("Export rules"), tr("Could not save %1:\n%2").arg(t->fileName(), err));
}

void RulesDialog::reloadCurrentTab()
{
    RulesTabBase* t = mTabList.value(mTabs->currentIndex());
    if (!t) return;
    if (t->dirty()
        && QMessageBox::question(this, tr("Export rules"), tr("Discard the unsaved changes in %1?").arg(t->fileName()),
                                 QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    t->clearDirty();
    QString err;
    if (!t->load(&err))
        QMessageBox::warning(this, tr("Export rules"), tr("Could not read %1:\n%2").arg(t->fileName(), err));
    refreshBottom();
}

bool RulesDialog::confirmDiscard()
{
    QStringList unsaved;
    for (RulesTabBase* t : mTabList)
        if (t->dirty()) unsaved << t->fileName();
    if (unsaved.isEmpty()) return true;
    return QMessageBox::question(this, tr("Export rules"),
                                 tr("The files %1 were changed but not saved. Close without saving?").arg(unsaved.join(QStringLiteral(", "))),
                                 QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}
