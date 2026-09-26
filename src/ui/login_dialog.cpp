#include "ui/login_dialog.h"

#include "yandex/oauth.h"
#include "yandex/token.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Ui {

namespace {
constexpr int kDialogWidth = 460;
}

LoginDialog::LoginDialog(QNetworkAccessManager* nam, QWidget* parent, const QString& oauthBase)
    : QDialog(parent)
    , device(new Yandex::DeviceLogin(nam, this)) {
    setWindowTitle(QStringLiteral("Вход в Яндекс Музыку"));
    setMinimumWidth(kDialogWidth);
    if (!oauthBase.isEmpty()) {
        device->setBaseUrl(oauthBase);
    }

    auto* layout = new QVBoxLayout(this);

    // --- Device code.
    auto* h1 = new QLabel(
        QStringLiteral("<b>Способ 1.</b> Откройте страницу подтверждения в любом браузере "
                       "(можно на телефоне) и введите код:")
    );
    h1->setWordWrap(true);
    layout->addWidget(h1);

    codeLabel = new QLabel(QStringLiteral("…"));
    QFont big = codeLabel->font();
    big.setPointSizeF(big.pointSizeF() * 2.2);
    big.setBold(true);
    big.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    codeLabel->setFont(big);
    codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel->setMinimumHeight(QFontMetrics(big).height() + 8);
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

    // --- Paste.
    auto* h2 = new QLabel(QStringLiteral(
        "<b>Способ 2.</b> Войдите через браузер. После входа Яндекс откроет страницу "
        "<i>music.yandex.ru/#access_token=…</i> — скопируйте её адрес целиком и вставьте сюда "
        "(или вставьте сам токен)."
    ));
    h2->setWordWrap(true);
    layout->addWidget(h2);
    auto* openBrowser = new QPushButton(QStringLiteral("Открыть страницу входа"));
    layout->addWidget(openBrowser);
    pasteField = new QLineEdit;
    pasteField->setPlaceholderText(QStringLiteral("https://music.yandex.ru/#access_token=..."));
    layout->addWidget(pasteField);
    pasteError = new QLabel;
    pasteError->setStyleSheet(QStringLiteral("color: #c0392b"));
    pasteError->hide();
    layout->addWidget(pasteError);
    auto* use = new QPushButton(QStringLiteral("Войти с этим токеном"));
    layout->addWidget(use);

    auto* cancel = new QPushButton(QStringLiteral("Отмена"));
    layout->addSpacing(8);
    layout->addWidget(cancel, 0, Qt::AlignRight);

    connect(openDevice, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(verifyUrl);
    });
    connect(openBrowser, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(Yandex::DeviceLogin::BrowserLoginUrl());
    });
    connect(use, &QPushButton::clicked, this, &LoginDialog::tryPasted);
    connect(pasteField, &QLineEdit::returnPressed, this, &LoginDialog::tryPasted);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(
        device, &Yandex::DeviceLogin::codeReady, this,
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
    connect(device, &Yandex::DeviceLogin::succeeded, this, &LoginDialog::finishWith);
    connect(device, &Yandex::DeviceLogin::failed, this, [this](const QString& error) {
        codeLabel->setText(QStringLiteral("—"));
        openDevice->setEnabled(false);
        deviceStatus->setText(
            QStringLiteral("Вход по коду не удался: %1. Воспользуйтесь способом 2.").arg(error)
        );
        fitToContents();
    });

    resize(kDialogWidth, 0);
    fitToContents();
    device->start();
}

void LoginDialog::fitToContents() {
    QLayout* l = layout();
    l->activate();
    const int w = std::max(width(), minimumWidth());
    const int h = l->totalHeightForWidth(w);
    setMinimumHeight(h);
    if (height() < h) {
        resize(w, h);
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
    device->cancel();
    accessToken = token;
    accept();
}

}  // namespace Ui
