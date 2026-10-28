#include "ui/search_window.h"

#include "core/player.h"
#include "skins/skin.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace Ui {

namespace {

constexpr int kGap = FormCanvas::kGap;
constexpr int kMaxSearchLength = 100;
constexpr int kFirstTracks = 6;
constexpr int kFirstPicks = 3;

QString DurationText(qint64 ms) {
    const qint64 seconds = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString KeyText(const Core::TrackKey& key) {
    return key.source + QLatin1Char('/') + key.id;
}

bool IsListKey(int key) {
    switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Insert:
        case Qt::Key_Backspace:
        case Qt::Key_Escape: return true;
        default: return false;
    }
}

}  // namespace

SearchWindow::SearchWindow(QList<Tab> tabs, Core::Player* player, const Skins::Skin* skin, QWidget* parent)
    : GenWindow(skin, QStringLiteral("SEARCH"), parent)
    , tabs(std::move(tabs))
    , corePlayer(player)
    , field(kMaxSearchLength) {
    retranslate();
    setFocusPolicy(Qt::StrongFocus);
    connect(corePlayer, &Core::Player::currentTrackChanged, this, qOverload<>(&SearchWindow::update));
}

void SearchWindow::showTab(int index) {
    if (index < 0 || index >= tabs.size() || index == tab) {
        return;
    }
    tab = index;
    const QString text = searchedText;
    state = State::Idle;
    results = {};
    opened.reset();
    ++request;
    if (!text.isEmpty()) {
        search(text);
    }
    update();
}

void SearchWindow::focusField() {
    fieldFocused = true;
    update();
}

void SearchWindow::search(const QString& text) {
    const QString query = text.trimmed();
    if (query.isEmpty() || !catalog() || !catalog()->canSearch()) {
        return;
    }
    field.setText(query);
    searchedText = query;
    state = State::Searching;
    results = {};
    opened.reset();
    added.clear();
    sending.clear();
    refusals.clear();
    expanded.clear();
    selected = -1;
    scroll = 0;
    const int asked = ++request;
    QPointer<SearchWindow> self(this);
    catalog()->find(query, [self, asked](const Core::SearchResults& found, const QString& error) {
        if (!self || asked != self->request) {
            return;
        }
        self->problem = error;
        self->results = found;
        self->state = error.isEmpty() ? State::Found : State::Failed;
        self->update();
    });
    update();
}

void SearchWindow::setJam(JamHooks hooks) {
    jam = std::move(hooks);
    update();
}

bool SearchWindow::inJam() const {
    return jam.active && jam.active();
}

void SearchWindow::jamChanged() {
    if (!inJam() || !jam.connected()) {
        sending.clear();
    }
    if (!inJam()) {
        refusals.clear();
    }
    sending.removeIf([this](const auto& waiting) { return jam.waitingFrom(waiting.value()).has_value(); });
    update();
}

void SearchWindow::jamRefused(const std::function<bool(const Core::Track&)>& refused, const QString& text) {
    for (auto it = sending.begin(); it != sending.end();) {
        if (refused(it.value())) {
            refusals.insert(it.key(), text);
            it = sending.erase(it);
        } else {
            ++it;
        }
    }
    update();
}

void SearchWindow::addToJam(const Core::Track& track, bool next) {
    if (!jam.add(track, next)) {
        Q_EMIT statusText(tr("No connection to the jam server"));
        return;
    }
    const QString key = KeyText(track.key);
    sending.insert(key, track);
    refusals.remove(key);
    Q_EMIT statusText(next ? tr("Sent to the jam, plays next") : tr("Sent to the jam"));
    update();
}

SearchWindow::Note SearchWindow::noteOf(const Core::Track& track) const {
    const QString key = KeyText(track.key);
    if (inJam()) {
        if (const std::optional<QString> from = jam.waitingFrom(track)) {
            return {from->isEmpty() ? tr("in the jam") : tr("in the jam from %1").arg(*from)};
        }
        if (sending.contains(key)) {
            return {tr("sending…")};
        }
        return {refusals.value(key), true};
    }
    const auto note = added.constFind(key);
    if (note == added.cend()) {
        return {};
    }
    return {*note == Core::AddPlacement::Next ? tr("plays next") : tr("at the end of the queue")};
}

void SearchWindow::setTextFont(const std::optional<QFont>& font) {
    fontOverride = font;
    update();
}

QList<SearchWindow::Control> SearchWindow::controls() {
    return render(nullptr);
}

Core::SearchCatalog* SearchWindow::catalog() const {
    return tab < tabs.size() ? tabs[tab].catalog : nullptr;
}

QString SearchWindow::searchTitle() const {
    return tr("Search: %1").arg(searchedText);
}

QFont SearchWindow::textFont() const {
    if (fontOverride) {
        return *fontOverride;
    }
    QFont font(skin().playlistStyle().font);
    font.setPixelSize(9);
    return font;
}

void SearchWindow::paintContent(QPainter& painter, const QRect& area) {
    painter.fillRect(area, skin().playlistStyle().normalBackground);
    render(&painter);
}

QList<SearchWindow::Control> SearchWindow::render(QPainter* painter) {
    FormCanvas canvas(painter, contentRect(), textFont(), skin());
    rowRects.clear();
    renderTabs(canvas);
    renderField(canvas);
    if (!catalog()->canSearch()) {
        const QString name = QCoreApplication::translate("Ui::SearchWindow", tabs[tab].name);
        canvas.paragraph(tr("Log in to %1 to search it.").arg(name), canvas.normal);
        canvas.y += kGap;
        canvas.button(canvas.left, canvas.y, tr("Log in…"), true, [this] { Q_EMIT loginRequested(tab); });
        return canvas.controls;
    }
    if (opened) {
        renderOpenedHead(canvas);
        if (opened->state != State::Found || opened->tracks.isEmpty()) {
            renderStatus(canvas);
            return canvas.controls;
        }
    } else if (state != State::Found || results.isEmpty()) {
        renderStatus(canvas);
        return canvas.controls;
    }
    renderRows(canvas, sections());
    return canvas.controls;
}

void SearchWindow::renderTabs(FormCanvas& canvas) {
    int x = canvas.left;
    for (int index = 0; index < tabs.size(); ++index) {
        const QString name = QCoreApplication::translate("Ui::SearchWindow", tabs[index].name);
        const QRect rect = canvas.button(x, canvas.y, name, true, [this, index] { showTab(index); }, index == tab);
        x = rect.right() + 1 + kGap;
    }
    if (inJam()) {
        const QString label = tr("Jam");
        const int labelWidth = canvas.textWidth(label) + 2;
        const int labelX = canvas.right - labelWidth;
        canvas.light(labelX - 7, canvas.y + canvas.buttonHeight() / 2, jam.connected());
        canvas.text(QRect(labelX, canvas.y, labelWidth, canvas.buttonHeight()), label, canvas.normal);
    }
    canvas.y += canvas.buttonHeight() + kGap;
}

void SearchWindow::renderField(FormCanvas& canvas) {
    const QString find = tr("Search");
    const int findX = canvas.right - canvas.buttonWidth(find);
    const QRect rect(canvas.left, canvas.y, findX - kGap - canvas.left, canvas.buttonHeight());
    const bool enabled = catalog()->canSearch();
    field.paint(canvas, rect, fieldFocused, tr("Artist, album or track"));
    const auto placeCaret = [this, rect, metrics = canvas.metrics](QPoint pos) {
        fieldFocused = true;
        field.pressAt(rect, pos, metrics);
        update();
    };
    canvas.controls << Control{rect, tr("Artist, album or track"), enabled, {}, placeCaret};
    const bool ready = enabled && !field.text().trimmed().isEmpty();
    canvas.button(findX, canvas.y, find, ready, [this] { search(field.text()); });
    canvas.y += canvas.buttonHeight() + kGap * 2;
}

void SearchWindow::renderStatus(FormCanvas& canvas) {
    const State shown = opened ? opened->state : state;
    switch (shown) {
        case State::Idle:
            canvas.paragraph(
                tr("Find a track, an album or an artist. Play it, or add it to play next; with Shift, to the "
                   "end of the queue."),
                canvas.dim
            );
            return;
        case State::Searching: canvas.line(tr("Searching…"), canvas.normal); return;
        case State::Failed:
            canvas.paragraph(tr("The search failed: %1").arg(opened ? opened->problem : problem), canvas.current, 3);
            canvas.y += kGap;
            canvas.button(canvas.left, canvas.y, tr("Try again"), true, [this] {
                if (opened) {
                    open(opened->pick);
                } else {
                    search(searchedText);
                }
            });
            return;
        case State::Found: break;
    }
    canvas.line(opened ? tr("No tracks") : tr("Nothing found"), canvas.normal);
}

void SearchWindow::renderOpenedHead(FormCanvas& canvas) {
    const QString backLabel = tr("Back");
    const QRect backRect = canvas.button(canvas.left, canvas.y, backLabel, true, [this] { back(); });
    const int titleX = backRect.right() + 1 + kGap;
    canvas.text(
        QRect(titleX, canvas.y, canvas.right - titleX, canvas.buttonHeight()), opened->pick.name, canvas.current, true
    );
    canvas.y += canvas.buttonHeight() + kGap;
    const bool album = opened->pick.kind == Core::SearchPick::Kind::Album;
    QStringList details;
    if (album) {
        details << opened->pick.artists.join(QStringLiteral(", "));
        if (opened->pick.year > 0) {
            details << QString::number(opened->pick.year);
        }
    } else {
        details << tr("popular tracks");
    }
    if (opened->state == State::Found) {
        details << tr("%n track(s)", nullptr, static_cast<int>(opened->tracks.size()));
    }
    details.removeAll(QString());
    canvas.line(details.join(QStringLiteral(" · ")), canvas.dim);
    canvas.y += kGap;
    if (opened->state != State::Found || opened->tracks.isEmpty()) {
        return;
    }
    const Core::SearchPick pick = opened->pick;
    const QRect play = canvas.button(canvas.left, canvas.y, tr("Play all"), true, [this, pick] {
        catalog()->playTracks(pick.name, opened->tracks, 0, pick);
    });
    if (inJam() || !album) {
        canvas.y += canvas.buttonHeight() + kGap * 2;
        return;
    }
    const auto addAll = [this, pick](Core::AddPlacement placement) {
        catalog()->add(pick.name, opened->tracks, placement);
        for (const Core::Track& track : opened->tracks) {
            added.insert(KeyText(track.key), placement);
        }
        update();
    };
    const QRect next = canvas.button(play.right() + 1 + kGap, canvas.y, tr("Next"), true, [addAll] {
        addAll(Core::AddPlacement::Next);
    });
    canvas.button(next.right() + 1 + kGap, canvas.y, tr("To the end"), true, [addAll] {
        addAll(Core::AddPlacement::End);
    });
    canvas.y += canvas.buttonHeight() + kGap * 2;
}

QList<SearchWindow::Section> SearchWindow::sections() const {
    QList<Section> parts;
    if (opened) {
        Section tracks{.part = Part::Tracks, .total = static_cast<int>(opened->tracks.size())};
        for (qsizetype i = 0; i < opened->tracks.size(); ++i) {
            tracks.rows << Row{
                .kind = RowKind::Track,
                .track = opened->tracks[i],
                .number = static_cast<int>(i) + 1,
                .playIndex = static_cast<int>(i)
            };
        }
        return {tracks};
    }
    Section best{.part = Part::Best, .title = tr("Best")};
    if (results.bestTrack) {
        best.rows << Row{.kind = RowKind::Track, .track = *results.bestTrack, .playIndex = 0};
    } else if (results.bestArtist) {
        best.rows << Row{.kind = RowKind::Artist, .pick = *results.bestArtist};
    } else if (results.bestAlbum) {
        best.rows << Row{.kind = RowKind::Album, .pick = *results.bestAlbum};
    }
    if (!best.rows.isEmpty()) {
        parts << best;
    }
    const int firstTrack = results.bestTrack ? 1 : 0;
    Section tracks{.part = Part::Tracks, .total = static_cast<int>(results.tracks.size())};
    tracks.title = tr("Tracks · %1").arg(tracks.total);
    for (qsizetype i = 0; i < results.tracks.size(); ++i) {
        tracks.rows << Row{
            .kind = RowKind::Track,
            .track = results.tracks[i],
            .number = static_cast<int>(i) + 1,
            .playIndex = firstTrack + static_cast<int>(i)
        };
    }
    Section artists{.part = Part::Artists, .total = static_cast<int>(results.artists.size())};
    artists.title = tr("Artists · %1").arg(artists.total);
    for (const Core::SearchPick& artist : results.artists) {
        artists.rows << Row{.kind = RowKind::Artist, .pick = artist};
    }
    Section albums{.part = Part::Albums, .total = static_cast<int>(results.albums.size())};
    albums.title = tr("Albums · %1").arg(albums.total);
    for (const Core::SearchPick& album : results.albums) {
        albums.rows << Row{.kind = RowKind::Album, .pick = album};
    }
    for (Section section : {tracks, artists, albums}) {
        if (section.rows.isEmpty()) {
            continue;
        }
        const int first = section.part == Part::Tracks ? kFirstTracks : kFirstPicks;
        if (!expanded.contains(section.part)) {
            section.rows = section.rows.mid(0, first);
        }
        parts << section;
    }
    return parts;
}

QList<SearchWindow::Row> SearchWindow::selectableRows() const {
    QList<Row> rows;
    for (const Section& section : sections()) {
        rows << section.rows;
    }
    return rows;
}

void SearchWindow::renderRows(FormCanvas& canvas, const QList<Section>& parts) {
    const int hintTop = canvas.bottom - canvas.lineHeight;
    QString hint = opened
        ? tr("Enter: play · Ins: next · Shift+Ins: to the end · Backspace: back")
        : tr("Enter: play or open · Ins: next · Shift+Ins: to the end");
    if (inJam() && opened) {
        hint = tr("Enter: play · Ins: to the jam · Shift+Ins: to the jam, next · Backspace: back");
    } else if (inJam()) {
        hint = tr("Enter: play or open · Ins: to the jam · Shift+Ins: to the jam, next");
    }
    canvas.text(QRect(canvas.left, hintTop, canvas.width(), canvas.lineHeight), hint, canvas.dim);
    const int listBottom = hintTop - kGap;
    const int rowHeight = canvas.buttonHeight() + 1;
    struct Line {
        const Section* section = nullptr;
        const Row* row = nullptr;
        int index = -1;
        int height = 0;
    };
    QList<Line> lines;
    rowLines.clear();
    titleLines.clear();
    int index = 0;
    for (const Section& section : parts) {
        if (!section.title.isEmpty()) {
            titleLines.insert(static_cast<int>(lines.size()));
            lines << Line{.section = &section, .height = rowHeight};
        }
        for (const Row& row : section.rows) {
            const bool note = row.kind == RowKind::Track && !noteOf(row.track).text.isEmpty();
            rowLines << static_cast<int>(lines.size());
            lines << Line{&section, &row, index++, rowHeight + (note ? canvas.lineHeight : 0)};
        }
    }
    listHeight = listBottom - canvas.y;
    lineHeights.clear();
    for (const Line& line : lines) {
        lineHeights << line.height;
    }
    maxScroll = static_cast<int>(lines.size());
    for (int i = static_cast<int>(lines.size()) - 1, used = 0; i >= 0; --i) {
        used += lines[i].height;
        if (used > listHeight) {
            break;
        }
        maxScroll = i;
    }
    scroll = std::clamp(scroll, 0, std::max(0, maxScroll));
    int shown = 0;
    for (qsizetype i = scroll; i < lines.size() && canvas.y + lines[i].height <= listBottom; ++i) {
        if (lines[i].row) {
            renderRow(canvas, *lines[i].row, lines[i].index);
            ++shown;
            continue;
        }
        const Section& section = *lines[i].section;
        const bool foldable =
            section.part != Part::Best && section.total > (section.part == Part::Tracks ? kFirstTracks : kFirstPicks);
        int textRight = canvas.right;
        if (foldable) {
            const QString label = expanded.contains(section.part) ? tr("Fewer") : tr("All");
            const int x = canvas.right - canvas.buttonWidth(label);
            const Part part = section.part;
            canvas.button(x, canvas.y, label, true, [this, part] {
                if (!expanded.remove(part)) {
                    expanded.insert(part);
                }
                selected = -1;
                update();
            });
            textRight = x - kGap;
        }
        const QRect titleRect(canvas.left, canvas.y, textRight - canvas.left, canvas.buttonHeight());
        canvas.text(titleRect, section.title, canvas.current, true);
        canvas.controls << Control{titleRect, section.title, true, {}};
        canvas.y += rowHeight;
    }
    shownRows = std::max(1, shown);
}

void SearchWindow::renderRow(FormCanvas& canvas, const Row& row, int index) {
    const int side = canvas.buttonHeight();
    const int top = canvas.y;
    int buttonsX = canvas.right - side;
    bool withAdd = row.kind != RowKind::Artist;
    bool addable = true;
    if (inJam()) {
        withAdd = row.kind == RowKind::Track && !jam.waitingFrom(row.track);
        addable = jam.canAdd(row.track) && !sending.contains(KeyText(row.track.key));
    }
    if (withAdd) {
        const auto addRow = [this, row] { add(row, placementNow()); };
        canvas.glyphButton(buttonsX, top, FormCanvas::Glyph::Add, tr("Add"), addable, addRow);
    }
    if (row.kind != RowKind::Artist) {
        buttonsX -= side + kGap;
    }
    canvas.glyphButton(buttonsX, top, FormCanvas::Glyph::Play, tr("Play"), true, [this, row] { play(row); });
    int textRight = buttonsX - kGap;
    const bool isCurrent =
        row.kind == RowKind::Track && corePlayer->currentTrack() && corePlayer->currentTrack()->key == row.track.key;
    const QColor color = isCurrent ? canvas.current : canvas.normal;
    QString text;
    if (row.kind == RowKind::Track) {
        const QString time = DurationText(row.track.durationMs);
        const int timeWidth = canvas.textWidth(time) + 1;
        canvas.text(
            QRect(textRight - timeWidth, top, timeWidth, side), time, color, false, Qt::AlignRight | Qt::AlignVCenter
        );
        textRight -= timeWidth + kGap;
        text = row.track.artists.join(QStringLiteral(", ")) + QStringLiteral(" - ") + row.track.title;
        if (row.number > 0) {
            text = QStringLiteral("%1. %2").arg(row.number).arg(text);
        }
    } else if (row.kind == RowKind::Artist) {
        text = row.pick.name + QStringLiteral(" · ") + tr("artist");
    } else {
        QStringList parts{row.pick.name, row.pick.artists.join(QStringLiteral(", "))};
        if (row.pick.year > 0) {
            parts << QString::number(row.pick.year);
        }
        if (row.pick.single) {
            parts << tr("single");
        }
        parts.removeAll(QString());
        text = parts.join(QStringLiteral(" · "));
    }
    const QRect textRect(canvas.left, top, textRight - canvas.left, side);
    if (canvas.painter && index == selected) {
        canvas.painter->fillRect(textRect.adjusted(-1, 0, 1, 0), canvas.selected);
    }
    canvas.text(textRect.adjusted(1, 0, 0, 0), text, color);
    canvas.controls << Control{textRect, text, true, [this, index] { select(index); }};
    rowRects << qMakePair(textRect, index);
    canvas.y += side + 1;
    if (row.kind == RowKind::Track) {
        const Note note = noteOf(row.track);
        if (!note.text.isEmpty()) {
            const QRect noteRect(canvas.left + 8, canvas.y, canvas.width() - 8, canvas.lineHeight);
            canvas.text(noteRect, note.text, note.loud ? canvas.current : canvas.dim);
            canvas.y += canvas.lineHeight;
        }
    }
}

void SearchWindow::activate(const Row& row) {
    if (row.kind == RowKind::Track) {
        play(row);
    } else {
        open(row.pick);
    }
}

void SearchWindow::play(const Row& row) {
    if (row.kind != RowKind::Track) {
        catalog()->playPick(row.pick);
    } else if (opened) {
        catalog()->playTracks(opened->pick.name, opened->tracks, row.playIndex, opened->pick);
    } else {
        catalog()->playTracks(searchTitle(), results.playable(), row.playIndex, std::nullopt);
    }
}

void SearchWindow::add(const Row& row, Core::AddPlacement placement) {
    if (inJam()) {
        const bool next = placement == Core::AddPlacement::End;
        const bool waiting = row.kind == RowKind::Track && jam.waitingFrom(row.track).has_value();
        const bool sent = row.kind == RowKind::Track && sending.contains(KeyText(row.track.key));
        if (row.kind == RowKind::Track && (next || !waiting) && !sent && jam.canAdd(row.track)) {
            addToJam(row.track, next);
        }
        return;
    }
    if (row.kind == RowKind::Album) {
        catalog()->addAlbum(row.pick, placement);
        return;
    }
    if (row.kind != RowKind::Track) {
        return;
    }
    catalog()->add(opened ? opened->pick.name : searchTitle(), {row.track}, placement);
    added.insert(KeyText(row.track.key), placement);
    update();
}

void SearchWindow::open(const Core::SearchPick& pick) {
    const int resultsScroll = opened ? opened->resultsScroll : scroll;
    const int resultsSelected = opened ? opened->resultsSelected : selected;
    opened = Opened{.pick = pick, .resultsScroll = resultsScroll, .resultsSelected = resultsSelected};
    selected = -1;
    scroll = 0;
    const int asked = ++openRequest;
    QPointer<SearchWindow> self(this);
    catalog()->open(pick, [self, asked](const QList<Core::Track>& tracks, const QString& error) {
        if (!self || !self->opened || asked != self->openRequest) {
            return;
        }
        self->opened->problem = error;
        self->opened->state = error.isEmpty() ? State::Found : State::Failed;
        for (const Core::Track& track : tracks) {
            if (track.available) {
                self->opened->tracks << track;
            }
        }
        self->update();
    });
    update();
}

void SearchWindow::back() {
    if (!opened) {
        return;
    }
    scroll = opened->resultsScroll;
    selected = opened->resultsSelected;
    opened.reset();
    ++openRequest;
    update();
}

void SearchWindow::select(int index) {
    selected = index;
    fieldFocused = false;
    update();
}

void SearchWindow::moveSelection(int by) {
    const int count = static_cast<int>(selectableRows().size());
    if (count == 0) {
        return;
    }
    selected = std::clamp(selected + by, 0, count - 1);
    render(nullptr);
    revealSelected();
    update();
}

void SearchWindow::revealSelected() {
    if (selected < 0 || selected >= rowLines.size()) {
        return;
    }
    const int line = rowLines[selected];
    scroll = std::min(scroll, titleLines.contains(line - 1) ? line - 1 : line);
    int used = 0;
    for (int i = scroll; i <= line; ++i) {
        used += lineHeights[i];
    }
    while (used > listHeight && scroll < line) {
        used -= lineHeights[scroll];
        ++scroll;
    }
}

Core::AddPlacement SearchWindow::placementNow() const {
    return pressModifiers.testFlag(Qt::ShiftModifier) ? Core::AddPlacement::End : Core::AddPlacement::Next;
}

void SearchWindow::mousePressEvent(QMouseEvent* event) {
    pressModifiers = event->modifiers();
    GenWindow::mousePressEvent(event);
}

bool SearchWindow::contentMousePress(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    for (const Control& control : render(nullptr)) {
        if (!control.rect.contains(pos)) {
            continue;
        }
        if (control.enabled && control.pressAt) {
            control.pressAt(pos);
        } else if (control.enabled && control.action) {
            fieldFocused = false;
            control.action();
        }
        update();
        return true;
    }
    fieldFocused = false;
    update();
    return false;
}

bool SearchWindow::skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) {
    if (button != Qt::LeftButton) {
        return false;
    }
    render(nullptr);
    const QList<Row> rows = selectableRows();
    for (const auto& [rect, index] : rowRects) {
        if (rect.contains(pos) && index < rows.size()) {
            select(index);
            activate(rows[index]);
            return true;
        }
    }
    return contentRect().contains(pos);
}

bool SearchWindow::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        const bool plain = (key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier)) == Qt::NoModifier;
        if ((fieldFocused && LineEdit::takes(key)) || (!fieldFocused && plain && IsListKey(key->key()))) {
            event->accept();
            return true;
        }
    }
    return GenWindow::event(event);
}

void SearchWindow::keyPressEvent(QKeyEvent* event) {
    if (fieldFocused) {
        switch (field.press(event)) {
            case LineEdit::KeyResult::Enter: search(field.text()); break;
            case LineEdit::KeyResult::Escape: fieldFocused = false; break;
            case LineEdit::KeyResult::Edited:
            case LineEdit::KeyResult::Moved: break;
            case LineEdit::KeyResult::Ignored:
                if (event->key() != Qt::Key_Down || selectableRows().isEmpty()) {
                    return GenWindow::keyPressEvent(event);
                }
                fieldFocused = false;
                moveSelection(selected < 0 ? 1 : 0);
                break;
        }
        update();
        return;
    }
    const QList<Row> rows = selectableRows();
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
        case Qt::Key_Up:
            if (selected <= 0) {
                fieldFocused = true;
                selected = -1;
                update();
            } else {
                moveSelection(-1);
            }
            return;
        case Qt::Key_Down: moveSelection(1); return;
        case Qt::Key_PageUp: moveSelection(-shownRows); return;
        case Qt::Key_PageDown: moveSelection(shownRows); return;
        case Qt::Key_Home: moveSelection(-static_cast<int>(rows.size())); return;
        case Qt::Key_End: moveSelection(static_cast<int>(rows.size())); return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (selected >= 0 && selected < rows.size()) {
                activate(rows[selected]);
            }
            return;
        case Qt::Key_Insert:
            if (selected >= 0 && selected < rows.size()) {
                add(rows[selected], shift ? Core::AddPlacement::End : Core::AddPlacement::Next);
            }
            return;
        case Qt::Key_Backspace:
        case Qt::Key_Escape:
            if (opened) {
                back();
            } else {
                focusField();
            }
            return;
        default: GenWindow::keyPressEvent(event);
    }
}

void SearchWindow::wheelEvent(QWheelEvent* event) {
    const int steps = wheelSteps(event);
    if (steps != 0) {
        render(nullptr);
        scroll = std::clamp(scroll - steps, 0, std::max(0, maxScroll));
        update();
    }
}

void SearchWindow::showEvent(QShowEvent* event) {
    GenWindow::showEvent(event);
    fieldFocused = true;
}

void SearchWindow::retranslate() {
    setWindowTitle(tr("QiYaa: search"));
}

}  // namespace Ui
