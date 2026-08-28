#include "relay/repository_discovery.hpp"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace relay {

bool isGitWorktree(const QString& directoryPath) {
  const QFileInfo marker(QDir(directoryPath).filePath(QStringLiteral(".git")));
  if (!marker.exists() || marker.isSymLink()) return false;
  if (marker.isDir()) return true;
  if (!marker.isFile()) return false;

  QFile file(marker.filePath());
  if (!file.open(QIODevice::ReadOnly)) return false;
  const auto markerText = QString::fromUtf8(file.read(64 * 1024));
  static const QRegularExpression gitDirectory(
      QStringLiteral(R"(^gitdir:\s*.+)"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
  return gitDirectory.match(markerText).hasMatch();
}

QStringList scanForRepositories(const QString& rootPath, const qsizetype maximum) {
  if (maximum <= 0) return {};

  QList<QString> queue{QFileInfo(rootPath).absoluteFilePath()};
  qsizetype queueIndex = 0;
  QSet<QString> visited;
  QStringList repositories;

  while (queueIndex < queue.size() && repositories.size() < maximum) {
    const auto directoryPath = QDir::cleanPath(queue[queueIndex++]);
    const auto canonicalPath = QFileInfo(directoryPath).canonicalFilePath();
    if (canonicalPath.isEmpty() || visited.contains(canonicalPath)) continue;
    visited.insert(canonicalPath);

    QDir directory(directoryPath);
    if (!directory.exists() || !directory.isReadable()) continue;
    if (isGitWorktree(directoryPath)) {
      repositories.push_back(directoryPath);
      continue;
    }

    const auto entries = directory.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
      if (!entry.isDir() || entry.isSymLink()) continue;
      const auto name = entry.fileName();
      if (name == QStringLiteral(".git") || name == QStringLiteral("node_modules")) continue;
      queue.push_back(entry.absoluteFilePath());
    }
  }

  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseSensitive);
  std::sort(repositories.begin(), repositories.end(), [&](const auto& left, const auto& right) {
    return collator.compare(left, right) < 0;
  });
  return repositories;
}

}  // namespace relay
