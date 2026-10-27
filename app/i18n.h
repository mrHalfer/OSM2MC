#pragma once
// Interface language: the source strings are English; a Russian translation is embedded as :/i18n/tmc_ru.qm.
// The choice is "auto" (follow the system language), "en" or "ru"; it is stored in QSettings and can be overridden by the --lang command-line key.
#include <QString>

//! The saved choice: "auto", "en" or "ru"
QString tmcLanguageSetting();
//! The choice that is in effect now: the --lang override of this run, if there is one, otherwise the saved choice
QString tmcLanguageInEffect();
void tmcSetLanguageSetting(const QString& value);
//! The language that is really used for a choice ("auto" is resolved by the system UI language): "en" or "ru"
QString tmcResolveLanguage(const QString& choice);
//! Switch the interface language while the program is running: the choice is saved, the translator is replaced; Qt sends QEvent::LanguageChange to every widget,
//! and the windows re-set their texts (changeEvent). A --lang override of this run is dropped.
void tmcSwitchLanguage(const QString& choice);
//! Install the translator for the choice (cliOverride, if not empty, takes priority and is not saved). Call once, right after the QApplication is created.
void tmcInstallTranslator(const QString& cliOverride = QString());
