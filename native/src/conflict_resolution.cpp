#include "relay/conflict_resolution.hpp"

#include <algorithm>
#include <utility>

namespace relay {
namespace {

QStringView withoutEnding(QStringView line) {
  if (line.endsWith(u'\n')) line.chop(1);
  if (line.endsWith(u'\r')) line.chop(1);
  return line;
}

// The size of a marker run of `marker` at the start of the line, or 0 when
// the line is not a marker. "=" markers stand alone; the others may carry a
// label after a space.
qsizetype markerSize(QStringView line, const QChar marker) {
  line = withoutEnding(line);
  qsizetype size = 0;
  while (size < line.size() && line.at(size) == marker) ++size;
  if (size < 7) return 0;
  if (size == line.size()) return size;
  return marker != u'=' && line.at(size) == u' ' ? size : 0;
}

}  // namespace

QStringList splitLinesKeepingEnds(const QStringView text) {
  QStringList lines;
  qsizetype start = 0;
  while (start < text.size()) {
    const auto end = text.indexOf(u'\n', start);
    if (end < 0) {
      lines.append(text.mid(start).toString());
      break;
    }
    lines.append(text.mid(start, end - start + 1).toString());
    start = end + 1;
  }
  return lines;
}

std::optional<QList<ConflictSegment>> parseConflictMarkers(const QString& text) {
  enum class State { outside, current, base, incoming };
  QList<ConflictSegment> segments;
  ConflictSegment common;
  ConflictSegment block;
  block.conflict = true;
  auto state = State::outside;
  qsizetype size = 0;
  for (const auto& line : splitLinesKeepingEnds(text)) {
    if (state == State::outside) {
      if (const auto opening = markerSize(line, u'<')) {
        if (!common.lines.isEmpty()) segments.append(std::exchange(common, {}));
        size = opening;
        block.raw = line;
        state = State::current;
      } else {
        common.lines.append(line);
      }
      continue;
    }
    block.raw += line;
    // A second opening marker of the same size inside a block is ambiguous.
    if (markerSize(line, u'<') == size) return std::nullopt;
    if (state == State::current && markerSize(line, u'|') == size) {
      state = State::base;
    } else if (state != State::incoming && markerSize(line, u'=') == size) {
      state = State::incoming;
    } else if (state == State::incoming && markerSize(line, u'>') == size) {
      segments.append(std::exchange(block, {}));
      block.conflict = true;
      state = State::outside;
    } else if (state == State::incoming && (markerSize(line, u'=') == size || markerSize(line, u'|') == size)) {
      return std::nullopt;
    } else {
      (state == State::current ? block.current : state == State::base ? block.base : block.incoming).append(line);
    }
  }
  if (state != State::outside) return std::nullopt;
  if (!common.lines.isEmpty()) segments.append(std::move(common));
  return segments;
}

int conflictCount(const QList<ConflictSegment>& segments) {
  return static_cast<int>(std::count_if(segments.cbegin(), segments.cend(),
      [](const ConflictSegment& segment) { return segment.conflict; }));
}

QString composeConflictResolution(const QList<ConflictSegment>& segments,
                                  const QList<ConflictChoice>& choices) {
  QString result;
  qsizetype block = 0;
  const auto selected = [&result](const QStringList& lines, const QList<bool>& chosen) {
    for (qsizetype index = 0; index < lines.size(); ++index)
      if (chosen.value(index)) result += lines.at(index);
  };
  for (const auto& segment : segments) {
    if (!segment.conflict) {
      for (const auto& line : segment.lines) result += line;
      continue;
    }
    const auto choice = choices.value(block++);
    if (!choice.decided) {
      result += segment.raw;
      continue;
    }
    if (choice.incomingFirst) {
      selected(segment.incoming, choice.incoming);
      selected(segment.current, choice.current);
    } else {
      selected(segment.current, choice.current);
      selected(segment.incoming, choice.incoming);
    }
  }
  return result;
}

}  // namespace relay
