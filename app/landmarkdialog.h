#pragma once
// The "3D figures" windows (a user request): the model list with a pre-generated voxel preview and the
// custom STL loader, and the per-instance settings (material, azimuth, size) that never touch landmarks.json.
#include <QDialog>
#include <QHash>
#include <QImage>
#include "app/tmcrun.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
template <class T> class QFutureWatcher;

//! The voxelized model as a picture (isometric view, the material's colour): stlLoad + stlVoxelize + painter's
//! algorithm. Runs in a worker thread, touches no GUI state. Null image — the model cannot be read or voxelized.
QImage landmarkPreviewImage(const QString& file, double heightBlocks, double yawDeg, const QString& material, int maxSide);

//! The model chooser: the presets of landmarks.json (data-driven, more models are added by editing the JSON),
//! the "load any STL" button, the voxel preview of the selected model at its standard size.
class LandmarkPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LandmarkPickerDialog(QWidget* parent = nullptr);
    const LandmarkPreset& preset() const { return mPreset; }    //!< valid after Accepted

protected:
    void accept() override;

private:
    void showCurrent();                     //!< the preview of the current row (cache, else an async render)
    void startRender(const QString& key);
    void loadStl();                         //!< any STL file: parsing success is enough to be allowed to place it
    QString keyOf(const LandmarkPreset& p) const;

    QListWidget* mItems;
    QLabel* mImage;
    QLabel* mInfo;
    QPushButton* mPlaceBtn;
    QVector<LandmarkPreset> mPresets;
    QHash<QString, QImage> mCache;          //!< file|height|yaw|material -> the rendered picture
    QFutureWatcher<QPair<QString, QImage>>* mWatch;
    int mCurrent = -1;
    LandmarkPreset mPreset;
};

//! The settings of ONE placed figure: the block it is built of (typed or from the list), the azimuth, the size,
//! the placement. The values start from the model's defaults but change only this instance (requirement 6).
class LandmarkEditDialog : public QDialog
{
    Q_OBJECT
public:
    LandmarkEditDialog(const LandmarkPreset& p, QWidget* parent = nullptr);
    const LandmarkPreset& preset() const { return mP; }
    bool removeRequested() const { return mRemoveWanted; }

private:
    void accept() override;

    LandmarkPreset mP;
    QComboBox* mMaterial;
    QDoubleSpinBox* mHeight;
    QDoubleSpinBox* mYaw;
    QComboBox* mPlacement;
    QPushButton* mRemoveBtn;
    bool mRemoveWanted = false;
};
