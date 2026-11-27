#pragma once
// The Russian voice of the "osmc_enc" pack (docs/city-encounters.md). The generator is written and
// stays English; when the export ran with the ru interface language, EncPack::build runs every
// generated entry through translate() below, which swaps the player-visible payloads for Russian:
// the {"text":"..."} of tellraw/titles/CustomName, the raw:"..." pages of the rules book, and the
// plain quoted strings (the book's author, the scarecrow's warning). Lines absent from the table
// pass through untouched, so a missing entry speaks English rather than breaking a function.
// Keys are the payloads as the pack bytes them OUT: the tier lines and the six timetable boards
// are keyed by their RESOLVED text (the %n placeholders are filled long before this pass).
#include <QByteArray>

namespace encRu {

inline QByteArray translate(const QByteArray& in)
{
    static const char* const kPairs[][2] = {
        // actionbar/chat voices of the events, the dread tiers, the ninth-and-a-half floor
        { "314: her office door stands open", "314: её кабинет открыт" },
        { "I did not ride. thank you anyway", "я не поехал. всё равно спасибо" },
        { "a bell answered somewhere - the city listens to its towers",
          "где-то отозвался колокол — город слушает свои башни" },
        { "a chest stood in the hall where a moment ago there was none",
          "в зале стоял сундук там, где мгновение назад его не было" },
        { "a dog with no collar falls in beside you", "пёс без ошейника идёт рядом с тобой" },
        { "a market is open here — where the field lay all day",
          "здесь открыт рынок — там, где весь день лежало поле" },
        { "a plate lies by the car door, where no pedal belongs",
          "табличка лежит у дверцы машины, там, где не бывает педали" },
        { "a rumble in the tunnel — the last train tonight", "гул в туннеле — последний поезд сегодня" },
        { "a spotlight stands over one empty chair", "прожектор светит на один пустой стул" },
        { "a warm hand - the night is less sure of itself", "тёплая рука — ночь стала менее уверенной в себе" },
        { "he was already behind you, and then he was not", "он уже был сзади, а потом его не стало" },
        { "motor oil smell — from beyond the quarter, from the dark lot",
          "запах машинного масла — из-за квартала, с тёмной стоянки" },
        { "route 13 rushes past a stop that never had it",
          "тринадцатый проносится мимо остановки, которой у него никогда не было" },
        { "somebody waits at the road edge with a lantern", "кто-то ждёт у обочины с фонарём" },
        { "something tall keeps pace with your steps", "что-то высокое идёт в ногу с твоими шагами" },
        { "something under the floor stopped digging and started listening",
          "что-то под полом перестало копать и начало слушать" },
        { "somewhere down the tunnel wheels begin to turn", "где-то в туннеле трогаются колёса" },
        { "somewhere under the bowl the stone begins to drink", "где-то под чашей камень начинает пить" },
        { "the air here holds no sound", "воздух здесь не держит звука" },
        { "the alley of the graveyard fills with cold blue light",
          "аллейка кладбища наполнилась холодным синим светом" },
        { "the board was rewritten; the last line reads V. Nezhina",
          "доску переписали; последняя строка — V. Nezhina" },
        { "the board was rewritten; the last line reads A. Voltinskaya",
          "доску переписали; последняя строка — A. Voltinskaya" },
        { "the board was rewritten; the last line reads K. Surov",
          "доску переписали; последняя строка — K. Surov" },
        { "the board was rewritten; the last line reads M. Golubeva",
          "доску переписали; последняя строка — M. Golubeva" },
        { "the board was rewritten; the last line reads I. Tish",
          "доску переписали; последняя строка — I. Tish" },
        { "the board was rewritten; the last line reads L. Morozova",
          "доску переписали; последняя строка — L. Morozova" },
        { "the bowl reflects a cloud this sky does not have",
          "чаша отражает облако, которого над этим небом нет" },
        { "the city fell silent - this park is on no map", "город замолчал — этого парка нет ни на одной карте" },
        { "the corridor goes quiet — 314 stayed closed all night",
          "коридор затихает — 314 простоял закрытым всю ночь" },
        { "the dusk stretches a shadow nothing here casts", "сумерки тянут тень, которую здесь не отбрасывает ничто" },
        { "the fountain runs again — and the water is colder than it was",
          "фонтан пошёл снова — и вода холоднее, чем была" },
        { "the fountain went silent — the water left through the bottom",
          "фонтан замолчал — вода ушла сквозь дно" },
        { "the garage is empty. it was never full", "гараж пуст. он никогда не был полон" },
        { "the hall is not empty any more", "зал больше не пуст" },
        { "the howl comes from past the last house", "вой доносится из-за последнего дома" },
        { "the lamp light holds; the extra shadow went with the night",
          "свет фонаря выдержал; лишняя тень ушла вместе с ночью" },
        { "the landing is here, the city outside it is not", "площадка здесь — города за ней нет" },
        { "the last train carried its riders to the end of the line",
          "последний поезд довёз своих до самого конца линии" },
        { "the lift answered on a floor the stairs do not list",
          "лифт отозвался на этаже, которого у лестницы нет" },
        { "the map lists seven flights tonight — you heard eight",
          "на карте сегодня семь пролётов — ты слышал восемь" },
        { "the parked car is playing the news", "припаркованная машина играет новости" },
        { "the platform is lit on a map that lists no such station",
          "платформа освещена — на карте такой станции нет" },
        { "the stairwell took its guest back; the wall is a wall again",
          "лестница вернула гостя; стена снова стала стеной" },
        { "the street is climbing a floor nobody will ever see",
          "улица поднимается на этаж, который никто никогда не увидит" },
        { "the timetable, rewritten", "переписанное расписание" },
        { "the traffic came back - the bench here is cold, as it should be",
          "трафик вернулся — скамейка здесь холодная, как и положено" },
        { "the tunnel breathes in", "туннель вдыхает" },
        { "the wires hum fifty hertz to nobody", "провода гудят пятьдесят герц в никуда" },
        { "you count the footsteps behind you and one is not yours",
          "ты считаешь шаги позади — и один из них не твой" },
        { "you photographed the 13. the city keeps it as a legend",
          "ты сфотографировал тринадцатый. город хранит это как легенду" },
        { "Somewhere in the city a window stopped being a window. The night noticed.",
          "Где-то в городе окно перестало быть окном. Ночь это заметила." },
        { "The doors you passed today are locking by themselves. The city heard the glass.",
          "Двери, мимо которых ты сегодня прошёл, запираются сами. Город услышал стекло." },
        { "The streetlights look at the pavement now. The tall windows look back.",
          "Фонари теперь смотрят в асфальт. Высокие окна смотрят в ответ." },
        { "The city knows your count. Tonight everything walks, and every house has a floor you never found.",
          "Город знает твой счёт. Сегодня ночью ходит всё, и в каждом доме есть этаж, который ты не находил." },
        { "that sound travelled further than the glass",
          "тот звук разнёсся дальше, чем само стекло" },
        // the rules book (raw:"..." pages, set as one data-modify value; the escapes below are the
        // book's own SNBT \n line breaks, matching byte-for-byte what the generator emits)
        { "Rules of the Night", "Правила ночи" },
        { R"(The world around you is alive.\nThe city feels broken glass:\nevery window you break raises its dread.\nThe higher the dread,\nthe more often the night's events find you.\nSome are harmless.\nSome can kill you.)",
          R"(Мир вокруг вас живой.\nГород чувствует разбитое стекло:\nкаждое разбитое окно поднимает его ужас.\nЧем выше ужас,\nтем чаще ночь наводит на вас события.\nОдни безобидны.\nДругие могут убить.)" },
        { R"(At 25, 50, 75 and 95\nthe night changes its voice.\nAt 95 every house\nopens its hidden floor.)",
          R"(На 25, 50, 75 и 95\nночь меняет голос.\nНа 95 каждый дом\nоткрывает свой скрытый этаж.)" },
        { R"(The dread calms down over kindness:\n- a torch in the dark: one point,\nat most once in five minutes\n- ten monsters laid to rest: one point\n- a bell answers by three\n- a finished trade by one)",
          R"(Ужас утихает от добрых дел:\n- свет в темноте: очко,\nне чаще раза в пять минут\n- десять упокоенных монстров: очко\n- колокол отвечает тремя\n- завершённая сделка - очком)" },
        { R"(Bells and trades together pay\nat most three times a day,\nand midnight renews the count.\nA tamed cat or dog pays three -\nas often as you earn it.)",
          R"(Колокол и сделки вместе платят\nне более трёх раз в день,\nи полночь обновляет счёт.\nПриручённый кот или пёс - три,\nи сколько раз заслужите.)" },
        { R"(The fog park flattens the dread\nto twenty-five, whatever it was.\nDawn takes one point back\non its own.)",
          R"(Туманный парк сглаживает ужас\nдо двадцати пяти, каким бы он ни был.\nРассвет сам забирает\nочко обратно.)" },
        { R"(Anything here can be switched off on the run:\nfunction osmc_enc:off/<name> turns one away,\nosmc_enc:on/<name> calls it back.\nThe dread answers the console itself:\nosmc_enc:dread/s25 - the same for\ns0, s50, s75, s95, s100.)",
          R"(Всё здесь выключается на ходу:\nфункция osmc_enc:off/<name> убирает одно,\nosmc_enc:on/<name> возвращает.\nСам ужас откликается консоли:\nosmc_enc:dread/s25 - так же\ns0, s50, s75, s95, s100.)" },
        // the plain quoted strings: the book's author, the scarecrow's night warning
        { "the city", "город" },
        { "leave by morning", "уйди к утру" },
    };
    static const QByteArray kPre[3] = { QByteArrayLiteral("{\"text\":\""), QByteArrayLiteral("raw:\""),
                                        QByteArrayLiteral("\"") };
    QByteArray out = in;
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); ++i)
        for (size_t s = 0; s < 3; ++s)
            out.replace(kPre[s] + kPairs[i][0] + "\"", kPre[s] + kPairs[i][1] + "\"");
    // SNBT values (the book pages, the author, the CustomName of the scarecrow) carry their quotes
    // ESCAPED as \"payload\" - without this pass every such payload stayed English in a Russian
    // world (a user catch: the rules book never came out in Russian)
    static const QByteArray kEsc = QByteArrayLiteral("\\\"");
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); ++i)
        out.replace(kEsc + kPairs[i][0] + kEsc, kEsc + kPairs[i][1] + kEsc);
    return out;
}

}   // namespace encRu
