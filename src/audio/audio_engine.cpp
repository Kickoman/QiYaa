#include "audio/audio_engine.h"

#include "audio/equalizer.h"
#include "audio/vis_tap.h"

#include <QStringList>
#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Audio {

namespace {

constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kRingSeconds = 2;

}  // namespace

class StreamBuffer {
public:
    void append(const char* data, size_t n) {
        {
            std::lock_guard lock(mutex);
            bytes.insert(bytes.end(), data, data + n);
        }
        wakeUp.notify_all();
    }
    void finish(bool failed) {
        {
            std::lock_guard lock(mutex);
            isFinished = true;
            endedWithError = failed;
        }
        wakeUp.notify_all();
    }
    void interrupt() {
        {
            std::lock_guard lock(mutex);
            interrupted = true;
        }
        wakeUp.notify_all();
    }
    void resume() {
        std::lock_guard lock(mutex);
        interrupted = false;
    }
    void rewind() {
        std::lock_guard lock(mutex);
        cursorRow = 0;
    }
    size_t size() const {
        std::lock_guard lock(mutex);
        return bytes.size();
    }
    bool finished() const {
        std::lock_guard lock(mutex);
        return isFinished;
    }

    ma_result read(void* out, size_t n, size_t* got) {
        std::unique_lock lock(mutex);
        wakeUp.wait(lock, [&] { return interrupted || isFinished || bytes.size() > cursorRow; });
        *got = 0;
        if (interrupted) {
            return MA_CANCELLED;
        }
        const size_t avail = bytes.size() - std::min(cursorRow, bytes.size());
        const size_t take = std::min(n, avail);
        if (take == 0) {
            return MA_AT_END;
        }
        std::memcpy(out, bytes.data() + cursorRow, take);
        cursorRow += take;
        *got = take;
        return MA_SUCCESS;
    }

    ma_result seek(ma_int64 offset, ma_seek_origin origin) {
        std::unique_lock lock(mutex);
        ma_int64 target = 0;
        switch (origin) {
            case ma_seek_origin_start: target = offset; break;
            case ma_seek_origin_current: target = ma_int64(cursorRow) + offset; break;
            case ma_seek_origin_end:
                // Size is unknown until the download completes; don't stall streaming for it.
                if (!isFinished) {
                    return MA_BAD_SEEK;
                }
                target = ma_int64(bytes.size()) + offset;
                break;
        }
        if (target < 0) {
            return MA_BAD_SEEK;
        }
        wakeUp.wait(lock, [&] {
            return interrupted || isFinished || ma_int64(bytes.size()) >= target;
        });
        if (interrupted) {
            return MA_CANCELLED;
        }
        if (target > ma_int64(bytes.size())) {
            return MA_BAD_SEEK;
        }
        cursorRow = size_t(target);
        return MA_SUCCESS;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable wakeUp;
    std::vector<char> bytes;
    size_t cursorRow = 0;
    bool isFinished = false;
    bool endedWithError = false;
    bool interrupted = false;
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

    std::atomic<bool> outputEnabled{false};
    std::atomic<float> gainL{1.0f};
    std::atomic<float> gainR{1.0f};
    std::atomic<ma_uint64> framesPlayed{0};
    EqualizerDsp eq;
    VisTap vis;
    std::atomic<ma_int64> frameOffset{0};

    std::atomic<bool> decoderStarted{false};
    std::atomic<bool> decoderDone{false};
    std::atomic<bool> decoderFailed{false};
    std::atomic<ma_int64> seekRequest{-1};
    std::atomic<int> seekEpoch{0};
    std::atomic<bool> finishedReported{false};

    std::atomic<int> decoderEpoch{0};
    std::atomic<int> uiEpoch{0};
    std::atomic<ma_uint64> boundaryFrame{0};
    std::atomic<bool> haltAtBoundary{false};
    std::atomic<int> nextRate{0};
    std::atomic<int> nextChannels{0};
    std::atomic<TStreamId> failedQueued{0};

    // Guarded by queueMutex.
    std::mutex queueMutex;
    std::shared_ptr<StreamBuffer> queued;
    TStreamId queuedId = 0;
    TStreamId chainingId = 0;
    bool dropChaining = false;
    TStreamId chainedId = 0;
    std::vector<std::shared_ptr<StreamBuffer>> decoderHeld;

    static void DataCallback(ma_device* dev, void* out, const void*, ma_uint32 frameCount) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        auto* dst = static_cast<float*>(out);
        ma_uint32 written = 0;
        // While a seek is pending the decoder thread owns the ring (see seek()).
        if (self->outputEnabled.load(std::memory_order_acquire)
            && self->seekRequest.load(std::memory_order_acquire) < 0) {
            ma_uint32 limit = frameCount;
            if (self->haltAtBoundary.load(std::memory_order_acquire)
                && self->decoderEpoch.load(std::memory_order_acquire)
                    > self->uiEpoch.load(std::memory_order_acquire)) {
                const ma_uint64 played = self->framesPlayed.load(std::memory_order_relaxed);
                const ma_uint64 boundary = self->boundaryFrame.load(std::memory_order_acquire);
                limit = played >= boundary
                    ? 0
                    : ma_uint32(std::min<ma_uint64>(frameCount, boundary - played));
            }
            while (written < limit) {
                ma_uint32 n = limit - written;
                void* src = nullptr;
                if (ma_pcm_rb_acquire_read(&self->ring, &n, &src) != MA_SUCCESS || n == 0) {
                    break;
                }
                std::memcpy(dst + written * kChannels, src, n * kChannels * sizeof(float));
                ma_pcm_rb_commit_read(&self->ring, n);
                written += n;
            }
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
        if (written < frameCount) {
            std::memset(
                dst + written * kChannels, 0, (frameCount - written) * kChannels * sizeof(float)
            );
        }
    }

    static ma_result OnRead(ma_decoder* dec, void* out, size_t n, size_t* got) {
        return static_cast<StreamBuffer*>(dec->pUserData)->read(out, n, got);
    }
    static ma_result OnSeek(ma_decoder* dec, ma_int64 off, ma_seek_origin origin) {
        return static_cast<StreamBuffer*>(dec->pUserData)->seek(off, origin);
    }

    // Heap-allocated: ma_decoder must not move.
    struct Source {
        std::shared_ptr<StreamBuffer> buf;
        ma_decoder dec{};
        int rate = 0;
        int channels = 0;
        bool initialised = false;
        ~Source() {
            if (initialised) {
                ma_decoder_uninit(&dec);
            }
        }
    };

    std::unique_ptr<Source> openSource(std::shared_ptr<StreamBuffer> buf) {
        auto s = std::make_unique<Source>();
        s->buf = std::move(buf);
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, sampleRate);
        cfg.encodingFormat = ma_encoding_format_mp3;
        ma_result r = ma_decoder_init(&Impl::OnRead, &Impl::OnSeek, s->buf.get(), &cfg, &s->dec);
        if (r != MA_SUCCESS && !stopDecoder) {
            s->buf->seek(0, ma_seek_origin_start);
            cfg.encodingFormat = ma_encoding_format_unknown;
            r = ma_decoder_init(&Impl::OnRead, &Impl::OnSeek, s->buf.get(), &cfg, &s->dec);
        }
        if (r != MA_SUCCESS) {
            return nullptr;
        }
        s->initialised = true;
        ma_format fmt;
        ma_uint32 ch = 0, rate = 0;
        if (ma_data_source_get_data_format(s->dec.pBackend, &fmt, &ch, &rate, nullptr, 0)
            == MA_SUCCESS) {
            s->rate = int(rate);
            s->channels = int(ch);
        }
        return s;
    }

    void decoderMain(
        std::shared_ptr<StreamBuffer> first,
        std::atomic<int>* srcRate,
        std::atomic<int>* srcChannels
    ) {
        std::unique_ptr<Source> cur = openSource(std::move(first));
        if (!cur) {
            if (!stopDecoder) {
                decoderFailed = true;
            }
            return;
        }
        *srcRate = cur->rate;
        *srcChannels = cur->channels;
        decoderStarted = true;

        std::unique_ptr<Source> tail;
        int epoch = 0;
        ma_uint64 written = 0;  // frames since the last ring reset, the origin of framesPlayed

        // Call with queueMutex held.
        auto publishHeld = [&] {
            decoderHeld.clear();
            if (cur) {
                decoderHeld.push_back(cur->buf);
            }
            if (tail) {
                decoderHeld.push_back(tail->buf);
            }
        };
        {
            std::lock_guard lock(queueMutex);
            publishHeld();
        }

        auto tryChain = [&]() -> bool {
            std::shared_ptr<StreamBuffer> buf;
            TStreamId id = 0;
            {
                std::lock_guard lock(queueMutex);
                if (!queued || !queued->finished() || finishedReported) {
                    return false;
                }
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
                    // Already reported finished: hand it back for playQueuedNow().
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
                // seekRequest >= 0: the callback leaves the ring to this thread.
                if (tail && seekEpoch.load(std::memory_order_acquire) < epoch) {
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
                // May block until that part is downloaded.
                ma_decoder_seek_to_pcm_frame(&cur->dec, ma_uint64(target));
                if (stopDecoder) {
                    break;
                }
                ma_pcm_rb_reset(&ring);
                frameOffset = target;
                framesPlayed = 0;
                written = 0;
                decoderDone = false;
                // Fails if a newer seek arrived meanwhile; the loop then serves that one.
                seekRequest.compare_exchange_strong(target, -1, std::memory_order_acq_rel);
                continue;
            }
            if (tail && uiEpoch.load(std::memory_order_acquire) >= epoch) {
                tail.reset();
                std::lock_guard lock(queueMutex);
                publishHeld();
            }
            if (decoderDone) {
                if (!tail && !finishedReported && tryChain()) {
                    decoderDone = false;
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
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
                if (ma_pcm_rb_acquire_write(&ring, &n, &dst) != MA_SUCCESS || n == 0) {
                    break;
                }
                std::memcpy(dst, p, n * kChannels * sizeof(float));
                ma_pcm_rb_commit_write(&ring, n);
                p += n * kChannels;
                remaining -= n;
                written += n;
            }
            if (r == MA_AT_END || (r != MA_SUCCESS && got == 0)) {
                if (tail || !tryChain()) {
                    decoderDone = true;
                }
            }
        }
    }

    void stopDecoderThread(const QHash<TStreamId, std::shared_ptr<StreamBuffer>>& streams) {
        stopDecoder = true;
        std::vector<std::shared_ptr<StreamBuffer>> held;
        {
            std::lock_guard lock(queueMutex);
            held = decoderHeld;
        }
        for (const auto& s : streams) {
            s->interrupt();
        }
        for (const auto& s : held) {
            s->interrupt();
        }
        if (decoderThread.joinable()) {
            decoderThread.join();
        }
        stopDecoder = false;
        for (const auto& s : held) {
            s->resume();
        }
        for (const auto& s : streams) {
            s->resume();
        }
        std::lock_guard lock(queueMutex);
        queued.reset();
        queuedId = chainingId = chainedId = 0;
        dropChaining = false;
        haltAtBoundary = false;
        decoderHeld.clear();
    }
};

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>()) {
    updateGains();
}

AudioEngine::~AudioEngine() {
    d->outputEnabled = false;
    d->stopDecoderThread(streams);
    if (d->deviceReady) {
        ma_device_uninit(&d->device);
    }
    if (d->contextReady) {
        ma_context_uninit(&d->context);
    }
    if (d->ringReady) {
        ma_pcm_rb_uninit(&d->ring);
    }
}

AudioEngine::InitResult AudioEngine::init() {
    if (d->deviceReady) {
        return {true, {}};
    }
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.sampleRate = 0;  // device native
    cfg.dataCallback = &Impl::DataCallback;
    cfg.pUserData = d.get();
    cfg.performanceProfile = ma_performance_profile_conservative;
#if defined(__linux__) || defined(__FreeBSD__)
    // No JACK: it spams stderr when no JACK server runs.
    std::vector<ma_backend> candidates = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_null};
#elif defined(_WIN32)
    std::vector<ma_backend> candidates = {
        ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_null
    };
#else
    std::vector<ma_backend> candidates = {ma_backend_coreaudio, ma_backend_null};
#endif
    if (QString want = qEnvironmentVariable("QIYAA_AUDIO_BACKEND").remove(QLatin1Char(' '));
        !want.isEmpty()) {
        bool known = false;
        for (int i = 0; i < MA_BACKEND_COUNT && !known; ++i) {
            const auto b = ma_backend(i);
            if (QString::fromLatin1(ma_get_backend_name(b))
                    .remove(QLatin1Char(' '))
                    .compare(want, Qt::CaseInsensitive)
                == 0) {
                candidates = {b};
                known = true;
            }
        }
        if (!known) {
            qWarning(
                "QIYAA_AUDIO_BACKEND=%s: no such audio backend, using the default ones",
                qPrintable(want)
            );
        }
    }
    // A context is bound to one backend, so try them one by one until a device opens.
    bool opened = false;
    for (ma_backend backend : candidates) {
        if (ma_context_init(&backend, 1, nullptr, &d->context) != MA_SUCCESS) {
            continue;
        }
        if (ma_device_init(&d->context, &cfg, &d->device) == MA_SUCCESS) {
            d->contextReady = true;
            opened = true;
            break;
        }
        ma_context_uninit(&d->context);
    }
    if (!opened) {
        QStringList tried;
        for (ma_backend backend : candidates) {
            tried << QString::fromLatin1(ma_get_backend_name(backend));
        }
        return {
            false, QStringLiteral("no audio output device opens (tried %1)").arg(tried.join(u", "))
        };
    }
    d->deviceReady = true;
    d->sampleRate = d->device.sampleRate;
    d->eq.setSampleRate(d->sampleRate);
    if (ma_pcm_rb_init(
            ma_format_f32, kChannels, d->sampleRate * kRingSeconds, nullptr, nullptr, &d->ring
        )
        != MA_SUCCESS) {
        return {
            false,
            QStringLiteral("cannot allocate a %1-second ring buffer at %2 Hz")
                .arg(kRingSeconds)
                .arg(d->sampleRate)
        };
    }
    d->ringReady = true;
    return {true, {}};
}

QString AudioEngine::backendName() const {
    if (!d->deviceReady) {
        return QStringLiteral("none");
    }
    return QString::fromLatin1(ma_get_backend_name(d->device.pContext->backend));
}

void AudioEngine::dropStreams() {
    d->outputEnabled.store(false, std::memory_order_release);
    d->stopDecoderThread(streams);
    streams.clear();
    current = queued = 0;
}

void AudioEngine::startDecoder() {
    // Stopping the device guarantees the callback isn't inside the ring right now.
    if (d->deviceReady && ma_device_is_started(&d->device)) {
        ma_device_stop(&d->device);
    }
    ma_pcm_rb_reset(&d->ring);
    if (d->deviceReady) {
        ma_device_start(&d->device);
    }

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
    sourceRate = 0;
    sourceChannelCount = 0;

    d->decoderThread = std::thread(
        &Impl::decoderMain, d.get(), streams.value(current), &sourceRate, &sourceChannelCount
    );
    d->outputEnabled.store(true, std::memory_order_release);
    setState(State::Buffering);
}

AudioEngine::TStreamId AudioEngine::beginStream() {
    dropStreams();
    if (!d->ringReady) {
        setState(State::Stopped);
        Q_EMIT errorOccurred(QStringLiteral("no audio output device"));
        return 0;
    }
    current = ++lastId;
    streams.insert(current, std::make_shared<StreamBuffer>());
    startDecoder();
    return current;
}

AudioEngine::TStreamId AudioEngine::queueStream() {
    clearQueued();
    if (!d->decoderThread.joinable()) {
        return 0;
    }
    queued = ++lastId;
    auto buf = std::make_shared<StreamBuffer>();
    streams.insert(queued, buf);
    std::lock_guard lock(d->queueMutex);
    d->queued = std::move(buf);
    d->queuedId = queued;
    return queued;
}

void AudioEngine::clearQueued() {
    const TStreamId id = std::exchange(queued, 0);
    if (!id) {
        return;
    }
    std::shared_ptr<StreamBuffer> buf = streams.take(id);
    std::lock_guard lock(d->queueMutex);
    if (d->queuedId == id) {
        d->queued.reset();
        d->queuedId = 0;
    } else if (d->chainingId == id) {
        d->dropChaining = true;
        if (buf) {
            buf->interrupt();
        }
    } else if (d->chainedId == id) {
        d->haltAtBoundary = true;
        if (buf) {
            buf->interrupt();
        }
    }
}

AudioEngine::TStreamId AudioEngine::queuedStream() const {
    return queued && d->failedQueued.load() != queued ? queued : 0;
}

AudioEngine::TStreamId AudioEngine::playQueuedNow() {
    const TStreamId id = queuedStream();
    if (!id) {
        return 0;
    }
    std::shared_ptr<StreamBuffer> buf = streams.value(id);
    d->outputEnabled.store(false, std::memory_order_release);
    d->stopDecoderThread(streams);
    streams.clear();
    buf->rewind();
    streams.insert(id, buf);
    current = id;
    queued = 0;
    startDecoder();
    return id;
}

void AudioEngine::appendData(TStreamId stream, const QByteArray& bytes) {
    if (const auto buf = streams.value(stream)) {
        buf->append(bytes.constData(), size_t(bytes.size()));
    }
}

void AudioEngine::finishData(TStreamId stream) {
    if (const auto buf = streams.value(stream)) {
        buf->finish(false);
    }
}

void AudioEngine::failData(TStreamId stream) {
    if (const auto buf = streams.value(stream)) {
        buf->finish(true);
    }
}

void AudioEngine::pause() {
    if (currentState != State::Playing && currentState != State::Buffering) {
        return;
    }
    if (d->deviceReady) {
        ma_device_stop(&d->device);
    }
    setState(State::Paused);
}

void AudioEngine::resume() {
    if (currentState != State::Paused) {
        return;
    }
    if (d->deviceReady) {
        ma_device_start(&d->device);
    }
    setState(d->decoderStarted ? State::Playing : State::Buffering);
}

void AudioEngine::stop() {
    dropStreams();
    if (d->deviceReady && ma_device_is_started(&d->device)) {
        ma_device_stop(&d->device);
    }
    d->framesPlayed = 0;
    d->frameOffset = 0;
    setState(State::Stopped);
}

bool AudioEngine::seek(double seconds) {
    if (!d->decoderThread.joinable() || !d->decoderStarted || seconds < 0) {
        return false;
    }
    // ma_device_stop() waits for a running callback; after the restart the callback sees
    // seekRequest >= 0 and leaves the ring alone until the decoder thread clears it.
    const bool wasRunning = d->deviceReady && ma_device_is_started(&d->device);
    if (wasRunning) {
        ma_device_stop(&d->device);
    }
    // The track the UI shows; the decoder may already be in the next one.
    d->seekEpoch.store(d->uiEpoch.load(), std::memory_order_release);
    d->seekRequest.store(ma_int64(seconds * d->sampleRate), std::memory_order_release);
    if (wasRunning) {
        ma_device_start(&d->device);
    }
    return true;
}

double AudioEngine::positionSeconds() const {
    if (d->sampleRate == 0) {
        return 0;
    }
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
    volumePercent = std::clamp(percent, 0, 100);
    updateGains();
}

void AudioEngine::setBalance(int balance) {
    balancePercent = std::clamp(balance, -100, 100);
    updateGains();
}

void AudioEngine::updateGains() {
    const float v = float(volumePercent) / 100.0f;
    const float g = v * v;
    const float b = float(balancePercent) / 100.0f;
    d->gainL = g * (b > 0 ? 1.0f - b : 1.0f);
    d->gainR = g * (b < 0 ? 1.0f + b : 1.0f);
}

void AudioEngine::poll() {
    if (d->decoderFailed.exchange(false)) {
        const std::shared_ptr<StreamBuffer> buffer = streams.value(current);
        const qulonglong received = buffer ? buffer->size() : 0;
        stop();
        Q_EMIT errorOccurred(
            QStringLiteral("cannot decode the audio stream: %1 bytes received, not mp3, flac or wav"
            )
                .arg(received)
        );
        return;
    }
    if (const TStreamId bad = d->failedQueued.exchange(0); bad && bad == queued) {
        streams.remove(bad);
        queued = 0;
    }
    const int chained = d->decoderEpoch.load(std::memory_order_acquire);
    if (chained > d->uiEpoch.load() && d->seekRequest.load() < 0 && !d->finishedReported
        && d->framesPlayed.load() >= d->boundaryFrame.load(std::memory_order_acquire)) {
        if (d->haltAtBoundary) {
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
        streams.remove(current);
        current = std::exchange(queued, 0);
        sourceRate = d->nextRate.load();
        sourceChannelCount = d->nextChannels.load();
        Q_EMIT trackAdvanced();
        return;
    }
    if (currentState == State::Buffering && d->decoderStarted
        && ma_pcm_rb_available_read(&d->ring) > 0) {
        setState(State::Playing);
    }
    if (currentState == State::Playing && d->decoderDone && ma_pcm_rb_available_read(&d->ring) == 0
        && d->seekRequest.load() < 0 && !d->finishedReported) {
        {
            // Under the lock: a chain in progress re-checks finishedReported before committing.
            std::lock_guard lock(d->queueMutex);
            if (d->decoderEpoch.load() > d->uiEpoch.load()) {
                return;
            }
            d->finishedReported = true;
        }
        Q_EMIT trackFinished();
    }
}

void AudioEngine::setState(State state) {
    if (currentState == state) {
        return;
    }
    currentState = state;
    Q_EMIT stateChanged(state);
}

}  // namespace Audio
