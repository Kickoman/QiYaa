#include "spotify/client/identity.h"

#include <QRandomGenerator>
#include <QSysInfo>

namespace SpotifyClient {

namespace {

using SpotifyProtocol::ApCpuFamily;
using SpotifyProtocol::ApOs;
using SpotifyProtocol::ApPlatform;

bool IsArm(const QString& cpu) {
    return cpu.startsWith(QLatin1String("arm"));
}

bool Is64Bit(const QString& cpu) {
    return cpu == QLatin1String("x86_64") || cpu == QLatin1String("arm64");
}

ApPlatform PlatformOf(const QString& os, const QString& cpu) {
    if (os == QLatin1String("windows")) {
        if (cpu == QLatin1String("i386")) {
            return ApPlatform::Win32X86;
        }
        return ApPlatform::Win32X86_64;
    }
    if (os == QLatin1String("macos")) {
        if (cpu == QLatin1String("x86_64")) {
            return ApPlatform::OsxX86_64;
        }
        return ApPlatform::OsxX86;
    }
    if (IsArm(cpu)) {
        return ApPlatform::LinuxArm;
    }
    if (cpu == QLatin1String("x86_64")) {
        return ApPlatform::LinuxX86_64;
    }
    return ApPlatform::LinuxX86;
}

ApCpuFamily CpuFamilyOf(const QString& cpu) {
    if (IsArm(cpu)) {
        return ApCpuFamily::Arm;
    }
    if (cpu == QLatin1String("x86_64")) {
        return ApCpuFamily::X86_64;
    }
    if (cpu == QLatin1String("i386")) {
        return ApCpuFamily::X86;
    }
    return ApCpuFamily::Unknown;
}

ApOs OsOf(const QString& os) {
    if (os == QLatin1String("windows")) {
        return ApOs::Windows;
    }
    if (os == QLatin1String("macos")) {
        return ApOs::Osx;
    }
    return ApOs::Linux;
}

QString OsName(const QString& os) {
    if (os == QLatin1String("windows")) {
        return QStringLiteral("Win32");
    }
    if (os == QLatin1String("macos")) {
        return QStringLiteral("OSX");
    }
    return QStringLiteral("Linux");
}

QString Version(const QString& appVersion) {
    return QStringLiteral("QiYaa ") + appVersion;
}

}  // namespace

QString NewDeviceId() {
    QByteArray bytes(20, '\0');
    for (char& byte : bytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(bytes.toHex());
}

SpotifyProtocol::ClientIdentity Identity(const QString& appVersion, const QString& deviceId) {
    const QString os = QSysInfo::productType();
    const QString cpu = QSysInfo::currentCpuArchitecture();
    SpotifyProtocol::ClientIdentity identity;
    identity.platform = PlatformOf(os, cpu);
    identity.cpuFamily = CpuFamilyOf(cpu);
    identity.os = OsOf(os);
    identity.protocolVersion = kSpotifyVersion;
    identity.systemInformation = Version(appVersion).toStdString();
    identity.versionString = Version(appVersion).toStdString();
    identity.deviceId = deviceId.toStdString();
    return identity;
}

SpotifyProtocol::ClientTokenParams ClientTokenParams(const QString& deviceId) {
    const QString os = QSysInfo::productType();
    const QString cpu = QSysInfo::currentCpuArchitecture();
    SpotifyProtocol::ClientTokenParams params;
    params.clientVersion = kClientVersion.toStdString();
    params.clientId = kClientId.toStdString();
    params.deviceId = deviceId.toStdString();
    if (os == QLatin1String("windows")) {
        SpotifyProtocol::WindowsPlatform windows;
        windows.osVersion = QSysInfo::productVersion().section(u'.', 0, 0).toInt();
        windows.osBuild = QSysInfo::kernelVersion().section(u'.', 2, 2).toInt();
        if (cpu == QLatin1String("arm64")) {
            windows.imageFileMachine = 452;
            windows.peMachine = 43620;
        } else if (!Is64Bit(cpu)) {
            windows.imageFileMachine = 332;
            windows.peMachine = 332;
        }
        params.platform = windows;
    } else if (os == QLatin1String("macos")) {
        params.platform = SpotifyProtocol::MacPlatform{QSysInfo::productVersion().toStdString(), cpu.toStdString()};
    } else {
        params.platform = SpotifyProtocol::LinuxPlatform{
            QSysInfo::kernelVersion().toStdString(),
            QSysInfo::productVersion().toStdString(),
            cpu.toStdString(),
        };
    }
    return params;
}

QByteArray UserAgent(const QString& appVersion) {
    const QString os = OsName(QSysInfo::productType());
    return QStringLiteral("Spotify/%1 %2/0 (%3)").arg(kSpotifyVersion).arg(os, Version(appVersion)).toUtf8();
}

}  // namespace SpotifyClient
