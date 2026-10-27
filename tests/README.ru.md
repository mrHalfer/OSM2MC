# Тесты tMC

[English](README.md) · **Русский**

Тесты запускают `tMC.exe` с `--lang en`, поэтому сверяются английские сообщения журнала.

Всё, что нужно для проверки экспортёра, лежит здесь, внутри проекта tMC.

```
tests\
  run_tests.py        единый запуск: py -3 run_tests.py [osm shp harness servers | all] [-v]
  tools\              проверки и помощники (Python)
  harness\            C++ стенд экспортёра: mc_export_test.cpp, mc_scene_run.cpp, build_harness.bat
  data\               тестовые данные: gen_osm_test.py -> osm_buildings.osm + osm_buildings.expected.json
  servers\mc26\       сервер Minecraft 26.3 (server.jar; рабочие файлы создаются при запуске)
  servers\mc121\      сервер Minecraft 1.21.11
  worlds\             сгенерированные миры (zip), их логи, распакованные копии не хранятся
  build\              артефакты стенда (exe, obj)
  local_config.json   необязательно, в репозиторий не входит: машинные пути (QT_BIN, GDAL_BIN, TMC_EXE, JAVA, TMC_TEST_SHP)
```

## Этапы `run_tests.py`

| этап | что делает | время |
|---|---|---|
| `osm` | синтетический OSM (`data/gen_osm_test.py`: 28 зданий с ожидаемыми кодами, дороги, рельсы, парковки, площадки, площадь) строится через `tMC.exe` по рабочим правилам `osm_rules.json`; сверяется код каждого здания (проход 1: теги, POI, участки; проход 2: соседи и площадь), число объектов и коды в остальных слоях, отсутствие лишних «зданий», запасной лес скрипунов; затем те же данные в 1.21.x и в суперплоскости | ~15 с |
| `shp` | режим «папка с shape-файлами» (каталог задаётся `TMC_TEST_SHP`), `default.t3d`, итог по категориям. Нет данных — SKIP | ~10 с |
| `harness` | сборка стенда из `mcworldexporter.cpp` корня tMC, 6 миров `mc_test*.zip`, полный `verify_mc_export.py` (ждём `RESULT PASS`) | ~10-20 мин |
| `servers` | настоящие серверы 26.3 и 1.21.11 открывают миры tMC (`server_check.py`) и шумовые миры стенда (`smoke_mc.py`) | ~10 мин |

Быстрый цикл при правке правил OSM: `py -3 run_tests.py osm`. Перед сдачей правок экспортёра — `py -3 run_tests.py all`.

## Требования
- Собранный `tMC.exe` (`build.bat` в корне); путь — переменная `TMC_EXE`, ключ `local_config.json` или `tMC.exe` из `PATH`.
- Каталоги Qt и GDAL с DLL: `QT_BIN`, `GDAL_BIN` (переменные окружения или `local_config.json`); они добавляются в `PATH` запускаемых программ.
- Java для серверов: `JAVA` (переменная или `local_config.json`), иначе `java` из `PATH`.
- Серверы Minecraft (`server.jar`) в репозиторий не входят: положите их в `servers\mc26` и `servers\mc121` (условия EULA Mojang).
- Серверы 26 и 121 делят порт 25565 — `run_tests.py` запускает их строго по очереди; параллельно не запускать.

## Полезное по отдельности
- `tools\test_osm.py`, `tools\test_shp.py` — отдельные этапы (код 0 — PASS, 77 — SKIP).
- `tools\server_check.py 26|121 <zip> [--area x0 z0 x1 z1]` — открыть любой мир сервером, forceload бокса.
- `tools\smoke_mc.py 26|121 <zip>` — серверный смоук шумовых миров стенда (манекены, спавнер, дно).
- `tools\verify_mc_export.py [zip ...]`, `tools\lightcheck.py`, `tools\probe_blocks.py` и остальные — проверки готовых миров; миры лежат в `tests\worlds`.
- Для TMC_OSM_DEBUG=1 в журнале tMC по строке на здание: теги, которые увидели правила, и итоговый слой/код.

## Что не переносилось
Скрипты извлечения шаблонов (`extract_*.py`, `gen_*.py`) в проект не входят: это не тесты, а генераторы `resources/templates/*.bin` из мира-донора.
