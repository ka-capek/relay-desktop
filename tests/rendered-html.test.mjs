import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";

const require = createRequire(import.meta.url);
const { GITHUB_DEVICE_URL, loginProgressFromOutput } = require("../electron/github-auth.cjs");
const { applyManualOrder, normalizeOrdering } = require("../electron/repository-order.cjs");
const { commandIds, menuDescriptor, menuTemplate } = require("../electron/application-menu.cjs");
const {
  cloneRepository,
  fetchOrigin,
  commitFiles,
  isSshRemote,
  pushOrigin,
} = require("../electron/git-service.cjs");
const {
  describeSshResult,
  isGitHubRemote,
  parseSshRemote,
  sshCommandFor,
} = require("../electron/ssh-service.cjs");
const {
  readCommitDetail,
  readCommitFileDiff,
  readHistoryPage,
  readRepositorySummary,
} = require("../electron/git-service.cjs");

/** Builds a fixture with a root commit, a branch, a merge, a tag and a deletion. */
async function historyFixture() {
  const root = await mkdtemp(path.join(tmpdir(), "relay-history-"));
  const git = (args) => execFileSync("git", args, {
    cwd: root,
    stdio: "pipe",
    env: {
      ...process.env,
      GIT_AUTHOR_NAME: "Ada Lovelace", GIT_AUTHOR_EMAIL: "ada@example.com",
      GIT_COMMITTER_NAME: "Grace Hopper", GIT_COMMITTER_EMAIL: "grace@example.com",
    },
  });

  git(["init", "-q", "-b", "main", "."]);
  await writeFile(path.join(root, "root.txt"), "root\n");
  git(["add", "."]);
  git(["commit", "-q", "-m", "Root commit", "-m", "Body of the root commit."]);
  await writeFile(path.join(root, "gone.txt"), "temporary\n");
  git(["add", "."]);
  git(["commit", "-q", "-m", "Add a file that will be removed"]);
  git(["checkout", "-q", "-b", "side"]);
  await writeFile(path.join(root, "side.txt"), "side\n");
  git(["add", "."]);
  git(["commit", "-q", "-m", "Side branch work"]);
  git(["checkout", "-q", "main"]);
  await writeFile(path.join(root, "main.txt"), "main\n");
  git(["add", "."]);
  git(["commit", "-q", "-m", "Main only change"]);
  git(["merge", "-q", "--no-ff", "side", "-m", "Merge side into main"]);
  git(["tag", "v1.0.0"]);
  git(["rm", "-q", "gone.txt"]);
  git(["commit", "-q", "-m", "Remove the temporary file"]);
  return root;
}

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);

  return worker.fetch(
    new Request("http://localhost/", { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("server-renders Relay with no repository selected", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>Relay — Git, without the account juggling<\/title>/i);
  assert.match(html, /Current repository/);
  assert.match(html, /Open a repository/);
  assert.match(html, /No repository open/);
  assert.match(html, /Open, clone, or scan a folder for repositories\./);
  assert.doesNotMatch(html, /git-fixture|All systems operational/i);
});

test("extracts a sanitized GitHub device-login progress event", () => {
  assert.deepEqual(
    loginProgressFromOutput(
      "One-time code (ABCD-1234) copied to clipboard\nOpen this URL to continue: https://github.com/login/device",
      "Open this URL to continue: https://github.com/login/device",
    ),
    {
      code: "ABCD-1234",
      verificationUrl: GITHUB_DEVICE_URL,
      message: "Enter this one-time code in the GitHub window.",
    },
  );
});

test("upgrades a repository store written before ordering existed", () => {
  const legacy = {
    repositories: [
      { path: "/b", name: "b", lastOpened: "2026-01-02T00:00:00.000Z" },
      { path: "/a", name: "a", lastOpened: "2026-01-01T00:00:00.000Z" },
    ],
  };

  const upgraded = normalizeOrdering(legacy);

  assert.deepEqual(upgraded.repositoryOrder, { mode: "manual", direction: "asc" });
  // Manual order seeds from the existing list rather than reshuffling it.
  assert.deepEqual(upgraded.manualOrder, ["/b", "/a"]);
  // addedAt backfills from lastOpened, which is never later than the truth.
  assert.equal(upgraded.repositories[0].addedAt, "2026-01-02T00:00:00.000Z");
  assert.equal(upgraded.repositories[1].addedAt, "2026-01-01T00:00:00.000Z");
  // A repository with no commits sorts last rather than breaking the list.
  assert.equal(upgraded.repositories[0].latestCommit, null);
});

test("keeps the manual order consistent with the remembered repositories", () => {
  const store = normalizeOrdering({
    repositories: [{ path: "/a" }, { path: "/b" }, { path: "/c" }],
    // A stale path, a duplicate, and a repository missing from the order.
    manualOrder: ["/c", "/removed", "/c", "/a"],
    repositoryOrder: { mode: "nonsense", direction: "sideways" },
  });

  assert.deepEqual(store.manualOrder, ["/c", "/a", "/b"]);
  assert.deepEqual(store.repositoryOrder, { mode: "manual", direction: "asc" });

  // A renderer-supplied order cannot introduce unknown paths or drop a
  // repository out of the sidebar.
  applyManualOrder(store, ["/b", "/ghost", "/a"]);
  assert.deepEqual(store.manualOrder, ["/b", "/a", "/c"]);

  assert.throws(() => applyManualOrder(store, "not-a-list"), /list of repository paths/);
});

test("reads the HEAD commit date used to sort the sidebar", async () => {
  const root = await mkdtemp(path.join(tmpdir(), "relay-order-"));
  try {
    const withCommits = path.join(root, "with-commits");
    const withoutCommits = path.join(root, "without-commits");
    await mkdir(withCommits);
    await mkdir(withoutCommits);

    const git = (cwd, args) => execFileSync("git", args, { cwd, stdio: "pipe" });
    for (const repository of [withCommits, withoutCommits]) git(repository, ["init", "-q", "-b", "main", "."]);
    await writeFile(path.join(withCommits, "a.txt"), "hello\n");
    git(withCommits, ["add", "a.txt"]);
    git(withCommits, ["-c", "user.email=t@example.com", "-c", "user.name=Test", "commit", "-q", "-m", "first"]);

    const committed = await readRepositorySummary(withCommits);
    assert.match(committed.latestCommit, /^\d{4}-\d{2}-\d{2}T/);

    // A repository with no commits must read cleanly and simply have no date.
    const empty = await readRepositorySummary(withoutCommits);
    assert.equal(empty.latestCommit, null);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("pages through history without gaps, duplicates or an artificial cap", async () => {
  const root = await historyFixture();
  try {
    const first = await readHistoryPage(root, { limit: 3 });
    assert.equal(first.commits.length, 3);
    assert.equal(first.endOfHistory, false);

    // Later pages are anchored to the same commit, so a moving HEAD cannot
    // make the list skip or repeat a commit at a batch boundary.
    const collected = [...first.commits];
    let page = first;
    while (!page.endOfHistory) {
      page = await readHistoryPage(root, { skip: collected.length, limit: 3, anchor: first.anchor });
      collected.push(...page.commits);
    }

    assert.equal(collected.length, 6);
    assert.equal(new Set(collected.map((commit) => commit.fullHash)).size, 6);
    assert.equal(collected.at(-1).title, "Root commit");
    // Decorations reach the renderer so branches and tags can be shown.
    assert.ok(collected[0].refs.some((ref) => ref.includes("main")));
    assert.ok(collected.some((commit) => commit.refs.includes("tag: v1.0.0")));
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("describes root, merge and deletion commits correctly", async () => {
  const root = await historyFixture();
  try {
    const { commits } = await readHistoryPage(root, { limit: 20 });
    const find = (title) => commits.find((commit) => commit.title === title).fullHash;

    const rootCommit = await readCommitDetail(root, find("Root commit"));
    assert.equal(rootCommit.isRoot, true);
    assert.deepEqual(rootCommit.parents, []);
    assert.equal(rootCommit.body, "Body of the root commit.");
    assert.deepEqual(rootCommit.files.map((file) => `${file.status} ${file.path}`), ["A root.txt"]);
    // The author and committer are genuinely different people here.
    assert.equal(rootCommit.author, "Ada Lovelace");
    assert.equal(rootCommit.committer, "Grace Hopper");

    // A merge is reported against its first parent, so it shows what landing
    // the branch brought in rather than the whole combined tree.
    const merge = await readCommitDetail(root, find("Merge side into main"));
    assert.equal(merge.isMerge, true);
    assert.equal(merge.parents.length, 2);
    assert.deepEqual(merge.files.map((file) => `${file.status} ${file.path}`), ["A side.txt"]);

    const removal = await readCommitDetail(root, find("Remove the temporary file"));
    assert.deepEqual(removal.files.map((file) => `${file.status} ${file.path}`), ["D gone.txt"]);
    assert.equal(removal.removed, 1);

    // A root commit has no parent to diff against, so it compares to the empty tree.
    assert.match(await readCommitFileDiff(root, rootCommit.fullHash, "root.txt"), /^@@|\+root/m);
    assert.match(await readCommitFileDiff(root, merge.fullHash, "side.txt"), /\+side/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("refuses commit hashes that are malformed or from another repository", async () => {
  const [root, other] = await Promise.all([historyFixture(), historyFixture()]);
  try {
    for (const bad of ["", "--output=/tmp/relay-pwn", "abc123; rm -rf /", "../../etc/passwd"]) {
      await assert.rejects(() => readCommitDetail(root, bad), /Invalid commit hash/);
    }

    // Well formed but absent, and a real commit belonging to a different clone.
    await assert.rejects(() => readCommitDetail(root, "0".repeat(40)), /not in this repository/);

    // The two fixtures are built identically, so Git gives them identical
    // commit hashes. This extra commit makes the second repository genuinely
    // distinct, which is the case worth guarding.
    await writeFile(path.join(other, "unique.txt"), `unique ${Date.now()} ${Math.random()}\n`);
    execFileSync("git", ["add", "."], { cwd: other, stdio: "pipe" });
    execFileSync("git", ["-c", "user.name=Other", "-c", "user.email=other@example.com", "commit", "-q", "-m", "Only in the other repository"], { cwd: other, stdio: "pipe" });

    const foreign = (await readHistoryPage(other, { limit: 1 })).commits[0].fullHash;
    await assert.rejects(() => readCommitDetail(root, foreign), /not in this repository/);

    const head = (await readHistoryPage(root, { limit: 1 })).commits[0].fullHash;
    await assert.rejects(() => readCommitFileDiff(root, head, ""), /Choose a file to compare/);
  } finally {
    await Promise.all([rm(root, { recursive: true, force: true }), rm(other, { recursive: true, force: true })]);
  }
});

test("builds the native menu and the in-window menu bar from one definition", () => {
  const nativeWindows = menuTemplate({ isMac: false, appName: "Relay", onAction: () => {} });
  const nativeMac = menuTemplate({ isMac: true, appName: "Relay", onAction: () => {} });
  const rendererWindows = menuDescriptor({ isMac: false });

  // Same menus, same order, in both the native template and the descriptor the
  // renderer draws on the Windows title row.
  assert.deepEqual(rendererWindows.map((menu) => menu.label), ["File", "Edit", "View", "Window"]);
  assert.deepEqual(
    nativeWindows.map((menu) => menu.label.replace("&", "")),
    rendererWindows.map((menu) => menu.label),
  );

  // macOS keeps its application menu and never shows Windows mnemonics.
  assert.equal(nativeMac[0].label, "Relay");
  assert.ok(nativeMac.every((menu) => !menu.label.includes("&")));
  assert.ok(nativeWindows.every((menu) => menu.label.includes("&")));

  // Exit belongs to Windows only; macOS quits from the application menu.
  const macFile = nativeMac.find((menu) => menu.label === "File");
  assert.ok(!macFile.submenu.some((item) => item.label === "Exit"));
  assert.ok(nativeWindows[0].submenu.some((item) => item.label?.includes("xit")));

  // Accelerators stay on the native menu, which is what keeps them working
  // while the Windows menu bar is hidden.
  const nativeFile = nativeWindows.find((menu) => menu.label === "&File");
  assert.equal(nativeFile.submenu[0].accelerator, "CmdOrCtrl+O");
  assert.equal(nativeFile.submenu[1].accelerator, "CmdOrCtrl+Shift+O");

  // Every renderer command id is one the main process will accept.
  const allowed = commandIds();
  for (const menu of rendererWindows) {
    for (const item of menu.items) if (item.id) assert.ok(allowed.has(item.id), `${item.id} is not an allowed command`);
  }
});

test("keeps one repository action row without a duplicate account pill", async () => {
  const [page, css] = await Promise.all([
    readFile(new URL("../app/page.tsx", import.meta.url), "utf8"),
    readFile(new URL("../app/globals.css", import.meta.url), "utf8"),
  ]);

  // Current repository, then branch, then the remote control, then a spacer,
  // then the account switcher. The spacer must not come before the sync button.
  const row = page.slice(page.indexOf('<section className="repo-bar">'), page.indexOf("<div className=\"workspace\">"));
  const order = ["repo-picker", "branch-control", "fetch-button", "toolbar-spacer", "account-wrap"];
  let cursor = -1;
  for (const marker of order) {
    const next = row.indexOf(marker, cursor + 1);
    assert.ok(next > cursor, `${marker} is out of order in the repository action row`);
    cursor = next;
  }

  // The duplicated "Using @account" pill is gone entirely, not relocated.
  assert.doesNotMatch(page, /identity-pill/);
  assert.doesNotMatch(css, /identity-pill/);
  assert.doesNotMatch(page, /Using <strong>/);

  // macOS folds the chrome into the single action row; Windows keeps a title
  // row and reserves space for the native window controls.
  assert.match(css, /html\.desktop\.macos \.titlebar \{ display: none; \}/);
  assert.match(css, /html\.desktop\.macos \.repo-bar \{ -webkit-app-region: drag;/);
  assert.match(css, /html\.desktop\.windows \.titlebar \{ padding-right: 150px;/);
});

test("recognises SSH remotes and keeps GitHub separate from other hosts", () => {
  assert.deepEqual(parseSshRemote("git@gitlab.example.com:team/tools.git"),
    { user: "git", host: "gitlab.example.com", port: null, path: "team/tools.git" });
  assert.deepEqual(parseSshRemote("ssh://git@git.internal:2222/srv/tools.git"),
    { user: "git", host: "git.internal", port: 2222, path: "/srv/tools.git" });

  // A Windows drive letter and a plain local path are not SSH remotes.
  assert.equal(parseSshRemote("C:\\Users\\me\\repo"), null);
  assert.equal(parseSshRemote("/Users/me/repo"), null);
  assert.equal(parseSshRemote("https://gitlab.com/a/b.git"), null);

  // GitHub is recognised in both its HTTPS and SSH forms, so it keeps using
  // the OAuth path and never picks up an SSH identity.
  for (const remote of ["https://github.com/a/b.git", "git@github.com:a/b.git", "ssh://git@github.com/a/b.git"]) {
    assert.equal(isGitHubRemote(remote), true, remote);
  }
  for (const remote of ["git@gitlab.com:a/b.git", "https://gitlab.com/a/b.git"]) {
    assert.equal(isGitHubRemote(remote), false, remote);
  }

  // Only an SSH remote can receive GIT_SSH_COMMAND.
  assert.equal(isSshRemote("git@gitlab.example.com:t/x.git"), true);
  assert.equal(isSshRemote("https://gitlab.com/t/x.git"), false);

  // A profile with no key changes nothing, so the agent and ~/.ssh/config win.
  assert.equal(sshCommandFor({ host: "gitlab.example.com" }), null);
  assert.equal(sshCommandFor({ host: "has space" }), null);

  // Paths are POSIX-quoted, including one containing a quote and a space.
  assert.equal(
    sshCommandFor({ host: "h", identityFile: "/keys/my key/id_'x" }),
    "'ssh' -i '/keys/my key/id_'\\''x' -o 'IdentitiesOnly=yes'",
  );
});

test("explains SSH failures without leaking key material", () => {
  const cases = [
    ["Hi acme! You've successfully authenticated", true, /accepted the key/],
    ["Permission denied (publickey).", false, /refused the key/],
    ["ssh: Could not resolve hostname nope", false, /could not be resolved/],
    ["Host key verification failed.", false, /not trusted yet/],
    ["ssh: connect to host x port 22: Connection timed out", false, /did not answer/],
    ["Enter passphrase for key '/home/me/.ssh/id_rsa':", false, /ssh-add/],
  ];
  for (const [output, ok, matcher] of cases) {
    const result = describeSshResult("git.example.com", 255, output);
    assert.equal(result.ok, ok, output);
    assert.match(result.message, matcher);
    // Every message names the host and stays one actionable sentence.
    assert.match(result.message, /git\.example\.com|ssh-add/);
    // A passphrase prompt must never echo the key path back at the user.
    assert.doesNotMatch(result.message, /id_rsa/);
  }
});

test("clones, fetches and pushes over SSH without a token", async () => {
  const root = await mkdtemp(path.join(tmpdir(), "relay-ssh-"));
  try {
    const run = (cwd, args, env) => execFileSync("git", args, { cwd, stdio: "pipe", env: { ...process.env, ...env } });

    // A bare repository standing in for the remote host.
    const server = path.join(root, "server.git");
    run(root, ["init", "-q", "--bare", "-b", "main", "server.git"]);
    const seed = path.join(root, "seed");
    await mkdir(seed);
    run(seed, ["init", "-q", "-b", "main", "."]);
    await writeFile(path.join(seed, "file.txt"), "hello over ssh\n");
    run(seed, ["add", "."]);
    run(seed, ["-c", "user.email=t@example.com", "-c", "user.name=T", "commit", "-q", "-m", "Initial over SSH"]);
    run(seed, ["remote", "add", "origin", server]);
    run(seed, ["push", "-q", "origin", "main"]);

    // Stands in for ssh: records its arguments, then runs the command the way
    // a real sshd would run it for Git.
    const shim = path.join(root, "fake-ssh");
    const log = path.join(root, "ssh-args.log");
    await writeFile(shim, `#!/bin/sh\necho "$@" >> ${JSON.stringify(log)}\nfor last; do :; done\nexec sh -c "$last"\n`, { mode: 0o755 });

    // Relay builds the command; only the executable is swapped for the shim.
    const key = path.join(root, "id_test");
    const sshCommand = sshCommandFor({ host: "git.example.com", identityFile: key })
      .replace(/^'ssh'/, `'${shim}'`);

    const remote = `git@git.example.com:${server}`;
    assert.equal(isSshRemote(remote), true);

    const cloned = path.join(root, "cloned");
    const repository = await cloneRepository(remote, cloned, null, null, sshCommand);
    assert.equal(repository.branch, "main");
    assert.equal(repository.history[0].title, "Initial over SSH");

    await fetchOrigin(cloned, null, null, sshCommand);

    await writeFile(path.join(cloned, "file.txt"), "hello over ssh\nsecond line\n");
    await commitFiles(cloned, ["file.txt"], "Push over SSH", "", { name: "T", email: "t@example.com" });
    await pushOrigin(cloned, null, null, sshCommand);

    // The push really reached the remote.
    const serverLog = execFileSync("git", ["-C", server, "log", "--oneline", "main"], { encoding: "utf8" });
    assert.match(serverLog, /Push over SSH/);

    // The identity Relay chose actually reached ssh.
    const args = await readFile(log, "utf8");
    assert.match(args, /-i .*id_test/);
    assert.match(args, /IdentitiesOnly=yes/);
    assert.match(args, /git-upload-pack/);
    assert.match(args, /git-receive-pack/);

    // No GitHub credential was involved anywhere in that exchange.
    assert.doesNotMatch(args, /RELAY_GIT_TOKEN|credential\.helper|Authorization/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("submits the commit-email modal from the keyboard", async () => {
  const page = await readFile(new URL("../app/page.tsx", import.meta.url), "utf8");

  // Enter only submits when the controls are inside a real form, so the button
  // and the Enter key have to share one submit handler.
  assert.match(page, /<form onSubmit=\{saveAccountEmail\} noValidate>/);
  assert.match(page, /<button type="submit" className="primary-modal-button"/);
  assert.match(page, /<button type="button" className="secondary-modal-button"/);

  // Repeated Enter presses must not start concurrent saves, and the busy string
  // cannot guard that on its own because it lands a render too late.
  assert.match(page, /if \(savingEmailRef\.current\) return;/);
  assert.match(page, /savingEmailRef\.current = true;/);
  assert.match(page, /savingEmailRef\.current = false;/);

  // A blank value still has to reach the main process, which turns it back into
  // the GitHub noreply address.
  assert.match(page, /placeholder="Leave blank to use GitHub noreply"/);
});

test("keeps native repository and multi-account workflows wired", async () => {
  const [page, main, preload, gitService, discovery, applicationMenu, css, packageJson] = await Promise.all([
    readFile(new URL("../app/page.tsx", import.meta.url), "utf8"),
    readFile(new URL("../electron/main.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/preload.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/git-service.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/repository-discovery.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/application-menu.cjs", import.meta.url), "utf8"),
    readFile(new URL("../app/globals.css", import.meta.url), "utf8"),
    readFile(new URL("../package.json", import.meta.url), "utf8"),
  ]);

  assert.match(packageJson, /"version": "0\.4\.0"/);
  // The menu labels now live in the shared definition both the native menu and
  // the Windows in-window menu bar are built from.
  assert.match(applicationMenu, /Clone Repository…/);
  assert.match(applicationMenu, /Scan Folder for Repositories…/);
  assert.match(main, /menuTemplate\(/);
  assert.match(main, /relay:list-github-repositories/);
  assert.match(main, /relay:remove-repository/);
  assert.match(main, /relay:set-account-email/);
  assert.match(main, /shell\.openExternal\(GITHUB_DEVICE_URL\)/);
  assert.match(preload, /listGitHubRepositories/);
  assert.match(preload, /onMenuAction/);
  assert.match(gitService, /GIT_EXEC_PATH/);
  assert.match(gitService, /cloneRepository/);
  assert.match(discovery, /isGitWorktree/);
  assert.match(page, /github-repository-picker/);
  assert.match(page, /document\.addEventListener\("pointerdown", dismissAccountMenu\)/);
  assert.match(page, /await navigator\.clipboard\.writeText\(code\)/);
  assert.match(page, /Code copied to the clipboard\./);
  assert.match(page, /Files were left untouched/);
  assert.doesNotMatch(css, /linear-gradient/);
});
