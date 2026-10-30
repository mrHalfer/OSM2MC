#include "app/exportsettingsdialog.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace
{
QString appData() { return qEnvironmentVariable("APPDATA"); }
QString userHome() { return QDir::homePath(); }
}

QString ExportSettingsDialog::modrinthRoot()
{
    const QString appData_ = appData();
    return appData_.isEmpty() ? QString() : QDir(appData_).filePath(QStringLiteral("ModrinthApp/profiles"));
}

QString ExportSettingsDialog::curseRoot()
{
    return QDir(userHome()).filePath(QStringLiteral("curseforge/minecraft/Instances"));
}

QVector<QPair<QString, QString>> ExportSettingsDialog::scanInstances(const QString& root, const char* manifestFile)
{
    QVector<QPair<QString, QString>> out;
    if (root.isEmpty()) return out;
    const QDir rd(root);
    if (!rd.exists()) return out;
    const QStringList dirs = rd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);   // the newest instances first
    for (const QString& d : dirs)
    {
        const QString full = rd.filePath(d);
        QFile mf(QDir(full).filePath(QString::fromLatin1(manifestFile)));
        QString name = d;
        if (mf.open(QIODevice::ReadOnly))
        {                                   // the instance's own name from its manifest, the folder name is the fallback
            const QJsonObject o = QJsonDocument::fromJson(mf.readAll()).object();
            const QString n = o.value(QStringLiteral("name")).toString();
            if (!n.isEmpty()) name = n;
        }
        out.append(qMakePair(name, QDir::toNativeSeparators(full)));
    }
    return out;
}

ExportSettingsDialog::ExportSettingsDialog(QWidget* parent, int dest, const QString& launcherDir,
                                           const QString& modrinthProfile, const QString& curseInstance)
    : QDialog(parent)
{
    setWindowTitle(tr("Where to save the exported world?"));
    auto* v = new QVBoxLayout(this);

    for (int i = 0; i < 4; ++i)
    {
        mRadio[i] = new QRadioButton(this);
        v->addWidget(mRadio[i]);
    }
    mRadio[0]->setText(tr("A zip archive next to the source file"));
    mZipHint = new QLabel(tr("The usual way: the world is packed into a zip archive."));
    {
        auto* p = new QWidget;
        auto* l = new QHBoxLayout(p);
        l->setContentsMargins(24, 0, 0, 0);
        l->addWidget(mZipHint);
        mRow[0] = p;
    }
    v->addWidget(mRow[0]);

    mRadio[1]->setText(tr("The Minecraft launcher (its saves folder)"));
    mLauncherEdit = new QLineEdit;
    mLauncherEdit->setReadOnly(true);
    mLauncherEdit->setMinimumWidth(320);                      // same footprint as the instance combos: the dialog does not jump between ways
    mLauncherEdit->setPlaceholderText(tr("not chosen"));
    mLauncherBrowse = new QPushButton(tr("Choose folder…"));
    {
        auto* p = new QWidget;
        auto* l = new QHBoxLayout(p);
        l->setContentsMargins(24, 0, 0, 0);
        l->setSpacing(6);
        l->addWidget(mLauncherEdit);
        l->addWidget(mLauncherBrowse);
        mRow[1] = p;
    }
    v->addWidget(mRow[1]);

    mRadio[2]->setText(tr("A Modrinth instance"));
    mModrinth = new QComboBox;
    {
        auto* p = new QWidget;
        auto* l = new QHBoxLayout(p);
        l->setContentsMargins(24, 0, 0, 0);
        l->addWidget(mModrinth);
        mRow[2] = p;
    }
    v->addWidget(mRow[2]);

    mRadio[3]->setText(tr("A CurseForge instance"));
    mCurse = new QComboBox;
    {
        auto* p = new QWidget;
        auto* l = new QHBoxLayout(p);
        l->setContentsMargins(24, 0, 0, 0);
        l->addWidget(mCurse);
        mRow[3] = p;
    }
    v->addWidget(mRow[3]);
    // one line per way, the hint line as high as a combo: switching a radio keeps the dialog's size
    mZipHint->setMinimumHeight(mModrinth->minimumSizeHint().height());

    // fill the instance lists; the saved choice is restored when the folder still exists, otherwise the newest instance wins
    auto fill = [&](QComboBox* box, QRadioButton* radio, const QString& root, const char* manifest, const QString& saved)
    {
        const QVector<QPair<QString, QString>> found = scanInstances(root, manifest);
        for (const QPair<QString, QString>& p : found)
            box->addItem(p.first, p.second);
        if (found.isEmpty())
        {
            box->addItem(tr("no instances found"), QString());
            radio->setEnabled(false);                     // nothing to choose — the way is not offered
            radio->setToolTip(tr("%1 was not found").arg(QDir::toNativeSeparators(root)));
            return;
        }
        int idx = saved.isEmpty() ? 0 : box->findData(QDir::toNativeSeparators(saved));
        box->setCurrentIndex(idx < 0 ? 0 : idx);
    };
    fill(mModrinth, mRadio[2], modrinthRoot(), "mcinstance.json", modrinthProfile);
    fill(mCurse, mRadio[3], curseRoot(), "minecraftinstance.json", curseInstance);
    const QString mRoot = modrinthRoot(), cRoot = curseRoot();
    const QString mTip = tr("Looked in: %1").arg(mRoot.isEmpty() ? QStringLiteral("-") : QDir::toNativeSeparators(mRoot));
    const QString cTip = tr("Looked in: %1").arg(cRoot.isEmpty() ? QStringLiteral("-") : QDir::toNativeSeparators(cRoot));
    mModrinth->setToolTip(mTip);
    mRadio[2]->setToolTip(mRadio[2]->toolTip().isEmpty() ? mTip : mRadio[2]->toolTip() + QLatin1Char('\n') + mTip);
    mCurse->setToolTip(cTip);
    mRadio[3]->setToolTip(mRadio[3]->toolTip().isEmpty() ? cTip : mRadio[3]->toolTip() + QLatin1Char('\n') + cTip);

    mLauncherEdit->setText(QDir::toNativeSeparators(launcherDir));

    connect(mLauncherBrowse, &QPushButton::clicked, this, [this]()
    {
        QString start = mLauncherEdit->text();
        if (start.isEmpty() || !QFileInfo::exists(start))
        {
            const QString ad = appData();
            start = ad.isEmpty() ? QDir::homePath() : QDir(ad).filePath(QStringLiteral(".minecraft"));
        }
        const QString d = QFileDialog::getExistingDirectory(this, tr("The saves folder of the launcher"), start);
        if (!d.isEmpty()) mLauncherEdit->setText(QDir::toNativeSeparators(d));
        updateEnabled();
    });

    auto* bb = new QHBoxLayout;
    bb->addStretch();
    mOk = new QPushButton(tr("Save"), this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    bb->addWidget(mOk);
    bb->addWidget(cancel);
    v->addLayout(bb);
    connect(mOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    for (int i = 0; i < 4; ++i)
        connect(mRadio[i], &QRadioButton::toggled, this, [this]() { updateEnabled(); });
    int want = qBound(0, dest, 3);
    if (!mRadio[want]->isEnabled()) want = 0;               // the saved instance app is gone — fall back to the zip
    mRadio[want]->setChecked(true);
    updateEnabled();

    // fixed window size in every state: the largest of the four size hints (measured with each row shown in turn),
    // capped so that an instance with a long name cannot stretch the dialog — the combo clips it, the popup is wide
    QSize best = sizeHint();
    for (QWidget* row : mRow)
    {
        for (QWidget* r : mRow) r->setVisible(r == row);
        const QSize s = sizeHint();
        best = QSize(qMax(best.width(), s.width()), qMax(best.height(), s.height()));
    }
    updateEnabled();
    setFixedSize(qMin(best.width(), 640), best.height());
}

void ExportSettingsDialog::updateEnabled()
{
    // only the chosen way is visible; every row is one line high, so the dialog keeps its size while switching
    for (int i = 0; i < 4; ++i)
        mRow[i]->setVisible(mRadio[i]->isChecked());
    const bool ok = dest() == 0
                    || (dest() == 1 && !mLauncherEdit->text().isEmpty() && QFileInfo::exists(mLauncherEdit->text()))
                    || (dest() == 2 && !mModrinth->currentData().toString().isEmpty())
                    || (dest() == 3 && !mCurse->currentData().toString().isEmpty());
    mOk->setEnabled(ok);
}

int ExportSettingsDialog::dest() const
{
    for (int i = 0; i < 4; ++i)
        if (mRadio[i]->isChecked()) return i;
    return 0;
}

QString ExportSettingsDialog::launcherDir() const { return mLauncherEdit->text().trimmed(); }
QString ExportSettingsDialog::modrinthProfile() const { return mModrinth->currentData().toString(); }
QString ExportSettingsDialog::curseInstance() const { return mCurse->currentData().toString(); }
