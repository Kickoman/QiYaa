#include "ui/spotify_login_dialog.h"

#include "spotify/client/endpoints.h"
#include "spotify/client/login.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Ui {

namespace {
constexpr int kDialogWidth = 460;
}  // namespace

SpotifyLoginDialog::SpotifyLoginDialog(
    QNetworkAccessManager* networkManager,
    const SpotifyClient::Endpoints& endpoints,
    QWidget* parent
)
    : QDialog(parent)
    , pairLogin(new SpotifyClient::PairLogin(networkManager, endpoints, this))
    , browserLogin(new SpotifyClient::BrowserLogin(networkManager, endpoints, this)) {
    setWindowTitle(tr("Log in to Spotify"));
    setMinimumWidth(kDialogWidth);

    auto* layout = new QVBoxLayout(this);

    auto* premium = new QLabel(tr("Spotify plays only with a Premium account."));
    premium->setWordWrap(true);
    layout->addWidget(premium);

    auto* pairHeading =
        new QLabel(tr("<b>Way 1.</b> Open the pairing page in any browser (a phone will do) and enter the code:"));
    pairHeading->setWordWrap(true);
    layout->addWidget(pairHeading);

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

    openPair = new QPushButton(tr("Open %1").arg(QStringLiteral("spotify.com/pair")));
    openPair->setEnabled(false);
    layout->addWidget(openPair);
    pairStatus = new QLabel(tr("Getting the code…"));
    pairStatus->setWordWrap(true);
    layout->addWidget(pairStatus);

    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    layout->addWidget(line);

    auto* browserHeading = new QLabel(tr("<b>Way 2.</b> Log in in the browser on this computer."));
    browserHeading->setWordWrap(true);
    layout->addWidget(browserHeading);
    auto* openBrowser = new QPushButton(tr("Log in in the browser"));
    layout->addWidget(openBrowser);
    browserStatus = new QLabel;
    browserStatus->setWordWrap(true);
    browserStatus->hide();
    layout->addWidget(browserStatus);

    auto* cancel = new QPushButton(tr("Cancel"));
    layout->addSpacing(8);
    layout->addWidget(cancel, 0, Qt::AlignRight);

    connect(openPair, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(pairUrl); });
    connect(openBrowser, &QPushButton::clicked, browserLogin, &SpotifyClient::BrowserLogin::start);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    connect(pairLogin, &SpotifyClient::PairLogin::codeReady, this, [this](const QString& code, const QUrl& url) {
        codeLabel->setText(code);
        pairUrl = url;
        openPair->setEnabled(true);
        pairStatus->setText(tr("Waiting for the confirmation… (the code is copied to the clipboard)"));
        QGuiApplication::clipboard()->setText(code);
        fitToContents();
    });
    connect(pairLogin, &SpotifyClient::PairLogin::succeeded, this, &SpotifyLoginDialog::finishWith);
    connect(pairLogin, &SpotifyClient::PairLogin::failed, this, [this](const QString& error) {
        codeLabel->setText(QStringLiteral("—"));
        openPair->setEnabled(false);
        pairStatus->setText(tr("Logging in with the code failed: %1. Use way 2.").arg(error));
        fitToContents();
    });
    connect(browserLogin, &SpotifyClient::BrowserLogin::openUrl, this, [this](const QUrl& url) {
        QDesktopServices::openUrl(url);
        browserStatus->setText(tr("Waiting for the browser…"));
        browserStatus->show();
        fitToContents();
    });
    connect(browserLogin, &SpotifyClient::BrowserLogin::succeeded, this, &SpotifyLoginDialog::finishWith);
    connect(browserLogin, &SpotifyClient::BrowserLogin::failed, this, [this](const QString& error) {
        browserStatus->setText(tr("Logging in in the browser failed: %1").arg(error));
        browserStatus->show();
        fitToContents();
    });

    resize(kDialogWidth, 0);
    fitToContents();
    pairLogin->start();
}

void SpotifyLoginDialog::fitToContents() {
    QLayout* dialogLayout = layout();
    dialogLayout->activate();
    const int dialogWidth = std::max(width(), minimumWidth());
    const int dialogHeight = dialogLayout->totalHeightForWidth(dialogWidth);
    setMinimumHeight(dialogHeight);
    if (height() < dialogHeight) {
        resize(dialogWidth, dialogHeight);
    }
}

void SpotifyLoginDialog::finishWith(const QString& token) {
    pairLogin->cancel();
    browserLogin->cancel();
    accessToken = token;
    accept();
}

}  // namespace Ui
