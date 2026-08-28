#pragma once

#include <QString>
#include <QStringList>

namespace relay {

inline constexpr qsizetype maximumRememberedRepositories = 5000;

[[nodiscard]] bool isGitWorktree(const QString& directoryPath);

// Breadth-first and non-destructive. Symlinked child directories, .git,
// node_modules, unreadable entries, and descendants of a repository root are
// not traversed.
[[nodiscard]] QStringList scanForRepositories(
    const QString& rootPath, qsizetype maximum = maximumRememberedRepositories);

}  // namespace relay
