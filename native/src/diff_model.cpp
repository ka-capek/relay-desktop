#include "relay/diff_model.hpp"

#include <QRegularExpression>
#include <QVariant>

#include <array>
#include <limits>
#include <utility>

namespace relay {
namespace {

constexpr std::array<QStringView, 14> diffHeaderPrefixes{
    QStringView{u"diff --git"},       QStringView{u"index "},
    QStringView{u"--- "},             QStringView{u"+++ "},
    QStringView{u"new file mode"},    QStringView{u"deleted file mode"},
    QStringView{u"old mode"},         QStringView{u"new mode"},
    QStringView{u"similarity index"}, QStringView{u"dissimilarity index"},
    QStringView{u"rename from"},      QStringView{u"rename to"},
    QStringView{u"copy from"},        QStringView{u"copy to"},
};

const QRegularExpression& hunkExpression() {
  static const QRegularExpression expression{
      QStringLiteral(R"(^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@)")};
  return expression;
}

bool isHeader(QStringView line) {
  for (const QStringView prefix : diffHeaderPrefixes) {
    if (line.startsWith(prefix)) {
      return true;
    }
  }
  return false;
}

QString lineNumber(qint64 value) {
  return value >= 0 ? QString::number(value) : QString{};
}

}  // namespace

DiffModel::DiffModel(QObject* parent) : QAbstractTableModel(parent) {}

int DiffModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  const auto maximum = static_cast<qsizetype>(std::numeric_limits<int>::max());
  return static_cast<int>(std::min(rows_.size(), maximum));
}

int DiffModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : columnCountValue;
}

QVariant DiffModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.column() < 0 || index.column() >= columnCountValue) {
    return {};
  }
  const Row* row = rowAt(index.row());
  if (row == nullptr) {
    return {};
  }

  switch (role) {
    case Qt::DisplayRole:
      if (row->kind == DiffLineKind::hunk) {
        return index.column() == oldLineColumn ? textAt(index.row()).toString() : QVariant{};
      }
      if (index.column() == oldLineColumn) {
        return lineNumber(row->oldLine);
      }
      if (index.column() == newLineColumn) {
        return lineNumber(row->newLine);
      }
      return textAt(index.row()).toString();

    case Qt::AccessibleTextRole:
      return accessibleText(*row);

    case Qt::AccessibleDescriptionRole:
      return tr("A line in a unified Git diff");

    case Qt::TextAlignmentRole:
      return index.column() == textColumn
                 ? QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter)
                 : QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);

    case kindRole:
      return QVariant::fromValue(row->kind);

    case oldLineNumberRole:
      return row->oldLine >= 0 ? QVariant::fromValue(row->oldLine) : QVariant{};

    case newLineNumberRole:
      return row->newLine >= 0 ? QVariant::fromValue(row->newLine) : QVariant{};

    case lineTextRole:
      return textAt(index.row()).toString();

    default:
      return {};
  }
}

QVariant DiffModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return {};
  }
  switch (section) {
    case oldLineColumn:
      return tr("Old line");
    case newLineColumn:
      return tr("New line");
    case textColumn:
      return tr("Content");
    default:
      return {};
  }
}

Qt::ItemFlags DiffModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QHash<int, QByteArray> DiffModel::roleNames() const {
  auto roles = QAbstractTableModel::roleNames();
  roles.insert(kindRole, QByteArrayLiteral("kind"));
  roles.insert(oldLineNumberRole, QByteArrayLiteral("oldLineNumber"));
  roles.insert(newLineNumberRole, QByteArrayLiteral("newLineNumber"));
  roles.insert(lineTextRole, QByteArrayLiteral("lineText"));
  return roles;
}

void DiffModel::setDiff(QString diff) {
  beginResetModel();
  diff_ = std::move(diff);
  rows_.clear();
  parse();
  endResetModel();
}

void DiffModel::clear() { setDiff({}); }

const QString& DiffModel::diff() const noexcept { return diff_; }

DiffLine DiffModel::lineAt(int row) const {
  const Row* value = rowAt(row);
  if (value == nullptr) {
    return {};
  }
  return DiffLine{
      .oldLine = lineNumber(value->oldLine),
      .newLine = lineNumber(value->newLine),
      .kind = value->kind,
      .text = textAt(row).toString(),
  };
}

DiffLineKind DiffModel::kindAt(int row) const noexcept {
  const Row* value = rowAt(row);
  return value != nullptr ? value->kind : DiffLineKind::plain;
}

QStringView DiffModel::textAt(int row) const noexcept {
  const Row* value = rowAt(row);
  if (value == nullptr) {
    return {};
  }
  return QStringView{diff_}.mid(value->textOffset, value->textLength);
}

bool DiffModel::isHunk(int row) const noexcept {
  const Row* value = rowAt(row);
  return value != nullptr && value->kind == DiffLineKind::hunk;
}

const DiffModel::Row* DiffModel::rowAt(int row) const noexcept {
  if (row < 0 || static_cast<qsizetype>(row) >= rows_.size()) {
    return nullptr;
  }
  return &rows_.at(row);
}

QString DiffModel::accessibleText(const Row& row) const {
  const QString text = QStringView{diff_}.mid(row.textOffset, row.textLength).toString();
  switch (row.kind) {
    case DiffLineKind::addition:
      return tr("Added line %1: %2").arg(row.newLine).arg(text);
    case DiffLineKind::removal:
      return tr("Removed line %1: %2").arg(row.oldLine).arg(text);
    case DiffLineKind::hunk:
      return tr("Diff hunk: %1").arg(text);
    case DiffLineKind::plain:
      if (row.oldLine < 0 && row.newLine < 0) {
        return tr("Diff metadata: %1").arg(text);
      }
      return tr("Context line %1 in the old file and %2 in the new file: %3")
          .arg(row.oldLine)
          .arg(row.newLine)
          .arg(text);
  }
  return text;
}

void DiffModel::parse() {
  if (diff_.isEmpty()) {
    return;
  }

  rows_.reserve(diff_.count(QChar{u'\n'}) + 1);
  const QStringView source{diff_};
  qint64 oldLine = 0;
  qint64 newLine = 0;
  qsizetype offset = 0;
  bool inHunk = false;

  while (offset < source.size()) {
    const qsizetype newline = source.indexOf(QChar{u'\n'}, offset);
    const qsizetype end = newline < 0 ? source.size() : newline;
    const QStringView line = source.mid(offset, end - offset);

    if (line.startsWith(QStringView{u"diff --git "})) inHunk = false;
    const QRegularExpressionMatch hunk = hunkExpression().matchView(line);
    if (hunk.hasMatch()) {
      inHunk = true;
      bool oldOk = false;
      bool newOk = false;
      const qint64 parsedOld = hunk.capturedView(1).toLongLong(&oldOk);
      const qint64 parsedNew = hunk.capturedView(2).toLongLong(&newOk);
      if (oldOk && newOk) {
        oldLine = parsedOld;
        newLine = parsedNew;
      }
      rows_.append(Row{offset, line.size(), -1, -1, DiffLineKind::hunk});
    } else if (inHunk || !isHeader(line)) {
      if (line.startsWith(QChar{u'+'}) && (inHunk || !line.startsWith(QStringView{u"+++"}))) {
        rows_.append(Row{offset, line.size(), -1, newLine, DiffLineKind::addition});
        ++newLine;
      } else if (line.startsWith(QChar{u'-'}) && (inHunk || !line.startsWith(QStringView{u"---"}))) {
        rows_.append(Row{offset, line.size(), oldLine, -1, DiffLineKind::removal});
        ++oldLine;
      } else if (line.startsWith(QChar{u'\\'})) {
        rows_.append(Row{offset, line.size(), -1, -1, DiffLineKind::plain});
      } else {
        const bool hasContextPrefix = line.startsWith(QChar{u' '});
        const qsizetype textOffset = offset + (hasContextPrefix ? 1 : 0);
        const qsizetype textLength = line.size() - (hasContextPrefix ? 1 : 0);
        rows_.append(Row{textOffset, textLength, oldLine, newLine, DiffLineKind::plain});
        ++oldLine;
        ++newLine;
      }
    }

    if (newline < 0) {
      break;
    }
    offset = newline + 1;
  }
}

}  // namespace relay
