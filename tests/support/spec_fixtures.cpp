#include "support/spec_fixtures.h"

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtGlobal>

namespace Tests {

namespace {

QByteArray ReadSpecFile(const QString& relativePath) {
    const QString path = QStringLiteral(QIYAA_SPEC_DIR "/") + relativePath;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal(
            "cannot read %s (is spec/ checked out? git submodule update --init)", qPrintable(path)
        );
    }
    return file.readAll();
}

}  // namespace

QByteArray Fixture(const QString& name) {
    return ReadSpecFile(QStringLiteral("fixtures/yandex/%1.json").arg(name));
}

int FixtureStatus(const QString& name) {
    static const QRegularExpression statusPrefix(QStringLiteral("/(\\d{3})-[^/]*$"));
    const QRegularExpressionMatch match = statusPrefix.match(name);
    return match.hasMatch() ? match.captured(1).toInt() : 200;
}

QJsonObject ExpectedObject(const QString& name) {
    const QByteArray bytes = ReadSpecFile(QStringLiteral("expected/yandex/%1.json").arg(name));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qFatal("expected/yandex/%s.json is not a JSON object", qPrintable(name));
    }
    return document.object();
}

QByteArray Expected(const QString& name) {
    return Json(ExpectedObject(name));
}

QByteArray Json(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

}  // namespace Tests
