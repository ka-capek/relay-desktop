import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { readFile } from "node:fs/promises";
import test from "node:test";

const require = createRequire(import.meta.url);
const { GITHUB_DEVICE_URL, loginProgressFromOutput } = require("../electron/github-auth.cjs");

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
