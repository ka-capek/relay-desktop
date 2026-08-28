#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace relay {

class AvatarCache final {
 public:
  static constexpr qsizetype maximumBytes = 512 * 1024;

  explicit AvatarCache(QString userDataPath);

  [[nodiscard]] QString read(const QString& accountId) const;
  [[nodiscard]] QString fetch(const QString& accountId, const QUrl& avatarUrl) const noexcept;
  [[nodiscard]] QString store(const QString& accountId, const QByteArray& contentType,
                              const QByteArray& payload) const noexcept;
  void forget(const QString& accountId) const noexcept;

  static bool isAllowedUrl(const QUrl& url);
  static QString safeAccountId(QString accountId);
  static QString dataUrl(const QByteArray& payload, const QString& extension);

 private:
  [[nodiscard]] QString cacheDirectory() const;
  [[nodiscard]] QString cacheFile(const QString& accountId, const QString& extension) const;
  [[nodiscard]] QString findExtension(const QString& accountId) const;

  QString userDataPath_;
};

}  // namespace relay
