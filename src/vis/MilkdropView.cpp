#include "vis/MilkdropView.h"

#include <algorithm>
#include <utility>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <projectM-4/projectM.h>

#include "audio/AudioEngine.h"

namespace qiyaa {

namespace {
constexpr uint32_t kMaxFramesPerFeed = 4096;

QSurfaceFormat viewFormat() {
    // projectM 4 needs OpenGL 3.3 core. macOS only gives core profiles when asked.
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(0);
    f.setStencilBufferSize(0);
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
        if (!probe.isOpenGLES() && f.majorVersion() * 10 + f.minorVersion() < 33)
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

void MilkdropView::setPresetDuration(double seconds) {
    m_duration = seconds;
    applySettings();
}

void MilkdropView::setLocked(bool locked) {
    m_locked = locked;
    applySettings();
}

void MilkdropView::setTextureSearchPaths(const QStringList& paths) {
    m_texturePaths = paths;
    applySettings();
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
    if (!m_pm) return;
    projectm_set_preset_duration(m_pm, m_duration);
    projectm_set_preset_locked(m_pm, m_locked);
    std::vector<QByteArray> utf8;
    std::vector<const char*> ptrs;
    for (const QString& p : m_texturePaths) utf8.push_back(p.toUtf8());
    for (const QByteArray& p : utf8) ptrs.push_back(p.constData());
    projectm_set_texture_search_paths(m_pm, ptrs.data(), ptrs.size());
}

void MilkdropView::initializeGL() {
    QOpenGLContext* ctx = context();
    const QSurfaceFormat f = ctx ? ctx->format() : QSurfaceFormat();
    if (!ctx || !ctx->isValid() || QOpenGLContext::currentContext() != ctx) {
        m_failure = QStringLiteral("нет OpenGL");
    } else if (f.majorVersion() * 10 + f.minorVersion() < 33 && !ctx->isOpenGLES()) {
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
    const qreal dpr = devicePixelRatio();
    projectm_set_window_size(m_pm, size_t(width() * dpr), size_t(height() * dpr));
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
    m_visCursor = m_engine->visCursor();
    Q_EMIT ready();
}

void MilkdropView::resizeGL(int, int) {
    if (!m_pm) return;
    const qreal dpr = devicePixelRatio();
    projectm_set_window_size(m_pm, size_t(width() * dpr), size_t(height() * dpr));
}

void MilkdropView::paintGL() {
    if (!m_pm) {
        if (QOpenGLContext* ctx = QOpenGLContext::currentContext(); ctx && ctx == context()) {
            ctx->functions()->glClearColor(0, 0, 0, 1);
            ctx->functions()->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }
    if (m_pending) {
        const auto [milk, smooth] = *std::exchange(m_pending, std::nullopt);
        projectm_load_preset_data(m_pm, milk.constData(), smooth);
    }
    // Only the samples that played since the last frame.
    const uint32_t n = m_engine->readNewVisSamples(&m_visCursor, m_pcm.data(), kMaxFramesPerFeed);
    if (n > 0) projectm_pcm_add_float(m_pm, m_pcm.data(), n, PROJECTM_STEREO);
    projectm_opengl_render_frame(m_pm);
    ++m_frames;
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
