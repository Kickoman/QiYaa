#include "app/translations.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace App {

const QList<Language>& Languages() {
    static const QList<Language> all{Language::Belarusian, Language::Russian, Language::English};
    return all;
}

QString LanguageCode(Language language) {
    switch (language) {
        case Language::Belarusian: return QStringLiteral("be");
        case Language::Russian: return QStringLiteral("ru");
        case Language::English: return QStringLiteral("en");
    }
    return {};
}

std::optional<Language> LanguageFromCode(const QString& code) {
    for (Language language : Languages()) {
        if (LanguageCode(language) == code) {
            return language;
        }
    }
    return std::nullopt;
}

QString LanguageName(Language language) {
    switch (language) {
        case Language::Belarusian: return QStringLiteral("Беларуская");
        case Language::Russian: return QStringLiteral("Русский");
        case Language::English: return QStringLiteral("English");
    }
    return {};
}

Translations::Translations() = default;

Translations::~Translations() {
    remove();
}

bool Translations::apply(Language language) {
    remove();
    current = language;
    const QString code = LanguageCode(language);
    QLocale::setDefault(QLocale(code));
    // English is the source language; its file holds only the plural forms.
    auto translator = std::make_unique<QTranslator>();
    const bool loaded = translator->load(QStringLiteral(":/i18n/qiyaa_%1.qm").arg(code));
    if (loaded) {
        appTranslator = std::move(translator);
        QCoreApplication::installTranslator(appTranslator.get());
    } else {
        qWarning("Translations: no :/i18n/qiyaa_%s.qm", qPrintable(code));
    }
    // Qt's own texts (standard buttons, file dialogs) where Qt ships them; not for Belarusian.
    auto qt = std::make_unique<QTranslator>();
    if (qt->load(
            QStringLiteral("qtbase_") + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath)
        )) {
        qtTranslator = std::move(qt);
        QCoreApplication::installTranslator(qtTranslator.get());
    }
    return loaded;
}

void Translations::remove() {
    for (std::unique_ptr<QTranslator>* translator : {&appTranslator, &qtTranslator}) {
        if (*translator) {
            QCoreApplication::removeTranslator(translator->get());
            translator->reset();
        }
    }
}

}  // namespace App
