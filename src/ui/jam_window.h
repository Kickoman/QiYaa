#pragma once

#include "ui/form_canvas.h"
#include "ui/gen_window.h"
#include "ui/line_edit.h"

#include <QFont>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <optional>

class QPainter;

namespace Core {
struct QueueEntry;
}  // namespace Core

namespace Jam {
class HostSession;
}  // namespace Jam

namespace Ui {

QString JamTrackNote(const Core::QueueEntry& entry, bool jamActive, const Jam::HostSession* host);

// The host's jam window (spec/jam/host.md HOST-25, HOST-35): the start of a jam, then its QR code
// and link, the room's settings, the guests and the end. "Add tracks…" asks for the search window
// (HOST-38). Drawn in the skin's playlist colours and font.
class JamWindow : public GenWindow {
    Q_OBJECT
public:
    JamWindow(Jam::HostSession* host, const Skins::Skin* skin, QWidget* parent = nullptr);

    // The name the start page offers, until the user types another.
    void setHostName(const QString& name);
    // The server shown on the start page.
    void setServerName(const QString& server);
    // Tests: a font that draws the same on every system; otherwise the skin's playlist font.
    void setTextFont(const std::optional<QFont>& font);

    using Control = FormControl;
    // What the current page shows that can be clicked, in skin pixels; for the tests.
    QList<Control> controls();

Q_SIGNALS:
    void statusText(const QString& text);
    void serverSettingsRequested();
    void searchRequested();

protected:
    void retranslate() override;
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Field { None, Name };

    // Draws the page when `painter` is set, and returns its controls either way: one pass for
    // both, so what is drawn and what a click finds never differ.
    QList<Control> render(QPainter* painter);
    void renderStart(FormCanvas& canvas);
    void renderCreating(FormCanvas& canvas);
    void renderTop(FormCanvas& canvas);
    void renderJam(FormCanvas& canvas);
    void field(FormCanvas& canvas, const QRect& rect, Field which, const QString& hint);
    const QImage& qrCode();
    void start();
    void copyLink();
    void end();
    void send(bool sent, const QString& done = {});
    QFont textFont() const;

    Jam::HostSession* jamHost;
    std::optional<QFont> fontOverride;
    Field focus = Field::None;
    LineEdit nameField;
    bool nameEdited = false;
    QString serverName;
    QString startProblem;
    int listScroll = 0;
    int listRows = 0;
    bool endArmed = false;
    QTimer endTimer;
    QString qrUrl;
    QImage qrImage;
};

}  // namespace Ui
