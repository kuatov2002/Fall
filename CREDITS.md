# CREDITS

Все ассеты, использованные в срезе, и их лицензии.

| Ассет | Откуда | Лицензия | Где используется |
|---|---|---|---|
| Манекен SKM_Manny_Simple, скелет SK_Mannequin, PA_Mannequin | шаблон Third Person, Unreal Engine 5.9 | Unreal Engine EULA (контент Epic в составе движка и шаблонов) | тело игрока и врагов |
| Анимации шаблона (MM_*, MF_*) | шаблон Third Person | Unreal Engine EULA | не используются в срезе (анимация процедурная) |
| /Engine/BasicShapes (Cube, Sphere, Cylinder, Cone, Plane) | Unreal Engine | Unreal Engine EULA | обломки, дирижабль, снаряжение, оружие, деревья, спрайты эффектов |
| /Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst | Unreal Engine | Unreal Engine EULA | слой объёмных облаков |
| SkyAtmosphere, VolumetricCloud, ProceduralMeshComponent | Unreal Engine | Unreal Engine EULA | небо, облачный слой, рельеф, ленты |

Созданное в проекте (код и скрипты в репозитории):
- Материалы `/Game/Freefall/Materials/*` генерируются скриптом `Tools/make_materials.py`.
- Рельеф генерируется кодом (`AFallPlanet`), текстур нет.
- Весь звук синтезируется кодом (`FallAudio.cpp`): ветер, выстрелы, попадания, свист, сигнал прицеливания, взрывы, раскрытие купола. Внешних звуковых файлов нет.
- Позы и движение тела процедурные (`FallAnimInstance.cpp`, `FallTuning.cpp`), мокапа нет.

Сторонних ассетов с Fab, Mixamo, Quixel и других источников в срезе нет. Предложения по платным и бесплатным пакам: `FAB_TODO.md`.
