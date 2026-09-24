# QiYaa

Плеер Яндекс Музыки в стиле Winamp 2 на C++/Qt 6 — переписанный [Yaamp](https://github.com/Kickoman/yaamp) без Electron: настоящие окна ОС, классические скины `.wsz`, минимум CPU.

> **Статус: этап 0 — прототип.** Главное окно со скином, перетаскивание с прилипанием к краям экрана, воспроизведение лайкнутых треков. Эквалайзер, плейлист, меню Яндекса — этап 1.

## Что умеет прототип

- Загрузка скинов Winamp 2.x (`.wsz`), 8 встроенных + свой файл (кнопка Eject или меню).
- Форма окна по `region.txt`, «Always on top», размер окна 100–300 % (меню → Size; «Double size» = 200 %). Дробные размеры рисуются через целое увеличение с плавным уменьшением, чтобы пиксели скина оставались ровными.
- Перетаскивание за любое место без кнопок; прилипание к краям экрана (15 px); окно **всегда остаётся в видимой области**, в том числе после отключения монитора. Позиция запоминается.
- Кнопки транспорта, громкость, баланс, перемотка по скачанному, shuffle/repeat, колесо мыши = громкость.
- Яндекс Музыка: лайкнутые треки (первые 200) → воспроизведение mp3 320 kbps с потоковой загрузкой, отметка прослушивания.
- В простое таймеров нет, аудиоустройство остановлено → CPU ≈ 0.

## Токен Яндекса

Прототип ищет токен по порядку:

1. переменная окружения `QIYAA_TOKEN`;
2. файл `~/.config/QiYaa/token` (Windows: `%LOCALAPPDATA%\QiYaa\token`);
3. `token.json` старого Yaamp (`~/.config/Yaamp/`, `%APPDATA%\Yaamp\`, `~/Library/Application Support/Yaamp/`) — если Yaamp у вас уже залогинен, ничего делать не нужно.

Нормальный вход (OAuth по коду устройства) — этап 1.

## Сборка

Нужны CMake ≥ 3.21, компилятор C++20 и Qt ≥ 6.4 (Core, Gui, Widgets, Network). Остальное (miniaudio, miniz) лежит в `third_party/`.

### Ubuntu 24.04

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/QiYaa
```

Для звука miniaudio сам подгружает PulseAudio/PipeWire/ALSA во время работы — dev-пакеты не нужны.

### Windows

Qt 6.8 через [онлайн-установщик](https://www.qt.io/download-qt-installer-oss) (компонент MSVC 2022 64-bit), затем в «x64 Native Tools Command Prompt»:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt build\QiYaa.exe
```

Готовые сборки под Linux, Windows и macOS появляются в артефактах GitHub Actions.

## Параметры командной строки

| Параметр | Что делает |
|---|---|
| `--play-file <путь>` | Играть локальный mp3/flac/wav вместо Яндекса (проверка звука) |
| `--offline` | Не подключаться к Яндексу |
| `--skin <file.wsz>` | Скин на этот запуск |
| `--screenshot <png>` | Отрисовать главное окно в PNG и выйти (работает с `QT_QPA_PLATFORM=offscreen`) |

## Wayland

Wayland не даёт приложениям ставить свои окна в нужные координаты, а на этом держится Winamp (окна прилипают друг к другу). Поэтому на Linux по умолчанию QiYaa запускается через XWayland (`QT_QPA_PLATFORM=xcb;wayland`). Нативный режим: `QIYAA_NATIVE_WAYLAND=1` — окно двигает композитор, без прилипания и запоминания позиции. Если вы сами задали `QT_QPA_PLATFORM`, выбор не меняется.

## Структура

```
src/app      main(), пути
src/skin     загрузка .wsz, спрайты, region.txt
src/ui       SkinnedWindow (окно без рамки, маска, перетаскивание), Snap, MainWindow
src/yandex   ApiClient, подпись ссылки на трек, токен
src/audio    AudioEngine: загрузка → поток декодера → кольцевой буфер → miniaudio
src/core     Player: плейлист лайков, next/prev/shuffle/repeat
tests/       модульные тесты (Qt Test)
```

## Лицензия

MIT, см. `LICENSE`. Сторонние компоненты и авторы скинов — в `THIRD_PARTY.md`.
