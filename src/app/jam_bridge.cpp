#include "app/jam_bridge.h"

#include "app/account.h"
#include "app/settings.h"
#include "app/window_set.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "jam/host_session.h"
#include "ui/jam_server_dialog.h"
#include "ui/jam_window.h"
#include "ui/main_window.h"
#include "ui/playlist_window.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

namespace App {

namespace {

QString RefusalText(const QString& reason) {
    // Translated on each call: the language can change while the app runs.
    static const QHash<QString, const char*> texts = {
        {QStringLiteral("server-full"), QT_TRANSLATE_NOOP("App::JamBridge", "The jam server is full, try later")},
        {QStringLiteral("rate-limited"), QT_TRANSLATE_NOOP("App::JamBridge", "Too many requests, wait a little")},
        {QStringLiteral("update-required"),
         QT_TRANSLATE_NOOP("App::JamBridge", "Update QiYaa to work with this jam server")},
        {QStringLiteral("not-allowed"), QT_TRANSLATE_NOOP("App::JamBridge", "The jam server does not allow this")},
        {QStringLiteral("queue-limit"), QT_TRANSLATE_NOOP("App::JamBridge", "The jam queue is full")},
        {QStringLiteral("duplicate"), QT_TRANSLATE_NOOP("App::JamBridge", "This track is already in the jam queue")},
        {QStringLiteral("stale"), QT_TRANSLATE_NOOP("App::JamBridge", "This track has already left the queue")},
    };
    return QCoreApplication::translate(
        "App::JamBridge", texts.value(reason, QT_TRANSLATE_NOOP("App::JamBridge", "The jam server refused"))
    );
}

QString EndText(Jam::HostEnd why) {
    switch (why) {
        case Jam::HostEnd::ByHost: return QCoreApplication::translate("App::JamBridge", "The jam is over");
        case Jam::HostEnd::ByServer: return QCoreApplication::translate("App::JamBridge", "The jam was ended");
        case Jam::HostEnd::Expired:
            return QCoreApplication::translate("App::JamBridge", "The jam closed while you were away");
        case Jam::HostEnd::Gone:
            return QCoreApplication::translate("App::JamBridge", "The jam is over: the server no longer knows it");
    }
    return {};
}

// What the jam window shows of the server: its host, and a port that is not the default one.
QString ServerName(const QString& server) {
    const QUrl url(server);
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
}

}  // namespace

JamBridge::JamBridge(const Parts& bridgeParts, QObject* parent)
    : QObject(parent)
    , parts(bridgeParts) {
    parts.window->setServerName(ServerName(parts.settings->jam().server));
    connect(parts.window, &Ui::JamWindow::statusText, this, &JamBridge::statusText);
    connect(parts.window, &Ui::JamWindow::serverSettingsRequested, this, &JamBridge::showServerDialog);

    const auto note = [this](int row) { return trackNote(row); };
    parts.mainWindow->setTrackNote(note);
    parts.playlistWindow->setTrackNote(note);
    connect(parts.host, &Jam::HostSession::changed, this, [this] { parts.playlistWindow->update(); });
    connect(parts.host, &Jam::HostSession::offline, this, [this] {
        Q_EMIT statusText(tr("No connection to the jam server"));
    });
    connect(parts.host, &Jam::HostSession::refused, this, [this](const QString& reason) {
        Q_EMIT statusText(RefusalText(reason));
    });
    connect(parts.host, &Jam::HostSession::ended, this, [this](Jam::HostEnd why) { Q_EMIT statusText(EndText(why)); });
    connect(parts.host, &Jam::HostSession::connectedChanged, this, [this](bool connected) {
        Q_EMIT statusText(
            connected ? tr("The jam is connected") : tr("No connection to the jam server · trying again by itself")
        );
    });

    connect(parts.account, &Account::connected, parts.window, &Ui::JamWindow::setHostName);
    connect(parts.account, &Account::loggingOut, this, [this] {
        parts.host->cancelCreate();
        parts.host->end();
    });
    parts.account->setLikesOnHold([this] { return parts.jamMode->isActive() || storedJamQuestion; });
}

JamBridge::~JamBridge() = default;

QString JamBridge::trackNote(int row) const {  // HOST-34
    const QList<Core::QueueEntry>& entries = parts.player->entries();
    if (row < 0 || row >= entries.size()) {
        return {};
    }
    return Ui::JamTrackNote(entries[row], parts.jamMode->isActive(), parts.host);
}

void JamBridge::offerStored() {
    if (!parts.host->hasStoredSession()) {
        return;
    }
    // HOST-23. Not modal: playback and the windows work while the question waits.
    auto* question = new QMessageBox(
        QMessageBox::Question,
        tr("A jam was on"),
        tr("Continue it? The guests' link stays the same."),
        QMessageBox::NoButton,
        parts.mainWindow
    );
    question->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* yes = question->addButton(tr("Continue"), QMessageBox::AcceptRole);
    QPushButton* no = question->addButton(tr("End it"), QMessageBox::DestructiveRole);
    question->setDefaultButton(yes);
    connect(question, &QMessageBox::finished, this, [this, question, yes, no] {
        // Closed without an answer: the jam stays stored, and the next start asks again.
        if (question->clickedButton() == yes) {
            parts.host->continueStored();
            parts.windows->setVisible(parts.window, true);
        } else if (question->clickedButton() == no) {
            parts.host->discardStored();
            parts.account->queueLikes();  // the question is still open here: no hold
        }
    });
    storedJamQuestion = question;
    question->open();
}

void JamBridge::addMenu(QMenu* menu) {
    QMenu* jam = menu->addMenu(tr("Jam"));
    const Jam::HostPhase phase = parts.host->phase();
    jam->addAction(phase == Jam::HostPhase::None ? tr("Start…") : tr("Jam window"), this, [this] {
        parts.windows->setVisible(parts.window, true);
    });
    jam->addAction(tr("Join…"))->setEnabled(false);  // Kickoman/QiYaa#16
    QAction* end = jam->addAction(tr("End"), this, [this] {
        QMessageBox ask(
            QMessageBox::Question,
            tr("End the jam"),
            tr("End the jam? The guests will see that it is over."),
            QMessageBox::NoButton,
            parts.mainWindow
        );
        QPushButton* endIt = ask.addButton(tr("End it"), QMessageBox::DestructiveRole);
        ask.setDefaultButton(ask.addButton(tr("Cancel"), QMessageBox::RejectRole));
        ask.exec();
        if (ask.clickedButton() == endIt) {
            parts.host->end();
        }
    });
    end->setEnabled(phase == Jam::HostPhase::Active);
    jam->addSeparator();
    jam->addAction(tr("Server settings…"), this, &JamBridge::showServerDialog);
    menu->addSeparator();
}

void JamBridge::showServerDialog() {
    const JamSettings jam = parts.settings->jam();
    Ui::JamServerDialog dialog(
        jam.server, jam.waveFeedback, jam.shareAudio, Settings::DefaultJamServer(), parts.mainWindow
    );
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    parts.settings->setJam(JamSettings{dialog.server(), dialog.waveFeedback(), dialog.shareAudio()});
    parts.window->setServerName(ServerName(dialog.server()));
}

}  // namespace App
