#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLWindow>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

struct projectm;

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace Vis {

class MilkdropView : public QOpenGLWindow {
    Q_OBJECT
public:
    explicit MilkdropView(Audio::AudioEngine* engine);
    ~MilkdropView() override;

    static QString OpenGlProblem();

    void loadPreset(const QByteArray& milk, bool smooth);
    void setPresetDuration(double seconds);
    void setLocked(bool locked);
    void setTextureSearchPaths(const QStringList& paths);
    void setRendering(bool on, int fps = 60);
    bool isRendering() const { return timer.isActive(); }

    bool isReady() const { return projectM != nullptr; }
    QString failure() const { return failureReason; }
    qint64 framesRendered() const { return frameCount; }
    QString glInfo() const { return glInfoText; }

    void setBlackWatch(bool on);

    void captureNextFrame();
    void setBlackWatchTiming(int graceMs, int intervalMs, int checks);

Q_SIGNALS:
    void ready();
    void failed(const QString& reason);
    void switchRequested(bool hardCut);
    void presetFailed(const QString& message);
    void staysBlack();
    void drawsPicture();
    void frameCaptured(const QImage& frame);
    void doubleClicked();
    void contextMenuRequested(const QPoint& globalPos);
    void keyPressed(int key, Qt::KeyboardModifiers modifiers);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applySettings();
    void applyTexturePaths();
    void syncWindowSize();
    void destroyProjectM();
    void makeOpaque();
    void watchForBlack();
    bool pictureIsBlack();

    Audio::AudioEngine* audioEngine;
    ::projectm* projectM = nullptr;
    QString failureReason;
    QTimer timer;
    uint32_t visReadCursor = 0;
    std::vector<float> pcm;
    std::optional<std::pair<QByteArray, bool>> pending;
    double duration = 30;
    bool lockEnabled = false;
    QStringList texturePaths;
    bool texturePathsDirty = false;
    QSize pixelSize;
    qint64 frameCount = 0;
    QString glInfoText;

    bool blackWatch = false;
    int blackGraceMs = 5000;
    int blackIntervalMs = 1000;
    int blackChecksNeeded = 4;
    int blackChecks = 0;
    bool sawPicture = false;
    bool captureRequested = false;
    QElapsedTimer sinceLoad;
    QElapsedTimer sinceCheck;
    unsigned probeFbo = 0;
    unsigned probeTex = 0;
};

}  // namespace Vis
