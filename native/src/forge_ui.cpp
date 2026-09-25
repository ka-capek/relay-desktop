#include "relay/main_window.hpp"
#include "relay/forge_dialog.hpp"
#include "relay/dialogs.hpp"
#include "relay/relay_controller.hpp"

namespace relay {
void MainWindow::showForgeDialog() {
  ForgeDialog browser(controller_, this);
  if (browser.exec() != QDialog::Accepted || browser.cloneUrl().isEmpty()) return;
  CloneDialog clone(this);
  clone.setSshProfiles(controller_->state().sshProfiles);
  CloneRequest request;
  request.source = CloneSource::url;
  request.remoteUrl = browser.cloneUrl();
  request.repositoryName = browser.repositoryName();
  clone.setRequest(request);
  if (clone.exec() != QDialog::Accepted) return;
  const auto selected = clone.request();
  controller_->cloneRepository(selected.remoteUrl, selected.parentPath, selected.repositoryName,
                               {}, selected.sshProfileId);
}
}  // namespace relay
