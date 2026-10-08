# Сборка

## Что нужно

- CMake ≥ 3.21 и компилятор C++20. CI собирает GCC 12 и 13, Apple Clang из Xcode на macOS 14 и
  MSVC 2022.
- Qt ≥ 6.4: Core, Gui, Widgets, Network и Qt Linguist (`lrelease` собирает переводы интерфейса;
  на Ubuntu это `qt6-tools-dev` и `qt6-l10n-tools`, в установщике Qt он есть всегда); для тестов —
  Test.
- macOS: медиаклавиши используют системные Foundation и MediaPlayer; нужен компилятор
  Objective-C++ из Xcode Command Line Tools. Поддержка включается автоматически.
- Linux: Qt DBus — для медиаклавиш (MPRIS). Без него QiYaa собирается и просто не публикует себя.
- Milkdrop: Qt OpenGL и заголовки OpenGL (на Ubuntu это `libgl-dev`), плюс projectM 4.1. projectM
  берётся установленный (vcpkg, Homebrew, пакет дистрибутива); если его нет, CMake скачивает
  версию 4.1.7 и собирает её сам — для этого при первой настройке нужен интернет.
- Джем: Qt WebSockets (на Ubuntu это `qt6-websockets-dev`, в установщике Qt — модуль Qt WebSockets).
  Без него QiYaa собирается без джема.
- Windows: SMTC (медиаклавиши) собирается из C++/WinRT, который входит в Windows SDK. projectM
  нужен из vcpkg (ему требуется GLEW).

miniaudio и miniz лежат в `contrib/`, ставить их не нужно. Звуковую подсистему (PulseAudio,
PipeWire, ALSA) miniaudio подгружает сам во время работы — dev-пакеты для неё не нужны.

Спецификация поведения, общая с Android-версией, подключена подмодулем в `spec/`. Её читают
тесты, поэтому клонируйте с подмодулями: `git clone --recursive`, или в уже склонированном
репозитории выполните `git submodule update --init`.

## Параметры CMake

| Параметр | По умолчанию | Что делает |
|---|---|---|
| `QIYAA_WITH_MILKDROP` | `ON` | Окно Milkdrop. Если Qt OpenGL или projectM не нашлись, собирается без него |
| `QIYAA_REQUIRE_MILKDROP` | `OFF` | Остановиться с ошибкой, если Milkdrop собрать нельзя (так собирает CI для установщиков) |
| `QIYAA_FETCH_PROJECTM` | `ON` | Скачать и собрать projectM 4.1.7, если он не установлен |
| `QIYAA_WITH_JAM` | `ON` | Джем. Если Qt WebSockets не нашёлся, собирается без него |
| `QIYAA_REQUIRE_JAM` | `OFF` | Остановиться с ошибкой, если джем собрать нельзя (так собирает CI для установщиков) |
| `QIYAA_JAM_URL` | `https://qiyaa.kanstancin.net` | Сервер джема, который приложение предлагает по умолчанию |
| `QIYAA_VERSION` | см. `CMakeLists.txt` | Версия `X.Y.Z`: имена пакетов и то, что показывает программа. CI на теге `vX.Y.Z` ставит её из тега |
| `QIYAA_WITH_TELEMETRY` | `OFF` | Телеметрия: статистика использования и отчёты о падениях ([telemetry.md](telemetry.md)). Включают её сборки для релизов |
| `QIYAA_TELEMETRY_URL` | `<QIYAA_JAM_URL>/api/telemetry` | Куда она уходит |
| `QIYAA_PACKAGE` | `source` | Как распространяется сборка (`deb`, `appimage`, `windows`, `macos`, `source`): поле `package` в телеметрии |
| `QIYAA_WITH_SMTC` | `ON` | Windows: медиаклавиши через SMTC |
| `QIYAA_BUILD_TESTS` | `ON` | Тесты (нужен Qt Test) |
| `QIYAA_BUNDLED_LIBDIR` | `<libdir>/qiyaa` | Linux: куда `cmake --install` кладёт скачанный projectM |
| `QIYAA_DEB_MAINTAINER` | `Kickoman <…>` | Поле Maintainer в .deb |

Что включилось, CMake пишет при настройке: `MPRIS: enabled`, `Milkdrop: enabled (150 built-in
presets)`, `Jam: enabled (default server …)`, `Telemetry: enabled (…)` или причину, почему нет.

## Ubuntu 24.04

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools \
  libqt6opengl6-dev libgl-dev qt6-websockets-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/QiYaa
```

### Свой .deb

```sh
sudo apt install dpkg-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack -G DEB
```

Зависимости пакета вычисляет `dpkg-shlibdeps` (поэтому нужен `dpkg-dev`), к ним добавляются
`qt6-qpa-plugins`, рекомендуется `libpulse0`, предлагается `qt6-wayland`. Скачанный projectM
кладётся в пакет отдельно, в `/usr/lib/<arch>/qiyaa/`, и не пересекается с projectM из
дистрибутива.

## Windows

Qt 6.8 через [онлайн-установщик](https://www.qt.io/download-qt-installer-oss) (компонент MSVC 2022
64-bit) и projectM через vcpkg: `vcpkg install projectm:x64-windows`. Затем в «x64 Native Tools
Command Prompt»:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt build\QiYaa.exe
```

Без vcpkg можно собрать без Milkdrop: `-DQIYAA_WITH_MILKDROP=OFF`.

## macOS

Qt 6.8 для macOS через онлайн-установщик, затем:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/macos
cmake --build build
open build/QiYaa.app
```

projectM CMake скачает и соберёт сам. Готовый к раздаче `.app` делает `macdeployqt` (см. CI ниже).

## Тесты

```sh
ctest --test-dir build --output-on-failure
```

Тесты написаны на Qt Test, идут без экрана (`QT_QPA_PLATFORM=offscreen`) и без звуковой карты
(`QIYAA_AUDIO_BACKEND=null`) — CTest сам задаёт им это окружение. Тест MPRIS поднимает свою
сессионную шину через `dbus-run-session`, тест Milkdrop с настоящим OpenGL запускается, только если
есть `xvfb-run`. Что проверяет каждый набор, как обновлять эталонные скриншоты и как запустить
один тест — в [tests/README.md](../tests/README.md).

Перед отправкой изменений — команды из [CLAUDE.md](../CLAUDE.md#before-you-send-a-change):
сборка без предупреждений, все тесты и проверка clang-format 18.

## Установщики в CI

`.github/workflows/ci.yml` на каждый push:

| Сборка | Где | Что получается |
|---|---|---|
| Format | Ubuntu 24.04 | `clang-format-18 --dry-run --Werror` по всем исходникам |
| Linux, Qt 6.4 из дистрибутива, без Milkdrop | Ubuntu 24.04 | проверка минимальной версии Qt |
| Linux, .deb | Ubuntu 24.04, Qt из дистрибутива | `qiyaa_<версия>_amd64.deb` через CPack; CI ставит пакет и запускает его |
| Linux, AppImage | Ubuntu 22.04, Qt 6.8 | `QiYaa-<версия>-x86_64.AppImage` через linuxdeploy; CI запускает его без Qt раннера |
| Windows | Windows 2022, Qt 6.8, MSVC | установщик Inno Setup (`packaging/windows/qiyaa.iss`) и zip |
| macOS | macOS 14 (arm64), Qt 6.8 | `.dmg`: `macdeployqt -codesign=-` (подпись ad hoc) и `hdiutil`; CI проверяет подпись и в самом образе |

Каждая сборка, кроме Format, прогоняет тесты и делает скриншот `--screenshot --offline` как
проверку запуска. Сборки, из которых делаются релизы (.deb, AppImage, Windows, macOS), собираются с
`QIYAA_WITH_TELEMETRY=ON` и своим `QIYAA_PACKAGE`; «Linux, Qt 6.4 из дистрибутива» — без неё,
проверяя сборку по умолчанию. Тесты и скриншоты в CI ничего не отправляют.
Тег вида `v0.3.0` публикует пакеты в GitHub Releases. Версию CI берёт из тега
(`-DQIYAA_VERSION=0.3.0`): по ней называются пакеты и её показывает программа, так что править
`CMakeLists.txt` перед релизом не нужно. Значение `QIYAA_VERSION` по умолчанию в `CMakeLists.txt`
— для сборок из исходников; его поднимают, когда удобно. Иконки всех размеров рисует
`tools/make_icons.py`.
