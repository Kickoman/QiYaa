# QiYaa

Плеер Яндекс Музыки в стиле Winamp 2 на C++20 и Qt 6 — переписанный
[Yaamp](https://github.com/Kickoman/yaamp) без Electron: настоящие окна ОС, которые прилипают друг
к другу, классические скины `.wsz`, эквалайзер, визуализации Winamp и Milkdrop, треки без пауз,
медиаклавиши. Работает на Linux, Windows и macOS.

## Окна и режимы

| Окно | Что там | Как открыть |
|---|---|---|
| Главное | Транспорт, время, бегущая строка, спектр или осциллограф, громкость, баланс, shuffle/repeat | Всегда открыто |
| Эквалайзер | 10 полос, преусилитель, 17 пресетов Winamp, `.eqf` | Alt+G, кнопка EQ |
| Плейлист | Текущая очередь, выбор, удаление, общее время | Alt+E, кнопка PL |
| «Сейчас играет» | Обложка, исполнители, альбом, год, «Мне нравится» | Меню → «Сейчас играет» |
| Milkdrop | Визуализация projectM, 150 пресетов, полный экран | Ctrl+Shift+K, меню → Milkdrop |
| Свёрнутый режим | Любое из первых трёх окон — полоска высотой 14 px | Двойной клик по заголовку, Ctrl+W |
| Меню Яндекс Музыки | Моя волна, Колесо волн, Для вас, Мне нравится, плейлисты, исполнители, альбомы, станции, поиск | Правый клик по любому окну |

Всё подробно — в [docs/features.md](docs/features.md). Чем отличается Android-версия — в
[таблице паритета](spec/parity.md).

## Быстрый старт

1. Скачайте сборку из [Releases](https://github.com/Kickoman/QiYaa/releases): `.deb` или AppImage
   для Linux, установщик или zip для Windows, `.dmg` для macOS
   ([как поставить](docs/install.md)).
2. Запустите QiYaa: откроется окно входа. Откройте ya.ru/device и введите показанный код.
3. Правый клик по любому окну → «Мая хваля» (интерфейс по умолчанию на белорусском; язык
   меняется в меню → «Мова»).

Собрать самому (Ubuntu 24.04):

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev qt6-l10n-tools \
  libqt6opengl6-dev libgl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build
./build/QiYaa
```

## Документация

| Документ | О чём |
|---|---|
| [docs/features.md](docs/features.md) | Возможности: окна, скины, эквалайзер, Milkdrop, Яндекс Музыка, горячие клавиши |
| [docs/install.md](docs/install.md) | Установка, вход, где лежат настройки и токен, Wayland |
| [docs/cli.md](docs/cli.md) | Параметры командной строки, переменные окружения, коды выхода |
| [docs/building.md](docs/building.md) | Сборка на Linux, Windows и macOS, свой .deb, тесты, установщики в CI |
| [docs/telemetry.md](docs/telemetry.md) | Какую статистику и отчёты о падениях отправляют релизы и как это выключить |
| [docs/architecture.md](docs/architecture.md) | Для разработчиков (English): модули, потоки, путь трека от меню до звуковой карты |
| [docs/code-style.md](docs/code-style.md), [CLAUDE.md](CLAUDE.md) | Правила кода (English) и чем QiYaa от них отличается |

У каждой папки `src/` и у `tests/` есть свой README (English) — справочник по файлам модуля.

## Лицензия

MIT, см. [LICENSE](LICENSE). Сторонние компоненты и авторы скинов — в
[THIRD_PARTY.md](THIRD_PARTY.md).
