#pragma once
// Online mode of the right panel: place search, the OSM map, a polygon boundary and the "Download the area" button (Overpass).
#include <QFutureWatcher>
#include <QWidget>
#include <memory>
#include "app/osmdownload.h"

class OsmTileMap;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class OsmOnlinePanel : public QWidget
{
    Q_OBJECT
public:
    explicit OsmOnlinePanel(QWidget* parent = nullptr);
    ~OsmOnlinePanel() override;

    OsmTileMap* map() const { return mMap; }
    bool busy() const { return mBusy; }
    //! Start downloading the area into the outDir folder; the file name is built from the searched place and the time
    void startDownload(const QString& outDir);
    void cancelDownload();

signals:
    //! "Download the area" was pressed (the boundary has already been checked): the owner decides where to save and calls startDownload()
    void downloadRequested();
    //! The data is downloaded: the .osm file and the boundary (longitude/latitude) it was downloaded with
    void downloaded(const QString& file, const QVector<QPointF>& clipLonLat);

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslate();
    struct SearchOut { QVector<OsmPlace> places; QString error; };
    void doSearch();
    void onSearchDone();
    void refreshState();
    QString currentPlaceSlug() const;

    OsmTileMap* mMap = nullptr;
    QLineEdit* mQuery = nullptr;
    QPushButton* mSearchBtn = nullptr;
    QComboBox* mResults = nullptr;
    QLabel* mStatus = nullptr;
    QPushButton* mDownloadBtn = nullptr;
    QPushButton* mClearBtn = nullptr;
    QPushButton* mResetMarksBtn = nullptr;
    QPushButton* mCancelBtn = nullptr;
    QVector<QPushButton*> mToolBtns;            //!< the tool buttons and their source names (re-translated when the language changes)
    QVector<const char*> mToolNames;
    QFutureWatcher<SearchOut>* mSearchWatcher = nullptr;
    QFutureWatcher<OsmDownloadResult>* mDownWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> mCancel;
    QVector<OsmPlace> mPlaces;
    QString mPlaceName;                         //!< the place chosen in the search (for the file name)
    QString mOutFile;
    QVector<QPointF> mDownClip;
    bool mBusy = false;
};
