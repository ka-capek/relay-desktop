#pragma once

#include <QMetaMethod>
#include <QObject>
#include <QStringList>
#include <QTest>

namespace relay::test {

// Runs a QtTest object, limited to every n-th test function when
// RELAY_TEST_SHARD is "i/n" (1-based). Splitting by declaration order means a
// new test function always lands in some shard; explicit function names on
// the command line still win.
inline int execSharded(QObject* test, int argc, char** argv) {
  QStringList arguments;
  for (int index = 0; index < argc; ++index) arguments.append(QString::fromLocal8Bit(argv[index]));
  const auto shard = qEnvironmentVariable("RELAY_TEST_SHARD").split(u'/');
  bool explicitFunctions = false;
  for (int index = 1; index < arguments.size(); ++index) {
    // Option values (-o file,format) are not function names.
    if (arguments.at(index).startsWith(u'-')) { ++index; continue; }
    explicitFunctions = true;
  }
  if (shard.size() == 2 && !explicitFunctions) {
    const auto position = shard.at(0).toInt();
    const auto count = shard.at(1).toInt();
    if (count < 1 || position < 1 || position > count) qFatal("RELAY_TEST_SHARD must be i/n with 1 <= i <= n");
    const auto* meta = test->metaObject();
    int ordinal = 0;
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
      const auto method = meta->method(index);
      const auto name = QString::fromLatin1(method.name());
      if (method.methodType() != QMetaMethod::Slot || method.access() != QMetaMethod::Private ||
          method.parameterCount() != 0 || name == QStringLiteral("initTestCase") ||
          name == QStringLiteral("cleanupTestCase") || name == QStringLiteral("init") ||
          name == QStringLiteral("cleanup") || name.endsWith(QStringLiteral("_data")))
        continue;
      if (ordinal++ % count == position - 1) arguments.append(name);
    }
  }
  return QTest::qExec(test, arguments);
}

}  // namespace relay::test
