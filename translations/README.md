# `translations` — the interface in Belarusian, Russian and English

QiYaa's interface texts are written in English in the code and translated here, in Qt Linguist's
`.ts` files. The build turns them into `.qm` files (`lrelease`, through `qt_add_translations` in
the top-level `CMakeLists.txt`) and puts them into the app's resources under `:/i18n/`.
`App::Translations` ([src/app](../src/app/README.md)) installs the one the user picked.

| File | Contains |
|---|---|
| `qiyaa_be.ts` | Belarusian: every text. The default language |
| `qiyaa_ru.ts` | Russian: every text |
| `qiyaa_en.ts` | English: only the plural forms of the texts with `%n` (the sources are English already) |

## Writing a text in the code

- In a `QObject` class: `tr("Liked")`; elsewhere `QCoreApplication::translate("Ui::LibraryMenu",
  "Liked")`, with the context written out (`lupdate` reads only literals). A table of texts keeps
  `QT_TRANSLATE_NOOP(context, text)` and translates on use, never a translated `QString` in a
  `static`: the language can change while the app runs.
- Arguments go through placeholders, not concatenation: `tr("Error: %1").arg(error)`, so a
  translation can move them. A count is `tr("Liked: %n track(s)", nullptr, count)`: Belarusian and
  Russian have three plural forms, English two.
- Texts kept outside the painting (a window's title) are set again in the window's
  `retranslate()`, which `SkinnedWindow` calls on `QEvent::LanguageChange`.
- Not translated: the Winamp-style marquee messages (`VOLUME`, `BALANCE`, `SEEK TO`, `EQ: …`), the
  GEN window titles drawn with the skin's letters (`JAM`, `NOW PLAYING`), names from Yandex,
  diagnostics in the log, and the command-line help.

## Updating the files

After changing texts in `src/`, from the repository root, with Qt 6's `lupdate`:

```bash
lupdate -locations none -no-obsolete src -ts translations/qiyaa_be.ts translations/qiyaa_ru.ts
lupdate -locations none -no-obsolete -pluralonly src -ts translations/qiyaa_en.ts
```

Then translate the new entries (Qt Linguist, or by hand: an entry is done when its
`<translation>` has no `type="unfinished"`). `translations_test` fails on an unfinished or empty
translation, on placeholders (`%1`, `%n`) that differ from the source, and on an English text with
the word "wave".

A new language: a `qiyaa_<code>.ts` (`lupdate … -target-language <code>`), the file in
`qt_add_translations`, and a value in `App::Language` with its code and its own name.

## Words

The same thing is called the same everywhere, in the app, the jam's web page and the Android app.

| English | Беларуская | Русский | What it is |
|---|---|---|---|
| My Vibe | Мая хваля | Моя волна | Yandex's endless personal stream ("волна"; never "wave" in English) |
| vibe | хваля | волна | any such stream: a station, a wave of the wheel |
| Wheel of vibes | Кола хваляў | Колесо волн | the vibes that match what plays |
| jam | джэм | джем | the shared queue |
| jam vibe | хваля джэма | волна джема | what plays when the guests' queue is empty |
| Liked | Мне падабаецца | Мне нравится | the liked tracks |
| track | трэк | трек | |
| playlist | плэйліст | плейлист | |
| preset | прэсэт | пресет | |
| host / guest | гаспадар / госць | хозяин / гость | the jam's sides |
| Cancel | Скасаваць | Отмена | |
