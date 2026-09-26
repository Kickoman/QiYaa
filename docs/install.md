# Установка и первый запуск

## Где взять

Готовые сборки лежат в [Releases](https://github.com/Kickoman/QiYaa/releases) (релиз появляется,
когда в репозиторий пушится тег вида `v0.2.0`) и в артефактах каждого прогона GitHub Actions.
Как CI их собирает — в [building.md](building.md#установщики-в-ci).

## Linux

- **Пакет .deb** (Ubuntu 24.04+, Debian 13+) — `qiyaa_<версия>_amd64.deb`:

  ```sh
  sudo apt install ./qiyaa_*.deb
  ```

  Использует Qt из дистрибутива (apt сам доставит зависимости), сразу появляется в меню
  приложений. projectM для Milkdrop лежит внутри пакета, в `/usr/lib/<arch>/qiyaa/`, и не
  пересекается с projectM из дистрибутива. Удаление: `sudo apt remove qiyaa`. Обновление вручную:
  скачать новый .deb и поставить так же.
- **AppImage** (Ubuntu 22.04+ и другие дистрибутивы того же возраста и новее) —
  `QiYaa-<версия>-x86_64.AppImage`:

  ```sh
  chmod +x QiYaa-*.AppImage && ./QiYaa-*.AppImage
  ```

  Всё своё носит с собой и ничего не ставит. Чтобы плеер появился в меню приложений, можно
  воспользоваться Gear Lever или AppImageLauncher.

## Windows

`QiYaa-<версия>-windows-x64-setup.exe` ставится для текущего пользователя, без прав администратора.
Есть и переносной `QiYaa-<версия>-windows-x64.zip`: распаковать и запустить `QiYaa.exe`. Сборка не
подписана, поэтому SmartScreen может предупредить: «Подробнее» → «Выполнить в любом случае».

## macOS

`QiYaa-<версия>-macos-arm64.dmg` (Apple Silicon): открыть и перетащить QiYaa в «Программы».

Приложение подписано только «ad hoc», без Apple ID и нотаризации, поэтому при первом запуске macOS
скажет, что не может проверить его на вредоносное ПО:

- macOS 15 и новее: «Системные настройки» → «Конфиденциальность и безопасность» → внизу «Всё равно
  открыть» (кнопка появляется после первой попытки запуска);
- macOS 14 и старее: правый клик по приложению → «Открыть»;
- или одной командой: `xattr -dr com.apple.quarantine /Applications/QiYaa.app`.

## Вход в Яндекс Музыку

Без токена бегущая строка просит войти, и сразу открывается окно входа (потом — меню → «Войти в
Яндекс Музыку...»):

1. **Код устройства.** Откройте ya.ru/device в любом браузере, хоть на телефоне, и введите
   показанный код; QiYaa дождётся подтверждения сама.
2. **Если код не сработал.** «Открыть страницу входа», войдите в браузере, скопируйте адрес
   страницы, на которую вас перенаправит (`music.yandex.ru/#access_token=…`), и вставьте в поле.

Откуда QiYaa берёт токен, по порядку:

1. переменная окружения `QIYAA_TOKEN`;
2. свой файл `token` (см. ниже); если он есть, но пустой, значит, вы вышли из аккаунта, и дальше
   QiYaa не ищет;
3. `token.json` старого Yaamp, если Yaamp был залогинен. После успешного подключения QiYaa копирует
   этот токен в свой файл.

Файл `token` доступен только владельцу. «Выйти из аккаунта» в меню очищает его.

## Где QiYaa хранит файлы

| Что | Linux | Windows | macOS |
|---|---|---|---|
| Настройки (`settings.ini`), токен (`token`), свои пресеты Milkdrop (`milkdrop/`) | `~/.config/QiYaa/` (или `$XDG_CONFIG_HOME/QiYaa/`) | `%LOCALAPPDATA%\QiYaa\` | `~/Library/Preferences/QiYaa/` |
| Обложки | `~/.cache/QiYaa/covers/` | `%LOCALAPPDATA%\QiYaa\cache\covers\` | `~/Library/Caches/QiYaa/covers/` |
| Токен Yaamp, который QiYaa подхватывает | `~/.config/Yaamp/token.json` | `%APPDATA%\Yaamp\token.json` | `~/Library/Application Support/Yaamp/token.json` |

Что записано в `settings.ini` — в [src/app/README.md](../src/app/README.md#file-format-settingsini).

## Wayland

Wayland не даёт приложениям ставить свои окна в нужные координаты, а на этом держится Winamp:
окна прилипают друг к другу. Поэтому в сеансе Wayland QiYaa запускается через XWayland
(`QT_QPA_PLATFORM=xcb;wayland`: если XWayland нет, Qt возьмёт Wayland).

- `QIYAA_NATIVE_WAYLAND=1` — нативный Wayland: окна двигает композитор, без прилипания и
  запоминания позиций.
- Если вы сами задали `QT_QPA_PLATFORM`, QiYaa его не меняет.
