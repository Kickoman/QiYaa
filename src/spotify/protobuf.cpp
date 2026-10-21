#include "spotify/protobuf.h"

namespace Spotify {

ProtoWriter& ProtoWriter::varint(int field, std::uint64_t value) {
    tag(field, ProtoWire::Varint);
    raw(value);
    return *this;
}

ProtoWriter& ProtoWriter::bytes(int field, ByteView value) {
    tag(field, ProtoWire::Bytes);
    raw(value.size);
    buffer.insert(buffer.end(), value.data, value.data + value.size);
    return *this;
}

ProtoWriter& ProtoWriter::string(int field, std::string_view value) {
    return bytes(field, ByteView::Of(value));
}

ProtoWriter& ProtoWriter::message(int field, const ProtoWriter& value) {
    return bytes(field, value.data());
}

void ProtoWriter::tag(int field, ProtoWire type) {
    raw(static_cast<std::uint64_t>(field) << 3 | static_cast<std::uint64_t>(type));
}

void ProtoWriter::raw(std::uint64_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<std::uint8_t>(value));
}

ProtoReader::ProtoReader(ByteView data)
    : input(data) { }

bool ProtoReader::next() {
    if (error || position >= input.size) {
        return false;
    }
    std::uint64_t key = 0;
    if (!readVarint(key) || key >> 3 == 0 || key >> 3 > 0x1fffffff) {
        error = true;
        return false;
    }
    number = static_cast<int>(key >> 3);
    wire = static_cast<ProtoWire>(key & 7);
    value = 0;
    payload = {};
    switch (wire) {
        case ProtoWire::Varint:
            if (!readVarint(value)) {
                error = true;
            }
            break;
        case ProtoWire::Fixed64:
        case ProtoWire::Fixed32: {
            const std::size_t width = wire == ProtoWire::Fixed64 ? 8 : 4;
            if (input.size - position < width) {
                error = true;
                break;
            }
            payload = input.sub(position, width);
            for (std::size_t i = width; i > 0; --i) {
                value = value << 8 | input.data[position + i - 1];
            }
            position += width;
            break;
        }
        case ProtoWire::Bytes: {
            std::uint64_t length = 0;
            if (!readVarint(length) || length > input.size - position) {
                error = true;
                break;
            }
            payload = input.sub(position, static_cast<std::size_t>(length));
            position += static_cast<std::size_t>(length);
            break;
        }
        default: error = true; break;
    }
    return !error;
}

std::int64_t ProtoReader::zigzag() const {
    return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
}

std::string ProtoReader::string() const {
    return std::string(reinterpret_cast<const char*>(payload.data), payload.size);
}

bool ProtoReader::readVarint(std::uint64_t& out) {
    out = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (position >= input.size) {
            return false;
        }
        const std::uint8_t byte = input.data[position++];
        out |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace Spotify
