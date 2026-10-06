#include "relay/relay_store.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

class RelayStoreTest final : public QObject {
  Q_OBJECT

 private slots:
  void normalizesLegacyStateAndNeverRestoresARepository() {
    const QJsonObject legacy{
        {QStringLiteral("accounts"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("github-1")},
                 {QStringLiteral("authSource"), QStringLiteral("github-cli")},
                 {QStringLiteral("encryptedToken"), QStringLiteral("legacy-secret")},
                 {QStringLiteral("token"), QStringLiteral("plain-secret")},
             },
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("native-2")},
                 {QStringLiteral("authSource"), QStringLiteral("native")},
             },
         }},
        {QStringLiteral("repositories"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("/b")},
                 {QStringLiteral("lastOpened"), QStringLiteral("2026-01-02T00:00:00.000Z")},
             },
             QJsonObject{
                 {QStringLiteral("path"), QStringLiteral("/a")},
                 {QStringLiteral("lastOpened"), QStringLiteral("2026-01-01T00:00:00.000Z")},
             },
         }},
        {QStringLiteral("selectedRepositoryPath"), QStringLiteral("/b")},
        {QStringLiteral("sshProfiles"),
         QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("work")},
                 {QStringLiteral("label"), QStringLiteral(" Work ")},
                 {QStringLiteral("host"), QStringLiteral(" GitLab.Example.com ")},
                 {QStringLiteral("identityFile"), QStringLiteral(" /keys/work ")},
             },
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("invalid")},
                 {QStringLiteral("host"), QStringLiteral("bad host")},
             },
         }},
        {QStringLiteral("repositorySshProfiles"),
         QJsonObject{
             {QStringLiteral("/a"), QStringLiteral("work")},
             {QStringLiteral("/b"), QStringLiteral("missing")},
         }},
        {QStringLiteral("futureField"), 42},
    };

    const auto normalized = relay::normalizeStoreJson(
        legacy, QDateTime::fromString(QStringLiteral("2026-02-01T00:00:00Z"), Qt::ISODate));
    QVERIFY(normalized.value(QStringLiteral("selectedRepositoryPath")).isNull());
    QCOMPARE(normalized.value(QStringLiteral("futureField")).toInt(), 42);
    const auto accounts = normalized.value(QStringLiteral("accounts")).toArray();
    QCOMPARE(accounts.size(), 1);
    QVERIFY(!accounts[0].toObject().contains(QStringLiteral("encryptedToken")));
    QVERIFY(!accounts[0].toObject().contains(QStringLiteral("token")));
    QCOMPARE(
        normalized.value(QStringLiteral("manualOrder")).toArray(),
        QJsonArray({QStringLiteral("/b"), QStringLiteral("/a")}));
    const auto profiles = normalized.value(QStringLiteral("sshProfiles")).toArray();
    QCOMPARE(profiles.size(), 1);
    QCOMPARE(profiles[0].toObject().value(QStringLiteral("host")).toString(), QStringLiteral("gitlab.example.com"));
    QCOMPARE(
        normalized.value(QStringLiteral("repositorySshProfiles")).toObject(),
        QJsonObject({{QStringLiteral("/a"), QStringLiteral("work")}}));
  }

  void writesAtomicallyAndReadsElectronCompatibleJson() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto path = root.filePath(QStringLiteral("profile/relay-data.json"));
    const relay::RelayStore store(path);
    store.write(QJsonObject{
        {QStringLiteral("selectedRepositoryPath"), QStringLiteral("/must-not-open")},
        {QStringLiteral("customPublicMetadata"), QStringLiteral("preserved")},
    });

    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));
    const auto read = store.read();
    QVERIFY(read.value(QStringLiteral("selectedRepositoryPath")).isNull());
    QCOMPARE(read.value(QStringLiteral("customPublicMetadata")).toString(), QStringLiteral("preserved"));

#ifndef Q_OS_WIN
    const auto permissions = QFileInfo(path).permissions();
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(permissions.testFlag(QFileDevice::WriteOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
#endif

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("not json"), 8);
    file.close();
    QVERIFY_EXCEPTION_THROWN(store.read(), relay::StoreError);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("not json"));
  }
};

QTEST_APPLESS_MAIN(RelayStoreTest)
#include "tst_relay_store.moc"
