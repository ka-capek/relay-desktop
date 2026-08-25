const { app, BrowserWindow, dialog, ipcMain, Menu, shell } = require("electron");
const fs = require("fs");
const path = require("path");
const {
  cloneRepository,
  commitFiles,
  fetchOrigin,
  getFileDiff,
  pushOrigin,
  readRepository,
  readRepositorySummary,
  switchBranch,
} = require("./git-service.cjs");
const { scanForRepositories } = require("./repository-discovery.cjs");
const {
  GITHUB_DEVICE_URL,
  accountToken,
  authenticatedAccounts,
  login: loginWithGitHub,
  removeAccount: removeGitHubAccount,
  switchAccount: switchGitHubAccount,
} = require("./github-auth.cjs");

const isMac = process.platform === "darwin";

function dataFile() {
  return path.join(app.getPath("userData"), "relay-data.json");
}

function emptyStore() {
  return {
    accounts: [],
    activeAccountId: null,
    repositories: [],
    selectedRepositoryPath: null,
    repositoryAccounts: {},
  };
}

function readStore() {
  try {
    const value = JSON.parse(fs.readFileSync(dataFile(), "utf8"));
    const store = { ...emptyStore(), ...value, selectedRepositoryPath: null };
    store.accounts = store.accounts
      .filter((account) => account.authSource === "github-cli")
      .map((account) => {
        const sanitized = { ...account };
        delete sanitized.encryptedToken;
        return sanitized;
      });
    return store;
  } catch {
    return emptyStore();
  }
}

function writeStore(store) {
  fs.mkdirSync(path.dirname(dataFile()), { recursive: true });
  const temporary = `${dataFile()}.tmp`;
  fs.writeFileSync(temporary, JSON.stringify(store, null, 2), { mode: 0o600 });
  fs.renameSync(temporary, dataFile());
}

function publicAccount(account) {
  return account;
}

function publicState(store) {
  return {
    accounts: store.accounts.map(publicAccount),
    activeAccountId: store.activeAccountId,
    repositories: store.repositories,
    selectedRepositoryPath: null,
    repositoryAccounts: store.repositoryAccounts,
  };
}

function initials(name) {
  return name
    .split(/\s+/)
    .filter(Boolean)
    .slice(0, 2)
    .map((part) => part[0]?.toUpperCase())
    .join("") || "GH";
}

function githubContext() {
  return {
    appPath: app.getAppPath(),
    isPackaged: app.isPackaged,
    resourcesPath: process.resourcesPath,
    userDataPath: app.getPath("userData"),
  };
}

async function githubProfile(handle) {
  const token = await accountToken(githubContext(), handle);
  const response = await fetch("https://api.github.com/user", {
    headers: {
      Accept: "application/vnd.github+json",
      Authorization: `Bearer ${token}`,
      "User-Agent": "Relay-Desktop",
      "X-GitHub-Api-Version": "2022-11-28",
    },
  });

  if (!response.ok) {
    if (response.status === 401) throw new Error(`GitHub credentials for @${handle} expired. Sign in again.`);
    throw new Error(`GitHub account check failed (${response.status}).`);
  }
  return response.json();
}

async function githubRepositories(account) {
  const token = await accountToken(githubContext(), account.handle);
  const repositories = [];
  let nextUrl = "https://api.github.com/user/repos?visibility=all&affiliation=owner%2Ccollaborator%2Corganization_member&sort=updated&direction=desc&per_page=100";

  while (nextUrl) {
    const response = await fetch(nextUrl, {
      headers: {
        Accept: "application/vnd.github+json",
        Authorization: `Bearer ${token}`,
        "User-Agent": "Relay-Desktop",
        "X-GitHub-Api-Version": "2022-11-28",
      },
    });
    if (!response.ok) {
      const detail = await response.json().catch(() => null);
      if (response.status === 401) throw new Error(`GitHub credentials for @${account.handle} expired. Sign in again.`);
      throw new Error(detail?.message || `GitHub repository list failed (${response.status}).`);
    }

    const page = await response.json();
    if (!Array.isArray(page)) throw new Error("GitHub returned an unexpected repository list.");
    repositories.push(...page.map((repository) => ({
      id: String(repository.id),
      name: repository.name,
      fullName: repository.full_name,
      owner: repository.owner?.login || repository.full_name?.split("/")[0] || "GitHub",
      description: repository.description || "",
      private: Boolean(repository.private),
      archived: Boolean(repository.archived),
      fork: Boolean(repository.fork),
      cloneUrl: repository.clone_url,
      updatedAt: repository.updated_at,
    })));

    const link = response.headers.get("link") || "";
    nextUrl = link.match(/<([^>]+)>;\s*rel="next"/)?.[1] || "";
  }

  return repositories;
}

async function syncGitHubAccounts() {
  const authenticated = await authenticatedAccounts(githubContext());
  const store = readStore();
  const knownByHandle = new Map(store.accounts.map((account) => [account.handle.toLowerCase(), account]));
  const accounts = [];
  for (const authenticatedAccount of authenticated) {
    const known = knownByHandle.get(authenticatedAccount.handle.toLowerCase());
    let profile = null;
    if (!known) profile = await githubProfile(authenticatedAccount.handle);
    const handle = profile?.login || authenticatedAccount.handle;
    const name = profile?.name || known?.name || handle;
    const profileId = profile?.id || known?.githubId || handle;
    accounts.push({
      id: known?.id || `github-${profileId}`,
      githubId: profileId,
      name,
      handle,
      email: profile?.email || known?.email || `${profileId}+${handle}@users.noreply.github.com`,
      initials: initials(name),
      status: known?.status || name,
      tone: known?.tone || ["coral", "violet", "blue"][Number(profile?.id || 0) % 3],
      authSource: "github-cli",
      tokenSource: authenticatedAccount.tokenSource,
      active: authenticatedAccount.active,
    });
  }
  store.accounts = accounts;
  store.activeAccountId = accounts.find((account) => account.active)?.id || accounts[0]?.id || null;
  const accountIds = new Set(accounts.map((account) => account.id));
  for (const [repositoryPath, accountId] of Object.entries(store.repositoryAccounts)) {
    if (!accountIds.has(accountId)) delete store.repositoryAccounts[repositoryPath];
  }
  writeStore(store);
  return publicState(store);
}

function rememberRepository(repository) {
  const store = readStore();
  const summary = {
    path: repository.path,
    name: repository.name,
    owner: repository.owner,
    branch: repository.branch,
    changes: repository.files.length,
    lastOpened: new Date().toISOString(),
  };
  store.repositories = [summary, ...store.repositories.filter((item) => item.path !== summary.path)].slice(0, 5000);
  writeStore(store);
  return publicState(store);
}

async function rememberRepositoryPaths(repositoryPaths) {
  const store = readStore();
  const existingPaths = new Set(store.repositories.map((repository) => repository.path));
  const summaries = [];
  for (let index = 0; index < repositoryPaths.length; index += 10) {
    const batch = repositoryPaths.slice(index, index + 10);
    const values = await Promise.all(batch.map((repositoryPath) => readRepositorySummary(repositoryPath).catch(() => null)));
    summaries.push(...values.filter(Boolean));
  }
  const scannedPaths = new Set(summaries.map((repository) => repository.path));
  store.repositories = [
    ...summaries,
    ...store.repositories.filter((repository) => !scannedPaths.has(repository.path)),
  ].slice(0, 5000);
  writeStore(store);
  return {
    state: publicState(store),
    added: summaries.filter((repository) => !existingPaths.has(repository.path)).length,
    readable: summaries.length,
  };
}

function sendMenuAction(action) {
  const window = BrowserWindow.getFocusedWindow() || BrowserWindow.getAllWindows()[0];
  if (window && !window.isDestroyed()) window.webContents.send("relay:menu-action", action);
}

function installApplicationMenu() {
  const fileSubmenu = [
    {
      label: isMac ? "Add Local Repository…" : "Add &Local Repository…",
      accelerator: "CmdOrCtrl+O",
      click: () => sendMenuAction("open-repository"),
    },
    {
      label: isMac ? "Clone Repository…" : "&Clone Repository…",
      accelerator: "CmdOrCtrl+Shift+O",
      click: () => sendMenuAction("clone-repository"),
    },
    {
      label: isMac ? "Scan Folder for Repositories…" : "&Scan Folder for Repositories…",
      click: () => sendMenuAction("scan-folder"),
    },
    { type: "separator" },
    {
      label: isMac ? "Remove Current Repository from Relay" : "&Remove Current Repository from Relay",
      click: () => sendMenuAction("remove-repository"),
    },
  ];
  if (!isMac) fileSubmenu.push({ type: "separator" }, { role: "quit", label: "E&xit" });

  const template = [];
  if (isMac) {
    template.push({
      label: app.name,
      submenu: [
        { role: "about" },
        { type: "separator" },
        { role: "services" },
        { type: "separator" },
        { role: "hide" },
        { role: "hideOthers" },
        { role: "unhide" },
        { type: "separator" },
        { role: "quit" },
      ],
    });
  }
  template.push(
    { label: isMac ? "File" : "&File", submenu: fileSubmenu },
    {
      label: isMac ? "Edit" : "&Edit",
      submenu: [
        { role: "undo" }, { role: "redo" }, { type: "separator" },
        { role: "cut" }, { role: "copy" }, { role: "paste" }, { role: "selectAll" },
      ],
    },
    {
      label: isMac ? "View" : "&View",
      submenu: [
        { role: "reload" }, { role: "forceReload" }, { role: "toggleDevTools" },
        { type: "separator" }, { role: "resetZoom" }, { role: "zoomIn" }, { role: "zoomOut" },
        { type: "separator" }, { role: "togglefullscreen" },
      ],
    },
    { label: isMac ? "Window" : "&Window", submenu: [{ role: "minimize" }, { role: "close" }] },
  );
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

function registerIpc() {
  ipcMain.handle("relay:get-state", () => syncGitHubAccounts());

  ipcMain.handle("relay:select-repository", async () => {
    const result = await dialog.showOpenDialog({
      title: "Open a Git repository",
      properties: ["openDirectory"],
      buttonLabel: "Open Repository",
    });
    if (result.canceled || !result.filePaths[0]) return null;
    const repository = await readRepository(result.filePaths[0]);
    rememberRepository(repository);
    return repository;
  });

  ipcMain.handle("relay:choose-clone-directory", async (event) => {
    const result = await dialog.showOpenDialog(BrowserWindow.fromWebContents(event.sender), {
      title: "Choose where to clone the repository",
      properties: ["openDirectory", "createDirectory"],
      buttonLabel: "Choose Folder",
    });
    return result.canceled ? null : result.filePaths[0] || null;
  });

  ipcMain.handle("relay:list-github-repositories", async (_event, accountId) => {
    const store = readStore();
    const account = store.accounts.find((item) => item.id === (accountId || store.activeAccountId));
    if (!account) throw new Error("Connect a GitHub account to browse its repositories.");
    return githubRepositories(account);
  });

  ipcMain.handle("relay:clone-repository", async (_event, input) => {
    const remoteUrl = String(input?.remoteUrl || "").trim();
    const requestedParentPath = String(input?.parentPath || "").trim();
    const repositoryName = String(input?.repositoryName || "").trim();
    if (!/^(https?:\/\/|ssh:\/\/|git@)/i.test(remoteUrl)) throw new Error("Enter a valid HTTPS or SSH Git repository URL.");
    if (!requestedParentPath) throw new Error("Choose a local folder for the clone.");
    const parentPath = path.resolve(requestedParentPath);
    if (!repositoryName || repositoryName !== path.basename(repositoryName) || repositoryName === "." || repositoryName === "..") {
      throw new Error("Enter a valid folder name for the cloned repository.");
    }
    const parentStats = await fs.promises.stat(parentPath).catch(() => null);
    if (!parentStats?.isDirectory()) throw new Error("Choose an existing local folder for the clone.");
    const destinationPath = path.join(parentPath, repositoryName);
    if (fs.existsSync(destinationPath)) throw new Error(`A file or folder named ${repositoryName} already exists there.`);

    const store = readStore();
    const accountId = input?.accountId || store.activeAccountId;
    const account = store.accounts.find((item) => item.id === accountId);
    const token = account ? await accountToken(githubContext(), account.handle) : null;
    const repository = await cloneRepository(remoteUrl, destinationPath, token, account?.handle);
    const state = rememberRepository(repository);
    return { repository, state };
  });

  ipcMain.handle("relay:scan-folder", async (event) => {
    const result = await dialog.showOpenDialog(BrowserWindow.fromWebContents(event.sender), {
      title: "Scan a folder for Git repositories",
      properties: ["openDirectory"],
      buttonLabel: "Scan Folder",
    });
    if (result.canceled || !result.filePaths[0]) return null;
    const folderPath = result.filePaths[0];
    const repositoryPaths = await scanForRepositories(folderPath);
    const remembered = await rememberRepositoryPaths(repositoryPaths);
    return { ...remembered, found: repositoryPaths.length, folderPath };
  });

  ipcMain.handle("relay:remove-repository", (_event, repositoryPath) => {
    const resolvedPath = path.resolve(String(repositoryPath || ""));
    const store = readStore();
    store.repositories = store.repositories.filter((repository) => path.resolve(repository.path) !== resolvedPath);
    for (const storedPath of Object.keys(store.repositoryAccounts)) {
      if (path.resolve(storedPath) === resolvedPath) delete store.repositoryAccounts[storedPath];
    }
    writeStore(store);
    return publicState(store);
  });

  ipcMain.handle("relay:open-repository", async (_event, repositoryPath) => {
    const repository = await readRepository(repositoryPath);
    rememberRepository(repository);
    return repository;
  });

  ipcMain.handle("relay:refresh-repository", async (_event, repositoryPath) => {
    const repository = await readRepository(repositoryPath);
    rememberRepository(repository);
    return repository;
  });

  ipcMain.handle("relay:get-file-diff", (_event, repositoryPath, filePath) => getFileDiff(repositoryPath, filePath));

  ipcMain.handle("relay:commit", async (_event, input) => {
    const store = readStore();
    const accountId = input.accountId || store.activeAccountId;
    const account = store.accounts.find((item) => item.id === accountId);
    if (!account) throw new Error("Connect and select a GitHub account before committing.");
    await commitFiles(input.repositoryPath, input.files, input.summary, input.description, account);
    const repository = await readRepository(input.repositoryPath);
    rememberRepository(repository);
    return repository;
  });

  ipcMain.handle("relay:fetch-origin", async (_event, repositoryPath, accountId) => {
    const store = readStore();
    const resolvedId = accountId || store.activeAccountId;
    const account = store.accounts.find((item) => item.id === resolvedId);
    const token = account ? await accountToken(githubContext(), account.handle) : null;
    await fetchOrigin(repositoryPath, token, account?.handle);
    return readRepository(repositoryPath);
  });

  ipcMain.handle("relay:push-origin", async (_event, repositoryPath, accountId) => {
    const store = readStore();
    const resolvedId = accountId || store.activeAccountId;
    const account = store.accounts.find((item) => item.id === resolvedId);
    const token = account ? await accountToken(githubContext(), account.handle) : null;
    await pushOrigin(repositoryPath, token, account?.handle);
    return readRepository(repositoryPath);
  });

  ipcMain.handle("relay:switch-branch", async (_event, repositoryPath, branch) => {
    await switchBranch(repositoryPath, branch);
    return readRepository(repositoryPath);
  });

  ipcMain.handle("relay:connect-account", async (event) => {
    let devicePageOpened = false;
    await loginWithGitHub(githubContext(), (progress) => {
      if (event.sender.isDestroyed()) return;
      event.sender.send("relay:github-login-progress", progress);
      if (devicePageOpened || !progress.code || progress.verificationUrl !== GITHUB_DEVICE_URL) return;
      devicePageOpened = true;
      void shell.openExternal(GITHUB_DEVICE_URL).catch(() => {
        if (!event.sender.isDestroyed()) {
          event.sender.send("relay:github-login-progress", {
            ...progress,
            browserOpenFailed: true,
            message: "GitHub did not open automatically. Use the button below to open it.",
          });
        }
      });
    });
    return syncGitHubAccounts();
  });

  ipcMain.handle("relay:set-active-account", async (_event, accountId) => {
    const store = readStore();
    const account = store.accounts.find((item) => item.id === accountId);
    if (!account) throw new Error("Account not found.");
    await switchGitHubAccount(githubContext(), account.handle);
    return syncGitHubAccounts();
  });

  ipcMain.handle("relay:set-account-email", (_event, accountId, requestedEmail) => {
    const store = readStore();
    const account = store.accounts.find((item) => item.id === accountId);
    if (!account) throw new Error("Account not found.");
    const email = String(requestedEmail || "").trim() || `${account.githubId}+${account.handle}@users.noreply.github.com`;
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) throw new Error("Enter a valid email address.");
    account.email = email;
    writeStore(store);
    return publicState(store);
  });

  ipcMain.handle("relay:set-repository-account", (_event, repositoryPath, accountId) => {
    const store = readStore();
    if (accountId && !store.accounts.some((account) => account.id === accountId)) throw new Error("Account not found.");
    if (accountId) store.repositoryAccounts[repositoryPath] = accountId;
    else delete store.repositoryAccounts[repositoryPath];
    writeStore(store);
    return publicState(store);
  });

  ipcMain.handle("relay:remove-account", async (_event, accountId) => {
    const store = readStore();
    const account = store.accounts.find((item) => item.id === accountId);
    if (!account) throw new Error("Account not found.");
    await removeGitHubAccount(githubContext(), account.handle);
    return syncGitHubAccounts();
  });

  ipcMain.handle("relay:open-external", (_event, url) => {
    if (typeof url === "string" && url.startsWith("https://")) return shell.openExternal(url);
  });
}

function createWindow() {
  const window = new BrowserWindow({
    width: 1420,
    height: 880,
    minWidth: 820,
    minHeight: 600,
    show: false,
    backgroundColor: "#e9ece7",
    title: "Relay",
    titleBarStyle: isMac ? "hiddenInset" : "default",
    trafficLightPosition: isMac ? { x: 18, y: 23 } : undefined,
    autoHideMenuBar: false,
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  window.once("ready-to-show", () => window.show());
  window.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith("https://")) shell.openExternal(url);
    return { action: "deny" };
  });
  window.webContents.on("will-navigate", (event, url) => {
    if (!url.startsWith("file://")) {
      event.preventDefault();
      if (url.startsWith("https://")) shell.openExternal(url);
    }
  });
  window.loadFile(path.join(__dirname, "..", "desktop-dist", "index.html"));
}

app.whenReady().then(() => {
  registerIpc();
  installApplicationMenu();
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  if (!isMac) app.quit();
});
