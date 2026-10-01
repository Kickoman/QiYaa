#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Tests {

// The files of spec/ (the submodule): `name` is "<endpoint>/<case>" without ".json".

// The response body of spec/fixtures/yandex/<name>.json, byte for byte.
QByteArray Fixture(const QString& name);

// The HTTP status of a fixture: a case named "401-session-expired" is answered with 401,
// every other case with 200.
int FixtureStatus(const QString& name);

// spec/expected/yandex/<name>.json (always an object), serialised by Json() so that it
// compares with the value a test builds from what the app parsed.
QByteArray Expected(const QString& name);
QJsonObject ExpectedObject(const QString& name);

// An object as indented JSON with sorted keys: the form both sides of a comparison take.
QByteArray Json(const QJsonObject& object);

// Any JSON object of spec/, by its path there ("dsp/eq-response.json").
QJsonObject SpecObject(const QString& relativePath);

// The absolute path of a file or folder of spec/ ("jam/protocol/examples").
QString SpecPath(const QString& relativePath);

// Writes a JSON object into spec/ the way its files are kept: 2-space indentation, sorted keys,
// arrays of numbers or strings on one line. For generators run by hand, never by CTest.
void WriteSpecObject(const QString& relativePath, const QJsonObject& object);

}  // namespace Tests
