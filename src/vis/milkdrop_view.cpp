#include "vis/milkdrop_view.h"

#include "audio/audio_engine.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <projectM-4/projectM.h>

#include <algorithm>
#include <array>
#include <utility>

namespace Vis {

namespace {
constexpr uint32_t kMaxFramesPerFeed = 4096;
constexpr int kProbeW = 32, kProbeH = 18;
constexpr int kBlackLevel = 12;  // brightest channel below this (of 255) everywhere = black

QSurfaceFormat ViewFormat() {
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

QString MilkdropView::OpenGlProblem() {
    static const QString problem = [] {
        QOpenGLContext probe;
        probe.setFormat(ViewFormat());
        if (!probe.create()) {
            return QStringLiteral("нет OpenGL");
        }
        const QSurfaceFormat f = probe.format();
        // projectM is built for desktop OpenGL.
        if (probe.isOpenGLES()) {
            return QStringLiteral("есть только OpenGL ES, а нужен OpenGL 3.3");
        }
        if (f.majorVersion() * 10 + f.minorVersion() < 33) {
            return QStringLiteral("нужен OpenGL 3.3, а доступен %1.%2")
                .arg(f.majorVersion())
                .arg(f.minorVersion());
        }
        return QString();
    }();
    return problem;
}

MilkdropView::MilkdropView(Audio::AudioEngine* engine)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate)
    , audioEngine(engine)
    , pcm(kMaxFramesPerFeed * 2) {
    setFormat(ViewFormat());
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this] { update(); });
}

MilkdropView::~MilkdropView() {
    destroyProjectM();
}

void MilkdropView::destroyProjectM() {
    if (!projectM) {
        return;
    }
    // projectM owns GL objects: free them with our context current.
    if (context()) {
        makeCurrent();
    }
    if (probeFbo && QOpenGLContext::currentContext()) {
        QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
        f->glDeleteFramebuffers(1, &probeFbo);
        f->glDeleteTextures(1, &probeTex);
        probeFbo = probeTex = 0;
    }
    projectm_destroy(projectM);
    projectM = nullptr;
    if (context()) {
        doneCurrent();
    }
}

void MilkdropView::loadPreset(const QByteArray& milk, bool smooth) {
    // Loading touches GL, and projectM may be inside a frame right now (its
    // callbacks come from there): do it at the start of the next frame.
    pending = std::make_pair(milk, smooth);
    update();
}

void MilkdropView::setBlackWatch(bool on) {
    if (on == blackWatch) {
        return;
    }
    blackWatch = on;
    blackChecks = 0;
    sinceLoad.start();  // judge only what plays from now on
}

void MilkdropView::captureNextFrame() {
    captureRequested = true;
    update();
}

void MilkdropView::setBlackWatchTiming(int graceMs, int intervalMs, int checks) {
    blackGraceMs = graceMs;
    blackIntervalMs = intervalMs;
    blackChecksNeeded = checks;
}

void MilkdropView::setPresetDuration(double seconds) {
    duration = seconds;
    applySettings();
}

void MilkdropView::setLocked(bool locked) {
    lockEnabled = locked;
    applySettings();
}

void MilkdropView::setTextureSearchPaths(const QStringList& paths) {
    // Rebuilds projectM's textures (GL work): applied at the next frame, with our context current.
    texturePaths = paths;
    texturePathsDirty = true;
    update();
}

void MilkdropView::setRendering(bool on, int fps) {
    if (!on) {
        timer.stop();
        return;
    }
    timer.setInterval(1000 / std::max(1, fps));
    if (!timer.isActive()) {
        visReadCursor = audioEngine->visCursor();  // don't replay what played while we were off
        timer.start();
    }
}

void MilkdropView::applySettings() {
    // Plain values, no GL: safe whatever context is current.
    if (!projectM) {
        return;
    }
    projectm_set_preset_duration(projectM, duration);
    projectm_set_preset_locked(projectM, lockEnabled);
}

void MilkdropView::applyTexturePaths() {
    // Needs our context current (paintGL / initializeGL).
    std::vector<QByteArray> utf8;
    std::vector<const char*> ptrs;
    for (const QString& p : texturePaths) {
        utf8.push_back(p.toUtf8());
    }
    for (const QByteArray& p : utf8) {
        ptrs.push_back(p.constData());
    }
    projectm_set_texture_search_paths(projectM, ptrs.data(), ptrs.size());
    texturePathsDirty = false;
}

void MilkdropView::syncWindowSize() {
    // Device pixels: a move to a screen with another scale factor changes them
    // without a resize.
    const QSize px = size() * devicePixelRatio();
    if (px == pixelSize) {
        return;
    }
    pixelSize = px;
    projectm_set_window_size(projectM, size_t(px.width()), size_t(px.height()));
}

void MilkdropView::initializeGL() {
    QOpenGLContext* ctx = context();
    const QSurfaceFormat f = ctx ? ctx->format() : QSurfaceFormat();
    if (!ctx || !ctx->isValid() || QOpenGLContext::currentContext() != ctx) {
        failureReason = QStringLiteral("нет OpenGL");
    } else if (ctx->isOpenGLES()) {
        failureReason = QStringLiteral("есть только OpenGL ES, а нужен OpenGL 3.3");
    } else if (f.majorVersion() * 10 + f.minorVersion() < 33) {
        failureReason = QStringLiteral("нужен OpenGL 3.3, а доступен %1.%2")
                            .arg(f.majorVersion())
                            .arg(f.minorVersion());
    } else {
        projectM = projectm_create();
        if (!projectM) {
            failureReason = QStringLiteral("projectM не запустился");
        }
    }
    if (!projectM) {
        timer.stop();
        Q_EMIT failed(failureReason);
        return;
    }
    pixelSize = {};
    syncWindowSize();
    projectm_set_aspect_correction(projectM, true);
    projectm_set_soft_cut_duration(projectM, 3.0);
    projectm_set_fps(projectM, 60);

    // Both callbacks fire inside projectM calls: hand them to the event loop.
    projectm_set_preset_switch_requested_event_callback(
        projectM,
        [](bool hardCut, void* self) {
            auto* view = static_cast<MilkdropView*>(self);
            QMetaObject::invokeMethod(
                view, [view, hardCut] { Q_EMIT view->switchRequested(hardCut); },
                Qt::QueuedConnection
            );
        },
        this
    );
    projectm_set_preset_switch_failed_event_callback(
        projectM,
        [](const char*, const char* message, void* self) {
            auto* view = static_cast<MilkdropView*>(self);
            const QString msg = QString::fromUtf8(message);
            QMetaObject::invokeMethod(
                view, [view, msg] { Q_EMIT view->presetFailed(msg); }, Qt::QueuedConnection
            );
        },
        this
    );
    applySettings();
    applyTexturePaths();
    visReadCursor = audioEngine->visCursor();
    {
        QOpenGLFunctions* gl = ctx->functions();
        auto str = [gl](GLenum e) {
            return QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(e)));
        };
        glInfoText = str(GL_VERSION) + QStringLiteral(" | ") + str(GL_RENDERER);
        qInfo("Milkdrop: OpenGL %s", qPrintable(glInfoText));
    }
    sinceLoad.start();
    sinceCheck.start();
    Q_EMIT ready();
}

void MilkdropView::resizeGL(int, int) {
    if (projectM) {
        syncWindowSize();
    }
}

void MilkdropView::paintGL() {
    if (!projectM) {
        if (QOpenGLContext* ctx = QOpenGLContext::currentContext(); ctx && ctx == context()) {
            ctx->functions()->glClearColor(0, 0, 0, 1);
            ctx->functions()->glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }
    syncWindowSize();
    if (texturePathsDirty) {
        applyTexturePaths();
    }
    if (pending) {
        const auto [milk, smooth] = *std::exchange(pending, std::nullopt);
        projectm_load_preset_data(projectM, milk.constData(), smooth);
        sinceLoad.start();
        blackChecks = 0;
        sawPicture = false;
    }
    // Only the samples that played since the last frame.
    const uint32_t n =
        audioEngine->readNewVisSamples(&visReadCursor, pcm.data(), kMaxFramesPerFeed);
    if (n > 0) {
        projectm_pcm_add_float(projectM, pcm.data(), n, PROJECTM_STEREO);
    }
    projectm_opengl_render_frame(projectM);
    makeOpaque();
    ++frameCount;
    watchForBlack();
    if (std::exchange(captureRequested, false)) {
        QImage frame(pixelSize, QImage::Format_RGBA8888);
        QOpenGLFunctions* gl = context()->functions();
        gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        gl->glReadPixels(
            0, 0, frame.width(), frame.height(), GL_RGBA, GL_UNSIGNED_BYTE, frame.bits()
        );
        frame = frame.mirrored();  // OpenGL rows start at the bottom
        QMetaObject::invokeMethod(
            this, [this, frame] { Q_EMIT frameCaptured(frame); }, Qt::QueuedConnection
        );
    }
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
    if (!blackWatch || sinceLoad.elapsed() < blackGraceMs
        || sinceCheck.elapsed() < blackIntervalMs) {
        return;
    }
    sinceCheck.start();
    if (!pictureIsBlack()) {
        blackChecks = 0;
        if (!std::exchange(sawPicture, true)) {
            QMetaObject::invokeMethod(
                this, [this] { Q_EMIT drawsPicture(); }, Qt::QueuedConnection
            );
        }
        return;
    }
    if (++blackChecks < blackChecksNeeded) {
        return;
    }
    blackChecks = 0;
    sinceLoad.start();  // one report per stretch
    QMetaObject::invokeMethod(this, [this] { Q_EMIT staysBlack(); }, Qt::QueuedConnection);
}

bool MilkdropView::pictureIsBlack() {
    // Scale the frame just drawn (framebuffer 0, before the swap) into a tiny
    // target on the GPU and read that back: one small readback per second.
    QOpenGLExtraFunctions* f = context()->extraFunctions();
    if (!probeFbo) {
        f->glGenTextures(1, &probeTex);
        f->glBindTexture(GL_TEXTURE_2D, probeTex);
        f->glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, kProbeW, kProbeH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
        );
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glGenFramebuffers(1, &probeFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
    }
    const GLuint screen = defaultFramebufferObject();
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, screen);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, probeFbo);
    f->glBlitFramebuffer(
        0, 0, pixelSize.width(), pixelSize.height(), 0, 0, kProbeW, kProbeH, GL_COLOR_BUFFER_BIT,
        GL_LINEAR
    );
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, probeFbo);
    std::array<quint8, kProbeW * kProbeH * 4> px{};
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    f->glReadPixels(0, 0, kProbeW, kProbeH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    f->glBindFramebuffer(GL_FRAMEBUFFER, screen);
    for (size_t i = 0; i < px.size(); i += 4) {
        if (std::max({px[i], px[i + 1], px[i + 2]}) >= kBlackLevel) {
            return false;
        }
    }
    return true;
}

void MilkdropView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        Q_EMIT doubleClicked();
    }
}

void MilkdropView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        Q_EMIT contextMenuRequested(event->globalPosition().toPoint());
    }
}

void MilkdropView::keyPressEvent(QKeyEvent* event) {
    Q_EMIT keyPressed(event->key(), event->modifiers());
}

}  // namespace Vis
