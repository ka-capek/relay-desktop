// libsecret's GLib headers use `signals` as a member name, which Qt defines as
// a macro; include them before any Qt header.
#if defined(__linux__)
#include <libsecret/secret.h>
#endif
#include "relay/credential_store.hpp"
#include <QByteArray>
#include <QRegularExpression>
#include <stdexcept>
#ifdef Q_OS_MACOS
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace relay {
namespace {
QString target(const QString& id) {
  if (id.isEmpty() || id.size() > 240 || id.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.:-]"))))
    throw std::runtime_error("Invalid Git server credential reference.");
  return QStringLiteral("dev.relay.gitclient.forge/") + id;
}
#ifdef Q_OS_MACOS
CFMutableDictionaryRef query(const QString& id) {
  auto dict = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  const auto bytes = target(id).toUtf8();
  auto account = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size(), kCFStringEncodingUTF8, false);
  CFDictionarySetValue(dict, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(dict, kSecAttrService, CFSTR("dev.relay.gitclient.forge"));
  CFDictionarySetValue(dict, kSecAttrAccount, account);
  CFRelease(account);
  return dict;
}
#elif defined(Q_OS_LINUX)
// The Secret Service (GNOME Keyring, KWallet, KeePassXC) through libsecret.
const SecretSchema* schema() {
  static const SecretSchema value{"dev.relay.gitclient.forge", SECRET_SCHEMA_NONE,
      {{"account", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
      0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  return &value;
}
// Reports whether the call failed and releases the error without its text.
bool failed(GError* error) {
  if (!error) return false;
  g_error_free(error);
  return true;
}
#endif
}
void CredentialStore::write(const QString& accountId, const QString& token) {
  const auto name = target(accountId);
  auto bytes = token.toUtf8();
  if (bytes.isEmpty() || bytes.size() > 2500) throw std::runtime_error("Invalid Git server token size.");
#ifdef Q_OS_MACOS
  auto search = query(accountId);
  auto value = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size());
  auto attributes = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(attributes, kSecValueData, value);
  OSStatus status = SecItemUpdate(search, attributes);
  if (status == errSecItemNotFound) { CFDictionarySetValue(search, kSecValueData, value); status = SecItemAdd(search, nullptr); }
  CFRelease(attributes); CFRelease(value); CFRelease(search);
  bytes.fill('\0');
  if (status != errSecSuccess) throw std::runtime_error("Could not save the Git server token in macOS Keychain.");
#elif defined(Q_OS_WIN)
  CREDENTIALW credential{};
  auto wide = name.toStdWString();
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = wide.data();
  credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
  credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  const bool ok = CredWriteW(&credential, 0);
  bytes.fill('\0');
  if (!ok) throw std::runtime_error("Could not save the Git server token in Windows Credential Manager.");
#elif defined(Q_OS_LINUX)
  GError* error{};
  const auto account = name.toUtf8();
  const bool ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "Relay Git server token",
      bytes.constData(), nullptr, &error, "account", account.constData(), nullptr);
  bytes.fill('\0');
  if (failed(error) || !ok)
    throw std::runtime_error("Could not save the Git server token in the system keyring. Relay needs a running Secret Service such as GNOME Keyring or KWallet; no plaintext fallback is used.");
#else
  Q_UNUSED(name);
  bytes.fill('\0');
  throw std::runtime_error("Git server credential storage is supported on macOS, Windows and Linux. No plaintext fallback is used.");
#endif
}
QString CredentialStore::read(const QString& accountId) {
  const auto name = target(accountId);
#ifdef Q_OS_MACOS
  auto search = query(accountId);
  CFDictionarySetValue(search, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(search, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef value{};
  const OSStatus status = SecItemCopyMatching(search, &value);
  CFRelease(search);
  if (status == errSecItemNotFound) return {};
  if (status != errSecSuccess || !value) throw std::runtime_error("Could not read the Git server token from macOS Keychain.");
  if (CFGetTypeID(value) != CFDataGetTypeID()) { CFRelease(value); throw std::runtime_error("Invalid credential vault entry."); }
  auto data = static_cast<CFDataRef>(value);
  auto result = QString::fromUtf8(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
  CFRelease(value);
  return result;
#elif defined(Q_OS_WIN)
  PCREDENTIALW credential{};
  if (!CredReadW(name.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
    if (GetLastError() == ERROR_NOT_FOUND) return {};
    throw std::runtime_error("Could not read the Git server token from Windows Credential Manager.");
  }
  auto result = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
  if (credential->CredentialBlob) SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
  CredFree(credential);
  return result;
#elif defined(Q_OS_LINUX)
  GError* error{};
  const auto account = name.toUtf8();
  auto* secret = secret_password_lookup_sync(schema(), nullptr, &error, "account", account.constData(), nullptr);
  if (failed(error)) {
    if (secret) secret_password_free(secret);
    throw std::runtime_error("Could not read the Git server token from the system keyring.");
  }
  if (!secret) return {};
  auto result = QString::fromUtf8(secret);
  secret_password_free(secret);
  return result;
#else
  Q_UNUSED(name);
  throw std::runtime_error("Git server credential storage is supported on macOS, Windows and Linux.");
#endif
}
void CredentialStore::remove(const QString& accountId) {
  const auto name = target(accountId);
#ifdef Q_OS_MACOS
  auto search = query(accountId);
  const OSStatus status = SecItemDelete(search);
  CFRelease(search);
  if (status != errSecSuccess && status != errSecItemNotFound) throw std::runtime_error("Could not remove the Git server token from macOS Keychain.");
#elif defined(Q_OS_WIN)
  if (!CredDeleteW(name.toStdWString().c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
    throw std::runtime_error("Could not remove the Git server token from Windows Credential Manager.");
#elif defined(Q_OS_LINUX)
  GError* error{};
  const auto account = name.toUtf8();
  // FALSE without an error means there was nothing to remove.
  secret_password_clear_sync(schema(), nullptr, &error, "account", account.constData(), nullptr);
  if (failed(error)) throw std::runtime_error("Could not remove the Git server token from the system keyring.");
#else
  Q_UNUSED(name);
  throw std::runtime_error("Git server credential storage is supported on macOS, Windows and Linux.");
#endif
}
}  // namespace relay
