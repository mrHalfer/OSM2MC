#include "app/encountersdialog.h"
#include "app/theme.h"
#include "export/encounters.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include <cstring>

namespace
{
// The ids MUST repeat export/encounters.h in the same order and count (checked at open time); the
// titles are literals so that lupdate takes them into osmc_ru/osmc_zh. The descriptions retell the
// idea texts of docs/city-encounters.md and stay Russian on purpose: the legend corpus lives in the
// author's notes, which the Chinese build reads as is.
struct Row { const char* id; const char* group; const char* title; const char* desc; };
const Row kRows[] = {
    { "lurker",            QT_TRANSLATE_NOOP("EncountersDialog", "Pursuers"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Lurker — a faceless figure in the lamplight"),
      "Тонкий высокий бесликий силуэт в чёрном. Встаёт вдали в свете фонаря — у парка с летающим "
      "островом, у одинокой лампы у дороги, на окраине квартала. Подойдёшь ближе — окажется за "
      "спиной в переулке. Ведёт себя как слух: помехи, шаги, которые ближе твоих. Финал арки — "
      "привести его к свету церкви или вокзала: растворится." },
    { "taxi",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Taxicab"),
      "У ночной парковки или у остановки метро стоит машина, а рядом — «попутчик». Заведёшь "
      "двигатель — на заднем сиденье силуэт. Выйдешь посмотреть — никого, и дверь сама хлопнет. "
      "Арка закрывается, когда найдёшь его «гараж» в промзоне: пустое место с пятном." },
    { "echo",              nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Echo of the tunnels"),
      "В тоннелях метро и подземных переходах повторяет шаги игрока с задержкой в полтора "
      "секунды — будто из другого тоннеля. Чем ближе, тем синхроннее. Вывести на поверхность: "
      "Эхо на улицу не выходит." },
    { "matron",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Matron (schools, hospitals)"),
      "В школах, детсадах и больничных корпусах: свет включается «в её» кабинетах по расписанию, "
      "на карте квартала появляется записка «проверь 3 этаж». Найти её шкафчик с ключом — квартал "
      "успокаивается." },
    { "mower",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Mower in the fog"),
      "Владелец газона, который кончился. Днём невидим, в тумане слышен мотор; к утру на траве "
      "ровные полосы, которых никто не косил. Полосы обкашивают путь к его сарайке — найти." },
    { "upstairs",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Neighbour Above"),
      "Топот строго над головой, где бы ты ни ночевал. Этажом выше — дверь, которой нет на плане. "
      "Подняться на крышу: там стул, маленький дворик и вид на весь город." },
    { "third_passenger",   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Third Passenger"),
      "В «вагоне» (тоннеле с путями) пассажиров всегда на одного больше, чем зашло. Не "
      "оборачиваться до конечной — пройдёт." },
    { "bus13",             nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Bus No. 13"),
      "Ночью на конечной у окраины останавливается маршрут, которого нет. Двери открыты; внутрь "
      "заходить нельзя — там темнота и дыхание. Засечь его расписание и сфотографировать — "
      "станет безобидной легендой." },
    { "carousel",          QT_TRANSLATE_NOOP("EncountersDialog", "Parks and yards"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Midnight Carousel"),
      "Крутится сама и играет, пока никто не видит. Уже работает как демо: холодные искры над "
      "отмеченными парками." },
    { "swings",            nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Swings Nobody Rides"),
      "На четвёртых качелях площадки укачивается невидимый; рядом в песке — след ботинка, "
      "которого вечером не было." },
    { "chalk_art",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Chalk That Redraws Itself"),
      "Детский рисунок мелом на асфальте: «доезжай сюда». Ночью рисунок меняется — функция "
      "перерисовывает его на новый адрес." },
    { "twin_island",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Twin Island"),
      "Над парком, которому уже достался летающий остров, появляется второй — без основания "
      "под ним. «Запасной выход» для Долгого." },
    { "dry_fountain",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fountain Running Downward"),
      "Ночью вода из фонтана течёт не в чашу, а на три блока ниже уровня земли. Под фонтаном — "
      "полость, которой нет на планах." },
    { "bench_otherworld",  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Bench Not For Everyone"),
      "Сесть сюда — и та же скамейка стоит уже в тумане и дожде, и от города ни одного звука. "
      "Мини-измерение; выход — просто встать." },
    { "stray_dog",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dog Without An Owner"),
      "Идёт за игроком молча и исчезает, стоит подойти к людям." },
    { "pumpkin_faces",     QT_TRANSLATE_NOOP("EncountersDialog", "Graveyards"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Pumpkins That Turn To You"),
      "Тыквы-фонари поворачиваются «лицом» к игроку — блок меняется, пока смотришь издалека." },
    { "fresh_grave",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fresh Grave"),
      "Подсвечена «свежим грунтом». Копать нельзя: звук лопаты обрывается сам." },
    { "procession",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Procession"),
      "Ночью колонна невидимок идёт к воротам — только частицы и шаги. Встанешь на путь — "
      "пройдут сквозь." },
    { "grave_caretaker",   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Caretaker"),
      "Скелет в будке: при игроке делает вид, что сидит; отвернёшься — уже стоит у двери." },
    { "clock_0307",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Always 03:07"),
      "Часы кладбища бьют 03:07, даже когда игрок сломал механизм." },
    { "last_train",        QT_TRANSLATE_NOOP("EncountersDialog", "Metro and tunnels"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Last Train"),
      "Раз в несколько ночей по тоннелю проходит поезд: враждебные мобы в это время не "
      "появляются. Можно «поймать» его и уехать в тайник (комната-телепорт)." },
    { "phantom_station",   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Station That Is Not"),
      "У игрока появляется указатель на выход к станции, которой нет. Придёшь — пустой зал, "
      "один горящий фонарь и школьная парта." },
    { "extra_flight",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Stair With One Flight More"),
      "Эскалатор или лестница вестибюля ведёт на пролёт больше, чем видно снаружи. Внутри — "
      "комната-обманка." },
    { "breathing_rails",   nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Breathing Rails"),
      "Зазор между путями циклически расширяется и сужается. На середине пролёта — голос." },
    { "dark_wave",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Darkness Behind You"),
      "Свет в тоннеле гаснет сегментами за спиной: у каждого фонаря свой таймер, тьма "
      "догоняет." },
    { "floor_half",        QT_TRANSLATE_NOOP("EncountersDialog", "Tower living"),
      QT_TRANSLATE_NOOP("EncountersDialog", "Floor Nine And A Half"),
      "Дверь между девятым и десятым этажом. За ней — тот же этаж, только в зеркалах: "
      "сдублированный мини-этаж." },
    { "ghost_lift",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Lift Without A Shaft"),
      "Вызвать лифт на пустом пролёте — будет звук и раскрывшиеся «двери» в глухую стену." },
    { "window_wave",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Window Wave"),
      "Свет во всех окнах одного дома включается по очереди снизу вверх, за двадцать секунд, и "
      "гаснет разом." },
    { "roof_garden",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Roof Garden"),
      "На крыше — грядка, кресло и табличка «не мешайте». Хозяин шикнет, если топтать по "
      "грядкам." },
    { "boarded_door",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Boarded-Over Door"),
      "Дверь, которая есть на плане, но заколочена. Ночью из-за неё слышно плач. Снять доски — "
      "комната с надписями." },
    { "extra_car",         QT_TRANSLATE_NOOP("EncountersDialog", "Streets and parking"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Extra Car"),
      "Машина на парковке, которой не было в списке (OSMC расставляет машины детерминированно — "
      "вот и «лишняя»). Внутри магнитофон играет новости про «этого водителя»." },
    { "flasher",           nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Flasher Without A Car"),
      "Мигалка без машины на пустом перекрёстке. Подойдёшь — гаснет здесь и включается за "
      "спиной." },
    { "loop_interchange",  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Interchange Loop"),
      "Проехал один и тот же знак развязки трижды — квартал вокруг «пересобрался»: соседние "
      "чанки подменены мини-локацией." },
    { "roadworks",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Roadworks On The Viaduct"),
      "На виадуке впереди «дорожные работы» из блоков. Разобрать завал — за ним обрыв в никуда: "
      "иллюзия конца мира." },
    { "hitchhiker",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Hitchhiker"),
      "Голос с обочины просит подвезти. Посадишь — через двести метров исчезнет, оставит "
      "записку." },
    { "night_game",        QT_TRANSLATE_NOOP("EncountersDialog", "Schools"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Game In The Gym"),
      "Ночью в спортзале на сетке кто-то играет — звуки мяча. Зайти: мяч катится к двери." },
    { "skip_rope",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Skipping Rope"),
      "Во дворе скачет скакалка, которую никто не крутит, — и «двое» под неё прыгают." },
    { "auditorium",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Empty Auditorium"),
      "Прожектор сам включается на пустое кресло; на сцене меловой контур «выпускника»." },
    { "timetable",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Yesterday's Timetable"),
      "Предметы на доске «на завтра» переписываются на предметы «на вчера» — включая тех, "
      "которых в школе никогда не было." },
    { "banya",             QT_TRANSLATE_NOOP("EncountersDialog", "Private sector"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Banya With Lights On"),
      "Баня, которая «брошена», а в ней пар и свет. Внутри таз и веник; возьмёшь веник — "
      "услышишь «положи обратно», и взять его нельзя до ночи." },
    { "unlocked_gate",     nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Gate That Is Always Locked"),
      "Калитка, которая «всегда заперта», в третий раз открыта. За ней — тот же участок, но "
      "зимой: мини-подмена." },
    { "scarecrow",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Scarecrow That Turned"),
      "Днём стоит спиной к дому — можно проверить и переставить. Ночью — лицом к дому, на груди "
      "доска «убирайся к утру»." },
    { "howling_dog",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dog Howling At One Fence"),
      "Соседская собака воет только у одного забора. У того забора — подземный ход в лес." },
    { "moths",             QT_TRANSLATE_NOOP("EncountersDialog", "Woods and industry"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The White Moths"),
      "Стая белых мотыльков-частиц вокруг одного фонаря. Встанешь в центр — звук крыльев "
      "стихает прямо в ухе." },
    { "power_hum",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Hum Of The Power Line"),
      "Просека линии без опор — провода лежат в земле, и слышно пятьдесят герц. В конце "
      "просеки — будка со счётчиком на весь район." },
    { "dead_camp",         nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Dead Camp"),
      "Заброшенный лагерь в лесу (OSMC такие строит): костёр дотлевёт сам, рядом свежая ложка "
      "у углей." },
    { "conveyor",          nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Conveyor At Night"),
      "Ночью конвейер цеха вывозит на улицу ящик. Внутри — манекен в рабочей робе." },
    { "forest_window",     nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Window In The Taiga"),
      "В тайге у частного сектора тропинка ведёт ровно к «окну» между домами. В окне — свет "
      "твоего костра." },
    { "lake_lightpath",    QT_TRANSLATE_NOOP("EncountersDialog", "Water"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Light Path On The Bottom"),
      "В озере парка по дну лежит дорожка из светлого камня. Идёшь по ней — свет гаснет "
      "участками за спиной." },
    { "bridge_offerings",  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "Offerings On The Railing"),
      "На перилах моста рядами складываются «приношения» по три предмета. К утру ряд "
      "дополняется сам." },
    { "cloud_reflection",  nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Reflection Of An Absent Cloud"),
      "В воде фонтана или реки отражается облако, которого на небе нет." },
    { "fourth_wall",       QT_TRANSLATE_NOOP("EncountersDialog", "Underground"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Fourth Wall"),
      "В пещерном гроте появляется коридор в «затопленную» копию улицы, которая наверху." },
    { "rune_circle",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Glowing Circle"),
      "Светящийся узорчатый круг на полу пещеры. Встанешь в центр — тишина звуков мира на "
      "тридцать секунд." },
    { "ghost_rolley",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Ghost Rrolley"),
      "Вагонетка на рельсах метростроя приедет, если просто стоять на путях и ждать." },
    { "base_radio",        QT_TRANSLATE_NOOP("EncountersDialog", "Base and prison"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Base Radio At Night"),
      "Ночью из динамиков базы — координаты и «не ходи». Сходишь туда: пусто, но «встреча» там "
      "уже не появляется." },
    { "lone_sentry",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Sentry Not In The Garrison"),
      "У калитки базы стоит один часовой, которого нет в гарнизоне. Через ворота не проходит — "
      "обходит забор." },
    { "refugee_camp",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Camp That Vanishes By Dawn"),
      "Лагерь из мешков и ящиков появляется у промзоны и исчезает к утру. В мешках — «паспорта» "
      "с именами жителей города." },
    { "blinking_lamp",     QT_TRANSLATE_NOOP("EncountersDialog", "Micro wonders"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Winking Lamp"),
      "Фонарь подмигивает прямо над игроком — смена lit-состояния." },
    { "hush_field",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Field Of Silence"),
      "Капкан тишины: в радиусе десяти блоков не слышно ни шагов, ни мобов — на пятнадцать "
      "секунд." },
    { "local_rain",        nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Rain Above One Street"),
      "Дождь идёт только над одним участком: splash-частицы над головой и звук, а рядом — "
      "сухо." },
    { "window_walker",     nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Silhouette In The Windows"),
      "На пятом этаже в окне стоит силуэт с фонарём и «лунатиком» идёт вдоль ряда окон." },
    { "long_shadows",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Shadows Too Long For Noon"),
      "Полуденные тени домов на десять градусов длиннее, чем должны быть." },
    { "cat_council",       nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Cat Council"),
      "Городские коты собираются «консилиумом» у одного подъезда. Подойдёшь — разойдутся по "
      "домам." },
    { "phone_booth",       QT_TRANSLATE_NOOP("EncountersDialog", "Bonus legends"),
      QT_TRANSLATE_NOOP("EncountersDialog", "The Telephone Booth That Rings"),
      "Городской «слуховой телефон»: будка звонит. Возьмёшь трубку — продиктуют маршрут к "
      "встрече." },
    { "night_market",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Night Market"),
      "На пустой площади — пять минут торговли. К утру ряд пустых ящиков и сдача на "
      "прилавке." },
    { "rising_street",     nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Street That Builds Itself"),
      "Улица, которая строится: заборы, и прогресс виден каждый день. К седьмому дню дом "
      "достроят — и в нём состоится первая встреча." },
    { "warning_sign",      nullptr,
      QT_TRANSLATE_NOOP("EncountersDialog", "The Teasing Sign"),
      "Табличка у метро: «не ходи в тоннель». Идёшь — утром табличка: «я же "
      "сказала»." },
};
const int kRowCount = int(sizeof(kRows) / sizeof(kRows[0]));

constexpr int kIdRole = Qt::UserRole;         // the event id of a leaf row
constexpr int kDescRole = Qt::UserRole + 1;   // its description

//! A real blur (Qt's QGraphicsBlurEffect only darkens here): bilinear shrink to a threadbare grid
//! and back up, so no glyph survives — the shape of the text and its colours stay, the letters die.
QPixmap blurredCopy(const QPixmap& src)
{
    QSize s1(qMax(2, src.width() / 12), qMax(2, src.height() / 12));
    const QPixmap s2 = src.scaled(s1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QSize s3(qMax(2, s1.width() / 3), qMax(2, s1.height() / 3));
    return s2.scaled(s3, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
           .scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

//! The text of the description drawn by hand: QWidget::render() over a widget that was never shown
//! gives an empty pixmap (the --gui-encounters shot), while QPainter on a pixmap always paints.
QPixmap textPixmap(const QFont& font, const QColor& color, const QString& text, const QSize& size)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter pr(&pm);
    pr.setFont(font);
    pr.setPen(color);
    const QRect r(0, 0, size.width(), size.height());
    pr.drawText(r.translated(10, 10).adjusted(0, 0, -20, 0), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, text);
    return pm;
}

//! The text of the description without the size hint a word-wrapped QLabel reports for a single
//! long line: the pane must not drag the whole dialog taller than the row list beside it.
class PlainLabel : public QLabel
{
public:
    using QLabel::QLabel;
    QSize sizeHint() const override { return QSize(360, 220); }
    QSize minimumSizeHint() const override { return QSize(160, 80); }
};

//! The right pane: the description behind a frosted plate. Clicking the plate clears the frost and
//! shows what was written (docs/city-encounters.md flavour). The text is painted by us as a blurred
//! grab of the label, so the blur is a blur, not a shade.
class BlurPane : public QWidget
{
public:
    explicit BlurPane(QWidget* parent) : QWidget(parent)
    {
        auto* stack = new QStackedLayout(this);
        stack->setStackingMode(QStackedLayout::StackAll);       // the hint stays on top of the text
        mLab = new PlainLabel(this);
        mLab->setWordWrap(true);
        mLab->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        mLab->setTextFormat(Qt::PlainText);
        mLab->setMargin(10);
        mHint = new QLabel(QCoreApplication::translate("EncountersDialog", "Click to read"), this);
        // NB: the context is the DIALOG, not the pane: BlurPane has no Q_OBJECT, so tr() here would
        // ask a different context than lupdate writes into the .ts
        mHint->setObjectName(QStringLiteral("encFrost"));           // the click target for --gui-encounters
        mHint->setAlignment(Qt::AlignCenter);
        mHint->setStyleSheet(QStringLiteral("QLabel { background: rgba(24,26,30,200); color: #cfd2d6;"
                                            " border-radius: 6px; padding: 10px 18px; font-weight: bold; }"));
        mHint->installEventFilter(this);                            // the plate is the click target
        stack->addWidget(mLab);
        stack->addWidget(mHint);
        mHint->raise();
    }

    void setText(const QString& s)
    {
        mDesc = s;
        showHint(true);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (watched == mHint && e->type() == QEvent::MouseButtonRelease)
        {
            showHint(false);
            return true;
        }
        return QWidget::eventFilter(watched, e);
    }

    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        invalidate();
    }

    void paintEvent(QPaintEvent* e) override
    {
        QWidget::paintEvent(e);
        if (!mFrost || size().isEmpty()) return;
        if (mPix.isNull() || mPix.size() != size())
            // the color from the theme (not the palette: the style sheet paints the labels, the
            // palette of a widget it styles keeps the system's light-theme ink)
            mPix = blurredCopy(textPixmap(mLab->font(), TmcTheme::colors().text, mDesc, size()));
        QPainter pr(this);
        pr.drawPixmap(0, 0, mPix);
    }

private:
    void showHint(bool on)
    {
        mFrost = on;
        mHint->setVisible(on);
        // the real text lives in mDesc and is ours to paint; while frosted the label is only empty
        // (hiding it would leave it without geometry: a hidden widget gets none from the layout)
        mLab->setText(on ? QString() : mDesc);
        invalidate();
    }

    void invalidate()
    {
        mPix = QPixmap();
        update();
    }

    QLabel* mLab = nullptr;
    QLabel* mHint = nullptr;
    QString mDesc;                                      // the description, our copy for the blurred paint
    QPixmap mPix;                                       // the blurred text, rebuilt on every text/size change
    bool mFrost = true;
};

}   // namespace

EncountersDialog::EncountersDialog(QWidget* parent, const QStringList& disabled)
    : QDialog(parent)
{
    setWindowTitle(tr("City events"));
    setMinimumSize(880, 560);
    if (kRowCount != kEncCount)
        qWarning("EncountersDialog: %d rows for %d exporter events", kRowCount, kEncCount);
    for (int i = 0; i < kRowCount && i < kEncCount; ++i)
        if (qstrcmp(kRows[i].id, kEncIds[i]) != 0)
            qWarning("EncountersDialog: row %d is \"%s\", the exporter expects \"%s\"", i, kRows[i].id, kEncIds[i]);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Random encounters over the exported world. The checked events ride into the world "
                                  "as the \"osmc_enc\" datapack; the unchecked ones are not written at all. "
                                  "The same list is on the command line: --encounters --encounters-off <id>,<id>…")));

    auto* split = new QHBoxLayout;
    root->addLayout(split, 1);

    auto* tree = new QTreeWidget(this);
    tree->setHeaderHidden(true);
    tree->setColumnCount(1);
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setUniformRowHeights(false);
    for (int i = 0; i < kRowCount; ++i)
    {
        const QString id = QLatin1String(kRows[i].id);
        if (kRows[i].group)
        {
            auto* head = new QTreeWidgetItem(
                { QCoreApplication::translate("EncountersDialog", kRows[i].group) });
            head->setFlags(Qt::NoItemFlags);                       // a section title: no box, no selection
            QFont f = head->font(0);
            f.setBold(true);
            head->setFont(0, f);
            tree->addTopLevelItem(head);
        }
        auto* item = new QTreeWidgetItem(tree, { QCoreApplication::translate("EncountersDialog", kRows[i].title) });
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, disabled.contains(id) ? Qt::Unchecked : Qt::Checked);
        item->setData(0, kIdRole, id);
        item->setData(0, kDescRole, QString::fromUtf8(kRows[i].desc));
        item->setToolTip(0, id);                                   // the --encounters-off key and function name
        tree->addTopLevelItem(item);                               // after its group head, if any
    }
    tree->setCurrentItem(nullptr);                                // the selection starts empty: only boxes are on
    split->addWidget(tree, 5);

    auto* pane = new BlurPane(this);
    pane->setText(tr("Select an event on the left."));
    split->addWidget(pane, 6);

    auto* buttons = new QHBoxLayout;
    auto* allOn = new QPushButton(tr("All on"), this);
    auto* allOff = new QPushButton(tr("All off"), this);
    buttons->addWidget(allOn);
    buttons->addWidget(allOff);
    buttons->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addWidget(box);
    root->addLayout(buttons);

    connect(tree, &QTreeWidget::currentItemChanged, this, [pane](QTreeWidgetItem* cur, QTreeWidgetItem*)
    {
        pane->setText(cur && !cur->data(0, kIdRole).toString().isEmpty()
                        ? cur->data(0, kDescRole).toString()
                        : QObject::tr("Select an event on the left."));
    });
    connect(allOn, &QPushButton::clicked, this, [tree]
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable)
                tree->topLevelItem(i)->setCheckState(0, Qt::Checked);
    });
    connect(allOff, &QPushButton::clicked, this, [tree]
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->flags() & Qt::ItemIsUserCheckable)
                tree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mTree = tree;
}

QStringList EncountersDialog::disabled() const
{
    QStringList off;
    for (int i = 0; i < mTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = mTree->topLevelItem(i);
        if (!(item->flags() & Qt::ItemIsUserCheckable)) continue;             // a group head
        if (item->checkState(0) != Qt::Checked)
            off << item->data(0, kIdRole).toString();
    }
    return off;
}
