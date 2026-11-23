#pragma once
#include <QMainWindow>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
#include "app/tmcrun.h"

namespace Ui { class MainWindow; }
class OsmOnlinePanel;
class QPushButton;
class QComboBox;
class QLabel;
class QEvent;
class QButtonGroup;
class QAbstractButton;

//! The exporter window: an OpenStreetMap file (or a folder with shapefiles typed in by hand),
//! world parameters, progress and the log; on the right a basic map preview
//! with the spawn point chosen by a click. The layout is in mainwindow.ui (edit it in Qt Designer), only the logic is here.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    //! the service paths (rules catalog, OSM rules, enhancer) and "real meters" are set on the command line only
    explicit MainWindow(const TmcRunOptions& initial, QWidget* parent = nullptr);
    ~MainWindow() override;
    //! The project file without dialogs (for the command-line checks): write / read it
    bool saveProjectTo(const QString& path) { return writeProject(path); }
    bool openProjectFrom(const QString& path, QString* error) { return readProject(path, error); }

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;                 // QEvent::LanguageChange: all the texts are set again
    bool eventFilter(QObject* watched, QEvent* event) override;   // the export-destination label: a click opens its folder

private slots:
    void browseOsm();                                 //!< "OSM file" row: an .osm/.pbf extract
    void openExportSettings();                        //!< "Export settings": the destination of the exported world (zip / launcher / instance)
    void openEncountersDialog();                      //!< "Events...": the random city events switched on for the export
    void generateSeed();
    void startExport();
    void cancelExport();
    void onFinished();
    void loadPreview();
    void onPreviewReady();
    void onSpawnPicked(double x, double y);
    void resetSpawn();
    void copyTp();
    void openFigures();                                     //!< "3D figures": choose a model, then click on the map where it stands

private:
    TmcRunOptions collect() const;
    void setBusy(bool busy);
    void appendLog(const QString& s);
    void onProgress(int stage, int done, int total);
    void loadSettings(const TmcRunOptions& initial);
    void saveSettings() const;
    void updateSpawnInfo();
    void updateClipInfo();
    //! 3D figures placed in the window (a user request): the window owns the list in longitude/latitude (so it survives
    //! preview rebuilds), the map only draws and reports clicks. The per-instance settings never reach landmarks.json.
    void pushLandmarksToMap();
    void placeLandmarkAt(const QPointF& projectPt);
    void editLandmark(int index);
    void removeLandmark(int index);

    void setOnlineMode(bool online);
    void onDownloaded(const QString& file, const QVector<QPointF>& clipLonLat);
    QString defaultOutputDir() const;

    void onSourceChanged();
    //! The result/error of an export in a window with a scrollable text field (a message box does not fit long texts and a 200% screen scale);
    //! file — the finished archive: then the window has the checkbox "Show the file in the folder"
    void showTextDialog(const QString& title, const QString& text, const QString& file = QString());
    void saveProjectFile();                                //!< "Save project…": the map view, the boundary, the marks and the world options into a .tmc file
    QString resolvedSavesDir() const;                      //!< saves folder for the chosen export destination; empty — the zip (the saved path is gone)
    QString exportFolderToOpen() const;                    //!< the folder opened by a click on the export-destination label (saves / output / source)
    void updateExportDestUi();                             //!< the "Export: …" label above the buttons (a chosen instance is named there)
    void openProjectFile();                                //!< "Open project…"
    bool writeProject(const QString& path);
    bool readProject(const QString& path, QString* error);
    void retranslate();                                    //!< sets again every text that is set in the code (the .ui ones — by retranslateUi)
    void updateClimateItems();                             //!< the names in the climate list
    void updateClimateAutoZone();                          //!< re-detects the "auto" zone: the export boundary's center, else the loaded data's center
    void updateStageLabel();                               //!< the stage caption under the progress bar

    QComboBox* mClimate = nullptr;
    QVector<QWidget*> mLocalRows;       //!< the rows of the local preview (top row, tools, spawn buttons), hidden as a whole in the online mode
    QComboBox* mLang = nullptr;         //!< the interface language selector (auto / English / Russian / Chinese)
    QLabel* mLangLabel = nullptr;
    QPushButton* mOpenProjBtn = nullptr;
    QPushButton* mSaveProjBtn = nullptr;
    QLabel* mClimateLabel = nullptr;
    int mExportDest = 0;              //!< export destination setting: 0 — zip, 1 — launcher saves, 2 — Modrinth, 3 — CurseForge
    QString mSavesLauncher, mSavesModrinth, mSavesCurse;   //!< saved choices of the "Export settings" dialog
    QStringList mEncountersOff;       //!< the event ids switched off in the "City events" window (QSettings)
    QString mOutPath;                 //!< the world zip path (command line -o / the project file): the window has no field for it since 1.7
    int mClimateAutoZone = -1;          //!< the climate zone detected for the data (-1 — not known yet)
    int mPreviewObjects = -1;           //!< the object count of the loaded preview (-1 — no preview)
    int mStageIdx = -1, mStageDone = 0, mStageTotal = 0;   //!< the current export stage (for the caption)
    int mFinalState = 0;                //!< 0 — running/idle, 1 — done, 2 — cancelled, 3 — error
    void onLanguageChosen(int index);
    bool mAutoLoad = true;              //!< build the preview right away when the file changes
    OsmOnlinePanel* mOnline = nullptr;
    bool mOnlineMode = false;
    bool mPendingSet[4] = { false, false, false, false };       //!< marks of the online map (spawn, stronghold, city, forest), longitude/latitude
    QPointF mPendingPt[4];
    QButtonGroup* mToolGroup = nullptr;                         //!< the map tool buttons (the picking mode unhooks the active one while it lasts)
    QAbstractButton* mRestoreBtn = nullptr;                     //!< the tool button to re-check when the picking mode ends
    bool mPickActive = false;                                   //!< waiting for the click that places mPickPreset
    LandmarkPreset mPickPreset;                                 //!< the model chosen in the "3D figures" window
    QVector<LandmarkPreset> mLandmarks;                         //!< the placed figures with their per-instance settings (longitude/latitude)
    QVector<QPointF> mPendingClip;     //!< boundary (longitude/latitude) of the downloaded area: moved onto the map once the preview is loaded
    TmcRunOptions mInit;              //!< command-line parameters (entrance chunks etc. that have no counterpart in the window)
    std::unique_ptr<Ui::MainWindow> ui;
    QFutureWatcher<TmcPreview>* mPrevWatcher = nullptr;
    QFutureWatcher<TmcRunResult>* mWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> mCancelFlag;
    bool mBusy = false;
};
