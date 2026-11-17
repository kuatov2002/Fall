# CREDITS

Все ассеты, которые видны или слышны в срезе, и их лицензии. Сырые загрузки лежат в `ThirdParty/` (не в git, скачиваются скриптами `Tools/fetch_*.py`), паки Fab ставятся через Fab (см. FAB_TODO.md).

## Fab (бесплатные листинги, Fab Standard License, личная и профессиональная лицензии бесплатны)
| Листинг | Автор | Где в срезе |
|---|---|---|
| Zeppelin Environment — https://www.fab.com/listings/f3136bd7-7ac0-4966-a31c-4493b9a14f22 | Phaedrus Byskou | дирижабль (10 кусков `/Game/Freefall/Assets/Airship`), 17 предметов-обломков (`/Game/Freefall/Assets/Debris/SM_Zep_*`), палуба и рубка в катсцене; текстуры пака в материалах `MI_Zep_*` |
| Retro Futuristic Soldier — https://www.fab.com/listings/e31b91f0-da8a-408c-a3fc-2831bd73400c | Red Mallet Studio | игрок и все враги (скелет Manny), материал `M_RetroSoldier` с вариантами `MI_Char_*`; клипы MM_Idle и MF_Unarmed_Jog_Fwd в катсцене |
| M1911 (FREE) — https://www.fab.com/listings/77247d26-33a3-4dba-a840-7a7d8c92bb9c | Xeradev | пистолет игрока и стрелков, магазин при перезарядке, гильзы |
| Niagara Examples Pack — https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 | Epic Games | взрывы, искры, вспышка выстрела, попадания по металлу (копии в `/Game/Freefall/FX`) |
| Realistic Fire & Explosion VFX Vol.1 - FREE Starter Pack — https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed | DeepBlues | огонь на дирижабле и обломках, большие взрывы, выхлоп ранца вингсьютера |
| temperate Vegetation: Spruce Forest — https://www.fab.com/listings/f8044501-17a2-498f-b198-5f1bc71ee87a | Project Nature | ельник в долине приземления |
| Free Weapon Sound Effects — https://www.fab.com/listings/1697af22-7e2a-410e-b8c0-88239216520d | SoundLab1 | выстрелы игрока и врагов, перезарядка, взрывы |
| IAF Pilot - Rigged WW2 Character — https://www.fab.com/listings/4782e2d0-67f7-466b-be85-f8aa044576c4 | RetroStyle Games | скачан, в срезе не используется (риг CAT, нужен ретаргет) |

## Poly Haven (CC0, https://polyhaven.com)
Скачиваются `Tools/fetch_polyhaven.py`, список и авторы в `ThirdParty/PolyHaven/manifest.json`.

Модели (обломки, мелочь, снаряжение): Barrel 01 (Jorge Camacho), Ammo Box (DanKit), Barrel 03 (Serhii Khromov), Caged Hanging Light, Ceiling Fan, Medical Box, Metal Jerrycan Green, Metal Office Desk, Metal Stool 02, Small LPG Tank (Ulan Cabanilla), Exterior Aircon Unit (Monsta3D), Fire Alarm, Propane Tank (Slinc), Hanging Industrial Lamp, Industrial Wall Lamp, Modular Electric Cables, Modular Pipes (Kuutti Siitonen), Industrial Storage Cart (Jule Bielitz), Korean Fire Extinguisher 01 (UM JOORIN), Ladder Sectioned 01, Rollershutter Door (MP), Lifebuoy (Hank Kaamura), Metal Tool Chest (Yann Kervran, John Hutcheson), Modular Airduct Circular 01 (Riley Queen), Modular Airduct Rectangular 01, Portable Generator, Utility Box 01 (James Ray Cock), Modular Fire Escape (Juniix), Modular Industrial Pipes 01 (Jorge Camacho), Old Gas Mask (Michał Wiśniewski), Old Military Compressor (Brian Speight), Old Military Crate (Jack Mava), Overhead Crane (Timothy3D), Power Box 01 (Rico Cilliers, Yann Kervran), Service Pistol, Vintage Radio Transceiver (Mateusz Sadek), Stick Grenade (singaii), Vintage Flashlight (Omar M. El-Safy), Wooden Military Crate (Prabhjinder Singh), Worn Metal Rack (Luca B).

Текстуры: Aerial Grass Rock, Aerial Rocks 02, Blue Metal Plate, Fabric Leather 02, Green Metal Rust, Metal Plate, Metal Plate 02, Rock 06 (Rob Tuytel), Metal Grate Rusty (Rob Tuytel, Dimitrios Savva), Caban, Hessian 380 (colormass, Rico Cilliers), Cliff Side (James Ray Cock, Jenelle van Heerden, Dario Barresi), Container Side (Dimitrios Savva), Corrugated Iron 02 (Jenelle van Heerden, Sergej Majboroda), Painted Metal Shutter (Dario Barresi, Rico Cilliers, Charlotte Baglioni). HDRI Belfast Sunset Pure Sky (Greg Zaal, Dimitrios Savva, Jarod Guest) скачан, не используется (небо даёт Sky Atmosphere).

## BigSoundBank (Joseph Sardin, CC0 1.0, https://bigsoundbank.com)
Скачиваются `Tools/fetch_bigsoundbank.py`. В срезе: Wind, Strong Wind #1, Whistling of the Wind #1 (три слоя ветра), Flag in the Wind #1 (хлопанье ткани), Flag #1 (хлопок купола), Wind from inside #1, Big Branching Fire #1, Evacuation Alarm #1 (катсцена). Остальные записи из списка скачаны для звуков ударов и пролётов, пока не подключены.

## Kenney (CC0, https://kenney.nl)
Скачиваются `Tools/fetch_kenney.py`. В срезе: Smoke Particles, клубы whitePuff00–15, собраны в атлас 4×4 `T_SmokeAtlas` (дым, огонь, облачные клочья через материал `M_FF_Sprite`). Particle Pack, Impact Sounds, Sci-Fi Sounds скачаны, пока не подключены.

## Рельеф
Высоты долины Роны у Мартиньи (46,112° с. ш., 7,075° в. д.) и Альп вокруг на 440 км: тайлы Terrarium (Mapzen terrain tiles, AWS Open Data, https://registry.opendata.aws/terrain-tiles/). Источники данных в тайлах для этого района: SRTM (общественное достояние, NASA/USGS), EU-DEM (произведено с использованием данных Copernicus), ETOPO1 и другие по списку https://github.com/tilezen/joerd/blob/master/docs/attribution.md. Скачиваются `Tools/fetch_terrain.py`, запекаются в `DA_FallTerrain` скриптом `Tools/make_terrain.py`.

## Unreal Engine (Unreal Engine EULA)
Sky Atmosphere, Volumetric Cloud (`m_SimpleVolumetricCloud_Inst`), Exponential Height Fog, ProceduralMeshComponent, Niagara, Level Sequence. Манекен шаблона Third Person больше не используется.

## Сделано в проекте
- Blender (`Tools/blender/make_meshes.py`): купол парашюта, контейнер парашюта, турель тяжёлого, служебные меши эффектов. Нарезка дирижабля: `Tools/blender/split_zeppelin.py`.
- Материалы: `Tools/make_materials.py`, `Tools/make_art.py`, `Tools/make_zeppelin_materials.py`, `Tools/make_vfx_assets.py`.
- Катсцена: `Tools/make_intro.py` (последовательность), `FallIntro.cpp` (события).
- Процедурный рельеф, процедурная анимация падения, синтез части звуков (попадания, свист, сигналы), код.

## Kimodo
Не использовался (DECISIONS.md, D-026).

## Итерация 3
- Купол: текстура Poly Haven Hessian 380 (colormass, Rico Cilliers) на материале ткани. Ближняя детализация рельефа: Poly Haven Rock 06 (Rob Tuytel).
- Референсы AAA для анализа и критика: 14 официальных скриншотов со страниц Steam (Just Cause 3 и 4, Battlefield 1, 4 и 2042, Far Cry 5, Halo Infinite). Лежат в `Saved/Review/References` (вне git и вне сборки), в игре не используются. Права принадлежат их владельцам.
- Превью персонажа с Fab (Red Mallet Studio) скачано только для сравнения в `Saved/Review/IT3/lookdev/fab`, в игре не используется.
