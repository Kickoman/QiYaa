#include "support/spec_fixtures.h"

#include <QByteArrayList>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtGlobal>

#include <algorithm>

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

// A scalar as JSON: Qt writes numbers in their shortest exact form.
QByteArray ScalarText(const QJsonValue& value) {
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

QByteArray SpecText(const QJsonValue& value, int depth) {
    const QByteArray indent((depth + 1) * 2, ' '), closing(depth * 2, ' ');
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            return "{}";
        }
        QByteArrayList members;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            members << indent + ScalarText(it.key()) + ": " + SpecText(it.value(), depth + 1);
        }
        return "{\n" + members.join(",\n") + "\n" + closing + "}";
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        const bool flat = std::all_of(array.begin(), array.end(), [](const QJsonValue& item) {
            return !item.isObject() && !item.isArray();
        });
        QByteArrayList items;
        for (const QJsonValue& item : array) {
            items << (flat ? ScalarText(item) : indent + SpecText(item, depth + 1));
        }
        if (flat || items.isEmpty()) {
            return "[" + items.join(", ") + "]";
        }
        return "[\n" + items.join(",\n") + "\n" + closing + "]";
    }
    return ScalarText(value);
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
    return SpecObject(QStringLiteral("expected/yandex/%1.json").arg(name));
}

QString SpecPath(const QString& relativePath) {
    return QStringLiteral(QIYAA_SPEC_DIR "/") + relativePath;
}

QJsonObject SpecObject(const QString& relativePath) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(ReadSpecFile(relativePath), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qFatal("spec/%s is not a JSON object", qPrintable(relativePath));
    }
    return document.object();
}

void WriteSpecObject(const QString& relativePath, const QJsonObject& object) {
    const QString path = QStringLiteral(QIYAA_SPEC_DIR "/") + relativePath;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("cannot write %s", qPrintable(path));
    }
    file.write(SpecText(object, 0) + '\n');
}

QByteArray Expected(const QString& name) {
    return Json(ExpectedObject(name));
}

QByteArray Json(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

}  // namespace Tests
