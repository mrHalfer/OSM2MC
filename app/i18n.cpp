#include "app/i18n.h"
#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace
{
const char* const kKey = "language";
QString g_override;                       // the --lang value of this run (not saved)
QTranslator* g_translator = nullptr;      // the Russian translator while it is installed

//! Makes the translator match the resolved language: removes the old one and installs the Russian one if needed
void applyLanguage(const QString& lang)
{
    if (g_translator) { QCoreApplication::removeTranslator(g_translator); delete g_translator; g_translator = nullptr; }
    if (lang != QLatin1String("ru")) return;                                // English is the source language: nothing to load
    auto* tr = new QTranslator(QCoreApplication::instance());
    if (tr->load(QStringLiteral(":/i18n/tmc_ru.qm"))) { g_translator = tr; QCoreApplication::installTranslator(tr); }
    else delete tr;
}

QString normalized(const QString& v)
{
    const QString s = v.trimmed().toLower();
    return (s == QLatin1String("en") || s == QLatin1String("ru")) ? s : QStringLiteral("auto");
}
}

QString tmcLanguageSetting()
{
    return normalized(QSettings().value(QLatin1String(kKey)).toString());
}

QString tmcLanguageInEffect()
{
    return g_override.isEmpty() ? tmcLanguageSetting() : normalized(g_override);
}

void tmcSetLanguageSetting(const QString& value)
{
    QSettings().setValue(QLatin1String(kKey), normalized(value));
}

QString tmcResolveLanguage(const QString& choice)
{
    const QString c = normalized(choice);
    if (c != QLatin1String("auto")) return c;
    const QStringList ui = QLocale::system().uiLanguages();                 // e.g. "ru-RU", "en-US"
    for (const QString& l : ui)
    {
        if (l.startsWith(QLatin1String("ru"), Qt::CaseInsensitive)) return QStringLiteral("ru");
        if (l.startsWith(QLatin1String("en"), Qt::CaseInsensitive)) return QStringLiteral("en");
    }
    return QStringLiteral("en");                                            // other languages: English until a translation appears
}

void tmcInstallTranslator(const QString& cliOverride)
{
    g_override = cliOverride;
    applyLanguage(tmcResolveLanguage(tmcLanguageInEffect()));
}

void tmcSwitchLanguage(const QString& choice)
{
    g_override.clear();
    tmcSetLanguageSetting(choice);
    applyLanguage(tmcResolveLanguage(choice));
}
