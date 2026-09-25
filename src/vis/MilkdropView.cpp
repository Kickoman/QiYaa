#include "vis/MilkdropView.h"

#include <algorithm>
#include <array>
#include <utility>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

#include <projectM-4/projectM.h>

#include "audio/AudioEngine.h"

namespace qiyaa {

namespace {
constexpr uint32_t kMaxFramesPerFeed = 4096;
constexpr int kProbeW = 32, kProbeH = 18;
constexpr int kBlackLevel = 12;  // brightest channel below this (of 255) everywhere = black

QSurfaceFormat viewFormat() {
    // projectM 4 needs OpenGL 3.3 core. macOS only gives core profiles when asked.
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(0);
    f.setStencilBufferSize(0);
    f.setAlphaBufferSize(0);  // an opaque window: projectM's output alpha means nothing
    f.setSwapInterval(1);
    return f;
}
}  // namespace

QString MilkdropView::openGLProblem() {
    static const QString problem = [] {
        QOpenGLContext probe;
        probe.setFormat(viewFormat());
        if (!probe.create()) return QStringLiteral("нет OpenGL");
        const QSurfaceFormat f = probe.format();
        // projectM is built for desktop OpenGL.
        if (probe.isOpenGLES()) return QStringLiteral("есть только OpenGL ES, а нужен OpenGL 3.3");
        if (f.majorVersion() * 10 + f.minorVersion() < 33)
            return QStringLiteral("нужен OpenGL 3.3, а доступен %1.%2").arg(f.majorVersion()).arg(f.minorVersion());
        return QString();
    }();
    return problem;
}

MilkdropView::MilkdropView(audio::AudioEngine* engine)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate), m_engine(engine), m_pcm(kMaxFramesPerFeed * 2) {
    setFormat(viewFormat());
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}

MilkdropView::~MilkdropView() {
    destroyProjectM();
}

void MilkdropView::destroyProjectM() {
    if (!m_pm) return;
    // projectM owns GL objects: free them with our context current.
    if (context()) makeCurrent();
    if (m_probeFbo && QOpenGLContext::currentContext()) {
        QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
        f->glDeleteFramebuffers(1, &m_probeFbo);
        f->glDeleteTextures(1, &m_probeTex);
        m_probeFbo = m_probeTex = 0;
    }
    projectm_destroy(m_pm);
    m_pm = nullptr;
    if (context()) doneCurrent();
}

void MilkdropView::loadPreset(const QByteArray& milk, bool smooth) {
    // Loading touches GL, and projectM may be inside a frame right now (its
    // callbacks come from there): do it at the start of the next frame.
    m_pending = std::make_pair(milk, smooth);
    update();
}

void MilkdropView::setBlackWatch(bool on) {
    if (on == m_blackWatch) return;
    m_blackWatch = on;
    m_blackChecks = 0;
    m_sinceLoad.start();  // judge only what plays from now on
}

void MilkdropView::setBlackWatchTiming(int graceMs, int intervalMs, int checks) {
    m_blackGraceMs = graceMs;
    m_blackIntervalMs = intervalMs;
    m_blackChecksNeeded = checks;
}

void MilkdropView::setPresetDuration(double seconds) {
    m_duration = seconds;
    applySettings();
}

void MilkdropView::setLocked(bool locked) {
    m_locked = locked;
    applySettings();
}

void MilkdropView::setTextureSearchPaths(const QStringList& paths) {
    // Rebuilds projectM's textures (GL work): applied at the next frame, with our context current.
    m_texturePaths = paths;
    m_texturePathsDirty = true;
    update();
}

void MilkdropView::setRendering(bool on, int fps) {
    if (!on) {
        m_timer.stop();
        return;
    }
    m_timer.setInterval(1000 / std::max(1, fps));
    if (!m_timer.isActive()) {
        m_visCursor = m_engine->visCursor();  // don't replay what played while we were off
        m_timer.start();
    }
}

void MilkdropView::applySettings() {
    // Plain values, no GL: safe whatever context is current.
    if (!m_pm) return;
    projectm_set_preset_duration(m_pm, m_duration);
    projectm_set_preset_locked(m_pm, m_locked);
}

void MilkdropView::applyTexturePaths() {
    // Needs our context current (paintGL / initializeGL).
    std::vector<QByteArray> utf8;
    std::vector<const char*> ptrs;
    for (const QString& p : m_texturePaths) utf8.push_back(p.toUtf8());
    for (const QByteArray& p : utf8) ptrs.push_back(p.constData());
    projectm_set_texture_search_paths(m_pm, ptrs.data(), ptrs.size());
    m_texturePathsDirty = false;
}

void MilkdropView::syncWindowSize() {
    // Device pixels: a move to a screen with another scale factor changes them
    // without a resize.
    const QSize px = size() * devicePixelRatio();
    if (px == m_pixelSize) return;
    m_pixelSize = px;
    projectm_set_window_size(m_pm, size_t(px.width()), size_t(px.height()));
}

void MilkdropView::initializeGL() {
    QOpenGLContext* ctx = context();
    const QSurfaceFormat f = ctx ? ctx->format() : QSurfaceFormat();
    if (!ctx || !ctx->isValid() || QOpenGLContext::currentContext() != ctx) {
        m_failure = QStringLiteral("нет OpenGL");
    } else if (ctx->isOpenGLES()) {
        m_failure = QStringLiteral("есть только OpenGL ES, а нужен OpenGL 3.3");
    } else if (f.majorVersion() * 10 + f.minorVersion() < 33) {
        m_failure = QStringLiteral("нужен OpenGL 3.3, а доступен %1.%2").arg(f.majorVersion()).arg(f.minorVersion());
    } else {
        m_pm = projectm_create();
        if (!m_pm) m_failure = QStringLiteral("projectM не запустился");
    }
    if (!m_pm) {
        m_timer.stop();
        Q_EMIT failed(m_failure);
        return;
    }
    m_pixelSize = {};
    syncWindowSize();
    projectm_set_aspect_correction(m_pm, true);
    projectm_set_soft_cut_duration(m_pm, 3.0);
    projectm_set_fps(m_pm, 60);

    // Both callbacks fire inside projectM calls: hand them to the event loop.
    projectm_set_preset_switch_requested_event_callback(
        m_pm,
        [](bool hardCut, void* self) {
            auto* view = static_cast<MilkdropView*>(self);
            QMetaObject::invokeMethod(view, [view, hardCut] { Q_EMIT view->switchRequested(hardCut); }, Qt::QueuedConnection);
        },
        this);
    projectm_set_preset_switch_failed_event_callback(
        m_pm,
        [](const char*, const char* message, void* self) {
            auto* view = static_cast<MilkdropView*>(self);
            const QString msg = QString::fromUtf8(message);
            QMetaObject::invokeMethod(view, [view, msg] { Q_EMIT view->presetFailed(msg); }, Qt::QueuedConnection);
        },
        this);
    applySettings();
    applyTexturePaths();
    m_visCursor = m_engine->visCursor();
    {
        QOpenGLFunctions* gl = ctx->functions();
        auto str = [gl](GLenum e) { return QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(e))); };
        m_glInfo = str(GL_VERSION) + QStringLiteral(" | ") + str(GL_RENDERER);
        qInfo("Milkdrop: OpenGL %s", qPrintable(m_glInfo));
    }
    m_sinceLoad.start();
    m_sinceCheck.start();
    Q_EMIT ready();
}

void MilkdropView::resizeGL(int, int) {
    if (m_pm) syncWindowSize();
}

void MilkdropView::paintGL() {
    if (!m_pm) {
        if (QOpenGLContext* ctx = QOpenGLContext::currentContext(); ctx && ctx == context()) {
            ctx->functions()->glClearColor(0, 0, 0, 1);
            ctx->functions()->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }
    syncWindowSize();
    if (m_texturePathsDirty) applyTexturePaths();
    if (m_pending) {
        const auto [milk, smooth] = *std::exchange(m_pending, std::nullopt);
        projectm_load_preset_data(m_pm, milk.constData(), smooth);
        m_sinceLoad.start();
        m_blackChecks = 0;
        m_sawPicture = false;
    }
    // Only the samples that played since the last frame.
    const uint32_t n = m_engine->readNewVisSamples(&m_visCursor, m_pcm.data(), kMaxFramesPerFeed);
    if (n > 0) projectm_pcm_add_float(m_pm, m_pcm.data(), n, PROJECTM_STEREO);
    projectm_opengl_render_frame(m_pm);
    makeOpaque();
    ++m_frames;
    watchForBlack();
}

void MilkdropView::makeOpaque() {
    // projectM leaves whatever alpha a preset produced in the window, often 0.
    // If the system gave the window an alpha channel anyway (an ARGB visual on
    // X11/XWayland, a translucent surface elsewhere), those pixels would be
    // composited as see-through, i.e. black over our frame. Set alpha to 1.
    QOpenGLFunctions* gl = context()->functions();
    gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    gl->glClearColor(0, 0, 0, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void MilkdropView::watchForBlack() {
    if (!m_blackWatch || m_sinceLoad.elapsed() < m_blackGraceMs || m_sinceCheck.elapsed() < m_blackIntervalMs) return;
    m_sinceCheck.start();
    if (!pictureIsBlack()) {
        m_blackChecks = 0;
        if (!std::exchange(m_sawPicture, true))
            QMetaObject::invokeMethod(this, [this] { Q_EMIT drawsPicture(); }, Qt::QueuedConnection);
        return;
    }
    if (++m_blackChecks < m_blackChecksNeeded) return;
    m_blackChecks = 0;
    m_sinceLoad.start();  // one report per stretch
    QMetaObject::invokeMethod(this, [this] { Q_EMIT staysBlack(); }, Qt::QueuedConnection);
}

bool MilkdropView::pictureIsBlack() {
    // Scale the frame just drawn (framebuffer 0, before the swap) into a tiny
    // target on the GPU and read that back: one small readback per second.
    QOpenGLExtraFunctions* f = context()->extraFunctions();
    if (!m_probeFbo) {
        f->glGenTextures(1, &m_probeTex);
        f->glBindTexture(GL_TEXTURE_2D, m_probeTex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kProbeW, kProbeH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glGenFramebuffers(1, &m_probeFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_probeFbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_probeTex, 0);
    }
    const GLuint screen = defaultFramebufferObject();
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, screen);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_probeFbo);
    f->glBlitFramebuffer(0, 0, m_pixelSize.width(), m_pixelSize.height(), 0, 0, kProbeW, kProbeH, GL_COLOR_BUFFER_BIT,
                         GL_LINEAR);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_probeFbo);
    std::array<quint8, kProbeW * kProbeH * 4> px{};
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    f->glReadPixels(0, 0, kProbeW, kProbeH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    f->glBindFramebuffer(GL_FRAMEBUFFER, screen);
    for (size_t i = 0; i < px.size(); i += 4)
        if (std::max({px[i], px[i + 1], px[i + 2]}) >= kBlackLevel) return false;
    return true;
}

void MilkdropView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) Q_EMIT doubleClicked();
}

void MilkdropView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) Q_EMIT contextMenuRequested(e->globalPosition().toPoint());
}

void MilkdropView::keyPressEvent(QKeyEvent* e) {
    Q_EMIT keyPressed(e->key(), e->modifiers());
}

}  // namespace qiyaa
