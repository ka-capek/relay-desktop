import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { readFile } from "node:fs/promises";
import test from "node:test";

import { execFileSync } from "node:child_process";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";

const require = createRequire(import.meta.url);
const { GITHUB_DEVICE_URL, loginProgressFromOutput } = require("../electron/github-auth.cjs");
const { applyManualOrder, normalizeOrdering } = require("../electron/repository-order.cjs");
const { readRepositorySummary } = require("../electron/git-service.cjs");

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
  const [page, main, preload, gitService, discovery, css, packageJson] = await Promise.all([
    readFile(new URL("../app/page.tsx", import.meta.url), "utf8"),
    readFile(new URL("../electron/main.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/preload.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/git-service.cjs", import.meta.url), "utf8"),
    readFile(new URL("../electron/repository-discovery.cjs", import.meta.url), "utf8"),
    readFile(new URL("../app/globals.css", import.meta.url), "utf8"),
    readFile(new URL("../package.json", import.meta.url), "utf8"),
  ]);

  assert.match(packageJson, /"version": "0\.4\.0"/);
  assert.match(main, /Clone Repository…/);
  assert.match(main, /Scan Folder for Repositories…/);
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
