#include "app/jam_bridge.h"

#include "app/account.h"
#include "app/settings.h"
#include "app/window_set.h"
#include "core/jam_mode.h"
#include "core/player.h"
#include "jam/host_session.h"
#include "sources/yandex/tracks.h"
#include "ui/jam_server_dialog.h"
#include "ui/jam_window.h"
#include "ui/main_window.h"
#include "ui/playlist_window.h"
#include "ui/search_window.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

#include <optional>

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

bool AddToJam(Jam::HostSession* host, const Core::Track& track, bool next) {
    return next ? host->playNext(track) : host->add(track);
}

std::optional<QString> WaitingFrom(const Jam::HostSession* host, const Core::Track& track) {
    const QString id = YandexSource::CatalogId(track.key);
    if (!host->room() || id.isEmpty()) {
        return std::nullopt;
    }
    for (const Jam::QueueItem& item : host->room()->queue) {
        if (item.track.id == id) {
            return host->participantName(item.addedBy);
        }
    }
    return std::nullopt;
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
    connect(parts.window, &Ui::JamWindow::searchRequested, this, [this] {
        parts.windows->setVisible(parts.searchWindow, true);
        parts.searchWindow->showTab(0);
        parts.searchWindow->focusField();
        parts.searchWindow->activateWindow();
    });
    parts.searchWindow->setJam(Ui::SearchWindow::JamHooks{
        .active = [this] { return parts.host->phase() == Jam::HostPhase::Active; },
        .connected = [this] { return parts.host->isConnected(); },
        .canAdd = [this](const Core::Track& track) { return parts.host->canAdd(track); },
        .add = [this](const Core::Track& track, bool next) { return AddToJam(parts.host, track, next); },
        .waitingFrom = [this](const Core::Track& track) { return WaitingFrom(parts.host, track); },
    });

    const auto note = [this](int row) { return trackNote(row); };
    parts.mainWindow->setTrackNote(note);
    parts.playlistWindow->setTrackNote(note);
    connect(parts.host, &Jam::HostSession::changed, this, [this] {
        parts.playlistWindow->update();
        parts.searchWindow->jamChanged();
    });
    connect(parts.host, &Jam::HostSession::offline, this, [this] {
        Q_EMIT statusText(tr("No connection to the jam server"));
    });
    connect(parts.host, &Jam::HostSession::refused, this, [this](const QString& reason) {
        Q_EMIT statusText(RefusalText(reason));
    });
    connect(parts.host, &Jam::HostSession::addRefused, this, [this](const QString& trackId, const QString& reason) {
        const auto refused = [trackId](const Core::Track& track) {
            return YandexSource::CatalogId(track.key) == trackId;
        };
        parts.searchWindow->jamRefused(refused, RefusalText(reason));
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
    QAction* end = jam->addAction(tr("End"), this, [this] { QTimer::singleShot(0, this, &JamBridge::askToEnd); });
    end->setObjectName(QStringLiteral("endJam"));
    end->setEnabled(phase == Jam::HostPhase::Active);
    jam->addSeparator();
    QAction* server = jam->addAction(tr("Server settings…"), this, [this] {
        QTimer::singleShot(0, this, &JamBridge::showServerDialog);
    });
    server->setObjectName(QStringLiteral("jamServerSettings"));
    menu->addSeparator();
}

void JamBridge::askToEnd() {
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
