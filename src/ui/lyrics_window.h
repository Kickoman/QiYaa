#pragma once

#include "core/lyrics.h"
#include "core/track.h"
#include "ui/gen_window.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <optional>

class QPainter;

namespace Core {
class LyricsFinder;
class Player;
}  // namespace Core

namespace Skins {
class Skin;
}  // namespace Skins

namespace Ui {

class LyricsWindow : public GenWindow {
    Q_OBJECT
public:
    LyricsWindow(Core::LyricsFinder* finder, Core::Player* player, const Skins::Skin* skin, QWidget* parent = nullptr);

    int currentLine() const;
    int lineAt(QPoint skinPos) const;
    bool isFollowing() const { return following; }
    void setFollowPauseMs(int ms) { followPause.setInterval(ms); }

protected:
    void retranslate() override;
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    void skinChanged() override;
    void wheelEvent(QWheelEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Row {
        int line = 0;
        QString text;
    };

    void lyricsChanged();
    void positionMoved();
    QRect textArea() const;
    int visibleRows() const;
    int firstRow() const;
    int lastScroll() const;
    bool isSynced() const;
    const QList<Row>& rows() const;
    QString footer() const;
    void paintStatus(QPainter& painter, const QRect& area) const;

    Core::LyricsFinder* lyricsFinder;
    Core::Player* corePlayer;
    QTimer followPause;
    bool following = true;
    int scrollRow = 0;
    int shownLine = -1;
    std::optional<Core::TrackKey> laidOutTrack;
    Core::Lyrics laidOut;
    mutable QList<Row> wrapped;
    mutable int wrappedWidth = -1;
};

}  // namespace Ui
