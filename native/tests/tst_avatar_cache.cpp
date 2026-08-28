#include "relay/avatar_cache.hpp"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class AvatarCacheTest final : public QObject {
  Q_OBJECT

 private slots:
  void restrictsHostsAndSchemes() {
    QVERIFY(relay::AvatarCache::isAllowedUrl(QUrl(QStringLiteral("https://avatars.githubusercontent.com/u/1"))));
    QVERIFY(relay::AvatarCache::isAllowedUrl(QUrl(QStringLiteral("https://foo.githubusercontent.com/a.png"))));
    QVERIFY(!relay::AvatarCache::isAllowedUrl(QUrl(QStringLiteral("http://avatars.githubusercontent.com/u/1"))));
    QVERIFY(!relay::AvatarCache::isAllowedUrl(QUrl(QStringLiteral("https://example.com/a.png"))));
  }

  void cachesAndForgetsAllowedImages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    relay::AvatarCache cache(directory.path());
    const QByteArray payload = QByteArrayLiteral("not-real-png-but-cache-does-not-decode");
    const auto value = cache.store(QStringLiteral("github-1/../../bad"), QByteArrayLiteral("image/png"), payload);
    QVERIFY(value.startsWith(QStringLiteral("data:image/png;base64,")));
    QCOMPARE(cache.read(QStringLiteral("github-1/../../bad")), value);
    cache.forget(QStringLiteral("github-1/../../bad"));
    QVERIFY(cache.read(QStringLiteral("github-1/../../bad")).isEmpty());
  }

  void rejectsUnexpectedTypesAndLargePayloads() {
    QTemporaryDir directory;
    relay::AvatarCache cache(directory.path());
    QVERIFY(cache.store(QStringLiteral("a"), QByteArrayLiteral("text/html"), QByteArrayLiteral("x")).isEmpty());
    QVERIFY(cache.store(QStringLiteral("a"), QByteArrayLiteral("image/png"),
                        QByteArray(relay::AvatarCache::maximumBytes + 1, 'x')).isEmpty());
  }
};

QTEST_APPLESS_MAIN(AvatarCacheTest)
#include "tst_avatar_cache.moc"
