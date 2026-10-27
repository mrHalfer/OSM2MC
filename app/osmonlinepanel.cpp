#include "app/osmonlinepanel.h"
#include "app/osmtilemap.h"
#include "app/uimetrics.h"
#include "core/geoutil.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrent>

OsmOnlinePanel::OsmOnlinePanel(QWidget* parent) : QWidget(parent), mCancel(std::make_shared<std::atomic_bool>(false))
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(kRowGap);

    auto* searchBox = new QWidget;                                  // the same band as the title row of the left column
    searchBox->setFixedHeight(kHeaderH);
    auto* searchRow = new QHBoxLayout(searchBox);
    searchRow->setContentsMargins(0, 0, 0, 0);
    mQuery = new QLineEdit;
    mQuery->setPlaceholderText(tr("Search a place: city, street, address…"));
    mQuery->setClearButtonEnabled(true);
    mSearchBtn = new QPushButton(tr("Search"));
    searchRow->addWidget(mQuery, 1, Qt::AlignVCenter);
    searchRow->addWidget(mSearchBtn, 0, Qt::AlignVCenter);
    lay->addWidget(searchBox);

    mResults = new QComboBox;
    mResults->setMinimumHeight(kControlH);
    mResults->hide();
    lay->addWidget(mResults);

    mMap = new OsmTileMap;
    {   // map tools — the same as in the data preview: boundary (mandatory), spawn, entrance places
        auto* toolRow = new QHBoxLayout;
        toolRow->setContentsMargins(0, 0, 0, kCardTitleH);       // the map starts on the level of the "Data" card frame on the left
        auto* group = new QButtonGroup(this);
        const std::pair<const char*, OsmTileMap::Tool> tools[] = { { QT_TRANSLATE_NOOP("OsmOnlinePanel", "Boundary"), OsmTileMap::Tool::Clip }, { QT_TRANSLATE_NOOP("OsmOnlinePanel", "Spawn"), OsmTileMap::Tool::Spawn },
            { QT_TRANSLATE_NOOP("OsmOnlinePanel", "Stronghold"), OsmTileMap::Tool::Stronghold }, { QT_TRANSLATE_NOOP("OsmOnlinePanel", "Ancient City"), OsmTileMap::Tool::City }, { QT_TRANSLATE_NOOP("OsmOnlinePanel", "Pale Garden"), OsmTileMap::Tool::Pale } };
        for (const auto& t : tools)
        {
            auto* b = new QPushButton(tr(t.first));
            mToolBtns.append(b);
            mToolNames.append(t.first);
            b->setCheckable(true);
            b->setChecked(t.second == OsmTileMap::Tool::Clip);
            group->addButton(b);
            toolRow->addWidget(b);
            const OsmTileMap::Tool tool = t.second;
            connect(b, &QPushButton::toggled, this, [this, tool](bool on) { if (on) { mMap->setTool(tool); refreshState(); } });
        }
        group->setExclusive(true);
        toolRow->addStretch(1);
        lay->addLayout(toolRow);
    }
    lay->addWidget(mMap, 1);

    mStatus = new QLabel;
    mStatus->setWordWrap(true);
    mStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mStatus->setFixedHeight(mStatus->fontMetrics().lineSpacing() * 3 + 4);
    mStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    lay->addWidget(mStatus);

    auto* btnRow = new QHBoxLayout;
    mDownloadBtn = new QPushButton(tr("Download the area"));
    mDownloadBtn->setObjectName(QStringLiteral("goButton"));         // the accent button, like "Export"
    mClearBtn = new QPushButton(tr("Remove the boundary"));
    mResetMarksBtn = new QPushButton(tr("Reset the marks"));
    mCancelBtn = new QPushButton(tr("Cancel"));
    mCancelBtn->hide();
    btnRow->addWidget(mClearBtn);
    btnRow->addWidget(mResetMarksBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(mCancelBtn);
    btnRow->addWidget(mDownloadBtn);
    lay->addLayout(btnRow);

    mSearchWatcher = new QFutureWatcher<SearchOut>(this);
    mDownWatcher = new QFutureWatcher<OsmDownloadResult>(this);

    connect(mQuery, &QLineEdit::returnPressed, this, &OsmOnlinePanel::doSearch);
    connect(mSearchBtn, &QPushButton::clicked, this, &OsmOnlinePanel::doSearch);
    connect(mSearchWatcher, &QFutureWatcher<SearchOut>::finished, this, &OsmOnlinePanel::onSearchDone);
    connect(mResults, QOverload<int>::of(&QComboBox::activated), this, [this](int i)
    {
        if (i < 0 || i >= mPlaces.size()) return;
        const OsmPlace& p = mPlaces[i];
        mPlaceName = p.name.section(QLatin1Char(','), 0, 0).trimmed();
        if (p.lon1 > p.lon0 && p.lat1 > p.lat0 && (p.lon1 - p.lon0) < 2.0) mMap->fitBounds(p.lon0, p.lat0, p.lon1, p.lat1);
        else mMap->setView(p.lon, p.lat, 13);
    });
    connect(mClearBtn, &QPushButton::clicked, mMap, &OsmTileMap::clearClip);
    connect(mResetMarksBtn, &QPushButton::clicked, mMap, &OsmTileMap::clearMarks);
    connect(mMap, &OsmTileMap::clipChanged, this, &OsmOnlinePanel::refreshState);
    connect(mMap, &OsmTileMap::marksChanged, this, &OsmOnlinePanel::refreshState);
    connect(mDownloadBtn, &QPushButton::clicked, this, [this]() { if (mMap->clipAllowed()) emit downloadRequested(); });
    connect(mCancelBtn, &QPushButton::clicked, this, &OsmOnlinePanel::cancelDownload);
    connect(mDownWatcher, &QFutureWatcher<OsmDownloadResult>::finished, this, [this]()
    {
        const OsmDownloadResult r = mDownWatcher->result();
        mBusy = false;
        mCancelBtn->hide();
        refreshState();
        if (r.ok)
        {
            mStatus->setText(tr("Downloaded: %1 MB (source: %2).").arg(r.bytes / 1048576.0, 0, 'f', 1).arg(r.source));
            emit downloaded(mOutFile, mDownClip);
        }
        else if (r.cancelled) mStatus->setText(tr("Download cancelled."));
        else mStatus->setText(tr("Could not download: %1").arg(r.error));
    });

    QSettings s;                                                // the map view is as it was at the last start
    mMap->setView(s.value(QStringLiteral("online/lon"), 37.62).toDouble(), s.value(QStringLiteral("online/lat"), 55.75).toDouble(),
                  s.value(QStringLiteral("online/zoom"), 10.0).toDouble());
    connect(mMap, &OsmTileMap::viewChanged, this, [this]()
    {
        QSettings st;
        st.setValue(QStringLiteral("online/lon"), mMap->centerLon());
        st.setValue(QStringLiteral("online/lat"), mMap->centerLat());
        st.setValue(QStringLiteral("online/zoom"), mMap->zoom());
    });
    refreshState();
}

OsmOnlinePanel::~OsmOnlinePanel()
{
    if (mBusy) { mCancel->store(true); mDownWatcher->waitForFinished(); }
    if (mSearchWatcher->isRunning()) mSearchWatcher->waitForFinished();
}

//! The language was switched: all the texts set in the code are set again (the state-dependent status line is rebuilt by refreshState)
void OsmOnlinePanel::retranslate()
{
    mQuery->setPlaceholderText(tr("Search a place: city, street, address…"));
    mSearchBtn->setText(tr("Search"));
    for (int i = 0; i < mToolBtns.size(); ++i) mToolBtns[i]->setText(tr(mToolNames[i]));
    mDownloadBtn->setText(tr("Download the area"));
    mClearBtn->setText(tr("Remove the boundary"));
    mResetMarksBtn->setText(tr("Reset the marks"));
    mCancelBtn->setText(tr("Cancel"));
    if (!mBusy) refreshState();
}

void OsmOnlinePanel::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::LanguageChange && mStatus) retranslate();
}

void OsmOnlinePanel::refreshState()
{
    QString why;
    const bool ok = mMap->clipAllowed(&why);
    mDownloadBtn->setEnabled(ok && !mBusy);
    mClearBtn->setEnabled(!mBusy);
    mResetMarksBtn->setEnabled(!mBusy && mMap->anyMark());
    mSearchBtn->setEnabled(!mBusy && !mSearchWatcher->isRunning());
    if (mBusy) return;
    using Tool = OsmTileMap::Tool;
    if (mMap->tool() != Tool::Clip)
    {
        const QString what = mMap->tool() == Tool::Spawn ? tr("Spawn: click on the map — the player will appear here.")
                           : mMap->tool() == Tool::Stronghold ? tr("Stronghold: click — the entrance will be in the church nearest to this place.")
                           : mMap->tool() == Tool::City ? tr("Ancient City: click — the entrance will be in the church nearest to this place.")
                           : tr("Pale Garden: click — the forest nearest to this place becomes the Pale Garden.");
        mStatus->setText(what + tr(" The right button removes the mark. Marks can be placed now, before the data is downloaded; the boundary is mandatory."));
        return;
    }
    if (ok) mStatus->setText(tr("The boundary is set: %1 km². Press \"Download the area\" — the data is loaded from the OpenStreetMap servers. The spawn and the entrances can be marked now or after the download.").arg(mMap->clipAreaKm2(), 0, 'f', 2));
    else if (mMap->hasClip()) mStatus->setText(tr("The boundary is not suitable: %1 (up to %2 km² is allowed). Change it by dragging the vertices.").arg(why).arg(kTmcMaxOnlineAreaKm2, 0, 'f', 0));
    else mStatus->setText(tr("Find a place and draw the boundary of the area: clicks add vertices, a click on the first vertex closes it (mandatory, not more than %1 km²). The right button deletes a vertex.").arg(kTmcMaxOnlineAreaKm2, 0, 'f', 0));
}

void OsmOnlinePanel::doSearch()
{
    const QString q = mQuery->text().trimmed();
    if (q.isEmpty() || mSearchWatcher->isRunning()) return;
    mSearchBtn->setEnabled(false);
    mStatus->setText(tr("Searching…"));
    mSearchWatcher->setFuture(QtConcurrent::run([q]() -> SearchOut { SearchOut o; o.places = tmcSearchPlaces(q, &o.error); return o; }));
}

void OsmOnlinePanel::onSearchDone()
{
    const SearchOut o = mSearchWatcher->result();
    mPlaces = o.places;
    mResults->clear();
    for (const OsmPlace& p : mPlaces) mResults->addItem(p.name);
    mResults->setVisible(mPlaces.size() > 1);
    if (!o.error.isEmpty()) mStatus->setText(tr("The search failed: %1").arg(o.error));
    else if (mPlaces.isEmpty()) mStatus->setText(tr("Nothing found."));
    else
    {
        mResults->setCurrentIndex(0);
        emit mResults->activated(0);                              // show the first variant right away
        refreshState();
    }
    mSearchBtn->setEnabled(!mBusy);
}

QString OsmOnlinePanel::currentPlaceSlug() const
{
    QString s = mPlaceName;
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    return s;
}

void OsmOnlinePanel::startDownload(const QString& outDir)
{
    if (mBusy || !mMap->clipAllowed()) return;
    mDownClip = mMap->clipLonLat();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString slug = currentPlaceSlug();
    mOutFile = QDir(outDir).filePath(QStringLiteral("osm_%1%2.osm").arg(slug.isEmpty() ? QString() : slug + QLatin1Char('_'), stamp));
    mBusy = true;
    mCancel->store(false);
    mCancelBtn->show();
    refreshState();
    mStatus->setText(tr("Downloading OpenStreetMap data… If the area is too large for the official API, Overpass will be used — this can take several minutes."));
    const QVector<QPointF> clip = mDownClip;
    const QString file = mOutFile;
    const auto cancel = mCancel;
    // the worker thread reports the steps (including the switch to the Overpass fallback); the text goes to the status line through the GUI thread
    QPointer<OsmOnlinePanel> self(this);
    const auto report = [self](const QString& msg)
    {
        if (self) QMetaObject::invokeMethod(self.data(), [self, msg]() { if (self && self->mBusy) self->mStatus->setText(msg); }, Qt::QueuedConnection);
    };
    mDownWatcher->setFuture(QtConcurrent::run([clip, file, cancel, report]() { return tmcDownloadOsm(clip, file, cancel.get(), report); }));
}

void OsmOnlinePanel::cancelDownload()
{
    mCancel->store(true);
    mStatus->setText(tr("Cancelling…"));
}
