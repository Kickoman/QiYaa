#pragma once

#include "core/queue_source.h"
#include "core/search.h"
#include "core/track.h"
#include "ui/form_canvas.h"
#include "ui/gen_window.h"
#include "ui/line_edit.h"

#include <QFont>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QWidget>

#include <optional>

class QPainter;

namespace Core {
class Player;
}  // namespace Core

namespace Ui {

class SearchWindow : public GenWindow {
    Q_OBJECT
public:
    struct Tab {
        const char* name = nullptr;
        Core::SearchCatalog* catalog = nullptr;
    };
    enum class Part { Best, Tracks, Artists, Albums };
    using Control = FormControl;

    SearchWindow(QList<Tab> tabs, Core::Player* player, const Skins::Skin* skin, QWidget* parent = nullptr);

    void showTab(int index);
    int currentTab() const { return tab; }
    void focusField();
    void search(const QString& text);
    void setTextFont(const std::optional<QFont>& font);
    QList<Control> controls();
    int selectedRow() const { return selected; }

Q_SIGNALS:
    void loginRequested(int tab);

protected:
    void retranslate() override;
    void paintContent(QPainter& painter, const QRect& area) override;
    bool contentMousePress(QPoint pos, Qt::MouseButton button) override;
    bool skinMouseDoubleClick(QPoint pos, Qt::MouseButton button) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    enum class State { Idle, Searching, Found, Failed };
    enum class RowKind { Track, Artist, Album };
    struct Row {
        RowKind kind = RowKind::Track;
        Core::Track track{};
        Core::SearchPick pick{};
        int number = 0;
        int playIndex = -1;
    };
    struct Section {
        Part part = Part::Tracks;
        QString title;
        int total = 0;
        QList<Row> rows;
    };
    struct Opened {
        Core::SearchPick pick;
        State state = State::Searching;
        QList<Core::Track> tracks;
        QString problem;
        int resultsScroll = 0;
        int resultsSelected = -1;
    };

    QList<Control> render(QPainter* painter);
    void renderTabs(FormCanvas& canvas);
    void renderField(FormCanvas& canvas);
    void renderStatus(FormCanvas& canvas);
    void renderOpenedHead(FormCanvas& canvas);
    void renderRows(FormCanvas& canvas, const QList<Section>& sections);
    void renderRow(FormCanvas& canvas, const Row& row, int index);
    QList<Section> sections() const;
    QList<Row> selectableRows() const;
    Core::SearchCatalog* catalog() const;
    QString searchTitle() const;
    void activate(const Row& row);
    void play(const Row& row);
    void add(const Row& row, Core::AddPlacement placement);
    void open(const Core::SearchPick& pick);
    void back();
    void select(int index);
    void moveSelection(int by);
    void revealSelected();
    Core::AddPlacement placementNow() const;
    QFont textFont() const;

    QList<Tab> tabs;
    Core::Player* corePlayer;
    int tab = 0;
    std::optional<QFont> fontOverride;
    LineEdit field;
    bool fieldFocused = true;
    State state = State::Idle;
    QString searchedText;
    QString problem;
    Core::SearchResults results;
    int request = 0;
    std::optional<Opened> opened;
    int openRequest = 0;
    QSet<Part> expanded;
    QHash<QString, Core::AddPlacement> added;
    int selected = -1;
    int scroll = 0;
    int shownRows = 0;
    int maxScroll = 0;
    int listHeight = 0;
    QList<int> lineHeights;
    QList<int> rowLines;
    Qt::KeyboardModifiers pressModifiers;
    QList<QPair<QRect, int>> rowRects;
};

}  // namespace Ui
