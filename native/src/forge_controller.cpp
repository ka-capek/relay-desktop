#include "relay/relay_controller.hpp"
#include "relay/forge_service.hpp"
#include "relay/credential_store.hpp"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>

namespace relay {
namespace {
// A worker may finish after its controller has closed. The last owner removes
// a credential that never became referenced by persisted public metadata.
struct PendingCredential {
  std::shared_ptr<CredentialStore> vault;
  QString key;
  bool committed{};
  ~PendingCredential() {
    if (committed) return;
    auto cleanup = QtConcurrent::run([store = vault, id = key] {
      try { store->remove(id); } catch (...) { /* No public metadata was committed. */ }
    });
    Q_UNUSED(cleanup);
  }
};
struct ForgeConnection {
  ForgeAccount account;
  std::shared_ptr<PendingCredential> credential;
};
}  // namespace


void RelayController::connectForgeAccount(ForgeKind kind, const QString& server, QString token) {
  if (repositoryMutationActive_ || accountMutationActive_) {
    emit operationFailed(QStringLiteral("connect-forge-account"), QStringLiteral("Wait for the current operation to finish."));
    return;
  }
  ++forgeRepositoriesGeneration_;
  const auto service = forge_;
  const auto vault = credentials_;
  const auto key = QString::fromLatin1(QCryptographicHash::hash(
      QFileInfo(config_.storeFile).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex()) +
      u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
  runAsync<ForgeConnection>(QStringLiteral("connect-forge-account"),
      [service, vault, kind, server, token = std::move(token), key]() mutable {
        auto account = service->profile(kind, server, token);
        account.credentialId = account.id + u':' + key;
        auto pending = std::make_shared<PendingCredential>();
        pending->vault = vault;
        pending->key = account.credentialId;
        vault->write(account.credentialId, token);
        token.fill(QChar{});
        token.clear();
        return ForgeConnection{account, pending};
      }, [this](ForgeConnection connection) {
        const auto& account = connection.account;
        QString previousKey;
        for (const auto& existing : state_.forgeAccounts)
          if (existing.id == account.id) previousKey = existing.credentialId;
        state_.forgeAccounts.removeIf([&account](const ForgeAccount& existing) { return existing.id == account.id; });
        state_.forgeAccounts.append(account);
        if (!previousKey.isEmpty() && previousKey != account.credentialId)
          state_.forgeCredentialCleanup.append(previousKey);
        persistState();
        connection.credential->committed = true;
        publishState();
        emit forgeAccountConnected(account.id);
        retryForgeCredentialCleanup();
      });
}

void RelayController::removeForgeAccount(const QString& accountId) {
  if (repositoryMutationActive_ || accountMutationActive_) {
    emit operationFailed(QStringLiteral("remove-forge-account"), QStringLiteral("Wait for the current operation to finish."));
    return;
  }
  const auto found = std::find_if(state_.forgeAccounts.cbegin(), state_.forgeAccounts.cend(),
      [&accountId](const ForgeAccount& account) { return account.id == accountId; });
  if (found == state_.forgeAccounts.cend()) return;
  const auto key = found->credentialId;
  ++forgeRepositoriesGeneration_;
  state_.forgeAccounts.removeIf([&accountId](const ForgeAccount& account) { return account.id == accountId; });
  if (!key.isEmpty()) state_.forgeCredentialCleanup.append(key);
  try { persistState(); publishState(); }
  catch (const std::exception& error) {
    emit operationFailed(QStringLiteral("remove-forge-account"), QString::fromUtf8(error.what()));
    return;
  }
  retryForgeCredentialCleanup();
}

void RelayController::retryForgeCredentialCleanup() {
  if (forgeCleanupActive_ || state_.forgeCredentialCleanup.isEmpty()) return;
  forgeCleanupActive_ = true;
  const auto vault = credentials_;
  const auto keys = state_.forgeCredentialCleanup;
  runAsync<QPair<QStringList, bool>>(QStringLiteral("forge-credential-cleanup"), [vault, keys] {
    QStringList removed;
    bool failed = false;
    for (const auto& key : keys) {
      try { vault->remove(key); removed.append(key); }
      catch (...) { failed = true; }
    }
    return qMakePair(removed, failed);
  }, [this](const QPair<QStringList, bool>& result) {
    forgeCleanupActive_ = false;
    for (const auto& key : result.first) state_.forgeCredentialCleanup.removeAll(key);
    persistState();
    publishState();
    if (result.second)
      emit operationFailed(QStringLiteral("forge-credential-cleanup"),
          QStringLiteral("The account was disconnected, but the system could not remove a saved token. Use Retry credential cleanup."));
    else if (!state_.forgeCredentialCleanup.isEmpty()) retryForgeCredentialCleanup();
  });
}

void RelayController::cancelForgeRepositoryRequest() { ++forgeRepositoriesGeneration_; }

void RelayController::requestForgeRepositories(const QString& accountId) {
  const auto generation = ++forgeRepositoriesGeneration_;
  const auto found = std::find_if(state_.forgeAccounts.cbegin(), state_.forgeAccounts.cend(),
      [&accountId](const ForgeAccount& account) { return account.id == accountId; });
  if (found == state_.forgeAccounts.cend()) return;
  const auto account = *found;
  const auto service = forge_;
  const auto vault = credentials_;
  runAsync<QList<ForgeRepository>>(QStringLiteral("forge-repositories"), [service, vault, account] {
    return service->repositories(account, vault->read(account.credentialId));
  }, [this, accountId](QList<ForgeRepository> repositories) {
    emit forgeRepositoriesReady(accountId, std::move(repositories));
  }, [this, generation] { return forgeRepositoriesGeneration_ == generation; });
}

}  // namespace relay
