const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("relayDesktop", {
  platform: process.platform,
  isDesktop: true,
  getState: () => ipcRenderer.invoke("relay:get-state"),
  selectRepository: () => ipcRenderer.invoke("relay:select-repository"),
  chooseCloneDirectory: () => ipcRenderer.invoke("relay:choose-clone-directory"),
  listGitHubRepositories: (accountId) => ipcRenderer.invoke("relay:list-github-repositories", accountId),
  cloneRepository: (input) => ipcRenderer.invoke("relay:clone-repository", input),
  scanFolder: () => ipcRenderer.invoke("relay:scan-folder"),
  removeRepository: (repositoryPath) => ipcRenderer.invoke("relay:remove-repository", repositoryPath),
  openRepository: (repositoryPath) => ipcRenderer.invoke("relay:open-repository", repositoryPath),
  refreshRepository: (repositoryPath) => ipcRenderer.invoke("relay:refresh-repository", repositoryPath),
  getFileDiff: (repositoryPath, filePath) => ipcRenderer.invoke("relay:get-file-diff", repositoryPath, filePath),
  readHistory: (repositoryPath, options) => ipcRenderer.invoke("relay:read-history", repositoryPath, options),
  readCommit: (repositoryPath, hash) => ipcRenderer.invoke("relay:read-commit", repositoryPath, hash),
  readCommitDiff: (repositoryPath, hash, filePath) => ipcRenderer.invoke("relay:read-commit-diff", repositoryPath, hash, filePath),
  commit: (input) => ipcRenderer.invoke("relay:commit", input),
  fetchOrigin: (repositoryPath, accountId) => ipcRenderer.invoke("relay:fetch-origin", repositoryPath, accountId),
  pushOrigin: (repositoryPath, accountId) => ipcRenderer.invoke("relay:push-origin", repositoryPath, accountId),
  switchBranch: (repositoryPath, branch) => ipcRenderer.invoke("relay:switch-branch", repositoryPath, branch),
  connectAccount: () => ipcRenderer.invoke("relay:connect-account"),
  onGitHubLoginProgress: (callback) => {
    const listener = (_event, progress) => callback(progress);
    ipcRenderer.on("relay:github-login-progress", listener);
    return () => ipcRenderer.removeListener("relay:github-login-progress", listener);
  },
  setActiveAccount: (accountId) => ipcRenderer.invoke("relay:set-active-account", accountId),
  setAccountEmail: (accountId, email) => ipcRenderer.invoke("relay:set-account-email", accountId, email),
  setRepositoryAccount: (repositoryPath, accountId) => ipcRenderer.invoke("relay:set-repository-account", repositoryPath, accountId),
  backfillRepositoryMetadata: () => ipcRenderer.invoke("relay:backfill-repository-metadata"),
  setRepositoryOrder: (mode, direction) => ipcRenderer.invoke("relay:set-repository-order", mode, direction),
  setManualOrder: (repositoryPaths) => ipcRenderer.invoke("relay:set-manual-order", repositoryPaths),
  removeAccount: (accountId) => ipcRenderer.invoke("relay:remove-account", accountId),
  onMenuAction: (callback) => {
    const listener = (_event, action) => callback(action);
    ipcRenderer.on("relay:menu-action", listener);
    return () => ipcRenderer.removeListener("relay:menu-action", listener);
  },
  saveSshProfile: (profile) => ipcRenderer.invoke("relay:save-ssh-profile", profile),
  removeSshProfile: (profileId) => ipcRenderer.invoke("relay:remove-ssh-profile", profileId),
  setRepositorySshProfile: (repositoryPath, profileId) => ipcRenderer.invoke("relay:set-repository-ssh-profile", repositoryPath, profileId),
  testSshProfile: (profile) => ipcRenderer.invoke("relay:test-ssh-profile", profile),
  getMenu: () => ipcRenderer.invoke("relay:get-menu"),
  runMenuCommand: (command) => ipcRenderer.invoke("relay:menu-command", command),
  openExternal: (url) => ipcRenderer.invoke("relay:open-external", url),
});

window.addEventListener("DOMContentLoaded", () => {
  const platform = process.platform === "darwin" ? "macos" : process.platform === "win32" ? "windows" : "linux";
  document.documentElement.classList.add("desktop", platform);
});
