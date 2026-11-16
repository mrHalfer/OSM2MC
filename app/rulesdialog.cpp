#include "rulesdialog.h"
#include <QtWidgets>
#include <QRandomGenerator>
#include <functional>

// The four reference files live in four tabs. The JSON tabs keep the whole file as a QJsonObject and re-emit it on
// save: unknown keys (osm_rules.json's "_help", "greenery", "neighbors"; default.t3d's "radio" and any future keys)
// survive untouched; the rule/source order is kept, the keys inside one object are sorted alphabetically by Qt.
// Condition and field values are shown as compact JSON text, so any form the parser accepts stays editable verbatim.

// ---------------------------------------------------------------- shared helpers

static QString resolveDataFile(const QString& name)
{   // the same order as the loaders: next to the exe, then the current folder; the exe folder is the default for "Save as"
    const QString app = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    if (QFile::exists(app)) return app;
    const QString cwd = QDir::current().filePath(name);
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

// ---------------------------------------------------------------- osm_rules.json


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
        : RulesTabBase(QStringLiteral("osm_rules.json"), QCoreApplication::translate("RulesDialog", "OSM rules"), std::move(changed))
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
        mLayer->addItems({ QStringLiteral("bldpol"), QStringLiteral("blkpol"), QStringLiteral("vegpol"),
                           QStringLiteral("vgbpol"), QStringLiteral("rdslin"), QStringLiteral("rrslin"),
                           QStringLiteral("csrpol"), QStringLiteral("csrlin"), QStringLiteral("csrpnt"),
                           QStringLiteral("rdcpol"), QStringLiteral("hdrpol"), QStringLiteral("hdrlin"),
                           QStringLiteral("hdtpnt"), QStringLiteral("mrspol"), QStringLiteral("-") });
        form->addWidget(mLayer, 1, 3);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "As")), 2, 0);
        mAs = new NoWheelComboBox;
        mAs->setEditable(true);
        mAs->addItems({ QString(), QStringLiteral("polygon"), QStringLiteral("line"),
                        QStringLiteral("square:10"), QStringLiteral("crosswalk:5") });
        mAs->setInsertPolicy(QComboBox::NoInsert);
        mAs->lineEdit()->setPlaceholderText(QCoreApplication::translate("RulesDialog", "as is"));
        form->addWidget(mAs, 2, 1);
        form->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "Pass")), 2, 2);
        mPass = new NoWheelSpinBox;
        mPass->setRange(1, 2);
        mPass->setToolTip(QCoreApplication::translate("RulesDialog", "2 — a guess for objects not recognized on pass 1 (they see the nb:* tags about neighbors)"));
        form->addWidget(mPass, 2, 3);
        mCont = new QCheckBox(QCoreApplication::translate("RulesDialog", "Continue (the object may also match the rules below)"));
        form->addWidget(mCont, 3, 0, 1, 4);
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
                               "then from the typical heights table heights.csv by the code; a fixed floor count can be written instead"));
        rightLay->addLayout(toolRow({ mFieldsAdd, mFieldsDel, mRelAuto }));
        split->addWidget(right);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 3);
        split->setSizes({ 460, 640 });

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
        connect(mFrom, QOverload<int>::of(&QComboBox::activated), this, [this] { commitWidgets(); });
        connect(mLayer, &QComboBox::editTextChanged, this, [this] { if (!mFilling) commitWidgets(); });
        connect(mAs, &QComboBox::editTextChanged, this, [this] { if (!mFilling) commitWidgets(); });
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

    static QStringList knownFields()
    {   // the layer fields the exporter reads (scene/mcscene.cpp, input/tmcproject.cpp)
        return { QStringLiteral("CODE"), QStringLiteral("REL"), QStringLiteral("MAT"), QStringLiteral("PART"),
                 QStringLiteral("PH"), QStringLiteral("MINH"), QStringLiteral("MINL"), QStringLiteral("ROOF"),
                 QStringLiteral("ROOFH"), QStringLiteral("ROOFD"), QStringLiteral("ROOFC"), QStringLiteral("ROOFM"),
                 QStringLiteral("LIFT"), QStringLiteral("BRG"), QStringLiteral("TUN"), QStringLiteral("LVL"),
                 QStringLiteral("LYR"), QStringLiteral("ELEC"), QStringLiteral("STREET"), QStringLiteral("TYSTR"),
                 QStringLiteral("NUMBR"), QStringLiteral("ADDNUM") };
    }

    static QString fieldPurpose(const QString& name)
    {
        if (name == QLatin1String("CODE")) return QCoreApplication::translate("RulesDialog", "the classifier code: what the object is (drives blocks, roads, vegetation)");
        if (name == QLatin1String("REL")) return QCoreApplication::translate("RulesDialog", "the building height in floors; \"auto\" - from the OSM tags, then from the heights.csv table");
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
        QWidget* ws[] = { mNote, mFrom, mLayer, mAs, mPass, mCont, mWhen, mFields,
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
            mAs->setEditText(o.value(QStringLiteral("as")).toString());
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
        cb->addItems(knownFields());
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
        const QString as = mAs->currentText().trimmed().toLower();
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

    QJsonObject mRoot;
    QJsonArray mRules;
    QTreeWidget* mTree = nullptr;
    QLineEdit* mFilter = nullptr;
    QPushButton *mAdd = nullptr, *mDup = nullptr, *mDel = nullptr, *mUp = nullptr, *mDown = nullptr;
    QPlainTextEdit* mNote = nullptr;
    QComboBox *mFrom = nullptr, *mLayer = nullptr, *mAs = nullptr;
    QSpinBox* mPass = nullptr;
    QCheckBox* mCont = nullptr;
    QTableWidget *mWhen = nullptr, *mFields = nullptr;
    QPlainTextEdit* mFieldHelp = nullptr;
    QPushButton *mWhenAdd = nullptr, *mWhenDel = nullptr, *mFieldsAdd = nullptr, *mFieldsDel = nullptr, *mRelAuto = nullptr;
    int mCur = -1;
    bool mFilling = false;
};


// ---------------------------------------------------------------- default.t3d

// A brand-new source gets a full set of the tqt keys with harmless random values: tqt must still open the file the
// same way, while OSMC only ever reads layer/effect/modelField/absorptionColumn (the colors decorate the tqt 3D
// window for it too). The swamp bit (128) is never rolled at random - it is the one effect value that changes the
// exported world.
static QJsonObject randomT3dSource()
{
    QRandomGenerator* rg = QRandomGenerator::global();
    auto hex = [rg] { return QStringLiteral("#") + QString::number(rg->bounded(0x1000000), 16).rightJustified(6, QLatin1Char('0')); };
    QJsonObject o;
    o[QStringLiteral("absorptionColumn")] = QString();
    o[QStringLiteral("absorptionDefault")] = 10 + rg->bounded(21);
    o[QStringLiteral("absorptionMaterial")] = QJsonObject();
    o[QStringLiteral("absorptionMode")] = 0;                     // no column to read a material from yet (see commitCell)
    o[QStringLiteral("absorptionValue")] = 10 + rg->bounded(21);
    o[QStringLiteral("color1")] = hex();
    o[QStringLiteral("color2")] = hex();
    o[QStringLiteral("colorMode")] = rg->bounded(2);             // tqt's own decoration, never the classifier
    o[QStringLiteral("effect")] = rg->bounded(128);
    o[QStringLiteral("extraExtrusion")] = 0;
    o[QStringLiteral("field")] = QString();
    o[QStringLiteral("isGround")] = true;
    o[QStringLiteral("layer")] = QString();
    o[QStringLiteral("liftAdd")] = rg->bounded(31) / 10.0;
    o[QStringLiteral("liftField")] = QString();
    o[QStringLiteral("liftMultiplier")] = 0.5 + rg->bounded(16) / 10.0;
    o[QStringLiteral("lineWidth")] = 1.5 + rg->bounded(66) / 10.0;
    o[QStringLiteral("modelField")] = QString();
    o[QStringLiteral("multiplier")] = 1;
    o[QStringLiteral("outline")] = rg->bounded(3);
    o[QStringLiteral("singleColor")] = hex();
    return o;
}

class T3dTab : public RulesTabBase
{
public:
    explicit T3dTab(std::function<void()> changed)
        : RulesTabBase(QStringLiteral("default.t3d"), QCoreApplication::translate("RulesDialog", "OSMC rules"), std::move(changed))
    {
        // Only the columns the exporter really reads are shown; everything else (the colors, lineWidth, lift*,
        // the radio absorption...) stays in the file untouched for tqt to open the same default.t3d back.
        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        lay->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "The layer sources (the order matters)")));
        mSources = makeTable({ QCoreApplication::translate("RulesDialog", "Layer"), QCoreApplication::translate("RulesDialog", "Effect"),
                               QCoreApplication::translate("RulesDialog", "Model field"), QCoreApplication::translate("RulesDialog", "Material column") });
        mSources->setColumnWidth(0, 120);                        // the headers must not clip ("Колонка моделей" is wide)
        mSources->setColumnWidth(1, 90);
        mSources->setColumnWidth(2, 170);
        if (mSources->horizontalHeaderItem(1))
            mSources->horizontalHeaderItem(1)->setToolTip(QCoreApplication::translate("RulesDialog", "a bit mask, see the decode below the table"));
        if (mSources->horizontalHeaderItem(2))
            mSources->horizontalHeaderItem(2)->setToolTip(QCoreApplication::translate("RulesDialog", "the column with the object codes looked up in point_models.csv (a source of point structures); empty - no models"));
        if (mSources->horizontalHeaderItem(3))
            mSources->horizontalHeaderItem(3)->setToolTip(QCoreApplication::translate("RulesDialog", "buildings take their material from this column instead of MAT; empty - MAT"));
        lay->addWidget(mSources, 1);
        mAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add"));
        mDup = new QPushButton(QCoreApplication::translate("RulesDialog", "Duplicate"));
        mDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Delete"));
        mUp = new QPushButton(QCoreApplication::translate("RulesDialog", "Up"));
        mDown = new QPushButton(QCoreApplication::translate("RulesDialog", "Down"));
        lay->addLayout(toolRow({ mAdd, mDup, mDel, mUp, mDown }));
        mEffectHelp = new QPlainTextEdit;                       // what the Effect number actually means
        mEffectHelp->setReadOnly(true);
        mEffectHelp->setFixedHeight(84);
        lay->addWidget(mEffectHelp);

        connect(mSources, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int, int) { selectSource(row); });
        connect(mSources, &QTableWidget::cellChanged, this, [this](int row, int col) { commitCell(row, col); });
        connect(mAdd, &QPushButton::clicked, this, [this] { addSource(false); });
        connect(mDup, &QPushButton::clicked, this, [this] { addSource(true); });
        connect(mDel, &QPushButton::clicked, this, [this] { delSource(); });
        connect(mUp, &QPushButton::clicked, this, [this] { moveSource(-1); });
        connect(mDown, &QPushButton::clicked, this, [this] { moveSource(1); });

        mSources->setSelectionBehavior(QAbstractItemView::SelectRows);
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
        mLoaded = true;
        rebuildSources(mSources->currentRow() >= 0 ? mSources->currentRow() : 0);
        return true;
    }

    bool saveTo(const QString& path, QString* err) override
    {
        if (!mLoaded) { if (err) *err = QCoreApplication::translate("RulesDialog", "the file was not loaded — nothing to save"); return false; }
        return writeTextFile(path, QJsonDocument(mRoot).toJson(QJsonDocument::Indented), err);
    }

private:
    enum { kColLayer = 0, kColEffect, kColModel, kColMaterial };

    QJsonArray sources() const { return mRoot.value(QStringLiteral("vectorSources")).toArray(); }
    QJsonObject sourceAt(int i) const { return sources().at(i).toObject(); }

    void rebuildSources(int selectIdx)
    {
        const QJsonArray arr = sources();
        mSources->blockSignals(true);
        mSources->setRowCount(0);
        for (int i = 0; i < arr.size(); ++i)
        {
            const QJsonObject o = arr.at(i).toObject();
            const int r = mSources->rowCount();
            mSources->insertRow(r);
            mSources->setItem(r, kColLayer, new QTableWidgetItem(o.value(QStringLiteral("layer")).toString()));
            const int eff = o.value(QStringLiteral("effect")).toInt(0);
            QTableWidgetItem* ef = new QTableWidgetItem(QString::number(eff));
            ef->setToolTip(effectDecode(eff));
            mSources->setItem(r, kColEffect, ef);
            mSources->setItem(r, kColModel, new QTableWidgetItem(o.value(QStringLiteral("modelField")).toString()));
            mSources->setItem(r, kColMaterial, new QTableWidgetItem(o.value(QStringLiteral("absorptionColumn")).toString()));
        }
        mSources->blockSignals(false);
        if (selectIdx >= arr.size()) selectIdx = arr.size() - 1;
        if (selectIdx >= 0) mSources->selectRow(selectIdx);
        else selectSource(-1);
    }

    static QString effectNames(int v)                    //!< "101 -> shadow + lamps + ..." for the decode pane
    {
        const QPair<int, QString> kBits[] = {
            { 1, QCoreApplication::translate("RulesDialog", "shadow") },
            { 2, QCoreApplication::translate("RulesDialog", "glow") },
            { 4, QCoreApplication::translate("RulesDialog", "lamps") },
            { 8, QCoreApplication::translate("RulesDialog", "rounded edges") },
            { 16, QCoreApplication::translate("RulesDialog", "light outline") },
            { 32, QCoreApplication::translate("RulesDialog", "dark outline") },
            { 64, QCoreApplication::translate("RulesDialog", "supports") },
            { 128, QCoreApplication::translate("RulesDialog", "swamp biome") },
        };
        QStringList on;
        for (const auto& b : kBits)
            if (v & b.first) on << QStringLiteral("%1 (%2)").arg(b.first).arg(b.second);
        return on.isEmpty() ? QCoreApplication::translate("RulesDialog", "no bits set") : on.join(QStringLiteral(" + "));
    }

    static QString effectDecode(int v)
    {
        return QCoreApplication::translate("RulesDialog", "Effect %1 = %2").arg(v).arg(effectNames(v)) + QLatin1Char('\n')
             + QCoreApplication::translate("RulesDialog", "Only the 128 bit (swamp biome) matters to the Minecraft export.");
    }

    void selectSource(int idx)
    {
        mCur = idx;
        QString s;
        if (idx >= 0)
            s = effectDecode(sourceAt(idx).value(QStringLiteral("effect")).toInt(0)) + QLatin1Char('\n');
        s += QCoreApplication::translate("RulesDialog",
               "Mask bits: 1 shadow, 2 glow, 4 lamps, 8 rounded edges, 16 light outline, 32 dark outline, 64 supports - all of them only decorate the model in the tqt 3D window. 128 is the swamp biome, the only bit the Minecraft export uses: the ground inside the layer outline becomes a swamp (swamp oaks, vines, mushrooms).");
        mEffectHelp->setPlainText(s);
    }

    void commitCell(int row, int col)
    {
        if (row < 0 || row >= sources().size() || col < 0 || col > kColMaterial) return;
        static const char* kKeys[] = { "layer", "effect", "modelField", "absorptionColumn" };
        QJsonObject o = sourceAt(row);
        const QString text = cellText(mSources, row, col).trimmed();
        if (col == kColEffect)
        {   // the effect is a number: keep the digits out of whatever else was typed
            static const QRegularExpression re(QStringLiteral("-?\\d+"));
            const QRegularExpressionMatch m = re.match(text);
            o[QLatin1String(kKeys[col])] = m.hasMatch() ? QJsonValue(m.captured(0).toInt()) : QJsonValue(0);
        }
        else
            o[QLatin1String(kKeys[col])] = text;
        if (col == kColMaterial) o[QStringLiteral("absorptionMode")] = text.isEmpty() ? 0 : 1;   // a column means the "material from a column" mode
        QJsonArray arr = sources();
        arr[row] = o;
        mRoot[QStringLiteral("vectorSources")] = arr;
        markDirty();
        if (col == kColEffect)                                   // re-show the normalized number and its decode,
            QTimer::singleShot(0, this, [this] { rebuildSources(mCur); });   // after the cell editor has closed
    }

    void addSource(bool duplicate)
    {
        QJsonObject o;
        if (duplicate && mCur >= 0) o = sourceAt(mCur);
        else o = randomT3dSource();                               // a full set of tqt keys, nothing meaningful for OSMC yet
        QJsonArray arr = sources();
        const int at = mCur >= 0 ? mCur + 1 : arr.size();
        arr.insert(at, o);
        mRoot[QStringLiteral("vectorSources")] = arr;
        rebuildSources(at);
        markDirty();
    }

    void delSource()
    {
        if (mCur < 0) return;
        QJsonArray arr = sources();
        arr.removeAt(mCur);
        mRoot[QStringLiteral("vectorSources")] = arr;
        rebuildSources(mCur >= arr.size() ? mCur - 1 : mCur);
        markDirty();
    }

    void moveSource(int dir)
    {
        if (mCur < 0) return;
        const int to = mCur + dir;
        QJsonArray arr = sources();
        if (to < 0 || to >= arr.size()) return;
        swapJson(arr, mCur, to);
        mRoot[QStringLiteral("vectorSources")] = arr;
        rebuildSources(to);
        markDirty();
    }

    QJsonObject mRoot;
    QTableWidget* mSources = nullptr;
    QPlainTextEdit* mEffectHelp = nullptr;
    QPushButton *mAdd = nullptr, *mDup = nullptr, *mDel = nullptr, *mUp = nullptr, *mDown = nullptr;
    int mCur = -1;
};

// ---------------------------------------------------------------- point model preview

// A small isometric renderer showing what step 3.575 of export/exportrun.cpp builds on a point. The shapes are
// duplicated here on purpose - the exporter owns the world, the dialog only draws. The variants that the exporter
// picks by the column hash are shown side by side; materials that alternate by hash are drawn as their first option.

class ModelPreview : public QWidget
{
public:
    explicit ModelPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 340);
    }

    void showModel(int id, const QString& title)
    {
        mId = id;
        mTitle = title;
        build();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        g.fillRect(rect(), palette().color(QPalette::Base));
        g.setPen(palette().color(QPalette::WindowText));
        g.drawText(rect().adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop, mTitle);
        if (mBoxes.isEmpty())
        {
            g.setPen(palette().color(QPalette::PlaceholderText));
            g.drawText(rect(), Qt::AlignCenter, mHint);
            return;
        }
        float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
        for (const Box& b : mBoxes)
        {
            u0 = qMin(u0, b.x - b.z - b.d);   u1 = qMax(u1, b.x + b.w - b.z);
            v0 = qMin(v0, (b.x + b.z) * 0.5f - b.y);
            v1 = qMax(v1, (b.x + b.w + b.z + b.d) * 0.5f - (b.y - b.h));
        }
        const qreal pad = 16.0, lh = fontMetrics().height();
        const qreal s = qBound(1.0, qMin((width() - 2 * pad) / (u1 - u0),
                                         (height() - 2 * pad - lh - 12) / (v1 - v0)), 22.0);
        const qreal cu = (u0 + u1) * 0.5, cv = (v0 + v1) * 0.5;
        const QPointF ctr(width() / 2.0, (height() - lh - 4) / 2.0 + 6);
        auto px = [&](float x, float y, float z)
            { return QPointF(ctr.x() + ((x - z) - cu) * 0.866 * s, ctr.y() + ((x + z) * 0.5f - y - cv) * s); };
        for (const Box& b : mBoxes) drawBox(g, b, px);
        QFont f = g.font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        g.setFont(f);
        g.setPen(palette().color(QPalette::PlaceholderText));
        for (const auto& lb : mLabels)                                   // the variant numbers under each model
            g.drawText(QRectF(ctr.x() + (lb.first - cu) * 0.866 * s - 30, ctr.y() + (v1 - cv) * s + 6, 60, lh),
                       Qt::AlignHCenter | Qt::AlignVCenter, lb.second);
    }

private:
    struct Box { float x, y, z, w, h, d; int mat; float key; };   // x/z/y are the TOP corner, the box goes down by h

    enum Mat { MatStone, MatCobb, MatMossy, MatSBrick, MatBricks, MatPDsl, MatCDsl, MatPBlk, MatBlk, MatGran,
               MatDior, MatAnd, MatSmth, MatGold, MatQuartz, MatPCalc, MatGlow, MatIron, MatWater, MatFall,
               MatWallC, MatWallS, MatOak, MatStair, MatComposter, MatLantern, MatCutter, MatPlate };

    static QColor matColor(int m)
    {
        switch (m)
        {
        case MatStone:     return QColor(0x83, 0x83, 0x83);
        case MatCobb:      return QColor(0x77, 0x77, 0x73);
        case MatMossy:     return QColor(0x6c, 0x7a, 0x55);
        case MatSBrick:    return QColor(0x7a, 0x7a, 0x7a);
        case MatBricks:    return QColor(0x9e, 0x5a, 0x4a);
        case MatPDsl:      return QColor(0x53, 0x55, 0x5f);
        case MatCDsl:      return QColor(0x4d, 0x4f, 0x55);
        case MatPBlk:      return QColor(0x44, 0x3f, 0x49);
        case MatBlk:       return QColor(0x33, 0x31, 0x38);
        case MatGran:      return QColor(0x9a, 0x69, 0x59);
        case MatDior:      return QColor(0xc8, 0xc8, 0xcb);
        case MatAnd:       return QColor(0x8c, 0x8c, 0x88);
        case MatSmth:      return QColor(0x88, 0x88, 0x8b);
        case MatGold:      return QColor(0xfb, 0xdf, 0x4f);
        case MatQuartz:    return QColor(0xec, 0xe6, 0xdc);
        case MatPCalc:     return QColor(0xd6, 0xd4, 0xd9);
        case MatGlow:      return QColor(0xf8, 0xc2, 0x51);
        case MatIron:      return QColor(0xd5, 0xd5, 0xd3);
        case MatWater:     return QColor(36, 92, 205, 150);
        case MatFall:      return QColor(110, 150, 230, 110);
        case MatWallC:     return QColor(0x77, 0x77, 0x73);
        case MatWallS:     return QColor(0x7a, 0x7a, 0x7a);
        case MatOak:       return QColor(0xb0, 0x8a, 0x5e);
        case MatStair:     return QColor(0x80, 0x80, 0x80);
        case MatComposter: return QColor(0x5d, 0x5f, 0x3a);
        case MatLantern:   return QColor(0xb0, 0x8c, 0x45);
        case MatCutter:    return QColor(0x74, 0x74, 0x74);
        default:           return QColor(0x9d, 0x9d, 0x9d);
        }
    }

    static QColor shade(const QColor& c, double f)
    {
        return QColor(qMin(255, int(c.red() * f)), qMin(255, int(c.green() * f)),
                      qMin(255, int(c.blue() * f)), c.alpha());
    }

    template <typename Px>
    void drawBox(QPainter& g, const Box& b, Px px) const
    {
        const QColor base = matColor(b.mat);
        const bool transp = (b.mat == MatWater || b.mat == MatFall);
        const bool emis = (b.mat == MatGlow || b.mat == MatLantern);        // light sources are barely shaded
        const QColor pen = transp ? QColor(0, 0, 0, 40) : QColor(30, 30, 30, 90);
        const float x1 = b.x + b.w, z1 = b.z + b.d, y0 = b.y - b.h;
        auto face = [&](const QPointF& a, const QPointF& p, const QPointF& q, const QPointF& r, const QColor& col)
        {
            g.setBrush(col);
            g.setPen(QPen(pen, 0.7));
            g.drawPolygon(QPolygonF() << a << p << q << r);
        };
        face(px(b.x, b.y, b.z), px(x1, b.y, b.z), px(x1, b.y, z1), px(b.x, b.y, z1),
             emis ? base : shade(base, 1.14));                                    // the top
        face(px(b.x, y0, z1), px(x1, y0, z1), px(x1, b.y, z1), px(b.x, b.y, z1),
             emis ? shade(base, 0.88) : shade(base, 0.70));                       // the +z face, the left
        face(px(x1, y0, b.z), px(x1, y0, z1), px(x1, b.y, z1), px(x1, b.y, b.z),
             emis ? shade(base, 0.94) : shade(base, 0.84));                       // the +x face, the right
    }

    void build()
    {
        mBoxes.clear();
        mLabels.clear();
        mHint = mId >= 1 && mId <= 9
            ? QCoreApplication::translate("RulesDialog", "No preview for model number %1").arg(mId)
            : QCoreApplication::translate("RulesDialog", "Select a row - the model that the exporter builds on such a point shows here");
        if (mId < 1 || mId > 9) return;

        float next = 0.0f;                                     // the u-coordinate the next variant starts at
        auto commit = [&](const QVector<Box>& v, const QString& label)
        {
            float u0 = 1e9f, u1 = -1e9f;
            for (const Box& b : v) { u0 = qMin(u0, b.x - b.z - b.d); u1 = qMax(u1, b.x + b.w - b.z); }
            const float sh = (next - u0) * 0.5f;               // +x and -z by the same amount: the row stays at one height
            for (Box b : v) { b.x += sh; b.z -= sh; mBoxes << b; }
            mLabels << qMakePair((u0 + u1) * 0.5f + 2 * sh, label);
            next = u1 + 2 * sh + 1.6f;
        };
        QVector<Box> v;
        auto add = [&v](float x, float yTop, float z, float w, float h, float d, int m)
            { Box b; b.x = x; b.y = yTop; b.z = z; b.w = w; b.h = h; b.d = d; b.mat = m; b.key = 0; v << b; };
        auto stair = [&add](float x, float k, float z, int m)   // the exporter's stairs: a slab plus a back half
            { add(x, k + 0.5f, z, 1, 0.5f, 1, m); add(x, k + 1.0f, z + 0.5f, 1, 0.5f, 0.5f, m); };
        auto wall = [&add](float x, float k, float z, int m, int segments)
        {
            for (int i = 0; i < segments; ++i) add(x + 0.25f, k + i + 0.81f, z + 0.25f, 0.5f, 0.81f, 0.5f, m);
        };
        auto lamp = [&add](float x, float k, float z, bool hang)
            { add(x + 0.3f, hang ? k + 0.95f : k + 0.5f, z + 0.3f, 0.4f, 0.5f, 0.4f, MatLantern); };

        switch (mId)
        {
        case 1:                                                 // memorial, the six variants of kStatue
        {
            v.clear();
            add(0, 1, 0, 1, 1, 1, MatMossy); add(1, 1, 0, 1, 1, 1, MatCobb);
            add(0, 1, 1, 1, 1, 1, MatCobb); add(1, 1, 1, 1, 1, 1, MatMossy);
            add(0, 4, 0, 1, 3, 1, MatPDsl); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a brick stele with a plaque
            add(0, 2, 0, 2, 2, 2, MatBricks); add(0, 4, 0, 1, 2, 1, MatSBrick);
            add(0.05f, 4.125f, 0.05f, 0.9f, 0.125f, 0.9f, MatPlate);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // an eternal flame with corner fence posts
            add(0, 2, 0, 1, 2, 1, MatPBlk); lamp(0, 2, 0, false);
            wall(-1, 0, -1, MatWallC, 1); wall(1, 0, -1, MatWallC, 1);
            wall(-1, 0, 1, MatWallC, 1); wall(1, 0, 1, MatWallC, 1);
            commit(v, QStringLiteral("3"));
            v.clear();                                         // a megalith of mossy boulders
            add(0, 1, 0, 1, 1, 1, MatMossy); add(1, 1, 0, 1, 1, 1, MatCobb);
            add(0, 1, 1, 1, 1, 1, MatAnd); add(1, 1, 1, 1, 1, 1, MatMossy);
            add(1, 2, 0, 1, 1, 1, MatCobb); add(0, 2, 1, 1, 1, 1, MatAnd);
            commit(v, QStringLiteral("4"));
            v.clear();                                         // a broken column on a platform
            add(-1, 1, -1, 3, 1, 3, MatCobb);
            add(-1, 2, -1, 1, 1, 1, MatSmth); add(1, 2, -1, 1, 1, 1, MatSmth);
            add(-1, 2, 1, 1, 1, 1, MatSmth); add(1, 2, 1, 1, 1, 1, MatSmth);
            add(0, 4, 0, 1, 3, 1, MatSBrick); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("5"));
            v.clear();                                         // a shattered deepslate stele
            add(0, 1, 0, 2, 1, 2, MatCDsl); add(0, 3, 0, 1, 2, 1, MatBlk);
            add(0, 4, 0, 1, 1, 1, MatCDsl); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("6"));
            break;
        }
        case 2:                                                 // monument, the three variants of kStele
        {
            v.clear();
            add(-1, 1, -1, 3, 1, 3, MatStone); add(0, 2, 0, 2, 1, 2, MatPDsl);
            add(0, 7, 0, 1, 5, 1, MatPBlk); add(0, 8, 0, 1, 1, 1, MatGold);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a brick stele with an eternal flame
            add(-1, 1, -1, 3, 1, 3, MatGran); add(0, 5, 0, 2, 4, 2, MatBricks);
            add(0, 6, 0, 2, 1, 2, MatSBrick); lamp(0, 6, 0, false);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // a column with side walls on a diorite base
            add(0, 1, 0, 2, 1, 2, MatDior); add(0, 7, 0, 1, 6, 1, MatSBrick);
            wall(-1, 0, 0, MatWallS, 2); wall(2, 0, 0, MatWallS, 2);
            stair(0, 7, 0, MatStair);
            commit(v, QStringLiteral("3"));
            break;
        }
        case 3:                                                 // fountain
        {
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) == 2) add(float(dx), 2, float(dz), 1, 2, 1, MatSBrick);
                    else if (dx || dz) { add(float(dx), 1, float(dz), 1, 1, 1, MatStone); add(float(dx), 2, float(dz), 1, 1, 1, MatWater); }
                }
            add(0, 1, 0, 1, 1, 1, MatStone); add(0, 3, 0, 1, 2, 1, MatSBrick);
            add(0, 4, 0, 1, 1, 1, MatWater);                   // the water source on the post
            add(1, 3, 0, 1, 1, 1, MatFall); add(-1, 3, 0, 1, 1, 1, MatFall);
            add(0, 3, 1, 1, 1, 1, MatFall); add(0, 3, -1, 1, 1, 1, MatFall);
            add(1, 4, 0, 1, 1, 1, MatFall); add(-1, 4, 0, 1, 1, 1, MatFall);
            add(0, 4, 1, 1, 1, 1, MatFall); add(0, 4, -1, 1, 1, 1, MatFall);
            commit(v, QString());
            break;
        }
        case 4:                                                 // communication tower, a 32-block body
            add(0, 1, 0, 1, 1, 1, MatIron);
            for (int k = 1; k <= 28; ++k) wall(0, float(k), 0, MatWallC, 1);
            add(0, 32, 0, 1, 3, 1, MatQuartz);
            commit(v, QString());
            break;
        case 5:                                                 // composter
            add(0, 1, 0, 1, 1, 1, MatComposter);
            commit(v, QString());
            break;
        case 6:                                                 // lighting mast
            add(0, 3, 0, 1, 3, 1, MatPCalc); add(0, 4, 0, 1, 1, 1, MatGlow);
            add(0, 6, 0, 1, 2, 1, MatPCalc); add(0, 7, 0, 1, 1, 1, MatGlow);
            commit(v, QString());
            break;
        case 7:                                                 // street bench: two oak stairs along one axis
            stair(0, 0, 0, MatOak); stair(1, 0, 0, MatOak);
            commit(v, QString());
            break;
        case 8:                                                 // small sculpture, the three variants
        {
            v.clear();
            add(0, 1, 0, 1, 1, 1, MatDior); add(0, 2, 0, 1, 1, 1, MatPDsl);
            add(0, 3, 0, 1, 1, 1, MatBlk); add(0, 4, 0, 1, 1, 1, MatPBlk);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a bust on a stone-brick column
            add(0, 3, 0, 1, 3, 1, MatSBrick); add(0, 4, 0, 1, 1, 1, MatAnd);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // a sculptor's stand with a stone cutter
            add(0, 1, 0, 2, 1, 2, MatCobb);
            add(0, 2, 0, 1, 1, 1, MatAnd); add(1, 2, 1, 1, 1, 1, MatAnd);
            add(0, 2, 1, 1, 1, 1, MatCutter);
            commit(v, QStringLiteral("3"));
            break;
        }
        case 9:                                                 // bus stop shelter, the road side toward the viewer
        {
            add(-2, 1, -1, 5, 1, 3, MatSmth);                  // the floor
            for (int dx = -2; dx <= 2; ++dx)                   // the walls, the dz==+1 side stays open
                for (int dz = -1; dz <= 0; ++dz)
                    if (std::abs(dx) == 2 || dz == -1) add(float(dx), 4, float(dz), 1, 3, 1, MatSBrick);
            add(-2, 5, -1, 5, 1, 3, MatSBrick);                // the flat roof
            lamp(-2, 3, -1, true);                             // the lamp under the roof
            commit(v, QString());
            break;
        }
        default:
            return;
        }

        for (Box& b : mBoxes) b.key = b.x + b.w * 0.5f + b.z + b.d * 0.5f;
        std::stable_sort(mBoxes.begin(), mBoxes.end(), [](const Box& a, const Box& b)   // back to front
                         { return a.key != b.key ? a.key < b.key : a.y < b.y; });
        mHint.clear();
    }

    int mId = 0;
    QString mTitle, mHint;
    QVector<Box> mBoxes;
    QVector<QPair<float, QString>> mLabels;
};

// ---------------------------------------------------------------- point_models.csv

class PointModelsTab : public RulesTabBase
{
public:
    explicit PointModelsTab(std::function<void()> changed)
        : RulesTabBase(QStringLiteral("point_models.csv"), QCoreApplication::translate("RulesDialog", "Point models"), std::move(changed))
    {
        QSplitter* split = new QSplitter(Qt::Horizontal);
        QWidget* left = new QWidget;
        QVBoxLayout* leftLay = new QVBoxLayout(left);
        leftLay->setContentsMargins(0, 0, 0, 0);
        leftLay->addWidget(new QLabel(QCoreApplication::translate("RulesDialog", "An object code -> the model the exporter builds on such points")));
        mTable = makeTable({ QCoreApplication::translate("RulesDialog", "Code"), QCoreApplication::translate("RulesDialog", "Model") });
        leftLay->addWidget(mTable, 1);
        mAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add row"));
        mDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Remove row"));
        leftLay->addLayout(toolRow({ mAdd, mDel }));
        mPreview = new ModelPreview;
        split->addWidget(left);
        split->addWidget(mPreview);
        split->setSizes({ 560, 470 });

        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        lay->addWidget(split, 1);

        connect(mTable, &QTableWidget::cellChanged, this, [this](int row, int) { if (!mFilling) commitRow(row); });
        connect(mTable, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int, int) { if (row >= 0 && row < mRows.size()) setPreview(mRows[row].second); });
        connect(mAdd, &QPushButton::clicked, this, [this] { mRows.append(qMakePair(QString(), 1)); rebuild(); markDirty(); });
        connect(mDel, &QPushButton::clicked, this, [this]
                { const int r = mTable->currentRow(); if (r >= 0) { mRows.removeAt(r); rebuild(); markDirty(); } });
    }

    bool load(QString* err) override
    {
        QFile f(mPath);
        if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); mLoaded = false; return false; }
        const QString text = QString::fromUtf8(f.readAll());
        mEol = text.contains(QLatin1String("\r\n")) ? QByteArray("\r\n") : QByteArray("\n");
        const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
        mPreamble.clear();
        mRows.clear();
        int i = 0;
        for (; i < lines.size(); ++i)
        {
            mPreamble.append(lines.at(i));
            if (lines.at(i).trimmed().compare(QStringLiteral("code;model"), Qt::CaseInsensitive) == 0) { ++i; break; }
        }
        for (; i < lines.size(); ++i)
        {
            const QString line = lines.at(i).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const int sc = line.indexOf(QLatin1Char(';'));
            if (sc <= 0) continue;
            mRows.append(qMakePair(line.left(sc).trimmed(), line.mid(sc + 1).trimmed().toInt()));
        }
        mLoaded = true;
        rebuild();
        return true;
    }

    bool saveTo(const QString& path, QString* err) override
    {
        if (!mLoaded) { if (err) *err = QCoreApplication::translate("RulesDialog", "the file was not loaded — nothing to save"); return false; }
        QStringList out = mPreamble;
        for (const auto& e : mRows) out << e.first + QLatin1Char(';') + QString::number(e.second);
        return writeTextFile(path, out.join(QString::fromUtf8(mEol)).toUtf8(), err);
    }

private:
    static QString modelCaption(int id)
    {
        switch (id)
        {
        case 1: return QStringLiteral("1 \u2014 ") + QCoreApplication::translate("RulesDialog", "memorial statue");
        case 2: return QStringLiteral("2 \u2014 ") + QCoreApplication::translate("RulesDialog", "monument stele");
        case 3: return QStringLiteral("3 \u2014 ") + QCoreApplication::translate("RulesDialog", "fountain");
        case 4: return QStringLiteral("4 \u2014 ") + QCoreApplication::translate("RulesDialog", "communication tower");
        case 5: return QStringLiteral("5 \u2014 ") + QCoreApplication::translate("RulesDialog", "composter");
        case 6: return QStringLiteral("6 \u2014 ") + QCoreApplication::translate("RulesDialog", "lighting mast");
        case 7: return QStringLiteral("7 \u2014 ") + QCoreApplication::translate("RulesDialog", "street bench");
        case 8: return QStringLiteral("8 \u2014 ") + QCoreApplication::translate("RulesDialog", "small sculpture");
        case 9: return QStringLiteral("9 \u2014 ") + QCoreApplication::translate("RulesDialog", "bus stop shelter");
        default: return QString::number(id);
        }
    }

    void rebuild()
    {
        mTable->blockSignals(true);
        mFilling = true;
        mTable->setRowCount(0);
        for (const auto& e : mRows)
        {
            const int r = mTable->rowCount();
            mTable->insertRow(r);
            mTable->setItem(r, 0, new QTableWidgetItem(e.first));
            QComboBox* cb = new NoWheelComboBox;
            cb->setEditable(true);
            for (int id = 1; id <= 9; ++id) cb->addItem(modelCaption(id), id);
            cb->lineEdit()->setValidator(new QIntValidator(1, 999, cb));
            if (e.second >= 1 && e.second <= 9) cb->setCurrentIndex(e.second - 1);
            else cb->setEditText(QString::number(e.second));
            connect(cb, &QComboBox::currentTextChanged, this, [this, r](const QString&)
                    {
                        QComboBox* s = qobject_cast<QComboBox*>(sender());
                        if (!s || mFilling || r >= mRows.size()) return;
                        mRows[r].second = s->currentData().isValid() ? s->currentData().toInt()
                                                                     : s->currentText().trimmed().toInt();
                        if (mTable->currentRow() == r) setPreview(mRows[r].second);
                        markDirty();
                    });
            mTable->setCellWidget(r, 1, cb);
        }
        mFilling = false;
        mTable->blockSignals(false);
        if (mTable->currentRow() < 0 && !mRows.isEmpty()) mTable->selectRow(0);
        setPreview(mRows.isEmpty() ? 0 : mRows[qMax(0, mTable->currentRow())].second);
    }

    void setPreview(int id)
    {
        mPreview->showModel(id, id >= 1 && id <= 9 ? modelCaption(id) : QString::number(id));
    }

    void commitRow(int row)
    {
        if (mFilling || row < 0 || row >= mRows.size()) return;
        mRows[row].first = cellText(mTable, row, 0).trimmed();
        markDirty();
    }

    QStringList mPreamble;                                  //!< everything up to and including the "code;model" line
    QVector<QPair<QString, int>> mRows;
    QTableWidget* mTable = nullptr;
    ModelPreview* mPreview = nullptr;
    QPushButton *mAdd = nullptr, *mDel = nullptr;
    bool mFilling = false;
};

// ---------------------------------------------------------------- heights.csv

class HeightsTab : public RulesTabBase
{
public:
    explicit HeightsTab(std::function<void()> changed)
        : RulesTabBase(QStringLiteral("heights.csv"), QCoreApplication::translate("RulesDialog", "Typical heights"), std::move(changed))
    {
        QVBoxLayout* lay = new QVBoxLayout(this);
        lay->setContentsMargins(6, 6, 6, 6);
        mFilter = new QLineEdit;
        mFilter->setPlaceholderText(QCoreApplication::translate("RulesDialog", "Filter (a code or a word of the name)"));
        mFilter->setClearButtonEnabled(true);
        lay->addWidget(mFilter);
        mTable = makeTable({ QCoreApplication::translate("RulesDialog", "Code"), QCoreApplication::translate("RulesDialog", "Height, blocks"), QCoreApplication::translate("RulesDialog", "Name") });
        lay->addWidget(mTable, 1);
        mAdd = new QPushButton(QCoreApplication::translate("RulesDialog", "Add row"));
        mDel = new QPushButton(QCoreApplication::translate("RulesDialog", "Remove row"));
        lay->addLayout(toolRow({ mAdd, mDel }));

        connect(mFilter, &QLineEdit::textChanged, this, [this] { applyFilter(); });
        connect(mTable, &QTableWidget::cellChanged, this, [this](int row, int)
                {
                    if (mFilling) return;
                    if (row < 0 || row >= mRows.size()) return;
                    for (int c = 0; c < 3; ++c) mRows[row][c] = cellText(mTable, row, c);
                    applyFilter();
                    markDirty();
                });
        connect(mAdd, &QPushButton::clicked, this, [this]
                { mRows.append({ QString(), QStringLiteral("3"), QString() }); rebuild(); markDirty(); });
        connect(mDel, &QPushButton::clicked, this, [this]
                { const int r = mTable->currentRow(); if (r >= 0) { mRows.removeAt(r); rebuild(); markDirty(); } });
    }

    bool load(QString* err) override
    {
        QFile f(mPath);
        if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); mLoaded = false; return false; }
        const QString text = QString::fromUtf8(f.readAll());
        mEol = text.contains(QLatin1String("\r\n")) ? QByteArray("\r\n") : QByteArray("\n");
        mRows.clear();
        for (const QString& line : text.split(QRegularExpression(QStringLiteral("\r?\n"))))
        {
            const QString t = line.trimmed();
            if (t.isEmpty()) continue;
            if (t.compare(QStringLiteral("CODE;HIGH;OBJNAME50"), Qt::CaseInsensitive) == 0) continue;
            const int a = t.indexOf(QLatin1Char(';'));
            const int b = t.indexOf(QLatin1Char(';'), a + 1);
            if (a <= 0) continue;
            QStringList row;
            row << t.left(a).trimmed() << (b < 0 ? QString() : t.mid(a + 1, b - a - 1).trimmed())
                << (b < 0 ? QString() : t.mid(b + 1).trimmed());
            mRows << row;
        }
        mLoaded = true;
        rebuild();
        return true;
    }

    bool saveTo(const QString& path, QString* err) override
    {
        if (!mLoaded) { if (err) *err = QCoreApplication::translate("RulesDialog", "the file was not loaded — nothing to save"); return false; }
        QStringList out = { QStringLiteral("CODE;HIGH;OBJNAME50") };
        for (const QStringList& r : mRows) out << r.join(QLatin1Char(';'));
        return writeTextFile(path, out.join(QString::fromUtf8(mEol)).toUtf8(), err);
    }

private:
    void rebuild()
    {
        mTable->blockSignals(true);
        mFilling = true;
        mTable->setRowCount(0);
        for (const QStringList& r : mRows)
        {
            const int row = mTable->rowCount();
            mTable->insertRow(row);
            for (int c = 0; c < 3; ++c) mTable->setItem(row, c, new QTableWidgetItem(r.value(c)));
        }
        mFilling = false;
        mTable->blockSignals(false);
        applyFilter();
    }

    void applyFilter()
    {
        const QString f = mFilter->text().trimmed();
        for (int r = 0; r < mTable->rowCount(); ++r)
        {
            const bool hide = !f.isEmpty() && !mTable->item(r, 0)->text().contains(f, Qt::CaseInsensitive)
                                            && !mTable->item(r, 2)->text().contains(f, Qt::CaseInsensitive);
            mTable->setRowHidden(r, hide);
        }
    }

    QVector<QStringList> mRows;
    QLineEdit* mFilter = nullptr;
    QTableWidget* mTable = nullptr;
    QPushButton *mAdd = nullptr, *mDel = nullptr;
    bool mFilling = false;
};

// ---------------------------------------------------------------- the dialog

RulesDialog::RulesDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Export rules"));
    setModal(true);
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
    mTabList << new OsmRulesTab(changed) << new T3dTab(changed) << new PointModelsTab(changed) << new HeightsTab(changed);
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
