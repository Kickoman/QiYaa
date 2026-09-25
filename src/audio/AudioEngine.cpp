#include "audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <miniaudio.h>

#include "audio/Equalizer.h"
#include "audio/VisTap.h"

namespace qiyaa::audio {

namespace {

constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kRingSeconds = 2;

}  // namespace

// Growing in-memory copy of the file being downloaded. The decoder thread reads
// from it and waits when it gets ahead of the download.
class StreamBuffer {
public:
    void append(const char* data, size_t n) {
        {
            std::lock_guard lock(m_mutex);
            m_data.insert(m_data.end(), data, data + n);
        }
        m_cv.notify_all();
    }
    void finish(bool failed) {
        {
            std::lock_guard lock(m_mutex);
            m_finished = true;
            m_failed = failed;
        }
        m_cv.notify_all();
    }
    // Makes a blocked (or the next) read return MA_CANCELLED until resume().
    void interrupt() {
        {
            std::lock_guard lock(m_mutex);
            m_interrupted = true;
        }
        m_cv.notify_all();
    }
    void resume() {
        std::lock_guard lock(m_mutex);
        m_interrupted = false;
    }
    void rewind() {
        std::lock_guard lock(m_mutex);
        m_cursor = 0;
    }
    // Completely downloaded (successfully or not): reading it never waits.
    bool finished() const {
        std::lock_guard lock(m_mutex);
        return m_finished;
    }

    // Blocking read at the cursor. Returns MA_AT_END at the end of a finished stream.
    ma_result read(void* out, size_t n, size_t* got) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_interrupted || m_finished || m_data.size() > m_cursor; });
        *got = 0;
        if (m_interrupted) return MA_CANCELLED;
        const size_t avail = m_data.size() - std::min(m_cursor, m_data.size());
        const size_t take = std::min(n, avail);
        if (take == 0) return MA_AT_END;
        std::memcpy(out, m_data.data() + m_cursor, take);
        m_cursor += take;
        *got = take;
        return MA_SUCCESS;
    }

    ma_result seek(ma_int64 offset, ma_seek_origin origin) {
        std::unique_lock lock(m_mutex);
        ma_int64 target = 0;
        switch (origin) {
        case ma_seek_origin_start: target = offset; break;
        case ma_seek_origin_current: target = ma_int64(m_cursor) + offset; break;
        case ma_seek_origin_end:
            // Size is unknown until the download completes; don't stall streaming for it.
            if (!m_finished) return MA_BAD_SEEK;
            target = ma_int64(m_data.size()) + offset;
            break;
        }
        if (target < 0) return MA_BAD_SEEK;
        // Seeking forward past downloaded data: wait for it (only happens on user seek).
        m_cv.wait(lock, [&] { return m_interrupted || m_finished || ma_int64(m_data.size()) >= target; });
        if (m_interrupted) return MA_CANCELLED;
        if (target > ma_int64(m_data.size())) return MA_BAD_SEEK;
        m_cursor = size_t(target);
        return MA_SUCCESS;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<char> m_data;
    size_t m_cursor = 0;
    bool m_finished = false;
    bool m_failed = false;
    bool m_interrupted = false;
};

struct AudioEngine::Impl {
    ma_context context{};
    bool contextReady = false;
    ma_device device{};
    bool deviceReady = false;
    ma_uint32 sampleRate = 44100;
    ma_pcm_rb ring{};
    bool ringReady = false;

    std::thread decoderThread;
    std::atomic<bool> stopDecoder{false};

    // Shared with the audio callback.
    std::atomic<bool> outputEnabled{false};  // false -> callback outputs silence, doesn't touch ring
    std::atomic<float> gainL{1.0f};
    std::atomic<float> gainR{1.0f};
    std::atomic<ma_uint64> framesPlayed{0};   // read from the ring since the last reset
    EqualizerDsp eq;
    VisTap vis;
    std::atomic<ma_int64> frameOffset{0};     // track position of framesPlayed == 0

    // Decoder -> UI.
    std::atomic<bool> decoderStarted{false};
    std::atomic<bool> decoderDone{false};
    std::atomic<bool> decoderFailed{false};
    std::atomic<ma_int64> seekRequest{-1};
    std::atomic<int> seekEpoch{0};            // the track (epoch) the UI meant
    std::atomic<bool> finishedReported{false};

    // Gapless chaining. Each track in one run of the decoder thread is an
    // "epoch". The decoder moves to epoch n+1 when it chains the queued stream;
    // the UI follows once playback reaches boundaryFrame.
    std::atomic<int> decoderEpoch{0};
    std::atomic<int> uiEpoch{0};
    std::atomic<ma_uint64> boundaryFrame{0};  // framesPlayed value where decoderEpoch starts
    std::atomic<bool> haltAtBoundary{false};  // the chained stream was cancelled: stop there
    std::atomic<int> nextRate{0};
    std::atomic<int> nextChannels{0};
    std::atomic<StreamId> failedQueued{0};    // queued stream the decoder couldn't open

    // Handoff of the queued stream (guarded by queueMutex).
    std::mutex queueMutex;
    std::shared_ptr<StreamBuffer> queued;     // waiting for the current track to end
    StreamId queuedId = 0;
    StreamId chainingId = 0;                  // being opened right now
    bool dropChaining = false;
    StreamId chainedId = 0;                   // decoding, boundary not reached yet
    // Every buffer the decoder thread may be reading, so stopping it can
    // always wake it up (even for streams the UI has already dropped).
    std::vector<std::shared_ptr<StreamBuffer>> decoderHeld;

    static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* dst = static_cast<float*>(out);
        ma_uint32 written = 0;
        // A pending seek owns the ring: the decoder may reset it at any moment
        // until it clears seekRequest, so don't touch it (see seek()).
        if (self->outputEnabled.load(std::memory_order_acquire) && self->seekRequest.load(std::memory_order_acquire) < 0) {
            ma_uint32 limit = frameCount;
            // A cancelled chained track: play up to the boundary, not into it.
            if (self->haltAtBoundary.load(std::memory_order_acquire) &&
                self->decoderEpoch.load(std::memory_order_acquire) > self->uiEpoch.load(std::memory_order_acquire)) {
                const ma_uint64 played = self->framesPlayed.load(std::memory_order_relaxed);
                const ma_uint64 boundary = self->boundaryFrame.load(std::memory_order_acquire);
                limit = played >= boundary ? 0 : ma_uint32(std::min<ma_uint64>(frameCount, boundary - played));
            }
            while (written < limit) {
                ma_uint32 n = limit - written;
                void* src = nullptr;
                if (ma_pcm_rb_acquire_read(&self->ring, &n, &src) != MA_SUCCESS || n == 0) break;
                std::memcpy(dst + written * kChannels, src, n * kChannels * sizeof(float));
                ma_pcm_rb_commit_read(&self->ring, n);
                written += n;
            }
            // DSP chain: EQ -> visualization tap -> volume/balance.
            self->eq.process(dst, written);
            self->vis.write(dst, written);
            const float gl = self->gainL.load(std::memory_order_relaxed);
            const float gr = self->gainR.load(std::memory_order_relaxed);
            for (ma_uint32 i = 0; i < written; ++i) {
                dst[i * 2] = std::clamp(dst[i * 2] * gl, -1.0f, 1.0f);
                dst[i * 2 + 1] = std::clamp(dst[i * 2 + 1] * gr, -1.0f, 1.0f);
            }
            self->framesPlayed.fetch_add(written, std::memory_order_relaxed);
        }
        if (written < frameCount)
            std::memset(dst + written * kChannels, 0, (frameCount - written) * kChannels * sizeof(float));
    }

    static ma_result onRead(ma_decoder* dec, void* out, size_t n, size_t* got) {
        return static_cast<StreamBuffer*>(dec->pUserData)->read(out, n, got);
    }
    static ma_result onSeek(ma_decoder* dec, ma_int64 off, ma_seek_origin origin) {
        return static_cast<StreamBuffer*>(dec->pUserData)->seek(off, origin);
    }

    // A decoder over one stream. Heap-allocated: ma_decoder must not move.
    struct Source {
        std::shared_ptr<StreamBuffer> buf;
        ma_decoder dec{};
        int rate = 0;
        int channels = 0;
        bool initialised = false;
        ~Source() {
            if (initialised) ma_decoder_uninit(&dec);
        }
    };

    std::unique_ptr<Source> openSource(std::shared_ptr<StreamBuffer> buf) {
        auto s = std::make_unique<Source>();
        s->buf = std::move(buf);
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, sampleRate);
        cfg.encodingFormat = ma_encoding_format_mp3;
        ma_result r = ma_decoder_init(&Impl::onRead, &Impl::onSeek, s->buf.get(), &cfg, &s->dec);
        if (r != MA_SUCCESS && !stopDecoder) {
            s->buf->seek(0, ma_seek_origin_start);
            cfg.encodingFormat = ma_encoding_format_unknown;
            r = ma_decoder_init(&Impl::onRead, &Impl::onSeek, s->buf.get(), &cfg, &s->dec);
        }
        if (r != MA_SUCCESS) return nullptr;
        s->initialised = true;
        ma_format fmt;
        ma_uint32 ch = 0, rate = 0;
        if (ma_data_source_get_data_format(s->dec.pBackend, &fmt, &ch, &rate, nullptr, 0) == MA_SUCCESS) {
            s->rate = int(rate);
            s->channels = int(ch);
        }
        return s;
    }

    void decoderMain(std::shared_ptr<StreamBuffer> first, std::atomic<int>* srcRate, std::atomic<int>* srcChannels) {
        std::unique_ptr<Source> cur = openSource(std::move(first));
        if (!cur) {
            if (!stopDecoder) decoderFailed = true;
            return;
        }
        *srcRate = cur->rate;
        *srcChannels = cur->channels;
        decoderStarted = true;

        std::unique_ptr<Source> tail;  // the previous track, until the UI moves past the boundary
        int epoch = 0;
        ma_uint64 written = 0;         // frames written to the ring since its last reset

        // Buffers this thread reads; call with queueMutex held.
        auto publishHeld = [&] {
            decoderHeld.clear();
            if (cur) decoderHeld.push_back(cur->buf);
            if (tail) decoderHeld.push_back(tail->buf);
        };
        {
            std::lock_guard lock(queueMutex);
            publishHeld();
        }

        // Continue with the queued stream after the current one. Only once it
        // is completely downloaded: then opening and decoding it never wait for
        // the network (an ID3 tag with a big cover alone can exceed any
        // "enough to start" guess), and a queued stream that isn't ready by the
        // end of the track is simply started by the player the usual way.
        auto tryChain = [&]() -> bool {
            std::shared_ptr<StreamBuffer> buf;
            StreamId id = 0;
            {
                std::lock_guard lock(queueMutex);
                if (!queued || !queued->finished() || finishedReported) return false;
                buf = std::move(queued);
                id = std::exchange(queuedId, 0);
                chainingId = id;
                dropChaining = false;
                decoderHeld.push_back(buf);
            }
            std::unique_ptr<Source> next = openSource(buf);
            std::lock_guard lock(queueMutex);
            chainingId = 0;
            if (!next || dropChaining || stopDecoder || finishedReported) {
                if (!next && !dropChaining && !stopDecoder) {
                    failedQueued = id;
                } else if (next && finishedReported && !dropChaining && !stopDecoder) {
                    // The track already ended for the UI: it starts this one itself.
                    buf->rewind();
                    queued = buf;
                    queuedId = id;
                }
                dropChaining = false;
                publishHeld();
                return false;
            }
            chainedId = id;
            tail = std::move(cur);
            cur = std::move(next);
            ++epoch;
            nextRate = cur->rate;
            nextChannels = cur->channels;
            boundaryFrame.store(written, std::memory_order_release);
            decoderEpoch.store(epoch, std::memory_order_release);
            publishHeld();
            return true;
        };

        std::vector<float> chunk(1024 * kChannels);
        while (!stopDecoder) {
            if (ma_int64 target = seekRequest.load(std::memory_order_acquire); target >= 0) {
                // While seekRequest >= 0 the callback doesn't read the ring, so it's ours.
                if (tail && seekEpoch.load(std::memory_order_acquire) < epoch) {
                    // The UI still plays the previous track: undo the chain and
                    // queue that stream again (unless it was cancelled meanwhile).
                    std::lock_guard lock(queueMutex);
                    if (!haltAtBoundary) {
                        cur->buf->rewind();
                        queued = cur->buf;
                        queuedId = chainedId;
                    }
                    chainedId = 0;
                    haltAtBoundary = false;
                    cur = std::move(tail);
                    --epoch;
                    decoderEpoch.store(epoch, std::memory_order_release);
                    publishHeld();
                } else if (tail) {
                    tail.reset();
                    std::lock_guard lock(queueMutex);
                    publishHeld();
                }
                // The seek itself may block until that part is downloaded.
                ma_decoder_seek_to_pcm_frame(&cur->dec, ma_uint64(target));
                if (stopDecoder) break;  // stream abandoned mid-seek: leave the ring alone
                ma_pcm_rb_reset(&ring);
                frameOffset = target;
                framesPlayed = 0;
                written = 0;
                decoderDone = false;
                // Hand the ring back only if no newer seek arrived meanwhile;
                // otherwise loop and serve the newer one.
                seekRequest.compare_exchange_strong(target, -1, std::memory_order_acq_rel);
                continue;
            }
            // The UI has moved into the new track: the old decoder can go.
            if (tail && uiEpoch.load(std::memory_order_acquire) >= epoch) {
                tail.reset();
                std::lock_guard lock(queueMutex);
                publishHeld();
            }
            if (decoderDone) {
                // The queued stream may arrive late; chain it while there's still
                // something in the ring (after that, the UI starts it itself).
                if (!tail && !finishedReported && tryChain()) {
                    decoderDone = false;
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            // Wait for room in the ring.
            ma_uint32 space = ma_pcm_rb_available_write(&ring);
            if (space < 1024) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            ma_uint64 got = 0;
            const ma_result r = ma_decoder_read_pcm_frames(&cur->dec, chunk.data(), 1024, &got);
            ma_uint32 remaining = ma_uint32(got);
            const float* p = chunk.data();
            while (remaining > 0) {
                ma_uint32 n = remaining;
                void* dst = nullptr;
                if (ma_pcm_rb_acquire_write(&ring, &n, &dst) != MA_SUCCESS || n == 0) break;
                std::memcpy(dst, p, n * kChannels * sizeof(float));
                ma_pcm_rb_commit_write(&ring, n);
                p += n * kChannels;
                remaining -= n;
                written += n;
            }
            if (r == MA_AT_END || (r != MA_SUCCESS && got == 0)) {
                if (tail || !tryChain()) decoderDone = true;
            }
        }
    }

    // Stops the decoder thread; afterwards nothing reads any stream.
    void stopDecoderThread(const QHash<StreamId, std::shared_ptr<StreamBuffer>>& streams) {
        stopDecoder = true;
        std::vector<std::shared_ptr<StreamBuffer>> held;
        {
            std::lock_guard lock(queueMutex);
            held = decoderHeld;
        }
        for (const auto& s : streams) s->interrupt();
        for (const auto& s : held) s->interrupt();
        if (decoderThread.joinable()) decoderThread.join();
        stopDecoder = false;
        for (const auto& s : held) s->resume();
        for (const auto& s : streams) s->resume();
        std::lock_guard lock(queueMutex);
        queued.reset();
        queuedId = chainingId = chainedId = 0;
        dropChaining = false;
        haltAtBoundary = false;
        decoderHeld.clear();
    }
};

AudioEngine::AudioEngine(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {
    updateGains();
}

AudioEngine::~AudioEngine() {
    d->outputEnabled = false;
    d->stopDecoderThread(m_streams);
    if (d->deviceReady) ma_device_uninit(&d->device);
    if (d->contextReady) ma_context_uninit(&d->context);
    if (d->ringReady) ma_pcm_rb_uninit(&d->ring);
}

bool AudioEngine::init(QString* error) {
    if (d->deviceReady) return true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.sampleRate = 0;  // device native
    cfg.dataCallback = &Impl::dataCallback;
    cfg.pUserData = d.get();
    cfg.performanceProfile = ma_performance_profile_conservative;  // bigger periods, less CPU
#if defined(__linux__) || defined(__FreeBSD__)
    // PipeWire/PulseAudio first, then ALSA. Skip JACK: it spams stderr when no
    // JACK server runs, and desktop users route through PipeWire anyway.
    std::vector<ma_backend> candidates = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_null};
#elif defined(_WIN32)
    std::vector<ma_backend> candidates = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_null};
#else
    std::vector<ma_backend> candidates = {ma_backend_coreaudio, ma_backend_null};
#endif
    // QIYAA_AUDIO_BACKEND picks one backend by its miniaudio name: "null" (no
    // sound, but real-time; the tests use it), "alsa", "pulseaudio", "wasapi"...
    if (QString want = qEnvironmentVariable("QIYAA_AUDIO_BACKEND").remove(QLatin1Char(' ')); !want.isEmpty()) {
        bool known = false;
        for (int i = 0; i < MA_BACKEND_COUNT && !known; ++i) {
            const auto b = ma_backend(i);
            if (QString::fromLatin1(ma_get_backend_name(b)).remove(QLatin1Char(' ')).compare(want, Qt::CaseInsensitive) == 0) {
                candidates = {b};
                known = true;
            }
        }
        if (!known) qWarning("QIYAA_AUDIO_BACKEND=%s: no such audio backend, using the default ones", qPrintable(want));
    }
    // A context is bound to one backend, so try them one by one until a device opens.
    bool opened = false;
    for (ma_backend backend : candidates) {
        if (ma_context_init(&backend, 1, nullptr, &d->context) != MA_SUCCESS) continue;
        if (ma_device_init(&d->context, &cfg, &d->device) == MA_SUCCESS) {
            d->contextReady = true;
            opened = true;
            break;
        }
        ma_context_uninit(&d->context);
    }
    if (!opened) {
        if (error) *error = QStringLiteral("cannot open audio output device");
        return false;
    }
    d->deviceReady = true;
    d->sampleRate = d->device.sampleRate;
    d->eq.setSampleRate(d->sampleRate);
    if (ma_pcm_rb_init(ma_format_f32, kChannels, d->sampleRate * kRingSeconds, nullptr, nullptr, &d->ring) != MA_SUCCESS) {
        if (error) *error = QStringLiteral("cannot allocate ring buffer");
        return false;
    }
    d->ringReady = true;
    return true;  // the device is started by beginStream()
}

QString AudioEngine::backendName() const {
    if (!d->deviceReady) return QStringLiteral("none");
    return QString::fromLatin1(ma_get_backend_name(d->device.pContext->backend));
}

void AudioEngine::dropStreams() {
    d->outputEnabled.store(false, std::memory_order_release);
    d->stopDecoderThread(m_streams);
    m_streams.clear();
    m_current = m_queued = 0;
}

void AudioEngine::startDecoder() {
    // Stopping the device guarantees the callback isn't inside the ring right now.
    if (d->deviceReady && ma_device_is_started(&d->device)) ma_device_stop(&d->device);
    ma_pcm_rb_reset(&d->ring);
    if (d->deviceReady) ma_device_start(&d->device);

    d->framesPlayed = 0;
    d->frameOffset = 0;
    d->decoderStarted = false;
    d->decoderDone = false;
    d->decoderFailed = false;
    d->finishedReported = false;
    d->seekRequest = -1;
    d->decoderEpoch = 0;
    d->uiEpoch = 0;
    d->failedQueued = 0;
    m_sourceRate = 0;
    m_sourceChannels = 0;

    d->decoderThread = std::thread(&Impl::decoderMain, d.get(), m_streams.value(m_current), &m_sourceRate, &m_sourceChannels);
    d->outputEnabled.store(true, std::memory_order_release);
    setState(State::Buffering);
}

AudioEngine::StreamId AudioEngine::beginStream() {
    dropStreams();
    if (!d->ringReady) {  // no output device: nothing can play
        setState(State::Stopped);
        Q_EMIT errorOccurred(QStringLiteral("no audio output device"));
        return 0;
    }
    m_current = ++m_lastId;
    m_streams.insert(m_current, std::make_shared<StreamBuffer>());
    startDecoder();
    return m_current;
}

AudioEngine::StreamId AudioEngine::queueStream() {
    clearQueued();
    if (!d->decoderThread.joinable()) return 0;
    m_queued = ++m_lastId;
    auto buf = std::make_shared<StreamBuffer>();
    m_streams.insert(m_queued, buf);
    std::lock_guard lock(d->queueMutex);
    d->queued = std::move(buf);
    d->queuedId = m_queued;
    return m_queued;
}

void AudioEngine::clearQueued() {
    const StreamId id = std::exchange(m_queued, 0);
    if (!id) return;
    std::shared_ptr<StreamBuffer> buf = m_streams.take(id);
    std::lock_guard lock(d->queueMutex);
    if (d->queuedId == id) {
        d->queued.reset();
        d->queuedId = 0;
    } else if (d->chainingId == id) {
        d->dropChaining = true;
        if (buf) buf->interrupt();  // don't wait for its data
    } else if (d->chainedId == id) {
        d->haltAtBoundary = true;   // already decoding into the ring: stop playback at its start
        if (buf) buf->interrupt();  // and never wait for more of its data
    }
}

AudioEngine::StreamId AudioEngine::queuedStream() const {
    return m_queued && d->failedQueued.load() != m_queued ? m_queued : 0;
}

AudioEngine::StreamId AudioEngine::playQueuedNow() {
    const StreamId id = queuedStream();
    if (!id) return 0;
    std::shared_ptr<StreamBuffer> buf = m_streams.value(id);
    d->outputEnabled.store(false, std::memory_order_release);
    d->stopDecoderThread(m_streams);
    m_streams.clear();
    buf->rewind();
    m_streams.insert(id, buf);
    m_current = id;
    m_queued = 0;
    startDecoder();
    return id;
}

void AudioEngine::appendData(StreamId stream, const QByteArray& bytes) {
    if (const auto buf = m_streams.value(stream)) buf->append(bytes.constData(), size_t(bytes.size()));
}

void AudioEngine::finishData(StreamId stream) {
    if (const auto buf = m_streams.value(stream)) buf->finish(false);
}

void AudioEngine::failData(StreamId stream) {
    if (const auto buf = m_streams.value(stream)) buf->finish(true);
}

void AudioEngine::pause() {
    if (m_state != State::Playing && m_state != State::Buffering) return;
    if (d->deviceReady) ma_device_stop(&d->device);
    setState(State::Paused);
}

void AudioEngine::resume() {
    if (m_state != State::Paused) return;
    if (d->deviceReady) ma_device_start(&d->device);
    setState(d->decoderStarted ? State::Playing : State::Buffering);
}

void AudioEngine::stop() {
    dropStreams();
    // Idle = no audio callbacks at all (CPU ~0 when nothing plays).
    if (d->deviceReady && ma_device_is_started(&d->device)) ma_device_stop(&d->device);
    d->framesPlayed = 0;
    d->frameOffset = 0;
    setState(State::Stopped);
}

bool AudioEngine::seek(double seconds) {
    if (!d->decoderThread.joinable() || !d->decoderStarted || seconds < 0) return false;
    // Stopping the device waits for any running callback to finish; from then
    // on the callback sees seekRequest >= 0 and leaves the ring to the decoder
    // thread, which clears the request once the ring holds the new position.
    const bool wasRunning = d->deviceReady && ma_device_is_started(&d->device);
    if (wasRunning) ma_device_stop(&d->device);
    // In the current track as the UI sees it (the decoder may already be in the next one).
    d->seekEpoch.store(d->uiEpoch.load(), std::memory_order_release);
    d->seekRequest.store(ma_int64(seconds * d->sampleRate), std::memory_order_release);
    if (wasRunning) ma_device_start(&d->device);
    return true;
}

double AudioEngine::positionSeconds() const {
    if (d->sampleRate == 0) return 0;
    const ma_int64 frames = d->frameOffset.load() + ma_int64(d->framesPlayed.load());
    return double(std::max<ma_int64>(0, frames)) / d->sampleRate;
}

void AudioEngine::setEqualizer(const EqSettings& settings) {
    d->eq.publish(settings);
}

void AudioEngine::readVisSamples(float* left, float* right, uint32_t count) const {
    d->vis.read(left, right, std::min<uint32_t>(count, VisTap::kSize));
}

uint32_t AudioEngine::readNewVisSamples(uint32_t* cursor, float* stereo, uint32_t maxFrames) const {
    return d->vis.readNew(cursor, stereo, maxFrames);
}

uint32_t AudioEngine::visCursor() const {
    return d->vis.position();
}

int AudioEngine::outputSampleRate() const {
    return int(d->sampleRate);
}

void AudioEngine::setVolume(int percent) {
    m_volume = std::clamp(percent, 0, 100);
    updateGains();
}

void AudioEngine::setBalance(int balance) {
    m_balance = std::clamp(balance, -100, 100);
    updateGains();
}

void AudioEngine::updateGains() {
    // Perceptual-ish volume curve; balance attenuates the opposite channel.
    const float v = float(m_volume) / 100.0f;
    const float g = v * v;
    const float b = float(m_balance) / 100.0f;
    d->gainL = g * (b > 0 ? 1.0f - b : 1.0f);
    d->gainR = g * (b < 0 ? 1.0f + b : 1.0f);
}

void AudioEngine::poll() {
    if (d->decoderFailed.exchange(false)) {
        stop();
        Q_EMIT errorOccurred(QStringLiteral("cannot decode audio stream"));
        return;
    }
    // The queued stream turned out undecodable: forget it (the UI starts the next track itself).
    if (const StreamId bad = d->failedQueued.exchange(0); bad && bad == m_queued) {
        m_streams.remove(bad);
        m_queued = 0;
    }
    // Playback crossed into the chained track?
    const int chained = d->decoderEpoch.load(std::memory_order_acquire);
    if (chained > d->uiEpoch.load() && d->seekRequest.load() < 0 && !d->finishedReported &&
        d->framesPlayed.load() >= d->boundaryFrame.load(std::memory_order_acquire)) {
        if (d->haltAtBoundary) {
            // Cancelled: the callback stopped at the boundary; this track is over.
            {
                std::lock_guard lock(d->queueMutex);
                d->finishedReported = true;
            }
            Q_EMIT trackFinished();
            return;
        }
        {
            std::lock_guard lock(d->queueMutex);
            d->chainedId = 0;
        }
        d->frameOffset = -ma_int64(d->boundaryFrame.load());
        d->uiEpoch.store(chained, std::memory_order_release);
        m_streams.remove(m_current);
        m_current = std::exchange(m_queued, 0);
        m_sourceRate = d->nextRate.load();
        m_sourceChannels = d->nextChannels.load();
        Q_EMIT trackAdvanced();
        return;
    }
    if (m_state == State::Buffering && d->decoderStarted && ma_pcm_rb_available_read(&d->ring) > 0)
        setState(State::Playing);
    if (m_state == State::Playing && d->decoderDone && ma_pcm_rb_available_read(&d->ring) == 0 &&
        d->seekRequest.load() < 0 && !d->finishedReported) {
        {
            // Under the lock: a chain in progress re-checks it before committing.
            std::lock_guard lock(d->queueMutex);
            if (d->decoderEpoch.load() > d->uiEpoch.load()) return;  // chained after all: advance next poll
            d->finishedReported = true;
        }
        Q_EMIT trackFinished();
    }
}

void AudioEngine::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

}  // namespace qiyaa::audio
