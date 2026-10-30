#include "app/mainwindow.h"
#include "ui_mainwindow.h"
#include "app/exportsettingsdialog.h"
#include "app/mappreview.h"
#include "app/i18n.h"
#include "app/osmonlinepanel.h"
#include "app/osmtilemap.h"
#include "app/uimetrics.h"
#include "export/climate.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QProcess>
#include <QRandomGenerator>
#include <cmath>

namespace
{
const char* kStageNames[] = { QT_TRANSLATE_NOOP("MainWindow", "Loading the project"), QT_TRANSLATE_NOOP("MainWindow", "Building object models"), QT_TRANSLATE_NOOP("MainWindow", "Export: objects"), QT_TRANSLATE_NOOP("MainWindow", "Export: ground and vegetation"), QT_TRANSLATE_NOOP("MainWindow", "Export: chunks"), QT_TRANSLATE_NOOP("MainWindow", "Export: writing the zip") };
}

MainWindow::MainWindow(const TmcRunOptions& initial, bool debug, QWidget* parent) : QMainWindow(parent), mInit(initial), mDebug(debug), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    {   // the rows of the local preview are put into containers: a hidden container takes no room (a hidden layout with spacers and margins would still shift the online map)
        auto wrap = [this](QLayout* row, int index)
        {
            ui->rightLayout->removeItem(row);
            auto* box = new QWidget;
            row->setContentsMargins(0, 0, 0, 0);
            box->setLayout(row);
            ui->rightLayout->insertWidget(index, box);
            mLocalRows.append(box);
        };
        wrap(ui->rowSpawn, 4);          // the order in the .ui: rowPreviewTop, rowTools, preview, spawnInfoLabel, rowSpawn
        wrap(ui->rowTools, 1);
        wrap(ui->rowPreviewTop, 0);
    }
    if (!debug)
    {   // the service fields are hidden: always the default files (next to the program), real meters on; the source is an OSM file
        applyEditionTexts();
        for (QWidget* w : std::initializer_list<QWidget*>{ ui->btnFolder, ui->labelT3d, ui->t3dEdit, ui->btnT3d, ui->labelOsmRules, ui->osmRulesEdit, ui->btnOsmRules,
                                                           ui->labelEnhancer, ui->enhancerEdit, ui->btnEnhancer, ui->realMetersCheck })
            w->hide();
    }
    {   // the interface language selector in the header: the choice is saved and takes effect after a restart
        ui->leftLayout->removeWidget(ui->titleLabel);
        auto* hdrBox = new QWidget;                                       // the header band has the same height as the search row / "Load the map" row on the right
        hdrBox->setFixedHeight(kHeaderH);
        auto* hdr = new QHBoxLayout(hdrBox);
        hdr->setContentsMargins(0, 0, 0, 0);
        hdr->addWidget(ui->titleLabel, 1, Qt::AlignVCenter);
        mOpenProjBtn = new QPushButton(tr("Open project…"));
        mSaveProjBtn = new QPushButton(tr("Save project…"));
        mOpenProjBtn->setToolTip(tr("Open a project file: the map view, the boundary, the marks and the world options"));
        mSaveProjBtn->setToolTip(tr("Save the map view, the boundary, the marks and the world options into a project file"));
        hdr->addWidget(mOpenProjBtn, 0, Qt::AlignVCenter);
        hdr->addWidget(mSaveProjBtn, 0, Qt::AlignVCenter);
        hdr->addSpacing(12);
        connect(mOpenProjBtn, &QPushButton::clicked, this, &MainWindow::openProjectFile);
        connect(mSaveProjBtn, &QPushButton::clicked, this, &MainWindow::saveProjectFile);
        mLangLabel = new QLabel(tr("Language:"));
        hdr->addWidget(mLangLabel, 0, Qt::AlignVCenter);
        mLang = new QComboBox;
        mLang->addItem(tr("Auto (system language)"), QStringLiteral("auto"));
        mLang->addItem(QStringLiteral("English"), QStringLiteral("en"));
        mLang->addItem(QStringLiteral("\u0420\u0443\u0441\u0441\u043a\u0438\u0439"), QStringLiteral("ru"));      // native names of languages are not translated
        mLang->setCurrentIndex(std::max(0, mLang->findData(tmcLanguageInEffect())));
        mLang->setMinimumHeight(kControlH);
        hdr->addWidget(mLang, 0, Qt::AlignVCenter);
        ui->leftLayout->insertWidget(0, hdrBox);
        // the subtitle takes the band of the tool row on the right, so that the "Data" card starts on the level of the map
        ui->subtitleLabel->setFixedHeight(kControlH);
        ui->subtitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        ui->rowTools->setContentsMargins(0, 0, 0, kCardTitleH);       // (the row is already inside its container)
        connect(mLang, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onLanguageChosen);
    }
    {   // the tool hint always takes the room of three lines so that changing the text does not move the rest of the interface
        QLabel* l = ui->previewStatusLabel;
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        l->setFixedHeight(kHeaderH);
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);         // the layout defines the width, a long text does not stretch it
        ui->loadPreviewButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->rowPreviewTop->setAlignment(ui->loadPreviewButton, Qt::AlignVCenter);
    }
    {   // map tools: one is active; a click on the map does what is selected
        auto* group = new QButtonGroup(this);
        const std::pair<QAbstractButton*, MapPreview::Tool> tools[] = {
            { ui->toolSpawn, MapPreview::Tool::Spawn }, { ui->toolClip, MapPreview::Tool::Clip }, { ui->toolStronghold, MapPreview::Tool::Stronghold },
            { ui->toolCity, MapPreview::Tool::City }, { ui->toolPale, MapPreview::Tool::Pale } };
        for (const auto& t : tools)
        {
            group->addButton(t.first);
            const MapPreview::Tool tool = t.second;
            connect(t.first, &QAbstractButton::toggled, this, [this, tool](bool on) { if (on) { ui->preview->setTool(tool); updateClipInfo(); } });
        }
        group->setExclusive(true);
    }
    {   // On the right there is the online OSM map by default (search, boundary, data download); as soon as a local file is chosen on the left its preview is shown,
        // and clearing the file field (the cross) brings the online map back.
        mOnline = new OsmOnlinePanel;
        ui->rightLayout->insertWidget(ui->rightLayout->indexOf(ui->preview), mOnline, 1);
        connect(mOnline, &OsmOnlinePanel::downloadRequested, this, [this]() { mOnline->startDownload(defaultOutputDir()); });
        connect(mOnline, &OsmOnlinePanel::downloaded, this, &MainWindow::onDownloaded);
        ui->sourceEdit->setClearButtonEnabled(true);
        // climate zone: "auto" is detected from the data coordinates, the other items are a manual choice
        mClimateLabel = new QLabel(tr("Climate:"));
        auto* lbl = mClimateLabel;
        mClimate = new QComboBox;
        mClimate->addItem(tr("Auto (by data coordinates)"), -1);
        for (int z = 0; z < Climate::Count; ++z) mClimate->addItem(Climate::name(z), z);
        mClimate->setToolTip(tr("Tree species, surface and biome inside the export area; the world around the area is not changed by this."));
        mClimate->setMinimumHeight(kControlH);
        ui->gridWorld->addWidget(lbl, 6, 0);
        ui->gridWorld->addWidget(mClimate, 6, 1, 1, 2);
    }
    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setSizes({ 700, 620 });
    ui->progressBar->setRange(0, 100);

    connect(ui->btnProject, &QPushButton::clicked, this, &MainWindow::browseProject);
    connect(ui->btnFolder, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(ui->btnT3d, &QPushButton::clicked, this, &MainWindow::browseT3d);
    connect(ui->btnOsmRules, &QPushButton::clicked, this, &MainWindow::browseOsmRules);
    connect(ui->btnEnhancer, &QPushButton::clicked, this, &MainWindow::browseEnhancer);
    connect(ui->btnOut, &QPushButton::clicked, this, &MainWindow::browseOut);
    connect(ui->genSeedButton, &QPushButton::clicked, this, &MainWindow::generateSeed);
    connect(ui->goButton, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(ui->exportDirButton, &QPushButton::clicked, this, &MainWindow::openExportSettings);
    connect(ui->cancelButton, &QPushButton::clicked, this, &MainWindow::cancelExport);
    connect(ui->flatRadio, &QRadioButton::toggled, this, [this](bool flat)
    {
        ui->cavesCheck->setEnabled(!flat); ui->seedEdit->setEnabled(!flat); ui->genSeedButton->setEnabled(!flat);
    });

    connect(ui->loadPreviewButton, &QPushButton::clicked, this, &MainWindow::loadPreview);
    connect(ui->resetSpawnButton, &QPushButton::clicked, this, &MainWindow::resetSpawn);
    connect(ui->copyTpButton, &QPushButton::clicked, this, &MainWindow::copyTp);
    connect(ui->preview, &MapPreview::spawnPicked, this, &MainWindow::onSpawnPicked);
    connect(ui->clipClearButton, &QPushButton::clicked, this, [this]() { ui->preview->clearClip(); });
    connect(ui->resetMarksButton, &QPushButton::clicked, this, [this]() { ui->preview->clearMarks(); });
    connect(ui->preview, &MapPreview::clipChanged, this, &MainWindow::updateClipInfo);
    connect(ui->preview, &MapPreview::marksChanged, this, &MainWindow::updateClipInfo);
    connect(ui->sourceEdit, &QLineEdit::textChanged, this, &MainWindow::onSourceChanged);
    // the path was typed by hand — load the preview when the input is finished
    connect(ui->sourceEdit, &QLineEdit::editingFinished, this, [this]()
    {
        const QString t = ui->sourceEdit->text().trimmed();
        if (!t.isEmpty() && !ui->preview->hasPreview() && QFileInfo::exists(t)) loadPreview();
    });

    mPrevWatcher = new QFutureWatcher<TmcPreview>(this);
    connect(mPrevWatcher, &QFutureWatcher<TmcPreview>::finished, this, &MainWindow::onPreviewReady);
    mWatcher = new QFutureWatcher<TmcRunResult>(this);
    connect(mWatcher, &QFutureWatcher<TmcRunResult>::finished, this, &MainWindow::onFinished);

    loadSettings(initial);
    updateSpawnInfo();
    updateClipInfo();
    if (mInit.source.endsWith(QLatin1String(".tmc"), Qt::CaseInsensitive))
    {   // a project file was given as the source (a double click / the command line): the window opens it
        const QString pf = mInit.source;
        ui->sourceEdit->clear();
        QTimer::singleShot(0, this, [this, pf]() { QString err; readProject(pf, &err); if (!err.isEmpty()) QMessageBox::warning(this, tr("tMC"), err); });
    }
    setOnlineMode(ui->sourceEdit->text().trimmed().isEmpty());
    if (!ui->sourceEdit->text().trimmed().isEmpty() && QFileInfo::exists(ui->sourceEdit->text().trimmed()))
        QTimer::singleShot(0, this, &MainWindow::loadPreview);            // a file from the command line — preview right away
}

//! The file field changed: empty — the online map again; a file is chosen — the online map is hidden and the data preview is built (after choosing via "Browse…")
void MainWindow::onSourceChanged()
{
    mPreviewObjects = -1;
    ui->preview->clearPreview();
    ui->previewStatusLabel->clear();
    updateSpawnInfo();
    const QString t = ui->sourceEdit->text().trimmed();
    setOnlineMode(t.isEmpty());
    if (!t.isEmpty() && mAutoLoad && QFileInfo::exists(t)) QTimer::singleShot(0, this, [this]() { if (!mPrevWatcher->isRunning()) loadPreview(); });
}

MainWindow::~MainWindow() = default;

void MainWindow::loadSettings(const TmcRunOptions& init)
{
    QSettings s;
    auto str = [&](const char* key, const QString& cli) { return !cli.isEmpty() ? cli : s.value(QLatin1String(key)).toString(); };
    // a saved path to a file that no longer exists (the program was moved) is dropped — the default next to the exe is used
    auto file = [&](const char* key, const QString& cli) { const QString v = str(key, cli); return (cli.isEmpty() && !QFileInfo::exists(v)) ? QString() : v; };
    mAutoLoad = false;                                     // at startup a file from the command line loads the preview separately, below
    ui->sourceEdit->setText(init.source);                  // the previously used file is not restored: the window always opens with the online map
    mAutoLoad = true;
    // without -debug the saved paths are not used — only what is explicitly set on the command line
    ui->t3dEdit->setText(mDebug ? file("t3d", init.t3dPath) : init.t3dPath);
    ui->osmRulesEdit->setText(mDebug ? file("osmRules", init.osmRulesPath) : init.osmRulesPath);
    ui->enhancerEdit->setText(mDebug ? file("enhancer", init.enhancerPath) : init.enhancerPath);
    updateDefaultHints();
    ui->outEdit->setText(init.outPath);
    const bool legacy = init.format == McWorldExporter::Format::Legacy121x
                        || (init.format == McWorldExporter::Format::Native26x && s.value(QStringLiteral("format"), 0).toInt() == 1 && init.source.isEmpty());
    (legacy ? ui->fmt121Radio : ui->fmt26Radio)->setChecked(true);
    const bool noise = init.noiseWorld;
    (noise ? ui->noiseRadio : ui->flatRadio)->setChecked(true);
    ui->cavesCheck->setChecked(init.caves);
    ui->realMetersCheck->setChecked(mDebug ? init.realMeters : true);
    mExportDest = s.value(QStringLiteral("exportDest"), 0).toInt();
    mSavesLauncher = s.value(QStringLiteral("savesLauncher")).toString();
    mSavesModrinth = s.value(QStringLiteral("savesModrinth")).toString();
    mSavesCurse = s.value(QStringLiteral("savesCurse")).toString();
    updateExportDestUi();
    ui->datapackCheck->setChecked(s.value(QStringLiteral("locateDatapack"), init.locateDatapack).toBool() && init.locateDatapack);
    ui->seedEdit->setText(init.seed ? QString::number(init.seed) : QString());
    ui->cavesCheck->setEnabled(noise); ui->seedEdit->setEnabled(noise); ui->genSeedButton->setEnabled(noise);
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("source"), ui->sourceEdit->text());
    if (mDebug)
    {
        s.setValue(QStringLiteral("t3d"), ui->t3dEdit->text());
        s.setValue(QStringLiteral("osmRules"), ui->osmRulesEdit->text());
        s.setValue(QStringLiteral("enhancer"), ui->enhancerEdit->text());
    }
    s.setValue(QStringLiteral("locateDatapack"), ui->datapackCheck->isChecked());
    s.setValue(QStringLiteral("format"), ui->fmt121Radio->isChecked() ? 1 : 0);
}

QString MainWindow::resolvedSavesDir() const
{
    switch (mExportDest)
    {
    case 1:                                       // the launcher folder picked by the user; .minecraft itself means "saves" inside it
        {
            if (mSavesLauncher.isEmpty() || !QFileInfo::exists(mSavesLauncher)) return QString();
            QString d = QDir::cleanPath(mSavesLauncher);
            if (QFileInfo(d).fileName().compare(QStringLiteral(".minecraft"), Qt::CaseInsensitive) == 0)
                d = QDir(d).filePath(QStringLiteral("saves"));
            return d;
        }
    case 2:                                       // a Modrinth profile / CurseForge instance keeps its own saves folder
    case 3:
        {
            const QString inst = mExportDest == 2 ? mSavesModrinth : mSavesCurse;
            if (inst.isEmpty() || !QFileInfo::exists(inst)) return QString();
            return QDir(inst).filePath(QStringLiteral("saves"));
        }
    default:
        return QString();
    }
}

void MainWindow::openExportSettings()
{
    ExportSettingsDialog dlg(this, mExportDest, mSavesLauncher, mSavesModrinth, mSavesCurse);
    if (dlg.exec() != QDialog::Accepted) return;
    mExportDest = dlg.dest();
    mSavesLauncher = dlg.launcherDir();
    mSavesModrinth = dlg.modrinthProfile();
    mSavesCurse = dlg.curseInstance();
    QSettings s;
    s.setValue(QStringLiteral("exportDest"), mExportDest);
    s.setValue(QStringLiteral("savesLauncher"), mSavesLauncher);
    s.setValue(QStringLiteral("savesModrinth"), mSavesModrinth);
    s.setValue(QStringLiteral("savesCurse"), mSavesCurse);
    updateExportDestUi();
}

void MainWindow::updateExportDestUi()
{
    const QString dest = resolvedSavesDir();
    const bool zipMode = dest.isEmpty();
    ui->labelOut->setEnabled(zipMode);
    ui->outEdit->setEnabled(zipMode);
    ui->btnOut->setEnabled(zipMode);
    const QString tip = zipMode ? QString()
        : tr("The export destination chosen in \"Export settings\" overrides this field: %1").arg(QDir::toNativeSeparators(dest));
    ui->labelOut->setToolTip(tip);
    ui->outEdit->setToolTip(tip);
    ui->btnOut->setToolTip(tip);
}

TmcRunOptions MainWindow::collect() const
{
    TmcRunOptions o;
    o.source = ui->sourceEdit->text().trimmed();
    o.t3dPath = ui->t3dEdit->text().trimmed();
    o.osmRulesPath = ui->osmRulesEdit->text().trimmed();
    o.enhancerPath = ui->enhancerEdit->text().trimmed();
    o.outPath = ui->outEdit->text().trimmed();
    o.savesDir = resolvedSavesDir();          // the "Export settings" destination; empty — the zip as before
    o.format = ui->fmt121Radio->isChecked() ? McWorldExporter::Format::Legacy121x : McWorldExporter::Format::Native26x;
    o.noiseWorld = ui->noiseRadio->isChecked();
    o.caves = ui->cavesCheck->isChecked() && o.noiseWorld;
    bool ok = false;
    const QString st = ui->seedEdit->text().trimmed();
    o.seed = st.isEmpty() ? 0 : st.toULongLong(&ok);
    if (!st.isEmpty() && !ok) o.seed = 0;           // not a number — a random seed
    o.realMeters = mDebug ? ui->realMetersCheck->isChecked() : true;
    o.climate = mClimate->currentData().toInt();
    o.locateDatapack = ui->datapackCheck->isChecked();
    o.strongholdChunk = mInit.strongholdChunk;                                  // chunks from the command line; otherwise — the marks on the map
    o.cityChunk = mInit.cityChunk;
    o.paleForestChunk = mInit.paleForestChunk;
    using Tool = MapPreview::Tool;
    if (ui->preview->hasMark(Tool::Stronghold)) { o.hasStrongholdPt = true; o.strongholdPt = ui->preview->mark(Tool::Stronghold); }
    if (ui->preview->hasMark(Tool::City)) { o.hasCityPt = true; o.cityPt = ui->preview->mark(Tool::City); }
    if (ui->preview->hasMark(Tool::Pale)) { o.hasPalePt = true; o.palePt = ui->preview->mark(Tool::Pale); }
    o.clipPolygon = ui->preview->clipPolygon();
    if (ui->preview->hasSpawn()) { o.hasSpawn = true; o.spawnX = ui->preview->spawnX(); o.spawnY = ui->preview->spawnY(); }
    return o;
}

void MainWindow::browseProject()
{
    const QString f = mDebug
        ? QFileDialog::getOpenFileName(this, tr("tqt project file or OSM extract"), ui->sourceEdit->text(),
                                       tr("tqt project, OSM extract (*.tqt *.osm *.pbf);;tqt project (*.tqt);;OSM extract (*.osm *.pbf);;All files (*)"))
        : QFileDialog::getOpenFileName(this, tr("OpenStreetMap extract"), ui->sourceEdit->text(),
                                       tr("OSM extract (*.osm *.pbf);;All files (*)"));
    if (!f.isEmpty()) ui->sourceEdit->setText(QDir::toNativeSeparators(f));
}

void MainWindow::browseFolder()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("Folder with shapefiles"), ui->sourceEdit->text());
    if (!d.isEmpty()) ui->sourceEdit->setText(QDir::toNativeSeparators(d));
}

void MainWindow::browseT3d()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("3D model settings"), ui->t3dEdit->text(), tr("3D model settings (*.t3d);;All files (*)"));
    if (!f.isEmpty()) ui->t3dEdit->setText(QDir::toNativeSeparators(f));
}

void MainWindow::browseOsmRules()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("OSM rules"), ui->osmRulesEdit->text(), tr("OSM rules (*.json);;All files (*)"));
    if (!f.isEmpty()) ui->osmRulesEdit->setText(QDir::toNativeSeparators(f));
}

void MainWindow::browseEnhancer()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Enhancer file"), ui->enhancerEdit->text(), tr("Enhancer (*.dat);;All files (*)"));
    if (!f.isEmpty()) ui->enhancerEdit->setText(QDir::toNativeSeparators(f));
}

void MainWindow::browseOut()
{
    const QString f = QFileDialog::getSaveFileName(this, tr("Save the Minecraft world (zip archive)"), ui->outEdit->text(), tr("ZIP archive (*.zip)"));
    if (!f.isEmpty()) ui->outEdit->setText(QDir::toNativeSeparators(f.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) ? f : f + QStringLiteral(".zip")));
}

void MainWindow::generateSeed()
{
    ui->seedEdit->setText(QString::number(qint64(QRandomGenerator::global()->generate64() >> 2) + 1));
}

void MainWindow::setBusy(bool busy)
{
    mBusy = busy;
    ui->goButton->setEnabled(!busy && !mOnlineMode);
    ui->cancelButton->setEnabled(busy);
    ui->loadPreviewButton->setEnabled(!busy && !mPrevWatcher->isRunning());
}

void MainWindow::appendLog(const QString& s)
{
    ui->logEdit->appendPlainText(s);
}

void MainWindow::onProgress(int stage, int done, int total)
{
    mStageIdx = qBound(0, stage, 5); mStageDone = done; mStageTotal = total; mFinalState = 0;
    updateStageLabel();
    if (total > 0) { ui->progressBar->setRange(0, total); ui->progressBar->setValue(done); }
    else ui->progressBar->setRange(0, 0);                           // no counter — "busy"
}

void MainWindow::startExport()
{
    if (mBusy) return;
    saveSettings();
    ui->logEdit->clear();
    mStageIdx = -1; mFinalState = 0;
    mCancelFlag = std::make_shared<std::atomic_bool>(false);
    setBusy(true);
    ui->progressBar->setRange(0, 0);
    const TmcRunOptions opt = collect();
    auto flag = mCancelFlag;
    mWatcher->setFuture(QtConcurrent::run([this, opt, flag]() -> TmcRunResult
    {
        return tmcRunExport(opt,
            [this](const QString& s) { QMetaObject::invokeMethod(this, [this, s]() { appendLog(s); }, Qt::QueuedConnection); },
            [this](int stage, int done, int total) { QMetaObject::invokeMethod(this, [this, stage, done, total]() { onProgress(stage, done, total); }, Qt::QueuedConnection); },
            flag.get());
    }));
}

void MainWindow::cancelExport()
{
    if (mCancelFlag) *mCancelFlag = true;
    ui->cancelButton->setEnabled(false);
    appendLog(tr("Cancelling…"));
}

void MainWindow::onFinished()
{
    const TmcRunResult r = mWatcher->result();
    setBusy(false);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(r.ok ? 100 : 0);
    mFinalState = r.ok ? 1 : (r.cancelled ? 2 : 3);
    updateStageLabel();
    if (r.ok) showTextDialog(tr("tMC"), r.summary, r.outPath);
    else if (r.cancelled) QMessageBox::information(this, tr("tMC"), tr("Export cancelled."));
    else showTextDialog(tr("tMC"), r.error);
}

void MainWindow::showTextDialog(const QString& title, const QString& text, const QString& file)
{
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    auto* lay = new QVBoxLayout(&dlg);
    auto* edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lay->addWidget(edit, 1);
    QCheckBox* show = nullptr;
    QSettings st;
    if (!file.isEmpty() && QFileInfo::exists(file))
    {
        show = new QCheckBox(tr("Show the file in the folder"));
        show->setChecked(st.value(QStringLiteral("showFileAfterExport"), true).toBool());
        lay->addWidget(show);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(buttons);
    // the size follows the screen and the text: up to 70% of the screen, at least a comfortable size in device-independent pixels (so a 200% scale is fine)
    const QRect avail = (screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 800));
    const int lines = text.count(QLatin1Char('\n')) + 1;
    dlg.resize(std::min(int(avail.width() * 0.7), std::max(640, edit->fontMetrics().averageCharWidth() * 90)),
               std::min(int(avail.height() * 0.7), std::max(280, edit->fontMetrics().lineSpacing() * (lines + 6))));
    dlg.exec();
    if (show)
    {
        st.setValue(QStringLiteral("showFileAfterExport"), show->isChecked());
        if (show->isChecked())
        {   // explorer.exe /select,"file" opens the folder with the file selected
            QProcess ex;
            ex.setProgram(QStringLiteral("explorer.exe"));
            ex.setNativeArguments(QStringLiteral("/select,\"") + QDir::toNativeSeparators(file) + QLatin1Char('"'));
            ex.startDetached();
        }
    }
}

void MainWindow::loadPreview()
{
    if (mPrevWatcher->isRunning() || mBusy) return;
    ui->logEdit->clear();
    ui->loadPreviewButton->setEnabled(false);
    ui->previewStatusLabel->setText(tr("Loading the map…"));
    const TmcRunOptions opt = collect();
    mPrevWatcher->setFuture(QtConcurrent::run([this, opt]() -> TmcPreview
    {
        return tmcBuildPreview(opt, [this](const QString& s) { QMetaObject::invokeMethod(this, [this, s]() { appendLog(s); }, Qt::QueuedConnection); });
    }));
}

void MainWindow::onPreviewReady()
{
    const TmcPreview p = mPrevWatcher->result();
    ui->loadPreviewButton->setEnabled(!mBusy);
    if (!p.ok)
    {
        ui->previewStatusLabel->setText(tr("Could not load: %1").arg(p.error));
        return;
    }
    ui->preview->setPreview(p);
    {   // in the climate list "auto" shows which zone was detected for the data center
        double lon = 0, lat = 0;
        if (tmcProjectToLonLat(p.wkt, p.centerX, p.centerY, lon, lat))
        {
            mClimateAutoZone = Climate::fromLonLat(lon, lat);
            updateClimateItems();
        }
    }
    if (!mPendingClip.isEmpty())
    {   // the boundary of the downloaded area (longitude/latitude) -> the data projection
        QVector<QPointF> pts;
        for (const QPointF& ll : mPendingClip)
        {
            double x, y;
            if (tmcLonLatToProject(p.wkt, ll.x(), ll.y(), x, y)) pts << QPointF(x, y);
        }
        if (pts.size() == mPendingClip.size()) ui->preview->setClipPolygon(pts, true);
        mPendingClip.clear();
    }
    {   // the marks placed on the online map
        using PT = MapPreview::Tool;
        const PT kinds[4] = { PT::Spawn, PT::Stronghold, PT::City, PT::Pale };
        for (int i = 0; i < 4; ++i)
        {
            if (!mPendingSet[i]) continue;
            mPendingSet[i] = false;
            double x, y;
            if (!tmcLonLatToProject(p.wkt, mPendingPt[i].x(), mPendingPt[i].y(), x, y)) continue;
            if (kinds[i] == PT::Spawn) ui->preview->setSpawn(x, y);
            else ui->preview->setMark(kinds[i], QPointF(x, y));
        }
    }
    mPreviewObjects = p.objects;
    updateSpawnInfo();
    updateClipInfo();
}

void MainWindow::onSpawnPicked(double, double) { updateSpawnInfo(); }

//! The language was chosen: it is switched at once (Qt sends QEvent::LanguageChange to the windows, see changeEvent)
void MainWindow::onLanguageChosen(int index)
{
    const QString lang = mLang->itemData(index).toString();
    if (lang == tmcLanguageInEffect()) return;
    tmcSwitchLanguage(lang);
}

//! The project file (.tmc, JSON): where the map was, the boundary, the spawn and entrance marks (all in longitude/latitude, so they do not depend on the projection of the data),
//! the OSM file, if the data was loaded from one, and the world options. It lets a work on a map be put aside and continued.
bool MainWindow::writeProject(const QString& path)
{
    QJsonObject root;
    root.insert(QStringLiteral("tmcProject"), 1);
    QVector<QPointF> clip;
    QPointF marks[4];
    bool markSet[4] = { false, false, false, false };       // spawn, stronghold, city, Pale Garden
    OsmTileMap* map = mOnline->map();
    double vLon = map->centerLon(), vLat = map->centerLat(), vZoom = map->zoom();
    if (!mOnlineMode && ui->preview->hasPreview())
    {   // the data preview: the boundary and the marks are in the data projection — into longitude/latitude
        const TmcPreview& p = ui->preview->preview();
        auto toLL = [&](double x, double y, QPointF* out) { double lon = 0, lat = 0; if (!tmcProjectToLonLat(p.wkt, x, y, lon, lat)) return false; *out = QPointF(lon, lat); return true; };
        for (const QPointF& v : ui->preview->clipPolygon()) { QPointF ll; if (toLL(v.x(), v.y(), &ll)) clip << ll; }
        using PT = MapPreview::Tool;
        if (ui->preview->hasSpawn()) markSet[0] = toLL(ui->preview->spawnX(), ui->preview->spawnY(), &marks[0]);
        const PT kinds[3] = { PT::Stronghold, PT::City, PT::Pale };
        for (int i = 0; i < 3; ++i)
            if (ui->preview->hasMark(kinds[i])) markSet[i + 1] = toLL(ui->preview->mark(kinds[i]).x(), ui->preview->mark(kinds[i]).y(), &marks[i + 1]);
        QPointF c;
        if (toLL(p.centerX, p.centerY, &c)) { vLon = c.x(); vLat = c.y(); vZoom = std::max(vZoom, 12.0); }
    }
    else
    {
        clip = map->clipLonLat();
        using OT = OsmTileMap::Tool;
        const OT kinds[4] = { OT::Spawn, OT::Stronghold, OT::City, OT::Pale };
        for (int i = 0; i < 4; ++i) if (map->hasMark(kinds[i])) { markSet[i] = true; marks[i] = map->mark(kinds[i]); }
    }
    QJsonObject view;
    view.insert(QStringLiteral("lon"), vLon); view.insert(QStringLiteral("lat"), vLat); view.insert(QStringLiteral("zoom"), vZoom);
    root.insert(QStringLiteral("view"), view);
    QJsonArray cj;
    for (const QPointF& v : clip) cj.append(QJsonArray{ v.x(), v.y() });
    root.insert(QStringLiteral("clip"), cj);
    static const char* const kNames[4] = { "spawn", "stronghold", "city", "pale" };
    QJsonObject mj;
    for (int i = 0; i < 4; ++i) if (markSet[i]) mj.insert(QLatin1String(kNames[i]), QJsonArray{ marks[i].x(), marks[i].y() });
    root.insert(QStringLiteral("marks"), mj);
    const QString src = ui->sourceEdit->text().trimmed();
    if (!src.isEmpty()) root.insert(QStringLiteral("source"), src);
    QJsonObject opt;
    opt.insert(QStringLiteral("format121"), ui->fmt121Radio->isChecked());
    opt.insert(QStringLiteral("noiseWorld"), ui->noiseRadio->isChecked());
    opt.insert(QStringLiteral("seed"), ui->seedEdit->text().trimmed());
    opt.insert(QStringLiteral("caves"), ui->cavesCheck->isChecked());
    opt.insert(QStringLiteral("datapack"), ui->datapackCheck->isChecked());
    opt.insert(QStringLiteral("climate"), mClimate->currentData().toInt());
    opt.insert(QStringLiteral("out"), ui->outEdit->text().trimmed());
    root.insert(QStringLiteral("options"), opt);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool MainWindow::readProject(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *error = tr("Could not open the file: %1").arg(QDir::toNativeSeparators(path)); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject() || !doc.object().contains(QStringLiteral("tmcProject"))) { *error = tr("This is not a tMC project file: %1").arg(QDir::toNativeSeparators(path)); return false; }
    const QJsonObject root = doc.object();
    auto pt = [](const QJsonValue& v, QPointF* out) { const QJsonArray a = v.toArray(); if (a.size() != 2) return false; *out = QPointF(a[0].toDouble(), a[1].toDouble()); return true; };
    // the world options
    const QJsonObject opt = root.value(QStringLiteral("options")).toObject();
    if (!opt.isEmpty())
    {
        (opt.value(QStringLiteral("format121")).toBool() ? ui->fmt121Radio : ui->fmt26Radio)->setChecked(true);
        (opt.value(QStringLiteral("noiseWorld")).toBool(true) ? ui->noiseRadio : ui->flatRadio)->setChecked(true);
        ui->seedEdit->setText(opt.value(QStringLiteral("seed")).toString());
        ui->cavesCheck->setChecked(opt.value(QStringLiteral("caves")).toBool(true));
        ui->datapackCheck->setChecked(opt.value(QStringLiteral("datapack")).toBool(true));
        const int ci = mClimate->findData(opt.value(QStringLiteral("climate")).toInt(-1));
        mClimate->setCurrentIndex(ci >= 0 ? ci : 0);
        ui->outEdit->setText(opt.value(QStringLiteral("out")).toString());
    }
    QVector<QPointF> clip;
    for (const QJsonValue& v : root.value(QStringLiteral("clip")).toArray()) { QPointF p; if (pt(v, &p)) clip << p; }
    static const char* const kNames[4] = { "spawn", "stronghold", "city", "pale" };
    const QJsonObject mj = root.value(QStringLiteral("marks")).toObject();
    bool markSet[4] = { false, false, false, false };
    QPointF marks[4];
    for (int i = 0; i < 4; ++i) markSet[i] = pt(mj.value(QLatin1String(kNames[i])), &marks[i]);
    const QJsonObject view = root.value(QStringLiteral("view")).toObject();
    const QString src = root.value(QStringLiteral("source")).toString();
    if (!src.isEmpty() && QFileInfo::exists(src))
    {   // the data file is there: it is loaded, the boundary and the marks are put on its preview when it is ready
        mPendingClip = clip;
        for (int i = 0; i < 4; ++i) { mPendingSet[i] = markSet[i]; mPendingPt[i] = marks[i]; }
        ui->sourceEdit->setText(QDir::toNativeSeparators(src));
        return true;
    }
    // the online map: the view, the boundary and the marks are restored on it
    ui->sourceEdit->clear();
    OsmTileMap* map = mOnline->map();
    if (view.contains(QStringLiteral("lon"))) map->setView(view.value(QStringLiteral("lon")).toDouble(), view.value(QStringLiteral("lat")).toDouble(), view.value(QStringLiteral("zoom")).toDouble(12.0));
    map->clearMarks();
    map->setClipLonLat(clip, clip.size() >= 3);
    using OT = OsmTileMap::Tool;
    const OT kinds[4] = { OT::Spawn, OT::Stronghold, OT::City, OT::Pale };
    for (int i = 0; i < 4; ++i) if (markSet[i]) map->setMark(kinds[i], marks[i]);
    if (!src.isEmpty()) *error = tr("The data file of the project was not found: %1").arg(QDir::toNativeSeparators(src));
    return true;
}

void MainWindow::saveProjectFile()
{
    const QString dir = defaultOutputDir();
    QDir().mkpath(dir);
    QString f = QFileDialog::getSaveFileName(this, tr("Save the project"), QDir(dir).filePath(QStringLiteral("project.tmc")), tr("tMC project (*.tmc);;All files (*)"));
    if (f.isEmpty()) return;
    if (QFileInfo(f).suffix().isEmpty()) f += QStringLiteral(".tmc");
    if (!writeProject(f)) QMessageBox::warning(this, tr("tMC"), tr("Could not write the file: %1").arg(QDir::toNativeSeparators(f)));
}

void MainWindow::openProjectFile()
{
    if (mBusy) return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open a project"), defaultOutputDir(), tr("tMC project (*.tmc);;All files (*)"));
    if (f.isEmpty()) return;
    QString err;
    const bool ok = readProject(f, &err);
    if (!err.isEmpty()) QMessageBox::warning(this, tr("tMC"), err);
    if (!ok) return;
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::LanguageChange && ui && mLang) retranslate();
}

//! The texts of the simple (non-debug) window: the source is always an OSM file
void MainWindow::applyEditionTexts()
{
    ui->labelSource->setText(tr("OSM file:"));
    ui->subtitleLabel->setText(tr("Export an OpenStreetMap area to a Minecraft world"));
    ui->btnProject->setText(tr("Browse…"));
    ui->sourceEdit->setPlaceholderText(tr("an OpenStreetMap extract (.osm / .pbf)"));
    ui->outEdit->setPlaceholderText(tr("empty — <file name>.zip in the folder of the OSM file"));
}

//! An empty field = the file next to the program; show which one exactly
void MainWindow::updateDefaultHints()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    auto hint = [&](QLineEdit* e, const char* name)
    {
        const QString p = appDir.filePath(QLatin1String(name));
        if (QFileInfo::exists(p)) e->setPlaceholderText(tr("default: %1").arg(QDir::toNativeSeparators(p)));
    };
    hint(ui->t3dEdit, "default.t3d");
    hint(ui->osmRulesEdit, "osm_rules.json");
    hint(ui->enhancerEdit, "tmc_buildings.dat");
}

void MainWindow::updateClimateItems()
{
    mClimate->setItemText(0, mClimateAutoZone >= 0 ? tr("Auto: %1").arg(Climate::name(mClimateAutoZone)) : tr("Auto (by data coordinates)"));
    for (int z = 0; z < Climate::Count; ++z) mClimate->setItemText(z + 1, Climate::name(z));
}

void MainWindow::updateStageLabel()
{
    if (mFinalState)
    {
        ui->stageLabel->setText(mFinalState == 1 ? tr("Done") : (mFinalState == 2 ? tr("Cancelled") : tr("Error")));
        return;
    }
    if (mStageIdx < 0) { ui->stageLabel->clear(); return; }
    const QString stageName = tr(kStageNames[qBound(0, mStageIdx, 5)]);
    ui->stageLabel->setText(mStageTotal > 0 ? tr("%1 — %2%").arg(stageName).arg(int(100.0 * mStageDone / mStageTotal)) : stageName);
}

//! The language changed: the texts from the .ui are reset by retranslateUi, the ones set in the code — here
void MainWindow::retranslate()
{
    ui->retranslateUi(this);
    if (!mDebug) applyEditionTexts();
    mLangLabel->setText(tr("Language:"));
    mOpenProjBtn->setText(tr("Open project…"));
    mSaveProjBtn->setText(tr("Save project…"));
    mOpenProjBtn->setToolTip(tr("Open a project file: the map view, the boundary, the marks and the world options"));
    mSaveProjBtn->setToolTip(tr("Save the map view, the boundary, the marks and the world options into a project file"));
    mLang->setItemText(0, tr("Auto (system language)"));
    mClimateLabel->setText(tr("Climate:"));
    mClimate->setToolTip(tr("Tree species, surface and biome inside the export area; the world around the area is not changed by this."));
    updateClimateItems();
    updateDefaultHints();
    updateSpawnInfo();
    updateClipInfo();
    updateStageLabel();
}

//! Where to put the downloaded data: the folder of the chosen world file, otherwise "Documents\tMC"
QString MainWindow::defaultOutputDir() const
{
    const QString out = ui->outEdit->text().trimmed();
    if (!out.isEmpty()) return QFileInfo(out).absolutePath();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/tMC");
}

//! Online: on the right the OSM map with search and boundary, on the left only the world file choice and the parameters; local: the usual data preview
void MainWindow::setOnlineMode(bool online)
{
    mOnlineMode = online;
    for (QWidget* w : std::initializer_list<QWidget*>{ ui->loadPreviewButton, ui->previewStatusLabel,
                                                       ui->toolSpawn, ui->toolClip, ui->toolStronghold, ui->toolCity, ui->toolPale, ui->preview,
                                                       ui->spawnInfoLabel, ui->resetSpawnButton, ui->copyTpButton, ui->clipClearButton, ui->resetMarksButton })
        w->setVisible(!online);
    for (QWidget* w : mLocalRows) w->setVisible(!online);
    mOnline->setVisible(online);
    ui->goButton->setEnabled(!online && !mBusy);
    if (!online) updateClipInfo();
}

//! The data is downloaded: open it as an ordinary local file, the boundary is moved onto the map once the preview is built
void MainWindow::onDownloaded(const QString& file, const QVector<QPointF>& clipLonLat)
{
    mPendingClip = clipLonLat;
    mAutoLoad = false;                                     // we start the preview ourselves below, after the marks are transferred
    using OT = OsmTileMap::Tool;
    const OT kinds[4] = { OT::Spawn, OT::Stronghold, OT::City, OT::Pale };
    for (int i = 0; i < 4; ++i)
    {
        mPendingSet[i] = mOnline->map()->hasMark(kinds[i]);
        if (mPendingSet[i]) mPendingPt[i] = mOnline->map()->mark(kinds[i]);
    }
    ui->sourceEdit->setText(QDir::toNativeSeparators(file));       // textChanged switches to local mode and resets the old preview
    mAutoLoad = true;
    loadPreview();
}

//! Button states and the hint for the polygon boundary
void MainWindow::updateClipInfo()
{
    const bool have = ui->preview->hasPreview();
    for (QAbstractButton* b : std::initializer_list<QAbstractButton*>{ ui->toolSpawn, ui->toolClip, ui->toolStronghold, ui->toolCity, ui->toolPale })
        b->setEnabled(have);
    ui->clipClearButton->setEnabled(have && ui->preview->hasClip());
    ui->resetMarksButton->setEnabled(have && ui->preview->anyMark());
    if (!have) return;
    using Tool = MapPreview::Tool;
    QString hint;
    switch (ui->preview->tool())
    {
    case Tool::Spawn: hint = tr("Spawn: click on the map — the player will appear here."); break;
    case Tool::Clip: hint = tr("Boundary: clicks add vertices, a click on the first (large) one closes it. Vertices can be dragged, a click on an edge adds a vertex, the right button deletes."); break;
    case Tool::Stronghold: hint = tr("Stronghold: click — the entrance will be in the church nearest to this place. The right button removes the mark."); break;
    case Tool::City: hint = tr("Ancient City: click — the entrance will be in the church nearest to this place. The right button removes the mark."); break;
    case Tool::Pale: hint = tr("Pale Garden: click — the forest nearest to this place becomes the Pale Garden. The right button removes the mark."); break;
    }
    if (ui->preview->hasClip()) hint += tr(" The export covers only the inside of the boundary; the world origin is the center of the clipped data.");
    if (mPreviewObjects >= 0) hint = tr("Objects on the map: %1. ").arg(mPreviewObjects) + hint;
    ui->previewStatusLabel->setText(hint);
    ui->previewStatusLabel->setWordWrap(true);
}

void MainWindow::resetSpawn()
{
    ui->preview->clearSpawn();
    updateSpawnInfo();
}

//! The label under the map: where the spawn point is (latitude/longitude and world blocks for /tp)
void MainWindow::updateSpawnInfo()
{
    if (!ui->preview->hasPreview())
    {
        ui->spawnInfoLabel->setText(tr("Spawn point: the data center. To choose another, load the map and click on it."));
        ui->copyTpButton->setEnabled(false);
        ui->resetSpawnButton->setEnabled(false);
        return;
    }
    const TmcPreview& p = ui->preview->preview();
    const bool chosen = ui->preview->hasSpawn();
    const double x = chosen ? ui->preview->spawnX() : p.centerX, y = chosen ? ui->preview->spawnY() : p.centerY;
    // world block: (0, 0) is the data center; the Z axis points south
    const int bx = int(std::lround((x - p.centerX) * p.unitsToMetersX)), bz = int(std::lround((p.centerY - y) * p.unitsToMetersY));
    double lon = 0, lat = 0;
    const bool haveLL = tmcProjectToLonLat(p.wkt, x, y, lon, lat);
    QString t = chosen ? tr("Spawn point chosen: ") : tr("Spawn point: the data center. ");
    if (haveLL) t += tr("latitude %1, longitude %2; ").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
    t += tr("in the world roughly block X=%1, Z=%2 (the data center is 0, 0).").arg(bx).arg(bz);
    ui->spawnInfoLabel->setText(t);
    ui->copyTpButton->setEnabled(true);
    ui->resetSpawnButton->setEnabled(chosen);
}

void MainWindow::copyTp()
{
    if (!ui->preview->hasPreview()) return;
    const TmcPreview& p = ui->preview->preview();
    const bool chosen = ui->preview->hasSpawn();
    const double x = chosen ? ui->preview->spawnX() : p.centerX, y = chosen ? ui->preview->spawnY() : p.centerY;
    const int bx = int(std::lround((x - p.centerX) * p.unitsToMetersX)), bz = int(std::lround((p.centerY - y) * p.unitsToMetersY));
    const QString cmd = QStringLiteral("/tp @s %1 120 %2").arg(bx).arg(bz);
    QApplication::clipboard()->setText(cmd);
    ui->previewStatusLabel->setText(tr("Copied: %1").arg(cmd));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (mBusy)
    {
        if (QMessageBox::question(this, tr("tMC"), tr("An export is running. Cancel it and quit?")) != QMessageBox::Yes) { e->ignore(); return; }
        if (mCancelFlag) *mCancelFlag = true;
        mWatcher->waitForFinished();
    }
    if (mPrevWatcher->isRunning()) mPrevWatcher->waitForFinished();
    saveSettings();
    e->accept();
}
