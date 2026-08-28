#include "relay/relay_application.hpp"

#include <QIcon>
#include <QLocale>
#include <QStyleFactory>

namespace relay {

RelayApplication::RelayApplication(int& argc, char** argv) : QApplication(argc, argv) {
  setApplicationName(QStringLiteral("relay-desktop"));
  setApplicationDisplayName(QStringLiteral("Relay"));
  setApplicationVersion(QStringLiteral(RELAY_VERSION));
  setOrganizationName(QStringLiteral("Relay"));
  setOrganizationDomain(QStringLiteral("dev.relay"));
  setDesktopFileName(QStringLiteral("dev.relay.gitclient"));
  setWindowIcon(QIcon(QStringLiteral(":/relay/icon.svg")));
  QLocale::setDefault(QLocale::system());
}

}  // namespace relay
