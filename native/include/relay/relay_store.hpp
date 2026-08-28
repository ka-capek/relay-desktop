#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <stdexcept>

namespace relay {

class StoreError final : public std::runtime_error {
 public:
  explicit StoreError(const QString& message);
};

[[nodiscard]] QJsonObject emptyStoreJson();

// Preserves unknown public fields for forward/backward compatibility, removes
// token-shaped account fields, retains only github-cli accounts, normalizes
// ordering and SSH bindings, and always clears selectedRepositoryPath.
[[nodiscard]] QJsonObject normalizeStoreJson(
    QJsonObject store, const QDateTime& now = QDateTime::currentDateTimeUtc());

class RelayStore final {
 public:
  explicit RelayStore(QString filePath = {});

  [[nodiscard]] const QString& filePath() const;
  [[nodiscard]] QJsonObject read() const;
  void write(const QJsonObject& store) const;

 private:
  QString filePath_;
};

}  // namespace relay
