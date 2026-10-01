#pragma once

#include <QList>
#include <QString>

#include <memory>
#include <optional>

class QTranslator;

namespace App {

enum class Language { Belarusian, Russian, English };

inline constexpr Language kDefaultLanguage = Language::Belarusian;

const QList<Language>& Languages();
QString LanguageCode(Language language);  // "be", "ru", "en"
std::optional<Language> LanguageFromCode(const QString& code);
QString LanguageName(Language language);  // in the language itself: "Беларуская"…

// The app's translation for a language, and Qt's own where Qt has one, installed on the
// application. Applying another replaces them, and Qt sends LanguageChange to every widget.
class Translations {
public:
    Translations();
    ~Translations();

    bool apply(Language language);
    Language language() const { return current; }

private:
    void remove();

    std::unique_ptr<QTranslator> appTranslator;
    std::unique_ptr<QTranslator> qtTranslator;
    Language current = Language::English;
};

}  // namespace App
