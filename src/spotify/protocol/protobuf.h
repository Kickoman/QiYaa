#pragma once

#include "spotify/protocol/bytes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace SpotifyProtocol {

enum class ProtoWire : std::uint8_t { Varint = 0, Fixed64 = 1, Bytes = 2, Fixed32 = 5 };

class ProtoWriter {
public:
    ProtoWriter& varint(int field, std::uint64_t value);
    ProtoWriter& bytes(int field, ByteView value);
    ProtoWriter& string(int field, std::string_view value);
    ProtoWriter& message(int field, const ProtoWriter& value);

    const TBytes& data() const { return buffer; }

private:
    void tag(int field, ProtoWire type);
    void raw(std::uint64_t value);

    TBytes buffer;
};

class ProtoReader {
public:
    explicit ProtoReader(ByteView data);

    bool next();
    bool failed() const { return error; }

    int field() const { return number; }
    ProtoWire type() const { return wire; }
    std::uint64_t varint() const { return value; }
    std::int64_t zigzag() const;
    ByteView bytes() const { return payload; }
    std::string string() const;

private:
    bool readVarint(std::uint64_t& out);

    ByteView input;
    std::size_t position = 0;
    int number = 0;
    ProtoWire wire = ProtoWire::Varint;
    std::uint64_t value = 0;
    ByteView payload;
    bool error = false;
};

}  // namespace SpotifyProtocol
