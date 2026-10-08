#include "telemetry/system_info.h"

#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QScreen>
#include <QSysInfo>
#include <QtGlobal>

#include <algorithm>

namespace Telemetry {

namespace {

QString Os() {
#if defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("other");
#endif
}

QString Printable(QString text, int length) {
    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f\\x7f]"));
    return text.remove(control).left(length);
}

QString Locale() {
    static const QRegularExpression valid(QStringLiteral("^(C|[a-z]{2,3}(_[A-Za-z0-9]{2,4})?)$"));
    const QString name = QLocale::system().name();
    return valid.match(name).hasMatch() ? name : QStringLiteral("C");
}

QString Arch() {
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{1,16}$"));
    const QString arch = QSysInfo::currentCpuArchitecture();
    return valid.match(arch).hasMatch() ? arch : QStringLiteral("unknown");
}

}  // namespace

QJsonObject SystemFields() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    const double dpr = primary ? std::clamp(primary->devicePixelRatio(), 0.5, 8.0) : 1.0;
    return {
        {QStringLiteral("os"), Os()},
        {QStringLiteral("osName"), Printable(QSysInfo::prettyProductName(), 64)},
        {QStringLiteral("osVersion"), Printable(QSysInfo::productVersion(), 32)},
        {QStringLiteral("kernel"), Printable(QSysInfo::kernelVersion(), 64)},
        {QStringLiteral("arch"), Arch()},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("package"), QStringLiteral(QIYAA_PACKAGE)},
        {QStringLiteral("locale"), Locale()},
        {QStringLiteral("screens"), static_cast<int>(std::min<qsizetype>(screens.size(), 16))},
        {QStringLiteral("dpr"), dpr},
    };
}

}  // namespace Telemetry
