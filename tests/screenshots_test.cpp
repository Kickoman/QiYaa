#include "audio/audio_engine.h"
#include "core/player.h"
#include "skins/skin.h"
#include "ui/equalizer_window.h"
#include "ui/main_window.h"
#include "yandex/api_client.h"
#include "yandex/library.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QTest>

namespace {

const QString kGoldenDir = QStringLiteral(QIYAA_TEST_DATA "/golden");

QString DescribeDifference(const QImage& actual, const QImage& expected) {
    if (actual.size() != expected.size()) {
        return QStringLiteral("size %1x%2, expected %3x%4")
            .arg(actual.width())
            .arg(actual.height())
            .arg(expected.width())
            .arg(expected.height());
    }
    int differing = 0;
    QPoint first(-1, -1);
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            if (actual.pixel(x, y) != expected.pixel(x, y)) {
                if (differing++ == 0) {
                    first = QPoint(x, y);
                }
            }
        }
    }
    if (differing == 0) {
        return QString();
    }
    return QStringLiteral("%1 pixels differ, first at (%2, %3): actual #%4, expected #%5")
        .arg(differing)
        .arg(first.x())
        .arg(first.y())
        .arg(actual.pixel(first), 8, 16, QChar('0'))
        .arg(expected.pixel(first), 8, 16, QChar('0'));
}

}  // namespace

class TestScreenshots : public QObject {
    Q_OBJECT
private:
    Skins::Skin skin;
    QNetworkAccessManager nam;
    std::unique_ptr<Yandex::ApiClient> api;
    std::unique_ptr<Yandex::Library> library;
    std::unique_ptr<Audio::AudioEngine> engine;
    std::unique_ptr<Core::Player> player;

    void compareWithGolden(QWidget* window, const QString& name) {
        const QImage actual = window->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const QString path = kGoldenDir + QLatin1Char('/') + name + QStringLiteral(".png");
        if (qEnvironmentVariable("QIYAA_UPDATE_GOLDEN") == QLatin1String("1")
            && !QFile::exists(path)) {
            QDir().mkpath(kGoldenDir);
            QVERIFY(actual.save(path));
            QSKIP(qPrintable(QStringLiteral("recorded new golden image ") + path));
        }
        const QImage expected(path);
        QVERIFY2(!expected.isNull(), qPrintable(QStringLiteral("no golden image ") + path));
        const QString difference =
            DescribeDifference(actual, expected.convertToFormat(QImage::Format_ARGB32));
        if (!difference.isEmpty() && !qEnvironmentVariableIsEmpty("QIYAA_TEST_SHOTS")) {
            actual.save(
                qEnvironmentVariable("QIYAA_TEST_SHOTS") + QLatin1Char('/') + name
                + QStringLiteral(".png")
            );
        }
        QVERIFY2(difference.isEmpty(), qPrintable(name + QStringLiteral(": ") + difference));
    }

private Q_SLOTS:
    void initTestCase() {
        skin = Skins::Skin::BuiltinBase();
        QVERIFY(skin.isValid());
        api = std::make_unique<Yandex::ApiClient>(&nam);
        library = std::make_unique<Yandex::Library>(api.get());
        engine = std::make_unique<Audio::AudioEngine>();
        player = std::make_unique<Core::Player>(library.get(), engine.get());
    }

    void mainWindowLooksAsRecorded_data() {
        QTest::addColumn<double>("scale");
        QTest::addColumn<bool>("shaded");
        QTest::newRow("main-x1") << 1.0 << false;
        QTest::newRow("main-x2") << 2.0 << false;
        QTest::newRow("main-shaded-x1") << 1.0 << true;
        QTest::newRow("main-shaded-x2") << 2.0 << true;
    }

    void mainWindowLooksAsRecorded() {
        QFETCH(double, scale);
        QFETCH(bool, shaded);
        Ui::MainWindow window(player.get(), &skin);
        window.setScale(scale);
        window.setShaded(shaded);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        compareWithGolden(&window, QString::fromLatin1(QTest::currentDataTag()));
    }

    void equalizerLooksAsRecorded_data() {
        QTest::addColumn<double>("scale");
        QTest::addColumn<bool>("shaded");
        QTest::newRow("equalizer-x1") << 1.0 << false;
        QTest::newRow("equalizer-x2") << 2.0 << false;
        QTest::newRow("equalizer-shaded-x1") << 1.0 << true;
    }

    void equalizerLooksAsRecorded() {
        QFETCH(double, scale);
        QFETCH(bool, shaded);
        Ui::EqualizerWindow window(&skin);
        window.setScale(scale);
        window.setShaded(shaded);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        compareWithGolden(&window, QString::fromLatin1(QTest::currentDataTag()));
    }
};

QTEST_MAIN(TestScreenshots)
#include "screenshots_test.moc"
