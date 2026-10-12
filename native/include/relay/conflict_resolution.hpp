#pragma once

#include "relay/domain.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace relay {

// Which lines of one conflict block go into the result. An undecided block
// keeps its conflict markers, so a result is complete only when every block
// is decided.
struct ConflictChoice {
  QList<bool> current;
  QList<bool> incoming;
  bool incomingFirst{};
  bool decided{};
};

// Splits text into lines that keep their "\n" or "\r\n"; the last line may
// have no ending.
[[nodiscard]] QStringList splitLinesKeepingEnds(QStringView text);

// Parses the conflict markers Git writes (any marker size of at least 7, with
// or without the diff3 base section). Returns nothing for incomplete markers.
[[nodiscard]] std::optional<QList<ConflictSegment>> parseConflictMarkers(const QString& text);

[[nodiscard]] int conflictCount(const QList<ConflictSegment>& segments);

// One choice per conflict block, in order. Missing choices count as undecided.
[[nodiscard]] QString composeConflictResolution(const QList<ConflictSegment>& segments,
                                                const QList<ConflictChoice>& choices);

}  // namespace relay
