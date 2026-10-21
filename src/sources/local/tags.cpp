#include "sources/local/tags.h"

#include <QFile>
#include <QLoggingCategory>
#include <fileref.h>
#include <flacfile.h>
#include <mp4file.h>
#include <mp4properties.h>
#include <mpegfile.h>
#include <mpegproperties.h>
#include <tbytevector.h>
#include <tdebuglistener.h>
#include <tfilestream.h>
#include <tlist.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tvariant.h>
#include <vorbisfile.h>
#include <wavfile.h>

#include <memory>
#include <mutex>

namespace LocalSource {

namespace {

Q_LOGGING_CATEGORY(TagLibLog, "qiyaa.taglib", QtWarningMsg)

class BoundedStream : public TagLib::FileStream {
public:
    using TagLib::FileStream::FileStream;

    TagLib::ByteVector readBlock(size_t length) override {
        if (length > static_cast<size_t>(kMaxReadBytes)) {
            return {};
        }
        return TagLib::FileStream::readBlock(length);
    }
};

class QtDebugListener : public TagLib::DebugListener {
public:
    void printMessage(const TagLib::String& message) override {
        qCDebug(TagLibLog, "%s", qUtf8Printable(QString::fromStdWString(message.toWString()).trimmed()));
    }
};

QString TextOf(const TagLib::String& text) {
    return QString::fromStdWString(text.toWString()).trimmed();
}

bool IsJpegOrPng(const TagLib::ByteVector& data) {
    return data.startsWith(TagLib::ByteVector("\xFF\xD8\xFF", 3))
        || data.startsWith(TagLib::ByteVector("\x89PNG\r\n\x1A\n", 8));
}

QByteArray PictureOf(const TagLib::FileRef& file) {
    const TagLib::List<TagLib::VariantMap> pictures = file.complexProperties("PICTURE");
    std::optional<TagLib::ByteVector> chosen;
    for (const TagLib::VariantMap& picture : pictures) {
        const TagLib::ByteVector data = picture.value("data").toByteVector();
        if (data.size() > static_cast<unsigned int>(kMaxPictureBytes) || !IsJpegOrPng(data)) {
            continue;
        }
        if (picture.value("pictureType").toString() == "Front Cover") {
            chosen = data;
            break;
        }
        if (!chosen) {
            chosen = data;
        }
    }
    return chosen ? QByteArray(chosen->data(), static_cast<qsizetype>(chosen->size())) : QByteArray();
}

bool Playable(TagLib::File* file) {
    if (const auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        return mp4->audioProperties() && mp4->audioProperties()->codec() == TagLib::MP4::Properties::AAC;
    }
    if (const auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        return !mpeg->audioProperties() || !mpeg->audioProperties()->isADTS();
    }
    return dynamic_cast<TagLib::FLAC::File*>(file) || dynamic_cast<TagLib::RIFF::WAV::File*>(file)
        || dynamic_cast<TagLib::Ogg::Vorbis::File*>(file);
}

std::unique_ptr<BoundedStream> OpenStream(const QString& path) {
    static QtDebugListener listener;
    static std::once_flag listening;
    std::call_once(listening, [] { TagLib::setDebugListener(&listener); });

#ifdef _WIN32
    auto stream = std::make_unique<BoundedStream>(reinterpret_cast<const wchar_t*>(path.utf16()), true);
#else
    const QByteArray encoded = QFile::encodeName(path);
    auto stream = std::make_unique<BoundedStream>(encoded.constData(), true);
#endif
    return stream->isOpen() ? std::move(stream) : nullptr;
}

}  // namespace

std::optional<FileTags> ReadTags(const QString& path) {
    const std::unique_ptr<BoundedStream> stream = OpenStream(path);
    if (!stream) {
        return std::nullopt;
    }
    const TagLib::FileRef file(stream.get(), true, TagLib::AudioProperties::Average);
    if (file.isNull()) {
        return std::nullopt;
    }
    FileTags tags;
    if (const TagLib::Tag* tag = file.tag()) {
        tags.title = TextOf(tag->title());
        tags.album = TextOf(tag->album());
        tags.genre = TextOf(tag->genre());
        tags.year = static_cast<int>(tag->year());
    }
    for (const TagLib::String& artist : file.properties().value("ARTIST")) {
        if (const QString name = TextOf(artist); !name.isEmpty()) {
            tags.artists << name;
        }
    }
    if (const TagLib::AudioProperties* audio = file.audioProperties()) {
        tags.durationMs = audio->lengthInMilliseconds();
    }
    tags.playable = Playable(file.file());
    tags.picture = PictureOf(file);
    return tags;
}

QString ReadTagLyrics(const QString& path) {
    const std::unique_ptr<BoundedStream> stream = OpenStream(path);
    if (!stream) {
        return {};
    }
    const TagLib::FileRef file(stream.get(), false);
    if (file.isNull()) {
        return {};
    }
    const TagLib::PropertyMap properties = file.properties();
    for (const auto& [key, values] : properties) {
        if ((key == "LYRICS" || key.startsWith("LYRICS:")) && !values.isEmpty()) {
            return QString::fromStdWString(values.front().toWString());
        }
    }
    return {};
}

}  // namespace LocalSource
