#include "relay/main_window.hpp"
#include "relay/relay_application.hpp"
#include "relay/relay_controller.hpp"
#include "relay/theme.hpp"

#include <QTimer>

int main(int argc, char** argv) {
  relay::RelayApplication application(argc, argv);
  relay::theme::apply(application);

  const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
  relay::RelayControllerConfig controllerConfig;
  controllerConfig.synchronizeAccountsOnStart = !smokeTest;
  relay::RelayController controller(controllerConfig);
  relay::MainWindow window(&controller);
  window.show();
  controller.start();
  if (smokeTest) {
    QTimer::singleShot(250, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
