// OpenGL surface that runs projectM (a Milkdrop reimplementation, LGPL) fed
// with the player's output PCM. A QOpenGLWindow on purpose: projectM draws its
// final image into framebuffer 0, which is the window itself here (a
// QOpenGLWidget would render into an offscreen FBO instead).
#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QElapsedTimer>
#include <QOpenGLWindow>
#include <QString>
#include <QTimer>

struct projectm;  // projectM's opaque instance (projectm_handle)

namespace qiyaa {

namespace audio {
class AudioEngine;
}

class MilkdropView : public QOpenGLWindow {
    Q_OBJECT
public:
    explicit MilkdropView(audio::AudioEngine* engine);
    ~MilkdropView() override;

    // Why projectM can't run here (no OpenGL, or older than 3.3), or empty if it can.
    // Check before showing a view: QOpenGLWindow itself breaks without a context.
    static QString openGLProblem();

    // Switches to this preset at the next frame, blended over unless `smooth` is false.
    void loadPreset(const QByteArray& milk, bool smooth);
    void setPresetDuration(double seconds);  // then switchRequested() asks for the next one
    void setLocked(bool locked);             // no automatic switching
    void setTextureSearchPaths(const QStringList& paths);
    // Renders frames while on (at `fps`); nothing at all while off.
    void setRendering(bool on, int fps = 60);
    bool isRendering() const { return m_timer.isActive(); }

    // After the first frame: did projectM start (needs OpenGL 3.3)?
    bool isReady() const { return m_pm != nullptr; }
    QString failure() const { return m_failure; }
    qint64 framesRendered() const { return m_frames; }

Q_SIGNALS:
    void ready();
    void failed(const QString& reason);
    void switchRequested(bool hardCut);    // the preset's time is up (or a hard cut on a beat)
    void presetFailed(const QString& message);
    void doubleClicked();
    void contextMenuRequested(const QPoint& globalPos);
    void keyPressed(int key, Qt::KeyboardModifiers modifiers);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void applySettings();
    void destroyProjectM();

    audio::AudioEngine* m_engine;
    ::projectm* m_pm = nullptr;
    QString m_failure;
    QTimer m_timer;
    uint32_t m_visCursor = 0;
    std::vector<float> m_pcm;
    std::optional<std::pair<QByteArray, bool>> m_pending;
    double m_duration = 30;
    bool m_locked = false;
    QStringList m_texturePaths;
    qint64 m_frames = 0;
};

}  // namespace qiyaa
