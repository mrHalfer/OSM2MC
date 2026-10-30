#pragma once
// The "Export settings" dialog (GUI only): where the exported world goes — a zip archive next to the source file (as always),
// the saves folder of a Minecraft launcher, or a Modrinth / CurseForge instance. Instances are found by the standard
// installation paths (%APPDATA%\ModrinthApp\profiles, %USERPROFILE%\curseforge\minecraft\Instances). The chosen values
// are persisted by MainWindow in QSettings.
#include <QDialog>
#include <QPair>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

class ExportSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    //! dest: 0 — zip, 1 — launcher, 2 — Modrinth, 3 — CurseForge; the rest are the saved paths (empty — nothing chosen yet)
    ExportSettingsDialog(QWidget* parent, int dest, const QString& launcherDir,
                         const QString& modrinthProfile, const QString& curseInstance);

    int dest() const;
    QString launcherDir() const;                        //!< the folder chosen by the user (.minecraft gets "saves" appended on export)
    QString modrinthProfile() const;                    //!< the profile folder of the chosen instance
    QString curseInstance() const;                      //!< the folder of the chosen instance

    //! Standard roots of instances (empty — the app is not installed there)
    static QString modrinthRoot();
    static QString curseRoot();
    //! Instance folders inside root: (name from the manifest or folder name, full path)
    static QVector<QPair<QString, QString>> scanInstances(const QString& root, const char* manifestFile);

private:
    void updateEnabled();                               // rows follow the chosen radio; OK needs a valid choice

    QRadioButton* mRadio[4] = { nullptr, nullptr, nullptr, nullptr };
    QWidget* mRow[4] = { nullptr, nullptr, nullptr, nullptr };       // the control row under each radio, only the chosen one is shown
    QLabel* mZipHint = nullptr;
    QLineEdit* mLauncherEdit = nullptr;
    QPushButton* mLauncherBrowse = nullptr;
    QComboBox* mModrinth = nullptr;
    QComboBox* mCurse = nullptr;
    QPushButton* mOk = nullptr;
};
