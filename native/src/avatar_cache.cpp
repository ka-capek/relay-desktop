#include "relay/avatar_cache.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace relay {
namespace {

const QHash<QByteArray, QString>& allowedTypes() {
  static const QHash<QByteArray, QString> values{
      {QByteArrayLiteral("image/png"), QStringLiteral("png")},
      {QByteArrayLiteral("image/jpeg"), QStringLiteral("jpg")},
      {QByteArrayLiteral("image/gif"), QStringLiteral("gif")},
      {QByteArrayLiteral("image/webp"), QStringLiteral("webp")},
  };
  return values;
}

QByteArray mimeForExtension(const QString& extension) {
  for (auto iterator = allowedTypes().cbegin(); iterator != allowedTypes().cend(); ++iterator) {
    if (iterator.value() == extension) return iterator.key();
  }
  return QByteArrayLiteral("image/png");
}

}  // namespace

AvatarCache::AvatarCache(QString userDataPath) : userDataPath_(std::move(userDataPath)) {}

QString AvatarCache::cacheDirectory() const {
  return QDir(userDataPath_).filePath(QStringLiteral("avatars"));
}

QString AvatarCache::safeAccountId(QString accountId) {
  static const QRegularExpression unsafe(QStringLiteral("[^a-zA-Z0-9._-]"));
  return accountId.replace(unsafe, QStringLiteral("_"));
}

QString AvatarCache::cacheFile(const QString& accountId, const QString& extension) const {
  return QDir(cacheDirectory()).filePath(QStringLiteral("%1.%2").arg(safeAccountId(accountId), extension));
}

QString AvatarCache::findExtension(const QString& accountId) const {
  for (const auto& extension : allowedTypes()) {
    if (QFileInfo::exists(cacheFile(accountId, extension))) return extension;
  }
  return {};
}

QString AvatarCache::dataUrl(const QByteArray& payload, const QString& extension) {
  return QStringLiteral("data:%1;base64,%2")
      .arg(QString::fromLatin1(mimeForExtension(extension)), QString::fromLatin1(payload.toBase64()));
}

QString AvatarCache::read(const QString& accountId) const {
  const auto extension = findExtension(accountId);
  if (extension.isEmpty()) return {};
  QFile file(cacheFile(accountId, extension));
  if (!file.open(QIODevice::ReadOnly)) return {};
  return dataUrl(file.readAll(), extension);
}

bool AvatarCache::isAllowedUrl(const QUrl& url) {
  if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) return false;
  const auto host = url.host().toLower();
  return host == QStringLiteral("avatars.githubusercontent.com") ||
         host.endsWith(QStringLiteral(".githubusercontent.com"));
}

QString AvatarCache::store(const QString& accountId, const QByteArray& contentType,
                           const QByteArray& payload) const noexcept {
  try {
    const auto normalizedType = contentType.split(';').front().trimmed().toLower();
    const auto extension = allowedTypes().value(normalizedType);
    if (extension.isEmpty() || payload.isEmpty() || payload.size() > maximumBytes) return {};
    if (!QDir().mkpath(cacheDirectory())) return {};
    for (const auto& value : allowedTypes()) {
      if (value != extension) QFile::remove(cacheFile(accountId, value));
    }
    QFile file(cacheFile(accountId, extension));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    if (file.write(payload) != payload.size()) return {};
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.close();
    return dataUrl(payload, extension);
  } catch (...) {
    return {};
  }
}

QString AvatarCache::fetch(const QString& accountId, const QUrl& avatarUrl) const noexcept {
  if (!isAllowedUrl(avatarUrl)) return {};
  try {
    QNetworkAccessManager manager;
    QNetworkRequest request(avatarUrl);
    request.setRawHeader(QByteArrayLiteral("User-Agent"), QByteArrayLiteral("Relay-Desktop"));
    request.setTransferTimeout(30000);
    auto* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const auto type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
    const auto payload = reply->read(maximumBytes + 1);
    const auto ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok ? store(accountId, type, payload) : QString{};
  } catch (...) {
    return {};
  }
}

void AvatarCache::forget(const QString& accountId) const noexcept {
  for (const auto& extension : allowedTypes()) QFile::remove(cacheFile(accountId, extension));
}

}  // namespace relay
