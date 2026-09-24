#include "ui/LoginDialog.h"

#include <algorithm>

#include <QDesktopServices>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "yandex/OAuth.h"
#include "yandex/Token.h"

namespace qiyaa {

namespace {
constexpr int kDialogWidth = 460;
}

LoginDialog::LoginDialog(QNetworkAccessManager* nam, QWidget* parent, const QString& oauthBase)
    : QDialog(parent), m_device(new yandex::DeviceLogin(nam, this)) {
    setWindowTitle(QStringLiteral("Вход в Яндекс Музыку"));
    setMinimumWidth(kDialogWidth);
    if (!oauthBase.isEmpty()) m_device->setBaseUrl(oauthBase);

    auto* layout = new QVBoxLayout(this);

    // --- Device code.
    auto* h1 = new QLabel(QStringLiteral("<b>Способ 1.</b> Откройте страницу подтверждения в любом браузере "
                                         "(можно на телефоне) и введите код:"));
    h1->setWordWrap(true);
    layout->addWidget(h1);

    m_code = new QLabel(QStringLiteral("…"));
    QFont big = m_code->font();
    big.setPointSizeF(big.pointSizeF() * 2.2);
    big.setBold(true);
    big.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    m_code->setFont(big);
    m_code->setAlignment(Qt::AlignCenter);
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_code->setMinimumHeight(QFontMetrics(big).height() + 8);
    layout->addWidget(m_code);

    m_openDevice = new QPushButton(QStringLiteral("Открыть ya.ru/device"));
    m_openDevice->setEnabled(false);
    layout->addWidget(m_openDevice);
    m_deviceStatus = new QLabel(QStringLiteral("Получаю код..."));
    m_deviceStatus->setWordWrap(true);
    layout->addWidget(m_deviceStatus);

    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    // --- Paste.
    auto* h2 = new QLabel(QStringLiteral(
        "<b>Способ 2.</b> Войдите через браузер. После входа Яндекс откроет страницу "
        "<i>music.yandex.ru/#access_token=…</i> — скопируйте её адрес целиком и вставьте сюда "
        "(или вставьте сам токен)."));
    h2->setWordWrap(true);
    layout->addWidget(h2);
    auto* openBrowser = new QPushButton(QStringLiteral("Открыть страницу входа"));
    layout->addWidget(openBrowser);
    m_paste = new QLineEdit;
    m_paste->setPlaceholderText(QStringLiteral("https://music.yandex.ru/#access_token=..."));
    layout->addWidget(m_paste);
    m_pasteError = new QLabel;
    m_pasteError->setStyleSheet(QStringLiteral("color: #c0392b"));
    m_pasteError->hide();
    layout->addWidget(m_pasteError);
    auto* use = new QPushButton(QStringLiteral("Войти с этим токеном"));
    layout->addWidget(use);

    auto* cancel = new QPushButton(QStringLiteral("Отмена"));
    layout->addSpacing(8);
    layout->addWidget(cancel, 0, Qt::AlignRight);

    connect(m_openDevice, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(m_verifyUrl); });
    connect(openBrowser, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(yandex::DeviceLogin::browserLoginUrl()); });
    connect(use, &QPushButton::clicked, this, &LoginDialog::tryPasted);
    connect(m_paste, &QLineEdit::returnPressed, this, &LoginDialog::tryPasted);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_device, &yandex::DeviceLogin::codeReady, this, [this](const QString& code, const QUrl& url) {
        m_code->setText(code);
        m_verifyUrl = url;
        m_openDevice->setText(QStringLiteral("Открыть %1").arg(url.host() + url.path()));
        m_openDevice->setEnabled(true);
        m_deviceStatus->setText(QStringLiteral("Жду подтверждения... (код скопирован в буфер обмена)"));
        QGuiApplication::clipboard()->setText(code);
        fitToContents();
    });
    connect(m_device, &yandex::DeviceLogin::succeeded, this, &LoginDialog::finishWith);
    connect(m_device, &yandex::DeviceLogin::failed, this, [this](const QString& err) {
        m_code->setText(QStringLiteral("—"));
        m_openDevice->setEnabled(false);
        m_deviceStatus->setText(QStringLiteral("Вход по коду не удался: %1. Воспользуйтесь способом 2.").arg(err));
        fitToContents();
    });

    resize(kDialogWidth, 0);
    fitToContents();
    m_device->start();
}

void LoginDialog::fitToContents() {
    QLayout* l = layout();
    l->activate();
    const int w = std::max(width(), minimumWidth());
    const int h = l->totalHeightForWidth(w);
    setMinimumHeight(h);
    if (height() < h) resize(w, h);
}

void LoginDialog::tryPasted() {
    const QString token = yandex::normalizeToken(m_paste->text().toUtf8());
    if (token.isEmpty()) {
        m_pasteError->setText(QStringLiteral("Не вижу здесь токена. Нужен адрес с «#access_token=…» или сам токен."));
        m_pasteError->show();
        fitToContents();
        return;
    }
    finishWith(token);
}

void LoginDialog::finishWith(const QString& token) {
    m_device->cancel();
    m_token = token;
    accept();
}

}  // namespace qiyaa
