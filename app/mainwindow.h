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

//! The exporter window: source (.tqt, OSM or a folder), 3D settings (.t3d), world parameters, progress and the log; on the right a basic map preview
//! with the spawn point chosen by a click. The layout is in mainwindow.ui (edit it in Qt Designer), only the logic is here.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    //! debug (the -debug command-line key) shows the service fields: paths to .t3d, the OSM rules and the enhancer, "real meters"; otherwise the default files are always used
    explicit MainWindow(const TmcRunOptions& initial, bool debug = false, QWidget* parent = nullptr);
    ~MainWindow() override;
    //! The project file without dialogs (for the command-line checks): write / read it
    bool saveProjectTo(const QString& path) { return writeProject(path); }
    bool openProjectFrom(const QString& path, QString* error) { return readProject(path, error); }

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;                 // QEvent::LanguageChange: all the texts are set again

private slots:
    void browseProject();
    void browseFolder();
    void browseT3d();
    void browseOsmRules();
    void browseEnhancer();
    void browseOut();
    void generateSeed();
    void startExport();
    void cancelExport();
    void onFinished();
    void loadPreview();
    void onPreviewReady();
    void onSpawnPicked(double x, double y);
    void resetSpawn();
    void copyTp();

private:
    TmcRunOptions collect() const;
    void setBusy(bool busy);
    void appendLog(const QString& s);
    void onProgress(int stage, int done, int total);
    void loadSettings(const TmcRunOptions& initial);
    void saveSettings() const;
    void updateSpawnInfo();
    void updateClipInfo();

    void setOnlineMode(bool online);
    void onDownloaded(const QString& file, const QVector<QPointF>& clipLonLat);
    QString defaultOutputDir() const;

    void onSourceChanged();
    //! The result/error of an export in a window with a scrollable text field (a message box does not fit long texts and a 200% screen scale);
    //! file — the finished archive: then the window has the checkbox "Show the file in the folder"
    void showTextDialog(const QString& title, const QString& text, const QString& file = QString());
    void saveProjectFile();                                //!< "Save project…": the map view, the boundary, the marks and the world options into a .tmc file
    void openProjectFile();                                //!< "Open project…"
    bool writeProject(const QString& path);
    bool readProject(const QString& path, QString* error);
    void retranslate();                                    //!< sets again every text that is set in the code (the .ui ones — by retranslateUi)
    void applyEditionTexts();                              //!< the texts of the simple (non-debug) window
    void updateDefaultHints();                             //!< "default: path" placeholders of the service fields
    void updateClimateItems();                             //!< the names in the climate list
    void updateStageLabel();                               //!< the stage caption under the progress bar

    QComboBox* mClimate = nullptr;
    QVector<QWidget*> mLocalRows;       //!< the rows of the local preview (top row, tools, spawn buttons), hidden as a whole in the online mode
    QComboBox* mLang = nullptr;         //!< the interface language selector (auto / English / Russian)
    QLabel* mLangLabel = nullptr;
    QPushButton* mOpenProjBtn = nullptr;
    QPushButton* mSaveProjBtn = nullptr;
    QLabel* mClimateLabel = nullptr;
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
    QVector<QPointF> mPendingClip;     //!< boundary (longitude/latitude) of the downloaded area: moved onto the map once the preview is loaded
    TmcRunOptions mInit;              //!< command-line parameters (entrance chunks etc. that have no counterpart in the window)
    bool mDebug = false;
    std::unique_ptr<Ui::MainWindow> ui;
    QFutureWatcher<TmcPreview>* mPrevWatcher = nullptr;
    QFutureWatcher<TmcRunResult>* mWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> mCancelFlag;
    bool mBusy = false;
};
