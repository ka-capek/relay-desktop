const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const GITHUB_HOST = "github.com";
const GITHUB_DEVICE_URL = "https://github.com/login/device";

function githubCliPath({ isPackaged, appPath, resourcesPath, platform = process.platform }) {
  const executable = platform === "win32" ? "gh.exe" : "gh";
  const platformFolder = platform === "win32" ? "win-x64" : "mac-arm64";
  const bundled = isPackaged
    ? path.join(resourcesPath, "gh", executable)
    : path.join(appPath, "runtime", "gh", platformFolder, executable);
  return fs.existsSync(bundled) ? bundled : executable;
}

function githubCliEnvironment(userDataPath, interactive = false) {
  const environment = {
    ...process.env,
    GH_CONFIG_DIR: path.join(userDataPath, "github-cli"),
    GH_NO_UPDATE_NOTIFIER: "1",
  };
  delete environment.GH_TOKEN;
  delete environment.GITHUB_TOKEN;
  delete environment.GH_HOST;
  if (!interactive) environment.GH_PROMPT_DISABLED = "1";
  return environment;
}

function cleanOutput(value) {
  return value
    .replace(/\u001b\[[0-9;?]*[ -/]*[@-~]/g, "")
    .replace(/\r/g, "")
    .trim();
}

function runGitHubCli(context, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(githubCliPath(context), args, {
      env: githubCliEnvironment(context.userDataPath, options.interactive),
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";

    child.stdout.on("data", (chunk) => {
      const value = chunk.toString();
      stdout += value;
      options.onOutput?.(cleanOutput(value));
    });
    child.stderr.on("data", (chunk) => {
      const value = chunk.toString();
      stderr += value;
      options.onOutput?.(cleanOutput(value));
    });
    child.on("error", (error) => reject(new Error(`GitHub sign-in could not start: ${error.message}`)));
    child.on("close", (code) => {
      if (code === 0 || options.allowFailure) {
        resolve({ code, stdout, stderr });
        return;
      }
      const detail = cleanOutput(stderr || stdout).split("\n").filter(Boolean).at(-1);
      reject(new Error(detail || `GitHub CLI exited with code ${code}.`));
    });
  });
}

function accountsFromStatus(value) {
  let parsed;
  try {
    parsed = JSON.parse(value);
  } catch {
    return [];
  }
  const hostValue = parsed?.hosts?.[GITHUB_HOST];
  const accounts = Array.isArray(hostValue) ? hostValue : hostValue ? [hostValue] : [];
  return accounts
    .filter((account) => account?.login && account?.state !== "failure")
    .map((account) => ({
      handle: account.login,
      active: Boolean(account.active),
      state: account.state || "success",
      tokenSource: account.tokenSource || "credential store",
    }));
}

async function authenticatedAccounts(context) {
  const result = await runGitHubCli(
    context,
    ["auth", "status", "--hostname", GITHUB_HOST, "--json", "hosts"],
    { allowFailure: true },
  );
  return accountsFromStatus(result.stdout);
}

async function accountToken(context, handle) {
  const result = await runGitHubCli(context, ["auth", "token", "--hostname", GITHUB_HOST, "--user", handle]);
  const token = result.stdout.trim();
  if (!token) throw new Error(`GitHub credentials for @${handle} are unavailable. Sign in again.`);
  return token;
}

async function switchAccount(context, handle) {
  await runGitHubCli(context, ["auth", "switch", "--hostname", GITHUB_HOST, "--user", handle]);
}

async function removeAccount(context, handle) {
  await runGitHubCli(context, ["auth", "logout", "--hostname", GITHUB_HOST, "--user", handle]);
}

function loginProgressFromOutput(combinedOutput, latestOutput) {
  const code = combinedOutput.match(/\b[A-Z0-9]{4}-[A-Z0-9]{4}\b/)?.[0] || null;
  return {
    code,
    verificationUrl: code ? GITHUB_DEVICE_URL : null,
    message: code ? "Enter this one-time code in the GitHub window." : latestOutput.split("\n").filter(Boolean).at(-1),
  };
}

async function login(context, onProgress) {
  let combinedOutput = "";
  const emit = (value) => {
    if (!value) return;
    combinedOutput = `${combinedOutput}\n${value}`.slice(-4000);
    onProgress?.(loginProgressFromOutput(combinedOutput, value));
  };
  await runGitHubCli(
    context,
    ["auth", "login", "--hostname", GITHUB_HOST, "--git-protocol", "https", "--web", "--clipboard", "--skip-ssh-key"],
    { interactive: true, onOutput: emit },
  );
}

module.exports = {
  GITHUB_DEVICE_URL,
  accountToken,
  authenticatedAccounts,
  githubCliPath,
  login,
  loginProgressFromOutput,
  removeAccount,
  switchAccount,
};
