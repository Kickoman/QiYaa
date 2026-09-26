// OpenGL surface that runs projectM (a Milkdrop reimplementation, LGPL) fed
// with the player's output PCM. A QOpenGLWindow on purpose: projectM draws its
// final image into framebuffer 0, which is the window itself here (a
// QOpenGLWidget would render into an offscreen FBO instead).
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLWindow>
#include <QString>
#include <QTimer>

#include <optional>
#include <vector>

struct projectm;  // projectM's opaque instance (projectm_handle)

namespace Audio {
class AudioEngine;
}  // namespace Audio

namespace Vis {

class MilkdropView : public QOpenGLWindow {
    Q_OBJECT
public:
    explicit MilkdropView(Audio::AudioEngine* engine);
    ~MilkdropView() override;

    // Why projectM can't run here (no OpenGL, or older than 3.3), or empty if it can.
    // Check before showing a view: QOpenGLWindow itself breaks without a context.
    static QString OpenGlProblem();

    // Switches to this preset at the next frame, blended over unless `smooth` is false.
    void loadPreset(const QByteArray& milk, bool smooth);
    void setPresetDuration(double seconds);  // then switchRequested() asks for the next one
    void setLocked(bool locked);  // no automatic switching
    void setTextureSearchPaths(const QStringList& paths);
    // Renders frames while on (at `fps`); nothing at all while off.
    void setRendering(bool on, int fps = 60);
    bool isRendering() const { return timer.isActive(); }

    // After the first frame: did projectM start (needs OpenGL 3.3)?
    bool isReady() const { return projectM != nullptr; }
    QString failure() const { return failureReason; }
    qint64 framesRendered() const { return frameCount; }
    QString glInfo() const { return glInfoText; }  // version | renderer, for reports

    // Watch for a preset that stays black (some GPUs/drivers can't run some
    // presets): while on, the picture is sampled once per `intervalMs` from
    // `graceMs` after a preset load, and `checks` black samples in a row emit
    // staysBlack(). Only meaningful while music plays.
    void setBlackWatch(bool on);

    // The next frame as drawn, read back before it's shown (frameCaptured).
    // QOpenGLWindow::grabFramebuffer() can't do this: it reads the back
    // buffer after the swap, whose content is undefined in this mode.
    void captureNextFrame();
    void setBlackWatchTiming(int graceMs, int intervalMs, int checks);

Q_SIGNALS:
    void ready();
    void failed(const QString& reason);
    void switchRequested(bool hardCut);  // the preset's time is up (or a hard cut on a beat)
    void presetFailed(const QString& message);
    void staysBlack();
    void drawsPicture();  // the first sample after a load that isn't black
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
    int blackGraceMs = 5000;  // new presets fade in (and the blend takes 3 s)
    int blackIntervalMs = 1000;
    int blackChecksNeeded = 4;
    int blackChecks = 0;
    bool sawPicture = false;
    bool captureRequested = false;
    QElapsedTimer sinceLoad;
    QElapsedTimer sinceCheck;
    unsigned probeFbo = 0;  // tiny render target the picture is scaled into
    unsigned probeTex = 0;
};

}  // namespace Vis
