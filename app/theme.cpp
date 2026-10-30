#include "app/theme.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QListView>
#include <QPalette>
#include <QProxyStyle>
#include <QScrollBar>
#include <QSettings>
#include <QStyleFactory>
#include <QWidget>

namespace
{
TmcTheme::Colors g_colors;

bool systemIsDark()
{
#ifdef Q_OS_WIN
    const QSettings s(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), QSettings::NativeFormat);
    return s.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
    return false;
#endif
}

struct Tokens { QColor bg, card, input, border, borderHover, text, muted, hover, accent, accentHover, accentPressed; };

Tokens lightTokens()
{
    return { "#f2f4f7", "#ffffff", "#ffffff", "#dde1e8", "#b8bfcc", "#1f2430", "#6a7383", "#f3f8f5", "#2f9e5e", "#278a51", "#1f7343" };
}

Tokens darkTokens()
{
    return { "#15171c", "#1e2128", "#171a20", "#2d323c", "#4a5262", "#e7e9ee", "#8d95a5", "#262b33", "#3fb877", "#4cc886", "#35a068" };
}

//! The combos carrying the property "tmcAlwaysDownPopup" always open their dropdown below the combo:
//! a styled combo aligns the popup by the selected item, so with the last entry chosen the list opened upward
class AlwaysDownComboStyle : public QProxyStyle
{
public:
    explicit AlwaysDownComboStyle(QStyle* base) : QProxyStyle(base) {}
    int styleHint(StyleHint hint, const QStyleOption* opt = nullptr, const QWidget* w = nullptr,
                  QStyleHintReturn* ret = nullptr) const override
    {
        if (hint == SH_ComboBox_Popup && w && w->property("tmcAlwaysDownPopup").toBool())
            return 0;
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
};

// Max rows of a capped dropdown; with a stylesheet active QComboBox silently ignores maxVisibleItems,
// so the popup view reports a capped sizeHint instead — the popup is sized and placed by that hint
constexpr int kPopupRows = 10;

class RowsCappedListView : public QListView
{
public:
    using QListView::QListView;
    QSize sizeHint() const override
    {
        const QSize base = QListView::sizeHint();
        const int rh = qMax(sizeHintForRow(0), fontMetrics().height() + 6);
        const int h = kPopupRows * rh + 2 * frameWidth() + 2;
        return base.height() > h ? QSize(base.width(), h) : base;
    }
};
}

namespace TmcTheme
{
void apply(QApplication& app, Mode mode)
{
    const bool dark = mode == Mode::Dark || (mode == Mode::Auto && systemIsDark());
    const Tokens t = dark ? darkTokens() : lightTokens();

    app.setStyle(new AlwaysDownComboStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QPalette pal;
    pal.setColor(QPalette::Window, t.bg);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Base, t.input);
    pal.setColor(QPalette::AlternateBase, t.card);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.card);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::ToolTipBase, t.card);
    pal.setColor(QPalette::ToolTipText, t.text);
    pal.setColor(QPalette::PlaceholderText, t.muted);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Link, t.accent);
    pal.setColor(QPalette::Mid, t.border);
    pal.setColor(QPalette::Dark, t.borderHover);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    app.setPalette(pal);

    QFile f(QStringLiteral(":/style/tmc.qss"));
    if (f.open(QIODevice::ReadOnly))
    {
        QString css = QString::fromUtf8(f.readAll());
        auto sub = [&](const char* name, const QColor& c) { css.replace(QLatin1Char('@') + QLatin1String(name), c.name()); };
        // longer names before shorter ones, otherwise "@accent" would also hit "@accentHover"
        sub("accentHover", t.accentHover); sub("accentPressed", t.accentPressed); sub("borderHover", t.borderHover);
        sub("accent", t.accent); sub("border", t.border); sub("bg", t.bg); sub("card", t.card); sub("input", t.input);
        sub("text", t.text); sub("muted", t.muted); sub("hover", t.hover);
        app.setStyleSheet(css);
    }

    g_colors = { t.bg, t.card, t.border, t.text, t.muted, t.accent, dark };
}

const Colors& colors() { return g_colors; }

void setupCappedDropdown(QComboBox* box)
{
    box->setMaxVisibleItems(kPopupRows);
    // the flag makes AlwaysDownComboStyle (above) switch the "align the popup by the selected item" hint
    // off for this combo, so the dropdown always opens below the field
    box->setProperty("tmcAlwaysDownPopup", true);
    auto* lv = new RowsCappedListView;
    box->setView(lv);
    lv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // the theme's scrollbar handle (@border) is barely visible on the popup background — style the scroller
    // right on the widget, that rule always reaches it (the popup is a separate window)
    lv->verticalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }"
        " QScrollBar::handle:vertical { background: #8a94a0; border-radius: 5px; min-height: 32px; }"
        " QScrollBar::handle:vertical:hover { background: #6b7683; }"
        " QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        " QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
}
}
