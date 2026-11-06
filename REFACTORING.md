# План разбиения export/mcworldexporter.cpp (18 659 строк)

## 0. Что есть сейчас (замерено)

| Диапазон | Блок | Строк | Состояние |
|---|---|---|---|
| 91..470 | `kBlockPalette`, `kWools`, таблицы | ~380 | данные |
| 471..1063 | `blockName`, `blockProperties`, `nearestBlockId/CarpetId`, `paletteIndexOf`, `materialBlockId`, veg-предикаты | ~590 | статики, узкий интерфейс |
| 1065..1098 | `pairKey`/`ColKey`/`qHash`/`sectionIndex` | ~35 | inline |
| 1099..1206 | `VoxelStore`, `GroundChunk`, `groundBlockAt` | ~110 | класс+структура |
| 1207..1611 | `CaveSet` (+`CaveBe`,`CaveTemplate`, зеркалирование) | ~405 | самодостаточный класс |
| 1612..1848 | `LairSet` (+`LairBe/Struct/Place`) | ~235 | самодостаточный класс |
| 1849..2073 | `PaleDonor`, `findPlacementSalt` | ~225 | самодостаточный класс |
| 2074..2310 | геометрия: clip'ы, `isRectilinearOutline`, `VxTransform`, `rasterizeTriangle` | ~235 | свободные функции |
| 2311..2587 | site-структуры (`ChestSite`, `NpcSite`, …), `BookText`, `pickChestLoot` | ~275 | данные |
| 2588..2687 | `decorateWalls` | ~100 | свободная функция |
| 2688..6547 | **`voxelizeObject` — 22 параметров, 3 860 строк** | ~3860 | главный монолит №1 |
| 6548..6758 | рельсы: `railCellChain`, `railShapeFor`, `electrifyRails`, spans, `footprintColumns` | ~210 | свободные функции |
| 6759..8079 | anvil/NBT: `buildChunkNbt`, `buildEntityChunkNbt`, `buildLevelDat`, `buildRegionFile`, … | ~1320 | свободные функции, NbtWriter уже в `format/` |
| 8080..8248 | сшивка зданий: `mergeableCode`, `raiseLowBuildingParts`, `mergeIndustrial` | ~170 | свободные функции |
| 8249..18659 | **`Export()` — ~10 410 строк** | ~10410 | главный монолит №2 |

Ключевая проблема `Export()`: ~50 общих контейнеров-локалов (`colMin`, `colWool`, `roadCols`,
`pads`, `chests`, `npcs`, …, строки 8373..8520), через которые этапы конвейера общаются друг
с другом. `voxelizeObject` — «long parameter list»: 22 аргумента, большинство — выходные site-векторы.

## 1. Принципы

- Рефакторинг без изменения поведения: ни одного функционального правка в этих коммитах.
- Каждый шаг: перенёс один блок → собрал → байт-контроль миров → тесты → коммит.
- Сначала чистые переносы (фаза 1, резать-вставлять), затем параметрические объекты (фаза 2),
  затем контекст и этапы (фаза 3, strangler).

## 2. Фаза 0 — страховка (поведение «забивается камнем»)

1. Контрольные миры: `tests/data/osm_buildings.osm` + 2–3 реальных проекта среднего размера.
   До начала рефакторинга собрать эталоны текущим exe: `C:\Temp\tmc_ref\<name>_before.zip`.
2. Скрипт `tests/tools/cmp_worlds.py`: попарное сравнение zip поэлементно, байт-в-байт `.mca`
   и датапак; `export_info.json`/`ATTRIBUTION.txt` исключить (зависят от версии). `level.dat`
   проверить на стабильность (если содержит время — исключить).
3. Стабильность вывода: один вход → два экспорта → сравнить. Если разойдётся — виновник порядок
   обхода `QHash`; зафиксировать `QT_HASH_SEED=0` в прогонах контроля.
4. Тайминги: `MCX_LOG_STEPS` — базовая линия до первого переноса (проверка деградации инлайна
   после переезда в разные TU).

## 3. Фаза 1 — чистые извлечения (≈12 шагов, файл до ~12 900 строк)

Все функции уже `static` с узкими сигнатурами — переносится cut/paste: снять `static`,
объявить во внутреннем заголовке, добавить в `osmc.pro`.

| Новый файл | Что | ~Строк |
|---|---|---|
| `export/colkey.h` | `pairKey/keyFirst/keySecond/ColKey/qHash/sectionIndex` (inline) | 40 |
| `export/blocks.h/.cpp` | enum BlockId, палитры, `blockName/blockProperties/nearest*/paletteIndexOf/materialBlockId/veg-предикаты` | 1150 |
| `export/voxelstore.h/.cpp` | `VoxelStore`, `GroundChunk`, `groundBlockAt` | 110 |
| `export/caveset.h/.cpp` | `CaveSet` и помощники состояний пещер | 400 |
| `export/lairset.h/.cpp` | `LairSet` | 235 |
| `export/paledonor.h/.cpp` | `PaleDonor`, `findPlacementSalt` | 225 |
| `export/geom.h/.cpp` | clip'ы, `VxTransform`+`toBlock*` (inline), `rasterizeTriangle`, `isRectilinearOutline` | 235 |
| `export/sites.h` | site-структуры, `BookText`, `pickChestLoot`, `zoneInside` | 275 |
| `export/facade.cpp` | `decorateWalls` (+родственные раскраски фасадов) | 100 |
| `export/rails.h/.cpp` | `railCellChain/railShapeFor/electrifyRails`, spans, `footprintColumns` | 210 |
| `export/anvil.h/.cpp` | `packSparse`, `build*ChunkNbt`, `buildLevelDat`, `buildWorldGenSettings`, `buildRegionFile`, `armorDamage` | 1320 |
| `export/buildingmerge.h/.cpp` | `mergeableCode`, `raiseLowBuildingParts`, `mergeIndustrial` | 170 |

Замечания:
- Горячее (`pairKey`, `groundBlockAt`, `toBlock*`) оставить inline в заголовках.
- Палитры — `extern const` в заголовке, определение в .cpp; обращения уже через функции.
- После каждой пачки — замер `MCX_LOG_STEPS` против базовой линии.

## 4. Фаза 2 — `voxelizeObject` → `export/voxelize.*` (~10 шагов)

1. **Introduce Parameter Object**: `struct VoxelOut` (`colMin`, `colAnchor`, `chests`, `paints`,
   `mannequins`, `spawners`, `npcs`, `frames`, `basements`, `signs`, `banners`, `zones`,
   `islands`, `storyCounter`) — сигнатура с 22 аргументов до ~7. Один чистый механический коммит.
2. Перенос функции целиком в `export/voxelize.h/.cpp` как класс `ObjectVoxelizer`
   (`obj`, `tf`, `store`, `out` — поля; `run()` — вход). Ключи распараллеливания (`VoxJob`)
   не трогаем: воркеры и так вызывают её на своих `store`.
3. Декомпозиция по уже имеющимся в коде комментариям-блокам (они и есть имена методов):
   `rasterizeGrid`, `floodFillBody`, `makeWalls(structFor)`, `placeWindows(windowDir-профили)`,
   `buildEntrances`, `floorSlabs`, `interFloorStairs`, `partitions`, `interiorsV2`,
   `basementSite`, `privSectorRoof`, `mergedBodies (оси X/Z/Y)` … — метод 150–400 строк,
   по одному методу за коммит.

## 5. Фаза 3 — `Export()` → контекст + этапы (~10 шагов)

1. **`struct ExportCtx`** (`export/stage_ctx.h`): ссылки на входы (`objects`, `tf`, `opts`,
   `res`) + все общие контейнеры конвейера + помощники `surfaceTop/groundAt` (сейчас лямбды).
   Шаг 3a: локалы `Export()` объявлены полями одного `ctx` — один большой, но механический диф.
2. Вынос этапов в свободные функции `void build(ExportCtx&)`, по файлу на этап:

| Файл | Этап (комментарии-маркеры в коде) | ~Строк |
|---|---|---|
| `stage_write.*` | шаги 4/5: чанки→region, zip, `level.dat`, датапак (оркестрация над `anvil.*`) | 400 |
| `stage_ground.*` | `3. Ground` g0..g4, `makeGround`, мосты, фаска 3.59b | 1500 |
| `stage_vegetation.*` | 3.5 (леса, болота), 3.51 правила деревьев, 3.55, 3.6 | 2000 |
| `stage_cemeteries.*` | overlay 3.5 (кладбища) — отделить от лесов | 400 |
| `stage_streets.*` | 3.57 мебель, 3.575 точки, 3.58 ограды, 3.576 плиты платформ, вывески | 1500 |
| `stage_sites.*` | мебель csrpol-площадок (спорт, детсады, кладбища-полигоны, памятники) | 1200 |
| `stage_subway.*` | диспетчер рельсов, тоннели, залы, шахты, секретные комнаты | 1800 |
| `stage_dungeons.*` | подземелья церк/лабов, деревни, стронгхолд, парящие острова | 1000 |
| `stage_dispatch.*` | диспетчер вокселизации, `flushJobs`, проходы после него (купола, клинья, стремянки) — самый запутанный, выносим последним | 1800 |

   Точное распределение (что в контексте, что живёт внутри этапа) решается на шаге выноса:
   в `ExportCtx` только те поля, которые читает следующий этап.
3. Порядок: снаружи вовнутрь — `write` → `ground` → `vegetation/cemeteries` → `streets/sites`
   → `subway` → `dungeons` → `dispatch`. Лямбды с 8+ аргументами (`digStationHall`) — в методы
   этапа, сигнатуру не менять.

## 6. Риски

- **Параллельность** (`flushJobs`, `QtConcurrent`): семантику потоков не менять; `VoxelOut`
  проверить на гонки при переносе (каждый воркер пишет только в свой store — сохранить как есть).
- **Инлайнинг между TU**: горячая мелочь остаётся inline в заголовках; каждый шаг — сверка
  `MCX_LOG_STEPS` (рост >20% у шага = стоп, разбираться).
- **Порядок `#include`** новых заголовков — как у соседей, osgeo-цепочек здесь нет.
- Байт-контроль — главный гарант, тесты `osm shp harness` — вторые руки.

## 7. Ожидаемый итог

`mcworldexporter.cpp` ~1 500 строк: `McObject`/`WorldOptions`, `Export()` из ~40 строк вызовов
этапов, общая обвязка. ~18 новых файлов по 100–1500 строк, каждый со своей ответственностью.
~30 коммитов вида `refactor: move X to export/Y` — ни одного с изменением поведения.
