#pragma once

#include "ui/gen_window.h"
#include "yandex/api_client.h"

#include <QFont>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <optional>

class QPainter;

namespace Jam {
class HostSession;
}  // namespace Jam

namespace Yandex {
class Library;
}  // namespace Yandex

namespace Ui {

// The host's jam window (spec/jam/host.md HOST-20, HOST-25, HOST-35): the start of a jam, then
// its QR code and link, the room's settings, the guests and the end on one page, and a search
// that adds tracks on the other. Drawn in the skin's playlist colours and font.
class JamWindow : public GenWindow {
    Q_OBJECT
public:
    enum class Page { Jam, Search };

    JamWindow(
        Jam::HostSession* host,
        Yandex::Library* library,
        const Skins::Skin* skin,
        QWidget* parent = nullptr
    );

    // The name the start page offers, until the user types another.
    void setHostName(const QString& name);
    // The server shown on the start page.
    void setServerName(const QString& server);
    // Tests: a font that draws the same on every system; otherwise the skin's playlist font.
    void setTextFont(const std::optional<QFont>& font);
    Page page() const { return currentPage; }
    void showPage(Page page);
    void search(const QString& text);

    struct Control {
        QRect rect;
        QString label;
        bool enabled = true;
        std::function<void()> action;
    };
    // What the current page shows that can be clicked, in skin pixels; for the tests.
    QList<Control> controls();

Q_SIGNALS:
    void statusText(const QString& text);
    void serverSettingsRequested();

protected:
    void retranslate() override;
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Field { None, Name, Search };
    enum class Results { None, Loading, Found, Failed };
    class Canvas;

    // Draws the page when `painter` is set, and returns its controls either way: one pass for
    // both, so what is drawn and what a click finds never differ.
    QList<Control> render(QPainter* painter);
    void renderStart(Canvas& canvas);
    void renderCreating(Canvas& canvas);
    void renderTabs(Canvas& canvas);
    void renderJam(Canvas& canvas);
    void renderSearch(Canvas& canvas);
    void field(Canvas& canvas, const QRect& rect, Field which, const QString& hint);
    const QImage& qrCode();
    void start();
    void copyLink();
    void end();
    void send(bool sent, const QString& done = {});
    QString& fieldText(Field field);
    QFont textFont() const;

    Jam::HostSession* jamHost;
    Yandex::Library* yandexLibrary;
    Page currentPage = Page::Jam;
    std::optional<QFont> fontOverride;
    Field focus = Field::None;
    QString nameText;
    bool nameEdited = false;
    QString serverName;
    QString searchText;
    QString startProblem;
    Results results = Results::None;
    QList<Yandex::Track> foundTracks;
    QString searchProblem;
    int searchRequest = 0;
    int listScroll = 0;
    int listRows = 0;
    bool endArmed = false;
    QTimer endTimer;
    QString qrUrl;
    QImage qrImage;
};

}  // namespace Ui
