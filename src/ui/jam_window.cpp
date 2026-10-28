#include "ui/jam_window.h"

#include "core/jam_mode.h"
#include "core/queue_source.h"
#include "jam/host_session.h"
#include "skins/skin.h"
#include "sources/yandex/catalog.h"
#include "yandex/api_client.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QWheelEvent>
#include <qrcodegen.hpp>

#include <algorithm>
#include <utility>

namespace Ui {

namespace {

constexpr int kGap = FormCanvas::kGap;
constexpr int kMaxQrSide = 120;
constexpr int kQuietZone = 4;
constexpr int kEndConfirmMs = 3000;
constexpr int kMaxSearchLength = 100;

QString KindText(Jam::ParticipantKind kind) {
    switch (kind) {
        case Jam::ParticipantKind::Host: return QCoreApplication::translate("Ui::JamWindow", "host");
        case Jam::ParticipantKind::Web: return QCoreApplication::translate("Ui::JamWindow", "browser");
        case Jam::ParticipantKind::Qiyaa: return QCoreApplication::translate("Ui::JamWindow", "app");
    }
    return {};
}

}  // namespace

QString JamTrackNote(const Core::QueueEntry& entry, bool jamActive, const Jam::HostSession* host) {
    if (!jamActive) {
        return {};
    }
    if (Core::IsJamItem(&entry)) {
        const QString name = host ? host->participantName(entry.addedBy) : QString();
        return name.isEmpty() ? QString() : QStringLiteral("+ ") + name;
    }
    return entry.fromHistory ? QCoreApplication::translate("Ui::JamWindow", "jam vibe") : QString();
}

JamWindow::JamWindow(Jam::HostSession* host, YandexSource::Catalog* catalog, const Skins::Skin* skin, QWidget* parent)
    : GenWindow(skin, QStringLiteral("JAM"), parent)
    , jamHost(host)
    , yandexCatalog(catalog)
    , nameField(Jam::kMaxHostNameLength)
    , searchField(kMaxSearchLength) {
    retranslate();
    setFocusPolicy(Qt::StrongFocus);
    endTimer.setSingleShot(true);
    endTimer.setInterval(kEndConfirmMs);
    connect(&endTimer, &QTimer::timeout, this, [this] {
        endArmed = false;
        update();
    });
    connect(jamHost, &Jam::HostSession::changed, this, [this, last = jamHost->phase()]() mutable {
        const Jam::HostPhase now = jamHost->phase();
        if (now != last) {
            currentPage = Page::Jam;
            listScroll = 0;
            endArmed = false;
            startProblem.clear();
            if (focus == Field::Name && now != Jam::HostPhase::None) {
                focus = Field::None;
            }
            last = now;
        }
        update();
    });
}

void JamWindow::setHostName(const QString& name) {
    if (!nameEdited) {
        nameField.setText(name);
        update();
    }
}

void JamWindow::setServerName(const QString& server) {
    serverName = server;
    update();
}

void JamWindow::setTextFont(const std::optional<QFont>& font) {
    fontOverride = font;
    update();
}

void JamWindow::showPage(Page page) {
    if (page == currentPage) {
        return;
    }
    currentPage = page;
    listScroll = 0;
    focus = page == Page::Search ? Field::Search : Field::None;
    update();
}

QFont JamWindow::textFont() const {
    if (fontOverride) {
        return *fontOverride;
    }
    QFont font(skin().playlistStyle().font);
    font.setPixelSize(9);
    return font;
}

QList<JamWindow::Control> JamWindow::controls() {
    return render(nullptr);
}

void JamWindow::paintContent(QPainter& painter, const QRect& area) {
    painter.fillRect(area, skin().playlistStyle().normalBackground);
    render(&painter);
}

QList<JamWindow::Control> JamWindow::render(QPainter* painter) {
    FormCanvas canvas(painter, contentRect(), textFont(), skin());
    switch (jamHost->phase()) {
        case Jam::HostPhase::None: renderStart(canvas); break;
        case Jam::HostPhase::Creating: renderCreating(canvas); break;
        case Jam::HostPhase::Active:
            renderTabs(canvas);
            if (currentPage == Page::Jam) {
                renderJam(canvas);
            } else {
                renderSearch(canvas);
            }
            break;
    }
    return canvas.controls;
}

void JamWindow::renderStart(FormCanvas& canvas) {
    canvas.line(tr("New jam"), canvas.current, true);
    canvas.paragraph(
        tr("Friends open the link or the QR code on their phones and add tracks to your queue. "
           "You play, they search through your account."),
        canvas.normal
    );
    canvas.y += kGap * 2;
    canvas.line(tr("Your name"), canvas.normal);
    const QRect nameRect(canvas.left, canvas.y, canvas.width(), canvas.buttonHeight());
    field(canvas, nameRect, Field::Name, tr("How the guests will see you"));
    canvas.y += nameRect.height() + kGap;
    const bool ready = !nameField.text().trimmed().isEmpty() && !serverName.isEmpty();
    canvas.button(canvas.left, canvas.y, tr("Start the jam"), ready, [this] { start(); });
    canvas.y += canvas.buttonHeight() + kGap;
    if (!startProblem.isEmpty()) {
        canvas.paragraph(startProblem, canvas.current);
    }
    canvas.y += kGap * 2;
    canvas.line(
        serverName.isEmpty() ? tr("The jam server is not set") : tr("Server: %1").arg(serverName), canvas.normal
    );
    canvas.y += kGap;
    canvas.button(canvas.left, canvas.y, tr("Server settings…"), true, [this] { Q_EMIT serverSettingsRequested(); });
}

void JamWindow::renderCreating(FormCanvas& canvas) {
    canvas.line(tr("New jam"), canvas.current, true);
    canvas.paragraph(tr("Connecting to the jam server…"), canvas.normal);
    canvas.y += kGap;
    canvas.button(canvas.left, canvas.y, tr("Cancel"), true, [this] { jamHost->cancelCreate(); });
}

void JamWindow::renderTabs(FormCanvas& canvas) {
    const QRect jamTab = canvas.button(
        canvas.left, canvas.y, tr("Jam"), true, [this] { showPage(Page::Jam); }, currentPage == Page::Jam
    );
    const QRect searchTab = canvas.button(
        jamTab.right() + 1 + kGap,
        canvas.y,
        tr("Add tracks"),
        true,
        [this] { showPage(Page::Search); },
        currentPage == Page::Search
    );
    const bool connected = jamHost->isConnected();
    const Jam::Status status = jamHost->connection();
    QString state = tr("No connection");
    if (connected) {
        state = tr("Connected");
    } else if (status == Jam::Status::Connecting || status == Jam::Status::Online) {
        state = tr("Connecting…");
    }
    const int stateWidth = canvas.textWidth(state) + 2;  // advances are fractional
    const int stateX = canvas.right - stateWidth;
    if (stateX - 8 > searchTab.right() + kGap) {
        canvas.light(stateX - 7, jamTab.center().y(), connected);
        canvas.text(
            QRect(stateX, jamTab.y(), stateWidth, jamTab.height()), state, connected ? canvas.normal : canvas.current
        );
    }
    canvas.y += canvas.buttonHeight() + kGap;
    if (!connected) {
        canvas.paragraph(tr("No connection to the jam server · trying again by itself"), canvas.current);
        canvas.y += kGap;
    }
}

void JamWindow::renderJam(FormCanvas& canvas) {
    const bool connected = jamHost->isConnected();
    const QImage& code = qrCode();
    const int top = canvas.y;
    int textLeft = canvas.left;
    int qrBottom = top;
    if (!code.isNull()) {
        const int side = std::min(kMaxQrSide, canvas.width() / 2 - kGap);
        const int module = std::max(1, side / code.width());
        const QRect qrRect(canvas.left, top, code.width() * module, code.height() * module);
        if (canvas.painter) {
            canvas.painter->drawImage(qrRect, code);
        }
        textLeft = qrRect.right() + 1 + kGap * 2;
        qrBottom = qrRect.bottom() + 1;
    }
    canvas.text(
        QRect(textLeft, canvas.y, canvas.right - textLeft, canvas.lineHeight),
        tr("Link for the guests"),
        canvas.current,
        true
    );
    canvas.y += canvas.lineHeight;
    canvas.paragraph(jamHost->joinUrl(), canvas.normal, 3, Qt::TextWrapAnywhere, textLeft);
    canvas.y += kGap;
    const QRect copy = canvas.button(textLeft, canvas.y, tr("Copy"), true, [this] { copyLink(); });
    const QString newLink = tr("New link");
    int newLinkX = copy.right() + 1 + kGap;
    if (newLinkX + canvas.buttonWidth(newLink) > canvas.right) {
        newLinkX = textLeft;
        canvas.y += canvas.buttonHeight() + kGap;
    }
    canvas.button(newLinkX, canvas.y, newLink, connected, [this] {
        send(jamHost->rotateLink(), tr("The guests' link has changed"));
    });
    canvas.y = std::max(qrBottom, canvas.y + canvas.buttonHeight()) + kGap * 2;

    const std::optional<Jam::Room>& room = jamHost->room();
    if (room) {
        const Jam::Settings& settings = room->settings;
        const auto setting = [&](const QString& label, const QString& value, Jam::SettingsPatch patch) {
            canvas.text(QRect(canvas.left, canvas.y, canvas.width(), canvas.buttonHeight()), label, canvas.normal);
            canvas.button(canvas.right - canvas.buttonWidth(value), canvas.y, value, connected, [this, patch] {
                send(jamHost->changeSettings(patch));
            });
            canvas.y += canvas.buttonHeight() + 1;
        };
        const bool roundRobin = settings.order == Jam::Order::RoundRobin;
        setting(
            tr("Order"),
            roundRobin ? tr("Taking turns") : tr("First come"),
            Jam::SettingsPatch{roundRobin ? Jam::Order::Fifo : Jam::Order::RoundRobin, {}, {}, {}}
        );
        setting(
            tr("Guests can skip"),
            settings.guestsCanSkip ? tr("Yes") : tr("No"),
            Jam::SettingsPatch{{}, !settings.guestsCanSkip, {}, {}}
        );
        setting(
            tr("New guests"),
            settings.joinOpen ? tr("Let in") : tr("Closed"),
            Jam::SettingsPatch{{}, {}, !settings.joinOpen, {}}
        );
        canvas.y += kGap;
    }

    QList<Jam::Participant> guests;
    if (room) {
        for (const Jam::Participant& participant : room->participants) {
            if (participant.kind != Jam::ParticipantKind::Host) {
                guests << participant;
            }
        }
    }
    canvas.line(tr("Guests · %1").arg(guests.size()), canvas.current, true);
    const int endTop = canvas.bottom - canvas.buttonHeight();
    const int rowHeight = canvas.buttonHeight() + 1;
    listRows = std::max(0, (endTop - kGap - canvas.y) / rowHeight);
    listScroll = std::clamp(listScroll, 0, std::max(0, static_cast<int>(guests.size()) - listRows));
    if (guests.isEmpty()) {
        canvas.paragraph(tr("Nobody yet. Show the guests the QR code or the link."), canvas.dim);
    }
    for (int i = listScroll; i < guests.size() && i < listScroll + listRows; ++i) {
        const Jam::Participant& guest = guests[i];
        const QString kick = tr("Remove");
        const int kickX = canvas.right - canvas.buttonWidth(kick);
        canvas.light(canvas.left, canvas.y + canvas.buttonHeight() / 2, guest.online);
        QString line = guest.name + QStringLiteral(" · ") + KindText(guest.kind);
        if (guest.pending > 0) {
            line += tr(" · waiting: %1").arg(guest.pending);
        }
        canvas.text(
            QRect(canvas.left + 7, canvas.y, kickX - kGap - canvas.left - 7, canvas.buttonHeight()),
            line,
            guest.online ? canvas.normal : canvas.dim
        );
        const QString publicId = guest.publicId;
        canvas.button(kickX, canvas.y, kick, connected, [this, publicId] { send(jamHost->kick(publicId)); });
        canvas.y += rowHeight;
    }

    canvas.button(
        canvas.left, endTop, endArmed ? tr("Press again to end") : tr("End the jam"), true, [this] { end(); }, endArmed
    );
}

void JamWindow::renderSearch(FormCanvas& canvas) {
    const QString find = tr("Search");
    const int findX = canvas.right - canvas.buttonWidth(find);
    field(
        canvas,
        QRect(canvas.left, canvas.y, findX - kGap - canvas.left, canvas.buttonHeight()),
        Field::Search,
        tr("Search tracks for the jam")
    );
    canvas.button(findX, canvas.y, find, !searchField.text().trimmed().isEmpty(), [this] {
        search(searchField.text());
    });
    canvas.y += canvas.buttonHeight() + kGap * 2;

    switch (results) {
        case Results::None:
            canvas.paragraph(
                tr("Find a track: “To the jam” puts it in the guests' queue, “Next” plays it "
                   "next."),
                canvas.dim
            );
            return;
        case Results::Loading: canvas.line(tr("Searching…"), canvas.normal); return;
        case Results::Failed:
            canvas.paragraph(tr("The search failed: %1").arg(searchProblem), canvas.current, 3);
            return;
        case Results::Found: break;
    }
    if (foundTracks.isEmpty()) {
        canvas.line(tr("Nothing found"), canvas.normal);
        return;
    }
    const int rowHeight = canvas.buttonHeight() + 1;
    listRows = std::max(0, (canvas.bottom - canvas.y) / rowHeight);
    listScroll = std::clamp(listScroll, 0, std::max(0, static_cast<int>(foundTracks.size()) - listRows));
    const QString add = tr("To the jam");
    const QString next = tr("Next");
    for (int i = listScroll; i < foundTracks.size() && i < listScroll + listRows; ++i) {
        const Core::Track track = foundTracks[i];
        const int nextX = canvas.right - canvas.buttonWidth(next);
        const int addX = nextX - kGap - canvas.buttonWidth(add);
        QString title = track.title;
        if (!track.artists.isEmpty()) {
            title += QStringLiteral(" — ") + track.artists.join(QStringLiteral(", "));
        }
        canvas.text(
            QRect(canvas.left, canvas.y, addX - kGap - canvas.left, canvas.buttonHeight()), title, canvas.normal
        );
        const bool addable = jamHost->canAdd(track);
        canvas.button(addX, canvas.y, add, addable, [this, track] {
            send(jamHost->add(track), tr("Sent to the jam"));
        });
        canvas.button(nextX, canvas.y, next, addable, [this, track] {
            send(jamHost->playNext(track), tr("Sent to the jam, plays next"));
        });
        canvas.y += rowHeight;
    }
}

void JamWindow::field(FormCanvas& canvas, const QRect& rect, Field which, const QString& hint) {
    fieldEdit(which).paint(canvas, rect, focus == which, hint);
    const auto focusField = [this, which] {
        focus = which;
        update();
    };
    const auto placeCaret = [this, which, rect, metrics = canvas.metrics](QPoint pos) {
        focus = which;
        fieldEdit(which).pressAt(rect, pos, metrics);
        update();
    };
    canvas.controls << Control{rect, hint, true, focusField, placeCaret};
}

const QImage& JamWindow::qrCode() {
    const QString url = jamHost->joinUrl();
    if (url == qrUrl) {
        return qrImage;
    }
    qrUrl = url;
    qrImage = QImage();
    if (url.isEmpty()) {
        return qrImage;
    }
    try {
        const qrcodegen::QrCode code =
            qrcodegen::QrCode::encodeText(url.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = code.getSize();
        // Dark on light whatever the skin: not every camera reads an inverted code.
        QImage image(size + kQuietZone * 2, size + kQuietZone * 2, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (code.getModule(x, y)) {
                    image.setPixel(x + kQuietZone, y + kQuietZone, qRgb(0, 0, 0));
                }
            }
        }
        qrImage = image;
    } catch (const qrcodegen::data_too_long&) {
        qWarning("jam: the link is too long for a QR code");
    }
    return qrImage;
}

void JamWindow::start() {
    startProblem.clear();
    if (!jamHost->create(nameField.text())) {
        startProblem =
            serverName.isEmpty() ? tr("Set the jam server first") : tr("Cannot start: check the name and the server");
    }
    focus = Field::None;
    update();
}

void JamWindow::copyLink() {
    QGuiApplication::clipboard()->setText(jamHost->joinUrl());
    Q_EMIT statusText(tr("The link is copied"));
}

void JamWindow::end() {
    if (!endArmed) {
        endArmed = true;
        endTimer.start();
        update();
        return;
    }
    endTimer.stop();
    endArmed = false;
    jamHost->end();
}

void JamWindow::send(bool sent, const QString& done) {
    if (!sent) {
        Q_EMIT statusText(tr("No connection to the jam server"));
    } else if (!done.isEmpty()) {
        Q_EMIT statusText(done);
    }
}

void JamWindow::search(const QString& text) {
    const QString query = text.trimmed();
    if (query.isEmpty()) {
        return;
    }
    if (query != searchField.text()) {
        searchField.setText(query);
    }
    results = Results::Loading;
    listScroll = 0;
    const int request = ++searchRequest;
    QPointer<JamWindow> self(this);
    auto showResults = [self, request](const QList<Core::Track>& tracks, const Yandex::RequestError& error) {
        if (!self || request != self->searchRequest) {
            return;
        }
        self->foundTracks.clear();
        if (error.isError()) {
            self->results = Results::Failed;
            self->searchProblem = error.text;
        } else {
            self->results = Results::Found;
            for (const Core::Track& track : tracks) {
                if (track.available && self->foundTracks.size() < Jam::kMaxSearchResults) {
                    self->foundTracks << track;
                }
            }
        }
        self->update();
    };
    yandexCatalog->searchTracks(query, showResults);
    update();
}

LineEdit& JamWindow::fieldEdit(Field which) {
    return which == Field::Name ? nameField : searchField;
}

bool JamWindow::contentMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    const Field before = focus;
    focus = Field::None;
    for (const Control& control : render(nullptr)) {
        if (control.rect.contains(pos)) {
            if (control.enabled && control.pressAt) {
                control.pressAt(pos);
            } else if (control.enabled && control.action) {
                control.action();
            }
            update();
            return true;
        }
    }
    if (before != Field::None) {
        update();
    }
    return false;
}

bool JamWindow::event(QEvent* event) {
    // While a field has the focus, the keys type into it instead of driving the player.
    if (event->type() == QEvent::ShortcutOverride && focus != Field::None
        && LineEdit::takes(static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return GenWindow::event(event);
}

void JamWindow::keyPressEvent(QKeyEvent* event) {
    if (focus == Field::None) {
        return GenWindow::keyPressEvent(event);
    }
    const Field which = focus;
    switch (fieldEdit(which).press(event)) {
        case LineEdit::KeyResult::Enter:
            if (which == Field::Name) {
                start();
            } else {
                search(searchField.text());
            }
            break;
        case LineEdit::KeyResult::Escape: focus = Field::None; break;
        case LineEdit::KeyResult::Edited: nameEdited = nameEdited || which == Field::Name; break;
        case LineEdit::KeyResult::Moved: break;
        case LineEdit::KeyResult::Ignored: return GenWindow::keyPressEvent(event);
    }
    update();
}

void JamWindow::wheelEvent(QWheelEvent* event) {
    const int steps = wheelSteps(event);
    if (steps != 0) {
        listScroll = std::max(0, listScroll - steps);
        update();
    }
}

void JamWindow::retranslate() {
    setWindowTitle(tr("QiYaa: jam"));
}

}  // namespace Ui
