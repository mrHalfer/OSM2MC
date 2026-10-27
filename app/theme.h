#pragma once
// Window styling: the Fusion style, a palette and the resources/tmc.qss style sheet; light or dark theme following the Windows setting.
#include <QColor>

class QApplication;

namespace TmcTheme
{
enum class Mode { Auto, Light, Dark };

//! Colors used by the map widget itself (everything else comes from the style sheet)
struct Colors
{
    QColor bg, card, border, text, muted, accent;
    bool dark = false;
};

//! Apply the theme to the whole application; Auto follows the Windows setting ("dark mode for apps")
void apply(QApplication& app, Mode mode = Mode::Auto);
//! Colors of the current theme (after apply)
const Colors& colors();
}
