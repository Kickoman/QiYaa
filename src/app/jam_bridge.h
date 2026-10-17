#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QMenu;
class QMessageBox;

namespace Core {
class JamMode;
class Player;
}  // namespace Core

namespace Jam {
class HostSession;
}  // namespace Jam

namespace Ui {
class JamWindow;
class MainWindow;
class PlaylistWindow;
}  // namespace Ui

namespace App {

class Account;
class Settings;
class WindowSet;

class JamBridge : public QObject {
    Q_OBJECT
public:
    struct Parts {
        Jam::HostSession* host = nullptr;
        Ui::JamWindow* window = nullptr;
        Core::JamMode* jamMode = nullptr;
        Core::Player* player = nullptr;
        Settings* settings = nullptr;
        WindowSet* windows = nullptr;
        Account* account = nullptr;
        Ui::MainWindow* mainWindow = nullptr;
        Ui::PlaylistWindow* playlistWindow = nullptr;
    };

    explicit JamBridge(const Parts& parts, QObject* parent = nullptr);
    ~JamBridge() override;

    void offerStored();
    void addMenu(QMenu* menu);
    void showServerDialog();
    QString trackNote(int row) const;

Q_SIGNALS:
    void statusText(const QString& text);

private:
    Parts parts;
    QPointer<QMessageBox> storedJamQuestion;
};

}  // namespace App
