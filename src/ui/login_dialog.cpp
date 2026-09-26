#include "ui/login_dialog.h"

#include "yandex/oauth.h"
#include "yandex/token.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace Ui {

namespace {
constexpr int kDialogWidth = 460;
}  // namespace

LoginDialog::LoginDialog(
    QNetworkAccessManager* networkManager,
    QWidget* parent,
    const QString& oauthBase
)
    : QDialog(parent)
    , deviceLogin(new Yandex::DeviceLogin(networkManager, this)) {
    setWindowTitle(QStringLiteral("Вход в Яндекс Музыку"));
    setMinimumWidth(kDialogWidth);
    if (!oauthBase.isEmpty()) {
        deviceLogin->setBaseUrl(oauthBase);
    }

    auto* layout = new QVBoxLayout(this);

    auto* deviceHeading = new QLabel(
        QStringLiteral("<b>Способ 1.</b> Откройте страницу подтверждения в любом браузере "
                       "(можно на телефоне) и введите код:")
    );
    deviceHeading->setWordWrap(true);
    layout->addWidget(deviceHeading);

    codeLabel = new QLabel(QStringLiteral("…"));
    QFont codeFont = codeLabel->font();
    codeFont.setPointSizeF(codeFont.pointSizeF() * 2.2);
    codeFont.setBold(true);
    codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    codeLabel->setFont(codeFont);
    codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel->setMinimumHeight(QFontMetrics(codeFont).height() + 8);
    layout->addWidget(codeLabel);

    openDevice = new QPushButton(QStringLiteral("Открыть ya.ru/device"));
    openDevice->setEnabled(false);
    layout->addWidget(openDevice);
    deviceStatus = new QLabel(QStringLiteral("Получаю код..."));
    deviceStatus->setWordWrap(true);
    layout->addWidget(deviceStatus);

    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    auto* browserHeading = new QLabel(QStringLiteral(
        "<b>Способ 2.</b> Войдите через браузер. После входа Яндекс откроет страницу "
        "<i>music.yandex.ru/#access_token=…</i> — скопируйте её адрес целиком и вставьте сюда "
        "(или вставьте сам токен)."
    ));
    browserHeading->setWordWrap(true);
    layout->addWidget(browserHeading);
    auto* openBrowser = new QPushButton(QStringLiteral("Открыть страницу входа"));
    layout->addWidget(openBrowser);
    pasteField = new QLineEdit;
    pasteField->setPlaceholderText(QStringLiteral("https://music.yandex.ru/#access_token=..."));
    layout->addWidget(pasteField);
    pasteError = new QLabel;
    pasteError->setStyleSheet(QStringLiteral("color: #c0392b"));
    pasteError->hide();
    layout->addWidget(pasteError);
    auto* useToken = new QPushButton(QStringLiteral("Войти с этим токеном"));
    layout->addWidget(useToken);

    auto* cancel = new QPushButton(QStringLiteral("Отмена"));
    layout->addSpacing(8);
    layout->addWidget(cancel, 0, Qt::AlignRight);

    connect(openDevice, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(verifyUrl);
    });
    connect(openBrowser, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(Yandex::DeviceLogin::BrowserLoginUrl());
    });
    connect(useToken, &QPushButton::clicked, this, &LoginDialog::tryPasted);
    connect(pasteField, &QLineEdit::returnPressed, this, &LoginDialog::tryPasted);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(
        deviceLogin, &Yandex::DeviceLogin::codeReady, this,
        [this](const QString& code, const QUrl& url) {
            codeLabel->setText(code);
            verifyUrl = url;
            openDevice->setText(QStringLiteral("Открыть %1").arg(url.host() + url.path()));
            openDevice->setEnabled(true);
            deviceStatus->setText(
                QStringLiteral("Жду подтверждения... (код скопирован в буфер обмена)")
            );
            QGuiApplication::clipboard()->setText(code);
            fitToContents();
        }
    );
    connect(deviceLogin, &Yandex::DeviceLogin::succeeded, this, &LoginDialog::finishWith);
    connect(deviceLogin, &Yandex::DeviceLogin::failed, this, [this](const QString& error) {
        codeLabel->setText(QStringLiteral("—"));
        openDevice->setEnabled(false);
        deviceStatus->setText(
            QStringLiteral("Вход по коду не удался: %1. Воспользуйтесь способом 2.").arg(error)
        );
        fitToContents();
    });

    resize(kDialogWidth, 0);
    fitToContents();
    deviceLogin->start();
}

void LoginDialog::fitToContents() {
    QLayout* dialogLayout = layout();
    dialogLayout->activate();
    const int dialogWidth = std::max(width(), minimumWidth());
    const int dialogHeight = dialogLayout->totalHeightForWidth(dialogWidth);
    setMinimumHeight(dialogHeight);
    if (height() < dialogHeight) {
        resize(dialogWidth, dialogHeight);
    }
}

void LoginDialog::tryPasted() {
    const QString token = Yandex::NormalizeToken(pasteField->text().toUtf8());
    if (token.isEmpty()) {
        pasteError->setText(
            QStringLiteral("Не вижу здесь токена. Нужен адрес с «#access_token=…» или сам токен.")
        );
        pasteError->show();
        fitToContents();
        return;
    }
    finishWith(token);
}

void LoginDialog::finishWith(const QString& token) {
    deviceLogin->cancel();
    accessToken = token;
    accept();
}

}  // namespace Ui
