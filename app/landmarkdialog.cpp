#include "app/landmarkdialog.h"
#include "app/uimetrics.h"
#include "core/stlmodel.h"
#include "export/blocks.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

//! The preview colour of a material name: the palette's own colour, stone gray for empty (like the exporter),
//! neutral gray for a block outside the palette (it still builds in the game from the runtime block tail).
static QColor landmarkMaterialColor(const QString& material)
{
    QString n = material.trimmed();
    if (n.isEmpty()) return QColor(0x9e, 0x9e, 0x9e);
    if (!n.startsWith(QLatin1String("minecraft:")))
        n = QLatin1String("minecraft:") + n;
    const QByteArray ba = n.toLatin1();
    const int i = paletteIndexOf(ba.constData());
    if (i < 0) return QColor(0x9a, 0x9a, 0x9a);
    const NamedColor& c = kBlockPalette[i];
    return QColor(c.r, c.g, c.b);
}

QImage landmarkPreviewImage(const QString& file, double heightBlocks, double yawDeg, const QString& material, int maxSide)
{
    QVector<StlTri> tris;
    QString err;
    if (!stlLoad(file, tris, &err)) return QImage();
    QVector<StlCell> cells;
    if (!stlVoxelize(tris, heightBlocks, yawDeg, cells, &err)) return QImage();
    if (cells.isEmpty()) return QImage();

    // the camera looks along (1,-1,-1)... rather: the visible faces are the top (+y) and the +x and +z sides
    // (they face down-right / down-left on screen); a face is drawn only when its neighbour cell is absent
    auto cellKey = [](int x, int y, int z) { return (quint64(quint16(x)) << 32) | (quint64(quint16(z)) << 16) | quint64(quint16(y)); };
    QSet<quint64> occ;
    occ.reserve(cells.size() * 2);
    for (const StlCell& c : cells)
        occ.insert(cellKey(c.x, c.y, c.z));

    // the painter's order of the isometric projection: by (x + z) back-to-front, then bottom-to-top; fully hidden cells are skipped
    QVector<int> order;
    order.reserve(cells.size());
    for (int i = 0; i < cells.size(); ++i)
    {
        const StlCell& c = cells[i];
        if (occ.contains(cellKey(c.x, c.y + 1, c.z)) && occ.contains(cellKey(c.x + 1, c.y, c.z)) && occ.contains(cellKey(c.x, c.y, c.z + 1)))
            continue;
        order.append(i);
    }
    std::sort(order.begin(), order.end(), [&cells](int a, int b)
    {
        const StlCell& A = cells[a];
        const StlCell& B = cells[b];
        return A.x + A.z != B.x + B.z ? A.x + A.z < B.x + B.z : A.y < B.y;
    });

    // isometric 2:1 projection in model units: sx = x - z, sy = (x + z)/2 - y; then scaled into maxSide pixels
    double sxMin = 1e30, sxMax = -1e30, syMin = 1e30, syMax = -1e30;
    for (const StlCell& c : cells)
    {
        sxMin = qMin(sxMin, double(c.x) - c.z - 1);
        sxMax = qMax(sxMax, double(c.x) + 1 - c.z);
        syMin = qMin(syMin, (c.x + c.z) * 0.5 - c.y - 1);
        syMax = qMax(syMax, (c.x + c.z + 2) * 0.5 - c.y);
    }
    const double w = sxMax - sxMin, h = syMax - syMin;
    if (w <= 0.0 || h <= 0.0 || maxSide < 16) return QImage();
    const double E = (maxSide - 8) / qMax(w, h);             // model units -> pixels
    const double ox = 4 - sxMin * E, oy = 4 - syMin * E;
    QImage img(int(w * E) + 8, int(h * E) + 8, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);          // antialiasing would put hairlines between adjacent faces
    const QColor base = landmarkMaterialColor(material);
    const QColor sideR = base.darker(118), sideL = base.darker(145);
    auto P = [&](double x, double y, double z) { return QPointF((x - z) * E + ox, ((x + z) * 0.5 - y) * E + oy); };
    for (const int i : order)
    {
        const StlCell& c = cells[i];
        const double x = c.x, y = c.y, z = c.z;
        if (!occ.contains(cellKey(c.x, c.y + 1, c.z)))
        {
            p.setPen(Qt::NoPen);
            p.setBrush(base);
            p.drawPolygon(QPolygonF() << P(x, y + 1, z) << P(x + 1, y + 1, z) << P(x + 1, y + 1, z + 1) << P(x, y + 1, z + 1));
        }
        if (!occ.contains(cellKey(c.x + 1, c.y, c.z)))
        {
            p.setPen(Qt::NoPen);
            p.setBrush(sideR);
            p.drawPolygon(QPolygonF() << P(x + 1, y, z) << P(x + 1, y + 1, z) << P(x + 1, y + 1, z + 1) << P(x + 1, y, z + 1));
        }
        if (!occ.contains(cellKey(c.x, c.y, c.z + 1)))
        {
            p.setPen(Qt::NoPen);
            p.setBrush(sideL);
            p.drawPolygon(QPolygonF() << P(x, y, z + 1) << P(x, y + 1, z + 1) << P(x + 1, y + 1, z + 1) << P(x + 1, y, z + 1));
        }
    }
    return img;
}

// --------------------------------------------------------------------------------------------- the model chooser

LandmarkPickerDialog::LandmarkPickerDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("3D figures"));
    mPresets = tmcLoadLandmarkPresets();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(kRowGap);
    auto* cols = new QHBoxLayout;
    mItems = new QListWidget;
    mItems->setMinimumWidth(200);
    mItems->setMaximumWidth(280);
    for (const LandmarkPreset& p : mPresets)
    {
        auto* it = new QListWidgetItem(p.label.isEmpty() ? QFileInfo(p.file).completeBaseName() : p.label);
        it->setToolTip(p.file);
        mItems->addItem(it);
    }
    auto* right = new QVBoxLayout;
    mImage = new QLabel(tr("Select a model"));
    mImage->setAlignment(Qt::AlignCenter);
    mImage->setMinimumSize(360, 300);
    mInfo = new QLabel;
    mInfo->setAlignment(Qt::AlignHCenter);
    mInfo->setWordWrap(true);
    right->addWidget(mImage, 1);
    right->addWidget(mInfo);
    cols->addWidget(mItems);
    cols->addLayout(right, 1);
    lay->addLayout(cols, 1);

    auto* row = new QHBoxLayout;
    auto* loadBtn = new QPushButton(tr("Load STL…"));
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    mPlaceBtn = new QPushButton(tr("Place on the map"));
    mPlaceBtn->setObjectName(QStringLiteral("goButton"));        // the accent button, like "Export"
    mPlaceBtn->setEnabled(false);
    row->addWidget(loadBtn);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(mPlaceBtn);
    lay->addLayout(row);

    mWatch = new QFutureWatcher<QPair<QString, QImage>>(this);
    connect(mItems, &QListWidget::currentRowChanged, this, [this](int i) { mCurrent = i; showCurrent(); });
    connect(mItems, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { if (mCurrent >= 0) accept(); });
    connect(mPlaceBtn, &QPushButton::clicked, this, [this]() { if (mCurrent >= 0) accept(); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(loadBtn, &QPushButton::clicked, this, &LandmarkPickerDialog::loadStl);
    connect(mWatch, &QFutureWatcher<QPair<QString, QImage>>::finished, this, [this]()
    {
        const QPair<QString, QImage> r = mWatch->result();
        mCache.insert(r.first, r.second);
        if (mCurrent >= 0 && mCurrent < mPresets.size() && keyOf(mPresets[mCurrent]) == r.first) showCurrent();
    });
    if (!mPresets.isEmpty()) mItems->setCurrentRow(0);           // the first model shows its preview at once
    resize(680, 440);
}

void LandmarkPickerDialog::accept()
{
    if (mCurrent >= 0 && mCurrent < mPresets.size()) mPreset = mPresets[mCurrent];
    QDialog::accept();
}

QString LandmarkPickerDialog::keyOf(const LandmarkPreset& p) const
{
    return QStringLiteral("%1|%2|%3|%4").arg(p.file).arg(p.heightM).arg(p.yaw).arg(p.material);
}

void LandmarkPickerDialog::showCurrent()
{
    mPlaceBtn->setEnabled(mCurrent >= 0);
    if (mCurrent < 0 || mCurrent >= mPresets.size())
    {
        mImage->setText(tr("Select a model"));
        mImage->setPixmap(QPixmap());
        mInfo->clear();
        return;
    }
    const LandmarkPreset& p = mPresets[mCurrent];
    const QImage img = mCache.value(keyOf(p));
    if (img.isNull())
    {
        mInfo->setText(tr("Rendering the voxel preview…"));
        if (!mWatch->isRunning()) startRender(keyOf(p));         // onRender shows the picture when it is ready
        return;
    }
    mImage->setText(QString());
    mImage->setPixmap(QPixmap::fromImage(img));
    mInfo->setText(tr("Standard size: %1 blocks • material: %2")
                       .arg(p.heightM, 0, 'g', 6)
                       .arg(p.material.isEmpty() ? QStringLiteral("minecraft:stone") : p.material));
}

void LandmarkPickerDialog::startRender(const QString& key)
{
    LandmarkPreset p;
    for (const LandmarkPreset& pr : mPresets)
        if (keyOf(pr) == key) { p = pr; break; }
    const QString file = p.file, material = p.material;
    const double h = p.heightM, yaw = p.yaw;
    mWatch->setFuture(QtConcurrent::run([file, h, yaw, material, key]() { return qMakePair(key, landmarkPreviewImage(file, h, yaw, material, 340)); }));
}

void LandmarkPickerDialog::loadStl()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("3D model file"), QString(), tr("STL models (*.stl);;All files (*)"));
    if (f.isEmpty()) return;
    QVector<StlTri> tris;
    QString err;
    if (!stlLoad(f, tris, &err))
    {
        QMessageBox::warning(this, tr("3D model"), tr("This file could not be read as an STL model: %1").arg(err));
        return;
    }
    // parsing success is enough to be allowed to place the model; the preview and the exporter handle the rest
    LandmarkPreset p;
    p.file = QFileInfo(f).absoluteFilePath();
    p.label = QFileInfo(f).completeBaseName();
    p.heightM = 24;                                             // a sane default for an unknown model, the size is set per instance
    p.placement = QStringLiteral("roof");
    mPresets.append(p);
    auto* it = new QListWidgetItem(p.label);
    it->setToolTip(p.file);
    mItems->addItem(it);
    mItems->setCurrentRow(mItems->count() - 1);
}

// ------------------------------------------------------------------------------------- one figure's parameters

LandmarkEditDialog::LandmarkEditDialog(const LandmarkPreset& p, QWidget* parent) : QDialog(parent), mP(p)
{
    setWindowTitle(tr("3D figure — %1").arg(p.label.isEmpty() ? QFileInfo(p.file).completeBaseName() : p.label));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(kRowGap);
    auto* form = new QFormLayout;
    form->setSpacing(kRowGap);
    mMaterial = new QComboBox;                                  // the game's tag typed by hand or picked from the list
    mMaterial->setEditable(true);
    mMaterial->setInsertPolicy(QComboBox::NoInsert);
    QStringList names;
    names.reserve(kPaletteCount);
    for (int i = 0; i < kPaletteCount; ++i) names << QString::fromLatin1(kBlockPalette[i].name);
    names.sort(Qt::CaseInsensitive);
    mMaterial->addItems(names);
    mMaterial->setCurrentText(mP.material);
    mMaterial->setPlaceholderText(tr("any block, for example minecraft:terracotta"));
    mHeight = new QDoubleSpinBox;
    mHeight->setRange(1, 1024);
    mHeight->setDecimals(0);
    mHeight->setSuffix(tr(" blocks"));
    mHeight->setValue(qBound(1.0, mP.heightM, 1024.0));
    mYaw = new QDoubleSpinBox;                                  // the azimuth: clockwise degrees as seen from above
    mYaw->setRange(0, 360);
    mYaw->setDecimals(1);
    mYaw->setSingleStep(5);
    mYaw->setSuffix(QStringLiteral("\u00b0"));
    double yaw = std::fmod(mP.yaw, 360.0);
    if (yaw < 0) yaw += 360.0;
    mYaw->setValue(yaw);
    mPlacement = new QComboBox;
    mPlacement->addItem(tr("On the roof of the building"), QStringLiteral("roof"));
    mPlacement->addItem(tr("In place of the building (the building is removed)"), QStringLiteral("instead"));
    const int pi = mPlacement->findData(mP.placement);
    mPlacement->setCurrentIndex(pi >= 0 ? pi : 0);
    form->addRow(tr("Material:"), mMaterial);
    form->addRow(tr("Size (height):"), mHeight);
    form->addRow(tr("Azimuth:"), mYaw);
    form->addRow(tr("Placement:"), mPlacement);
    lay->addLayout(form);

    auto* row = new QHBoxLayout;
    mRemoveBtn = new QPushButton(tr("Remove the figure"));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    row->addWidget(mRemoveBtn);
    row->addStretch(1);
    row->addWidget(bb);
    lay->addLayout(row);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);   // the virtual accept() writes the fields into mP
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mRemoveBtn, &QPushButton::clicked, this, [this]() { mRemoveWanted = true; accept(); });
}

void LandmarkEditDialog::accept()
{
    mP.material = mMaterial->currentText().trimmed();
    mP.heightM = mHeight->value();
    mP.yaw = mYaw->value();
    mP.placement = mPlacement->currentData().toString();
    QDialog::accept();
}
