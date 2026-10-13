#include "audio/audio_engine.h"

#include "audio/blocks.h"
#include "audio/decoder.h"
#include "audio/equalizer.h"
#include "audio/stream_buffer.h"
#include "audio/vis_tap.h"

#include <QStringList>
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace Audio {

namespace {

constexpr ma_uint32 kChannels = kBlockChannels;
constexpr uint32_t kQueueSeconds = 2;

static_assert(
    std::atomic<uint32_t>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free
    && std::atomic<float>::is_always_lock_free
);

struct Format {
    int rate = 0;
    int channels = 0;
};

}  // namespace

struct AudioEngine::Implementation {
    ma_context context{};
    bool contextReady = false;
    ma_device device{};
    bool deviceReady = false;
    ma_uint32 sampleRate = 44'100;

    std::unique_ptr<BlockQueue> blocks;
    std::unique_ptr<Decoder> decoder;
    PlaybackState playback;

    std::atomic<uint32_t> generation{0};
    std::atomic<uint64_t> allowedNext{0};
    std::atomic<float> gainLeft{1.0f};
    std::atomic<float> gainRight{1.0f};
    EqualizerDsp eq;
    VisTap visTap;

    uint64_t frontIndex = ~uint64_t{0};
    uint32_t frontOffset = 0;
    PlaybackSnapshot playing;

    int64_t shownFrame = 0;
    bool finishReported = false;
    std::optional<Format> queuedFormat;

    static void DataCallback(ma_device* outputDevice, void* output, const void*, ma_uint32 frameCount) {
        static_cast<Implementation*>(outputDevice->pUserData)->play(static_cast<float*>(output), frameCount);
    }

    void play(float* output, ma_uint32 frameCount) {
        const uint32_t current = generation.load(std::memory_order_acquire);
        const PlaybackSnapshot before = playing;
        ma_uint32 written = 0;
        while (written < frameCount) {
            const Block* block = blocks->front();
            if (!block) {
                break;
            }
            if (const uint64_t index = blocks->frontIndex(); index != frontIndex) {
                frontIndex = index;
                frontOffset = 0;
            }
            if (Older(block->generation, current)) {
                blocks->pop();
                continue;
            }
            if (block->generation != playing.generation) {
                playing = {block->generation, block->stream, block->frame, false};
            } else if (playing.ended) {
                break;
            } else if (block->stream != playing.stream) {
                if (block->stream != allowedNext.load(std::memory_order_acquire)) {
                    playing.ended = true;
                    break;
                }
                playing.stream = block->stream;
            }
            const uint32_t count = std::min(frameCount - written, block->frameCount - frontOffset);
            std::memcpy(
                output + written * kChannels,
                block->samples.data() + frontOffset * kChannels,
                count * kChannels * sizeof(float)
            );
            written += count;
            frontOffset += count;
            playing.frame = block->frame + frontOffset;
            if (frontOffset >= block->frameCount) {
                playing.ended = block->endOfStream;
                blocks->pop();
                if (playing.ended) {
                    break;
                }
            }
        }
        if (!(playing == before)) {
            playback.publish(playing);
        }
        eq.process({output, written * kChannels});
        visTap.write({output, written * kChannels});
        const float leftGain = gainLeft.load(std::memory_order_relaxed);
        const float rightGain = gainRight.load(std::memory_order_relaxed);
        for (ma_uint32 i = 0; i < written; ++i) {
            output[i * 2] = std::clamp(output[i * 2] * leftGain, -1.0f, 1.0f);
            output[i * 2 + 1] = std::clamp(output[i * 2 + 1] * rightGain, -1.0f, 1.0f);
        }
        if (written < frameCount) {
            std::memset(output + written * kChannels, 0, (frameCount - written) * kChannels * sizeof(float));
        }
    }

    bool deviceRunning() { return deviceReady && ma_device_is_started(&device); }

    void dropStaleBlocks() {
        const uint32_t current = generation.load(std::memory_order_relaxed);
        while (const Block* block = blocks ? blocks->front() : nullptr) {
            if (!Older(block->generation, current)) {
                return;
            }
            blocks->pop();
        }
    }

    bool currentBlockReady() const {
        const Block* block = blocks ? blocks->front() : nullptr;
        return block && block->generation == generation.load(std::memory_order_relaxed);
    }
};

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
    , implementation(std::make_unique<Implementation>()) {
    updateGains();
}

AudioEngine::~AudioEngine() {
    if (implementation->deviceReady) {
        ma_device_uninit(&implementation->device);
    }
    implementation->decoder.reset();
    if (implementation->contextReady) {
        ma_context_uninit(&implementation->context);
    }
}

AudioEngine::InitResult AudioEngine::init() {
    if (implementation->deviceReady) {
        return {true, {}};
    }
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = kChannels;
    deviceConfig.sampleRate = 0;  // device native
    deviceConfig.dataCallback = &Implementation::DataCallback;
    deviceConfig.pUserData = implementation.get();
    deviceConfig.performanceProfile = ma_performance_profile_conservative;
#if defined(__linux__) || defined(__FreeBSD__)
    // No JACK: it spams stderr when no JACK server runs.
    std::vector<ma_backend> candidates = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_null};
#elif defined(_WIN32)
    std::vector<ma_backend> candidates = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_null};
#else
    std::vector<ma_backend> candidates = {ma_backend_coreaudio, ma_backend_null};
#endif
    if (QString requestedBackend = qEnvironmentVariable("QIYAA_AUDIO_BACKEND").remove(QLatin1Char(' '));
        !requestedBackend.isEmpty()) {
        bool known = false;
        for (int i = 0; i < MA_BACKEND_COUNT && !known; ++i) {
            const auto backend = ma_backend(i);
            const QString name = QString::fromLatin1(ma_get_backend_name(backend)).remove(QLatin1Char(' '));
            if (name.compare(requestedBackend, Qt::CaseInsensitive) == 0) {
                candidates = {backend};
                known = true;
            }
        }
        if (!known) {
            qWarning(
                "QIYAA_AUDIO_BACKEND=%s: no such audio backend, using the default ones", qPrintable(requestedBackend)
            );
        }
    }
    // A context is bound to one backend, so try them one by one until a device opens.
    bool opened = false;
    for (ma_backend backend : candidates) {
        if (ma_context_init(&backend, 1, nullptr, &implementation->context) != MA_SUCCESS) {
            continue;
        }
        if (ma_device_init(&implementation->context, &deviceConfig, &implementation->device) == MA_SUCCESS) {
            implementation->contextReady = true;
            opened = true;
            break;
        }
        ma_context_uninit(&implementation->context);
    }
    if (!opened) {
        QStringList tried;
        for (ma_backend backend : candidates) {
            tried << QString::fromLatin1(ma_get_backend_name(backend));
        }
        return {false, QStringLiteral("no audio output device opens (tried %1)").arg(tried.join(u", "))};
    }
    implementation->deviceReady = true;
    const ma_uint32 rate = implementation->device.sampleRate;
    implementation->sampleRate = rate;
    implementation->eq.setSampleRate(rate);
    implementation->blocks = std::make_unique<BlockQueue>((kQueueSeconds * rate + kBlockFrames - 1) / kBlockFrames);
    const uint32_t endWaitFrames = std::max(rate / 10, 2 * implementation->device.playback.internalPeriodSizeInFrames);
    implementation->decoder = std::make_unique<Decoder>(*implementation->blocks, rate, endWaitFrames);
    return {true, {}};
}

QString AudioEngine::backendName() const {
    if (!implementation->deviceReady) {
        return QStringLiteral("none");
    }
    return QString::fromLatin1(ma_get_backend_name(implementation->device.pContext->backend));
}

void AudioEngine::newGeneration(int64_t frame) {
    uint32_t next = implementation->generation.load(std::memory_order_relaxed) + 1;
    if (next == 0) {
        next = 1;
    }
    implementation->generation.store(next, std::memory_order_release);
    implementation->shownFrame = frame;
    implementation->finishReported = false;
}

void AudioEngine::startDevice() {
    if (implementation->deviceReady && !ma_device_is_started(&implementation->device)) {
        implementation->dropStaleBlocks();
        ma_device_start(&implementation->device);
    }
}

AudioEngine::TStreamId AudioEngine::beginStream(double startSeconds) {
    streams.clear();
    currentId = queuedId = 0;
    if (!implementation->decoder) {
        setState(State::Stopped);
        Q_EMIT errorOccurred(QStringLiteral("no audio output device"));
        return 0;
    }
    implementation->allowedNext.store(0, std::memory_order_release);
    implementation->queuedFormat.reset();
    const int64_t startFrame = startSeconds > 0 ? static_cast<int64_t>(startSeconds * implementation->sampleRate) : 0;
    newGeneration(startFrame);
    currentId = ++lastId;
    Decoder* decoder = implementation->decoder.get();
    auto buffer = std::make_shared<StreamBuffer>([decoder] { decoder->wake(); });
    streams.insert(currentId, buffer);
    sourceRate = sourceChannelCount = 0;
    decoder->send(Decoder::Command::Start(currentId, buffer, implementation->generation.load(), startFrame));
    startDevice();
    setState(State::Buffering);
    return currentId;
}

AudioEngine::TStreamId AudioEngine::queueStream() {
    clearQueued();
    if (!currentId) {
        return 0;
    }
    queuedId = ++lastId;
    Decoder* decoder = implementation->decoder.get();
    auto buffer = std::make_shared<StreamBuffer>([decoder] { decoder->wake(); });
    streams.insert(queuedId, buffer);
    implementation->allowedNext.store(queuedId, std::memory_order_release);
    decoder->send(Decoder::Command::Queue(queuedId, buffer));
    return queuedId;
}

void AudioEngine::clearQueued() {
    const TStreamId id = std::exchange(queuedId, 0);
    if (!id) {
        return;
    }
    streams.remove(id);
    implementation->allowedNext.store(0, std::memory_order_release);
    implementation->queuedFormat.reset();
    implementation->decoder->send(Decoder::Command::ClearQueued(id));
}

AudioEngine::TStreamId AudioEngine::queuedStream() const {
    return queuedId;
}

AudioEngine::TStreamId AudioEngine::playQueuedNow() {
    const TStreamId id = queuedId;
    if (!id) {
        return 0;
    }
    const std::shared_ptr<StreamBuffer> buffer = streams.value(id);
    streams.clear();
    streams.insert(id, buffer);
    currentId = id;
    queuedId = 0;
    implementation->allowedNext.store(0, std::memory_order_release);
    implementation->queuedFormat.reset();
    newGeneration(0);
    sourceRate = sourceChannelCount = 0;
    implementation->decoder->send(Decoder::Command::Start(id, buffer, implementation->generation.load(), 0));
    startDevice();
    setState(State::Buffering);
    return id;
}

void AudioEngine::appendData(TStreamId stream, const QByteArray& bytes) {
    if (const auto buffer = streams.value(stream)) {
        buffer->append(bytes.constData(), static_cast<size_t>(bytes.size()));
    }
}

void AudioEngine::finishData(TStreamId stream) {
    if (const auto buffer = streams.value(stream)) {
        buffer->finish();
    }
}

void AudioEngine::failData(TStreamId stream) {
    finishData(stream);
}

void AudioEngine::pause() {
    if (currentState != State::Playing && currentState != State::Buffering) {
        return;
    }
    if (implementation->deviceRunning()) {
        ma_device_stop(&implementation->device);
    }
    implementation->dropStaleBlocks();
    setState(State::Paused);
}

void AudioEngine::resume() {
    if (currentState != State::Paused) {
        return;
    }
    implementation->dropStaleBlocks();
    const bool played = implementation->playback.read().generation == implementation->generation.load();
    const bool ready = played || implementation->currentBlockReady();
    if (implementation->deviceReady) {
        ma_device_start(&implementation->device);
    }
    setState(ready ? State::Playing : State::Buffering);
}

void AudioEngine::stop() {
    if (implementation->decoder) {
        newGeneration(0);
        implementation->decoder->send(Decoder::Command::Stop());
    }
    streams.clear();
    currentId = queuedId = 0;
    implementation->allowedNext.store(0, std::memory_order_release);
    implementation->queuedFormat.reset();
    sourceRate = sourceChannelCount = 0;
    if (implementation->deviceRunning()) {
        ma_device_stop(&implementation->device);
    }
    implementation->dropStaleBlocks();
    setState(State::Stopped);
}

bool AudioEngine::seek(double seconds) {
    if (!currentId || seconds < 0) {
        return false;
    }
    const auto frame = static_cast<int64_t>(seconds * implementation->sampleRate);
    newGeneration(frame);
    implementation->decoder->send(Decoder::Command::Seek(currentId, implementation->generation.load(), frame));
    if (!implementation->deviceRunning()) {
        implementation->dropStaleBlocks();
    }
    return true;
}

double AudioEngine::positionSeconds() const {
    const PlaybackSnapshot snapshot = implementation->playback.read();
    if (currentId && snapshot.generation == implementation->generation.load(std::memory_order_relaxed)
        && snapshot.stream == currentId) {
        implementation->shownFrame = snapshot.frame;
    }
    return static_cast<double>(std::max<int64_t>(0, implementation->shownFrame)) / implementation->sampleRate;
}

void AudioEngine::setEqualizer(const EqSettings& settings) {
    implementation->eq.publish(settings);
}

void AudioEngine::readVisSamples(std::span<float> left, std::span<float> right) const {
    implementation->visTap.read(left, right);
}

VisReadResult AudioEngine::readNewVisSamples(uint32_t cursor, std::span<float> stereo) const {
    return implementation->visTap.readNew(cursor, stereo);
}

uint32_t AudioEngine::visCursor() const {
    return implementation->visTap.position();
}

int AudioEngine::outputSampleRate() const {
    return static_cast<int>(implementation->sampleRate);
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
    const float volume = static_cast<float>(volumePercent) / 100.0f;
    const float gain = volume * volume;
    const float balance = static_cast<float>(balancePercent) / 100.0f;
    implementation->gainLeft = gain * (balance > 0 ? 1.0f - balance : 1.0f);
    implementation->gainRight = gain * (balance < 0 ? 1.0f + balance : 1.0f);
}

void AudioEngine::poll() {
    if (!implementation->decoder) {
        return;
    }
    const PlaybackSnapshot snapshot = implementation->playback.read();
    for (const Decoder::Event& event : implementation->decoder->takeEvents()) {
        if (event.kind == Decoder::Event::Kind::Opened) {
            if (event.stream == currentId) {
                sourceRate = event.rate;
                sourceChannelCount = event.channels;
            } else if (event.stream == queuedId) {
                implementation->queuedFormat = Format{event.rate, event.channels};
            }
        } else if (event.stream == currentId) {
            const TStreamId failedId = currentId;
            const std::shared_ptr<StreamBuffer> buffer = streams.value(failedId);
            const qulonglong received = buffer ? buffer->available() : 0;
            stop();
            Q_EMIT errorOccurred(
                QStringLiteral("cannot decode the audio stream: %1 bytes received, "
                               "not mp3, AAC in MP4, flac or wav")
                    .arg(received)
            );
            Q_EMIT streamUndecodable(failedId);
            return;
        } else if (event.stream == queuedId) {
            streams.remove(queuedId);
            queuedId = 0;
            implementation->allowedNext.store(0, std::memory_order_release);
            implementation->queuedFormat.reset();
        }
    }
    if (!currentId || snapshot.generation != implementation->generation.load(std::memory_order_relaxed)) {
        return;
    }
    if (snapshot.stream == currentId) {
        implementation->shownFrame = snapshot.frame;
        if (currentState == State::Buffering) {
            setState(State::Playing);
        } else if (currentState == State::Playing && snapshot.ended && !implementation->finishReported) {
            implementation->finishReported = true;
            Q_EMIT trackFinished();
        }
        return;
    }
    if (queuedId && snapshot.stream == queuedId) {
        streams.remove(currentId);
        currentId = std::exchange(queuedId, 0);
        implementation->allowedNext.store(0, std::memory_order_release);
        const Format format = implementation->queuedFormat.value_or(Format{});
        implementation->queuedFormat.reset();
        sourceRate = format.rate;
        sourceChannelCount = format.channels;
        implementation->shownFrame = snapshot.frame;
        implementation->finishReported = false;
        implementation->decoder->send(Decoder::Command::Advanced(currentId));
        Q_EMIT trackAdvanced();
        return;
    }
    if (!implementation->finishReported) {
        newGeneration(implementation->shownFrame);
        implementation->finishReported = true;
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
