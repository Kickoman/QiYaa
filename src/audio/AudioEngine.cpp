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

namespace qiyaa::audio {

namespace {

constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kRingSeconds = 2;

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
    void abort() {
        {
            std::lock_guard lock(m_mutex);
            m_aborted = true;
        }
        m_cv.notify_all();
    }

    // Blocking read at the cursor. Returns MA_AT_END at the end of a finished stream.
    ma_result read(void* out, size_t n, size_t* got) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_aborted || m_finished || m_data.size() > m_cursor; });
        *got = 0;
        if (m_aborted) return MA_CANCELLED;
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
        m_cv.wait(lock, [&] { return m_aborted || m_finished || ma_int64(m_data.size()) >= target; });
        if (m_aborted) return MA_CANCELLED;
        if (target > ma_int64(m_data.size())) return MA_BAD_SEEK;
        m_cursor = size_t(target);
        return MA_SUCCESS;
    }

    size_t size() const {
        std::lock_guard lock(m_mutex);
        return m_data.size();
    }
    bool finished() const {
        std::lock_guard lock(m_mutex);
        return m_finished;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<char> m_data;
    size_t m_cursor = 0;
    bool m_finished = false;
    bool m_failed = false;
    bool m_aborted = false;
};

}  // namespace

struct AudioEngine::Impl {
    ma_context context{};
    bool contextReady = false;
    ma_device device{};
    bool deviceReady = false;
    ma_uint32 sampleRate = 44100;
    ma_pcm_rb ring{};
    bool ringReady = false;

    std::shared_ptr<StreamBuffer> stream;
    std::thread decoderThread;
    std::atomic<bool> stopDecoder{false};

    // Shared with the audio callback.
    std::atomic<bool> outputEnabled{false};  // false -> callback outputs silence, doesn't touch ring
    std::atomic<float> gainL{1.0f};
    std::atomic<float> gainR{1.0f};
    std::atomic<ma_uint64> framesPlayed{0};
    std::atomic<ma_uint64> frameOffset{0};  // position of framesPlayed==0 (after a seek)

    // Decoder -> UI.
    std::atomic<bool> decoderStarted{false};
    std::atomic<bool> decoderDone{false};
    std::atomic<bool> decoderFailed{false};
    std::atomic<ma_int64> seekRequest{-1};
    std::atomic<bool> finishedReported{false};

    static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* dst = static_cast<float*>(out);
        ma_uint32 written = 0;
        if (self->outputEnabled.load(std::memory_order_acquire)) {
            const float gl = self->gainL.load(std::memory_order_relaxed);
            const float gr = self->gainR.load(std::memory_order_relaxed);
            while (written < frameCount) {
                ma_uint32 n = frameCount - written;
                void* src = nullptr;
                if (ma_pcm_rb_acquire_read(&self->ring, &n, &src) != MA_SUCCESS || n == 0) break;
                const float* s = static_cast<const float*>(src);
                float* d = dst + written * kChannels;
                for (ma_uint32 i = 0; i < n; ++i) {
                    d[i * 2] = s[i * 2] * gl;
                    d[i * 2 + 1] = s[i * 2 + 1] * gr;
                }
                ma_pcm_rb_commit_read(&self->ring, n);
                written += n;
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

    void decoderMain(std::shared_ptr<StreamBuffer> buf, std::atomic<int>* srcRate, std::atomic<int>* srcChannels) {
        ma_decoder decoder{};
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, sampleRate);
        cfg.encodingFormat = ma_encoding_format_mp3;
        ma_result r = ma_decoder_init(&Impl::onRead, &Impl::onSeek, buf.get(), &cfg, &decoder);
        if (r != MA_SUCCESS && !stopDecoder) {
            buf->seek(0, ma_seek_origin_start);
            cfg.encodingFormat = ma_encoding_format_unknown;
            r = ma_decoder_init(&Impl::onRead, &Impl::onSeek, buf.get(), &cfg, &decoder);
        }
        if (r != MA_SUCCESS) {
            if (!stopDecoder) decoderFailed = true;
            return;
        }

        ma_format fmt;
        ma_uint32 ch = 0, rate = 0;
        if (ma_data_source_get_data_format(decoder.pBackend, &fmt, &ch, &rate, nullptr, 0) == MA_SUCCESS) {
            *srcRate = int(rate);
            *srcChannels = int(ch);
        }
        decoderStarted = true;

        std::vector<float> chunk(1024 * kChannels);
        while (!stopDecoder) {
            if (const ma_int64 target = seekRequest.exchange(-1); target >= 0) {
                // Caller disabled output; ring is ours to reset.
                ma_decoder_seek_to_pcm_frame(&decoder, ma_uint64(target));
                ma_pcm_rb_reset(&ring);
                frameOffset = ma_uint64(target);
                framesPlayed = 0;
                decoderDone = false;
                outputEnabled.store(true, std::memory_order_release);
            }
            if (decoderDone) {
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
            r = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 1024, &got);
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
            }
            if (r == MA_AT_END || (r != MA_SUCCESS && got == 0)) decoderDone = true;
        }
        ma_decoder_uninit(&decoder);
    }

    void stopDecoderThread() {
        stopDecoder = true;
        if (stream) stream->abort();
        if (decoderThread.joinable()) decoderThread.join();
        stopDecoder = false;
        stream.reset();
    }
};

AudioEngine::AudioEngine(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {
    updateGains();
}

AudioEngine::~AudioEngine() {
    d->outputEnabled = false;
    d->stopDecoderThread();
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
    const ma_backend candidates[] = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_null};
#elif defined(_WIN32)
    const ma_backend candidates[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_null};
#else
    const ma_backend candidates[] = {ma_backend_coreaudio, ma_backend_null};
#endif
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

void AudioEngine::beginStream() {
    d->outputEnabled.store(false, std::memory_order_release);
    d->stopDecoderThread();
    // Stopping the device guarantees the callback isn't inside the ring right now.
    if (d->deviceReady && ma_device_is_started(&d->device)) ma_device_stop(&d->device);
    if (d->ringReady) ma_pcm_rb_reset(&d->ring);
    if (d->deviceReady) ma_device_start(&d->device);

    d->framesPlayed = 0;
    d->frameOffset = 0;
    d->decoderStarted = false;
    d->decoderDone = false;
    d->decoderFailed = false;
    d->finishedReported = false;
    d->seekRequest = -1;
    m_sourceRate = 0;
    m_sourceChannels = 0;

    d->stream = std::make_shared<StreamBuffer>();
    d->decoderThread = std::thread(&Impl::decoderMain, d.get(), d->stream, &m_sourceRate, &m_sourceChannels);
    d->outputEnabled.store(true, std::memory_order_release);
    setState(State::Buffering);
}

void AudioEngine::appendData(const QByteArray& bytes) {
    if (d->stream) d->stream->append(bytes.constData(), size_t(bytes.size()));
}

void AudioEngine::finishData() {
    if (d->stream) d->stream->finish(false);
}

void AudioEngine::failData() {
    if (d->stream) d->stream->finish(true);
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
    d->outputEnabled = false;
    d->stopDecoderThread();
    // Idle = no audio callbacks at all (CPU ~0 when nothing plays).
    if (d->deviceReady && ma_device_is_started(&d->device)) ma_device_stop(&d->device);
    d->framesPlayed = 0;
    d->frameOffset = 0;
    setState(State::Stopped);
}

bool AudioEngine::seek(double seconds) {
    if (!d->stream || !d->decoderStarted || seconds < 0) return false;
    // Stop the device so the callback is guaranteed not to be reading the ring,
    // hand the seek to the decoder thread, and let it re-enable output.
    const bool wasRunning = d->deviceReady && ma_device_is_started(&d->device);
    if (wasRunning) ma_device_stop(&d->device);
    d->outputEnabled = false;
    d->seekRequest = ma_int64(seconds * d->sampleRate);
    if (wasRunning) ma_device_start(&d->device);
    return true;
}

double AudioEngine::positionSeconds() const {
    if (d->sampleRate == 0) return 0;
    return double(d->frameOffset.load() + d->framesPlayed.load()) / d->sampleRate;
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
    if (m_state == State::Buffering && d->decoderStarted && ma_pcm_rb_available_read(&d->ring) > 0)
        setState(State::Playing);
    if (m_state == State::Playing && d->decoderDone && ma_pcm_rb_available_read(&d->ring) == 0 &&
        d->seekRequest.load() < 0 && !d->finishedReported.exchange(true)) {
        Q_EMIT trackFinished();
    }
}

void AudioEngine::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

}  // namespace qiyaa::audio
