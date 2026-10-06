// Regenerates build/icon.png, build/icon.ico and build/icon.icns from the two
// SVG masters. Run it through the `icons` CMake target:
//
//   cmake --build --preset <preset> --target icons
//
// Sizes of 48px and below use icon-small.svg, which has heavier strokes and
// no shadow; larger sizes use icon.svg. Every container slot holds an image
// rendered at its true pixel size, including the Retina (@2x) slots of the
// .icns. The output is byte-stable for a given Qt version, so regenerating
// without editing a master produces no Git diff.

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>
#include <QtEndian>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

namespace {

constexpr int smallMasterLimit = 48;

class Renderer {
 public:
  Renderer(const QString& large, const QString& small) : large_(large), small_(small) {}

  [[nodiscard]] bool isValid() const { return large_.isValid() && small_.isValid(); }

  // PNG bytes for one square size, rendered once and reused across containers.
  QByteArray png(const int size) {
    if (const auto found = cache_.find(size); found != cache_.end()) return found->second;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
      QPainter painter(&image);
      painter.setRenderHint(QPainter::Antialiasing);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      (size <= smallMasterLimit ? small_ : large_).render(&painter, QRectF(0, 0, size, size));
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    cache_.emplace(size, bytes);
    return bytes;
  }

 private:
  QSvgRenderer large_;
  QSvgRenderer small_;
  std::map<int, QByteArray> cache_;
};

// Windows .ico with PNG-compressed entries (supported since Windows Vista).
QByteArray icoFile(Renderer& renderer) {
  const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
  QByteArray header(6, '\0');
  qToLittleEndian<quint16>(1, header.data() + 2);
  qToLittleEndian<quint16>(static_cast<quint16>(sizes.size()), header.data() + 4);
  QByteArray directory;
  QByteArray images;
  quint32 offset = static_cast<quint32>(6 + 16 * sizes.size());
  for (const int size : sizes) {
    const auto png = renderer.png(size);
    QByteArray entry(16, '\0');
    entry[0] = static_cast<char>(size >= 256 ? 0 : size);
    entry[1] = static_cast<char>(size >= 256 ? 0 : size);
    qToLittleEndian<quint16>(1, entry.data() + 4);   // colour planes
    qToLittleEndian<quint16>(32, entry.data() + 6);  // bits per pixel
    qToLittleEndian<quint32>(static_cast<quint32>(png.size()), entry.data() + 8);
    qToLittleEndian<quint32>(offset, entry.data() + 12);
    directory += entry;
    images += png;
    offset += static_cast<quint32>(png.size());
  }
  return header + directory + images;
}

// macOS .icns with PNG entries. The @2x types (ic11-ic14) hold images at
// twice the point size, which is what Retina displays use.
QByteArray icnsFile(Renderer& renderer) {
  const QList<std::pair<const char*, int>> entries{
      {"icp4", 16}, {"icp5", 32}, {"icp6", 64}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512},
      {"ic10", 1024}, {"ic11", 32}, {"ic12", 64}, {"ic13", 256}, {"ic14", 512}};
  QByteArray body;
  for (const auto& [type, size] : entries) {
    const auto png = renderer.png(size);
    QByteArray header(8, '\0');
    std::memcpy(header.data(), type, 4);
    qToBigEndian<quint32>(static_cast<quint32>(png.size() + 8), header.data() + 4);
    body += header + png;
  }
  QByteArray header(8, '\0');
  std::memcpy(header.data(), "icns", 4);
  qToBigEndian<quint32>(static_cast<quint32>(body.size() + 8), header.data() + 4);
  return header + body;
}

bool write(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    std::fprintf(stderr, "Could not write %s: %s\n", qPrintable(path), qPrintable(file.errorString()));
    return false;
  }
  std::printf("Wrote %s (%lld bytes)\n", qPrintable(QDir::toNativeSeparators(path)), static_cast<long long>(bytes.size()));
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  // QSvgRenderer needs a GUI application for fonts and image handlers; the
  // offscreen platform keeps this usable on build machines without a display.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication application(argc, argv);
  const auto arguments = QCoreApplication::arguments();
  if (arguments.size() != 2) {
    std::fprintf(stderr, "Usage: %s <directory containing icon.svg and icon-small.svg>\n", argv[0]);
    return 2;
  }
  const QDir directory(arguments.at(1));
  Renderer renderer(directory.filePath(QStringLiteral("icon.svg")), directory.filePath(QStringLiteral("icon-small.svg")));
  if (!renderer.isValid()) {
    std::fprintf(stderr, "Could not read icon.svg and icon-small.svg in %s\n", qPrintable(directory.path()));
    return 1;
  }
  const bool ok = write(directory.filePath(QStringLiteral("icon.png")), renderer.png(1024)) &&
                  write(directory.filePath(QStringLiteral("icon.ico")), icoFile(renderer)) &&
                  write(directory.filePath(QStringLiteral("icon.icns")), icnsFile(renderer));
  return ok ? 0 : 1;
}
