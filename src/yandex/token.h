#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Yandex {

struct TokenSource {
    QString token;
    QString origin;
};

QString NormalizeToken(const QByteArray& raw);

TokenSource FindToken(const QString& tokenFile, const QStringList& importFiles);

bool SaveToken(const QString& tokenFile, const QString& token);

void ForgetToken(const QString& tokenFile);

}  // namespace Yandex
