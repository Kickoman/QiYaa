#include "spotify/protocol/audio_file.h"
#include "spotify/protocol/bytes.h"
#include "spotify/protocol/crypto.h"
#include "spotify/protocol/ids.h"
#include "spotify/protocol/login.h"
#include "spotify/protocol/metadata.h"
#include "spotify/protocol/protobuf.h"
#include "spotify/protocol/shannon.h"

#include <QObject>
#include <QTest>

#include <array>
#include <cstring>
#include <optional>
#include <string>

namespace {

using SpotifyProtocol::TBytes;

TBytes Hex(const std::string& hex) {
    return SpotifyProtocol::FromHex(hex).value_or(TBytes{});
}

std::string HexOf(SpotifyProtocol::ByteView bytes) {
    return SpotifyProtocol::ToHex(bytes);
}

const std::string kRsaModulus =
    "b2b1db1fb64016e7dbd118224e634e09efeb0368b00a236a315ebc4c341d41c852996075f0055b5ec124621575"
    "8f9ae3e2ab8994f686cb3eae1896b1606eb89eaf8d034270fcc9e4a2e04c299480c524c4f4c30d1c53e1a4e0a2"
    "dd154b75a4b52deeb9d95c5ca22c3a4e4561450e6facfabdbd30ea919b9ce1317322ee1f1723";
const std::string kRsaSignature =
    "219cd729834f6b39182a7403a7325ba061d85b34558fef39b2ec95ff453b67f74e37ab903499422d8ab1d3e0c"
    "78917748783ed01dcb48c493ddcdc2237cfbc1319d6fbf49981c5f22515ce5968271e3c761a62b245289c741"
    "8b6657f8958f4a7117cdd6768a0f6a1353cfe3a90f04c25ca54e7cb203cc4fb41fe0c5a0cbe8ede";

}  // namespace

class TestSpotifyProtocol : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sha1MatchesTheStandardVector();
    void hmacSha1MatchesRfc2202();
    void aesCtrMatchesSp80038a();
    void aesCtrDecryptsFromAnyOffset();
    void aesCtrCarriesPastTheLowCounterWord();
    void modPowMatchesASmallExample();
    void diffieHellmanAgreesOnTheSharedSecret();
    void rsaSignatureOfAbcVerifies();
    void rsaSignatureOfAnotherTextFails();
    void shannonMatchesTheReferenceImplementation();
    void shannonDecryptsAHeaderAndPayloadSeparately();
    void protobufRoundTripsNestedFields();
    void protobufRejectsATruncatedField();
    void base62RoundTripsATrackId();
    void hashcashSolutionHasTheAskedZeros();
    void trackMetadataPicksOggVorbis160();
    void normalisationIsReadAfterTheFirst144Bytes();
    void oggVorbisFollowsTheQualityThenFallsBackTo160();
    void clientTokenNamesThePlatformItRunsOn();
};

void TestSpotifyProtocol::sha1MatchesTheStandardVector() {
    QCOMPARE(
        HexOf(SpotifyProtocol::Sha1({SpotifyProtocol::ByteView::Of("abc")})), "a9993e364706816aba3e25717850c26c9cd0d89d"
    );
}

void TestSpotifyProtocol::hmacSha1MatchesRfc2202() {
    const TBytes key(20, 0x0b);
    const SpotifyProtocol::TSha1 mac =
        SpotifyProtocol::HmacSha1(key, {SpotifyProtocol::ByteView::Of("Hi "), SpotifyProtocol::ByteView::Of("There")});
    QCOMPARE(HexOf(mac), "b617318655057264e28bc0b6fb378c8ef146be00");
}

void TestSpotifyProtocol::aesCtrMatchesSp80038a() {
    SpotifyProtocol::TAesKey key{};
    std::array<std::uint8_t, 16> counter{};
    const TBytes keyBytes = Hex("2b7e151628aed2a6abf7158809cf4f3c");
    const TBytes counterBytes = Hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    std::copy(keyBytes.begin(), keyBytes.end(), key.begin());
    std::copy(counterBytes.begin(), counterBytes.end(), counter.begin());
    TBytes data = Hex(
        "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710"
    );
    const SpotifyProtocol::AesCtr cipher(key, counter);
    cipher.apply(data.data(), data.size(), 0);
    QCOMPARE(
        HexOf(data),
        "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
        "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee"
    );
}

void TestSpotifyProtocol::aesCtrDecryptsFromAnyOffset() {
    SpotifyProtocol::TAesKey key{};
    key.fill(7);
    TBytes whole(200);
    for (std::size_t i = 0; i < whole.size(); ++i) {
        whole[i] = static_cast<std::uint8_t>(i);
    }
    const TBytes plain = whole;
    const SpotifyProtocol::AudioDecryptor decryptor(key);
    decryptor.apply(whole.data(), whole.size(), 0);
    TBytes piece(plain.begin() + 21, plain.begin() + 21 + 77);
    decryptor.apply(piece.data(), piece.size(), 21);
    QCOMPARE(HexOf(piece), HexOf({whole.data() + 21, 77}));
    TBytes tiny(plain.begin() + 5, plain.begin() + 8);
    decryptor.apply(tiny.data(), tiny.size(), 5);
    QCOMPARE(HexOf(tiny), HexOf({whole.data() + 5, 3}));
}

void TestSpotifyProtocol::aesCtrCarriesPastTheLowCounterWord() {
    SpotifyProtocol::TAesKey key{};
    key.fill(3);
    std::array<std::uint8_t, 16> counter{};
    counter[11] = 0x01;
    counter[12] = 0xff;
    counter[13] = 0xff;
    counter[14] = 0xff;
    counter[15] = 0xfe;
    const SpotifyProtocol::AesCtr cipher(key, counter);
    TBytes run(64, 0);
    cipher.apply(run.data(), run.size(), 0);
    for (std::size_t block = 0; block < 4; ++block) {
        TBytes single(16, 0);
        cipher.apply(single.data(), single.size(), block * 16);
        QCOMPARE(HexOf(single), HexOf({run.data() + block * 16, 16}));
    }
    std::array<std::uint8_t, 16> carried{};
    carried[11] = 0x02;
    const SpotifyProtocol::AesCtr after(key, carried);
    TBytes third(16, 0);
    after.apply(third.data(), third.size(), 0);
    QCOMPARE(HexOf(third), HexOf({run.data() + 32, 16}));
}

void TestSpotifyProtocol::modPowMatchesASmallExample() {
    const std::uint8_t base[] = {4};
    const std::uint8_t exponent[] = {13};
    const std::uint8_t modulus[] = {0x01, 0xf1};
    const std::optional<TBytes> result = SpotifyProtocol::ModPow({base, 1}, {exponent, 1}, {modulus, 2});
    QVERIFY(result.has_value());
    QCOMPARE(HexOf(*result), "01bd");
}

void TestSpotifyProtocol::diffieHellmanAgreesOnTheSharedSecret() {
    const TBytes prime = Hex(
        "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404dd"
        "ef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a63a3620ffffffffffffffff"
    );
    const std::uint8_t generator[] = {2};
    TBytes a(95);
    TBytes b(95);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = static_cast<std::uint8_t>(i * 7 + 1);
        b[i] = static_cast<std::uint8_t>(i * 13 + 5);
    }
    const std::optional<TBytes> publicA = SpotifyProtocol::ModPow({generator, 1}, a, prime);
    const std::optional<TBytes> publicB = SpotifyProtocol::ModPow({generator, 1}, b, prime);
    QVERIFY(publicA && publicB);
    const std::optional<TBytes> sharedA = SpotifyProtocol::ModPow(*publicB, a, prime);
    const std::optional<TBytes> sharedB = SpotifyProtocol::ModPow(*publicA, b, prime);
    QVERIFY(sharedA && sharedB);
    QCOMPARE(HexOf(*sharedA), HexOf(*sharedB));
    QCOMPARE(sharedA->size(), prime.size());
}

void TestSpotifyProtocol::rsaSignatureOfAbcVerifies() {
    const SpotifyProtocol::TSha1 digest = SpotifyProtocol::Sha1({SpotifyProtocol::ByteView::Of("abc")});
    QVERIFY(SpotifyProtocol::VerifyRsaSha1(Hex(kRsaModulus), 65537, digest, Hex(kRsaSignature)));
}

void TestSpotifyProtocol::rsaSignatureOfAnotherTextFails() {
    const SpotifyProtocol::TSha1 digest = SpotifyProtocol::Sha1({SpotifyProtocol::ByteView::Of("abd")});
    QVERIFY(!SpotifyProtocol::VerifyRsaSha1(Hex(kRsaModulus), 65537, digest, Hex(kRsaSignature)));
}

void TestSpotifyProtocol::shannonMatchesTheReferenceImplementation() {
    TBytes key(32);
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    SpotifyProtocol::Shannon cipher(key);
    cipher.nonce(0);
    TBytes message(SpotifyProtocol::ByteView::Of("spotify shannon kat").copy());
    cipher.encrypt(message.data(), message.size());
    std::uint8_t mac[4];
    cipher.finish(mac, sizeof mac);
    QCOMPARE(HexOf(message), "b90d865fcc7f43ab9648a97e8dec605d95822a");
    QCOMPARE(HexOf({mac, sizeof mac}), "bea0ba6c");
}

void TestSpotifyProtocol::shannonDecryptsAHeaderAndPayloadSeparately() {
    TBytes key(32);
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    SpotifyProtocol::Shannon cipher(key);
    TBytes packet = Hex("1097369f33bec9959eb9656da89b6edc8fc358");
    cipher.nonce(1);
    cipher.decrypt(packet.data(), 3);
    cipher.decrypt(packet.data() + 3, packet.size() - 3);
    std::uint8_t mac[4];
    cipher.finish(mac, sizeof mac);
    QCOMPARE(HexOf(packet), "ab001030313233343536373839616263646566");
    QCOMPARE(HexOf({mac, sizeof mac}), "8cc90655");
}

void TestSpotifyProtocol::protobufRoundTripsNestedFields() {
    SpotifyProtocol::ProtoWriter inner;
    inner.varint(1, 300).string(2, "name");
    SpotifyProtocol::ProtoWriter outer;
    outer.message(10, inner).varint(100, 1);
    QCOMPARE(HexOf(inner.data()), "08ac0212046e616d65");
    SpotifyProtocol::ProtoReader reader(outer.data());
    QVERIFY(reader.next());
    QCOMPARE(reader.field(), 10);
    SpotifyProtocol::ProtoReader nested(reader.bytes());
    QVERIFY(nested.next());
    QCOMPARE(nested.varint(), std::uint64_t{300});
    QVERIFY(nested.next());
    QCOMPARE(nested.string(), std::string("name"));
    QVERIFY(reader.next());
    QCOMPARE(reader.field(), 100);
    QVERIFY(!reader.next());
    QVERIFY(!reader.failed());
}

void TestSpotifyProtocol::protobufRejectsATruncatedField() {
    const TBytes truncated = Hex("0a0568656c");
    SpotifyProtocol::ProtoReader reader(truncated);
    QVERIFY(!reader.next());
    QVERIFY(reader.failed());
}

void TestSpotifyProtocol::base62RoundTripsATrackId() {
    const std::optional<SpotifyProtocol::TGid> gid = SpotifyProtocol::GidFromBase62("6PCoZdjSPbRMehHviTrpB3");
    QVERIFY(gid.has_value());
    QCOMPARE(SpotifyProtocol::Base62FromGid(*gid), std::string("6PCoZdjSPbRMehHviTrpB3"));
    const std::optional<SpotifyProtocol::TGid> one = SpotifyProtocol::GidFromBase62("0000000000000000000001");
    QVERIFY(one.has_value());
    QCOMPARE(HexOf(*one), "00000000000000000000000000000001");
    QVERIFY(!SpotifyProtocol::GidFromBase62("tooShort").has_value());
}

void TestSpotifyProtocol::hashcashSolutionHasTheAskedZeros() {
    SpotifyProtocol::HashcashChallenge challenge;
    challenge.prefix = Hex("0123456789abcdef");
    challenge.length = 10;
    const std::optional<SpotifyProtocol::HashcashSolution> solution = SpotifyProtocol::SolveHashcash({}, challenge);
    QVERIFY(solution.has_value());
    const SpotifyProtocol::TSha1 digest = SpotifyProtocol::Sha1({challenge.prefix, solution->suffix});
    std::uint64_t tail = 0;
    for (std::size_t i = 12; i < 20; ++i) {
        tail = tail << 8 | digest[i];
    }
    QCOMPARE(tail & ((std::uint64_t{1} << 10) - 1), std::uint64_t{0});
}

void TestSpotifyProtocol::trackMetadataPicksOggVorbis160() {
    SpotifyProtocol::ProtoWriter ogg96;
    ogg96.bytes(1, TBytes(20, 0x96)).varint(2, 0);
    SpotifyProtocol::ProtoWriter ogg160;
    ogg160.bytes(1, TBytes(20, 0x16)).varint(2, 1);
    SpotifyProtocol::ProtoWriter flac;
    flac.bytes(1, TBytes(20, 0xf1)).varint(2, 16);
    SpotifyProtocol::ProtoWriter track;
    track.bytes(1, TBytes(16, 0x01)).string(2, "Song").varint(7, 222546 * 2);
    track.message(12, ogg96).message(12, flac).message(12, ogg160);
    SpotifyProtocol::ProtoWriter any;
    any.string(1, "type.googleapis.com/spotify.metadata.Track").message(2, track);
    SpotifyProtocol::ProtoWriter data;
    data.string(2, "spotify:track:x").message(3, any);
    SpotifyProtocol::ProtoWriter array;
    array.varint(2, 10).message(3, data);
    SpotifyProtocol::ProtoWriter response;
    response.message(2, array);

    const std::optional<SpotifyProtocol::TrackInfo> info = SpotifyProtocol::ParseTrackMetadata(response.data());
    QVERIFY(info.has_value());
    QCOMPARE(info->name, std::string("Song"));
    QCOMPARE(info->durationMs, std::int64_t{222546});
    QCOMPARE(info->files.size(), std::size_t{3});
    const std::optional<SpotifyProtocol::PlayableFile> picked = SpotifyProtocol::PickOggVorbis(*info, 160);
    QVERIFY(picked.has_value());
    QCOMPARE(picked->file.format, std::uint32_t{1});
    QCOMPARE(picked->file.id[0], std::uint8_t{0x16});
}

void TestSpotifyProtocol::normalisationIsReadAfterTheFirst144Bytes() {
    TBytes head(160, 0);
    const float values[] = {-6.5f, 0.98f, -7.25f, 1.0f};
    std::memcpy(head.data() + 144, values, sizeof values);
    const std::optional<SpotifyProtocol::Normalisation> gain = SpotifyProtocol::ParseNormalisation(head);
    QVERIFY(gain.has_value());
    QCOMPARE(gain->trackGainDb, -6.5f);
    QCOMPARE(gain->albumPeak, 1.0f);
    QVERIFY(!SpotifyProtocol::ParseNormalisation({head.data(), 150}).has_value());
}

namespace {

SpotifyProtocol::TrackInfo TrackWithFormats(std::initializer_list<SpotifyProtocol::AudioFormat> formats) {
    SpotifyProtocol::TrackInfo track;
    std::uint8_t mark = 1;
    for (const SpotifyProtocol::AudioFormat format : formats) {
        SpotifyProtocol::AudioFileRef file;
        file.id.fill(mark++);
        file.format = static_cast<std::uint32_t>(format);
        track.files.push_back(file);
    }
    return track;
}

std::optional<std::uint32_t> PickedFormat(const SpotifyProtocol::TrackInfo& track, int kbps) {
    const std::optional<SpotifyProtocol::PlayableFile> picked = SpotifyProtocol::PickOggVorbis(track, kbps);
    if (!picked) {
        return std::nullopt;
    }
    return picked->file.format;
}

SpotifyProtocol::ByteView PlatformOf(const TBytes& request, int field) {
    SpotifyProtocol::ByteView found;
    for (SpotifyProtocol::ProtoReader top(request); top.next();) {
        if (top.field() != 2) {
            continue;
        }
        for (SpotifyProtocol::ProtoReader client(top.bytes()); client.next();) {
            if (client.field() != 3) {
                continue;
            }
            for (SpotifyProtocol::ProtoReader connectivity(client.bytes()); connectivity.next();) {
                if (connectivity.field() != 1) {
                    continue;
                }
                for (SpotifyProtocol::ProtoReader platform(connectivity.bytes()); platform.next();) {
                    if (platform.field() == field) {
                        found = platform.bytes();
                    }
                }
            }
        }
    }
    return found;
}

}  // namespace

void TestSpotifyProtocol::oggVorbisFollowsTheQualityThenFallsBackTo160() {  // SPOT-13
    using SpotifyProtocol::AudioFormat;
    const auto every = TrackWithFormats({AudioFormat::OggVorbis96, AudioFormat::OggVorbis160, AudioFormat::OggVorbis320}
    );
    QCOMPARE(PickedFormat(every, 320), std::optional<std::uint32_t>{2});
    QCOMPARE(PickedFormat(every, 96), std::optional<std::uint32_t>{0});
    const auto middle = TrackWithFormats({AudioFormat::Flac, AudioFormat::OggVorbis160, AudioFormat::Mp3_320});
    QCOMPARE(PickedFormat(middle, 320), std::optional<std::uint32_t>{1});
    QCOMPARE(PickedFormat(middle, 96), std::optional<std::uint32_t>{1});
    const auto lossless = TrackWithFormats({AudioFormat::Flac, AudioFormat::Flac24Bit, AudioFormat::Aac24});
    QCOMPARE(PickedFormat(lossless, 320), std::optional<std::uint32_t>{});
}

void TestSpotifyProtocol::clientTokenNamesThePlatformItRunsOn() {
    SpotifyProtocol::ClientTokenParams params;
    params.clientId = "id";
    params.deviceId = "device";
    params.platform = SpotifyProtocol::LinuxPlatform{"6.8.0", "#1 SMP", "x86_64"};
    QCOMPARE(PlatformOf(SpotifyProtocol::ClientTokenRequest(params), 5).size > 0, true);
    params.platform = SpotifyProtocol::WindowsPlatform{};
    QCOMPARE(PlatformOf(SpotifyProtocol::ClientTokenRequest(params), 4).size > 0, true);
    params.platform = SpotifyProtocol::MacPlatform{"15.4", "aarch64"};
    const TBytes mac = SpotifyProtocol::ClientTokenRequest(params);
    QCOMPARE(PlatformOf(mac, 3).size > 0, true);
    QCOMPARE(PlatformOf(mac, 5).size, std::size_t{0});
}

QTEST_GUILESS_MAIN(TestSpotifyProtocol)
#include "spotify_protocol_test.moc"
