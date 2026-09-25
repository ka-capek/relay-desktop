#include "relay/main_window.hpp"
#include "relay/relay_application.hpp"
#include "relay/relay_controller.hpp"
#include "relay/theme.hpp"

#include <QTimer>
#include <QEvent>
#include <QTemporaryDir>
#include <memory>

#ifdef Q_OS_MACOS
namespace {
class WindowReactivation final : public QObject {
 public:
  explicit WindowReactivation(QWidget* window) : window_(window) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::ApplicationActivate && !window_->isVisible()) {
      window_->show();
      window_->raise();
      window_->activateWindow();
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QWidget* window_;
};
}  // namespace
#endif

int main(int argc, char** argv) {
  relay::RelayApplication application(argc, argv);
  relay::theme::apply(application);

  const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
  relay::RelayControllerConfig controllerConfig;
  std::unique_ptr<QTemporaryDir> smokeProfile;
  if (smokeTest) {
    smokeProfile = std::make_unique<QTemporaryDir>();
    if (!smokeProfile->isValid()) return 1;
    controllerConfig.storeFile = smokeProfile->filePath(QStringLiteral("relay-data.json"));
  }
  controllerConfig.synchronizeAccountsOnStart = !smokeTest;
  relay::RelayController controller(controllerConfig);
  relay::MainWindow window(&controller);
#ifdef Q_OS_MACOS
  application.setQuitOnLastWindowClosed(false);
  WindowReactivation reactivation(&window);
  application.installEventFilter(&reactivation);
#endif
  window.show();
  controller.start();
  if (smokeTest) {
    QTimer::singleShot(250, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
