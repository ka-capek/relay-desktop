const { execFile } = require("child_process");
const fs = require("fs");
const path = require("path");
const { promisify } = require("util");

const execFileAsync = promisify(execFile);

function gitExecutable() {
  if (process.resourcesPath) {
    const packaged = process.platform === "win32"
      ? path.join(process.resourcesPath, "git", "cmd", "git.exe")
      : path.join(process.resourcesPath, "git", "bin", "git");
    if (fs.existsSync(packaged)) return packaged;
  }

  const development = process.platform === "win32"
    ? path.join(__dirname, "..", "runtime", "git", "win-x64", "cmd", "git.exe")
    : path.join(__dirname, "..", "runtime", "git", "mac-arm64", "bin", "git");
  if (fs.existsSync(development)) return development;
  return "git";
}

function gitProcessEnvironment(environment = {}) {
  const executable = gitExecutable();
  if (executable === "git") return { ...process.env, ...environment };

  const root = path.resolve(path.dirname(executable), "..");
  const windows = process.platform === "win32";
  const bundledPaths = windows
    ? [path.join(root, "cmd"), path.join(root, "mingw64", "bin"), path.join(root, "usr", "bin")]
    : [path.join(root, "bin"), path.join(root, "libexec", "git-core")];
  const bundled = {
    GIT_EXEC_PATH: windows ? path.join(root, "mingw64", "libexec", "git-core") : path.join(root, "libexec", "git-core"),
    GIT_TEMPLATE_DIR: windows ? path.join(root, "mingw64", "share", "git-core", "templates") : path.join(root, "share", "git-core", "templates"),
    GIT_CONFIG_SYSTEM: path.join(root, "etc", "gitconfig"),
    PATH: [...bundledPaths, process.env.PATH || ""].filter(Boolean).join(path.delimiter),
  };
  if (windows) bundled.GIT_SSL_CAINFO = path.join(root, "mingw64", "etc", "ssl", "certs", "ca-bundle.crt");
  return { ...process.env, ...bundled, ...environment };
}

async function git(repositoryPath, args, environment = {}) {
  try {
    const { stdout } = await execFileAsync(gitExecutable(), ["-C", repositoryPath, ...args], {
      encoding: "utf8",
      env: gitProcessEnvironment(environment),
      maxBuffer: 20 * 1024 * 1024,
      windowsHide: true,
    });
    return stdout.trimEnd();
  } catch (error) {
    const detail = String(error.stderr || error.stdout || error.message || "Git command failed").trim();
    throw new Error(detail.replace(/^fatal:\s*/i, ""));
  }
}

async function gitWithoutRepository(args, environment = {}, workingDirectory) {
  try {
    const { stdout } = await execFileAsync(gitExecutable(), args, {
      cwd: workingDirectory,
      encoding: "utf8",
      env: gitProcessEnvironment(environment),
      maxBuffer: 20 * 1024 * 1024,
      windowsHide: true,
    });
    return stdout.trimEnd();
  } catch (error) {
    const detail = String(error.stderr || error.stdout || error.message || "Git command failed").trim();
    throw new Error(detail.replace(/^fatal:\s*/i, ""));
  }
}

function repositoryIdentity(remote, root) {
  const normalized = remote.replace(/\\/g, "/");
  const github = normalized.match(/github\.com[/:]([^/]+)\/([^/]+?)(?:\.git)?$/i);
  return {
    owner: github?.[1] || path.basename(path.dirname(root)),
    name: github?.[2] || path.basename(root),
  };
}

function parseStatus(statusText, statText, root) {
  const stats = new Map();
  for (const line of statText.split("\n")) {
    if (!line) continue;
    const [added, removed, ...fileParts] = line.split("\t");
    const filePath = fileParts.join("\t");
    stats.set(filePath, {
      added: Number.isFinite(Number(added)) ? Number(added) : 0,
      removed: Number.isFinite(Number(removed)) ? Number(removed) : 0,
    });
  }

  return statusText.split("\n").filter(Boolean).map((line) => {
    const code = line.slice(0, 2);
    let filePath = line.slice(3).trim();
    if (filePath.includes(" -> ")) filePath = filePath.split(" -> ").pop();
    if (filePath.startsWith('"') && filePath.endsWith('"')) filePath = filePath.slice(1, -1);
    const status = code.includes("?") || code.includes("A") ? "A" : code.includes("D") ? "D" : "M";
    const values = stats.get(filePath) || { added: 0, removed: 0 };

    if (code === "??" && values.added === 0) {
      try {
        const buffer = fs.readFileSync(path.join(root, filePath));
        if (buffer.length < 2 * 1024 * 1024 && !buffer.includes(0)) {
          values.added = buffer.toString("utf8").split("\n").length;
        }
      } catch {
        // File stats are cosmetic; status remains usable if a file disappears.
      }
    }

    return {
      path: filePath,
      name: path.basename(filePath),
      directory: path.dirname(filePath) === "." ? "Repository root" : path.dirname(filePath),
      status,
      tone: status === "A" ? "added" : status === "D" ? "deleted" : "modified",
      added: values.added,
      removed: values.removed,
    };
  });
}

function parseHistory(logText) {
  return logText.split("\x1e").map((record) => record.trim()).filter(Boolean).map((record) => {
    const [fullHash, hash, title, author, email, date] = record.split("\x1f");
    return { fullHash, hash, title, author, email, date };
  });
}

async function readRepository(repositoryPath) {
  const root = await git(repositoryPath, ["rev-parse", "--show-toplevel"]);
  const [branch, status, remote, branches, history] = await Promise.all([
    git(root, ["branch", "--show-current"]).catch(() => ""),
    git(root, ["-c", "core.quotepath=false", "status", "--porcelain=v1", "--untracked-files=all"]),
    git(root, ["remote", "get-url", "origin"]).catch(() => ""),
    git(root, ["for-each-ref", "--format=%(refname:short)", "refs/heads/"]).catch(() => ""),
    git(root, ["log", "-30", "--pretty=format:%H%x1f%h%x1f%s%x1f%an%x1f%ae%x1f%aI%x1e"]).catch(() => ""),
  ]);

  let statText = "";
  try {
    statText = await git(root, ["diff", "--numstat", "HEAD", "--"]);
  } catch {
    statText = await git(root, ["diff", "--numstat", "--cached", "--"]).catch(() => "");
  }

  const identity = repositoryIdentity(remote, root);
  let ahead = 0;
  let behind = 0;
  let hasUpstream = false;
  try {
    const counts = await git(root, ["rev-list", "--left-right", "--count", "HEAD...@{upstream}"]);
    [ahead, behind] = counts.trim().split(/\s+/).map(Number);
    hasUpstream = true;
  } catch {
    if (remote && branch) {
      try {
        const counts = await git(root, ["rev-list", "--left-right", "--count", `HEAD...refs/remotes/origin/${branch}`]);
        [ahead, behind] = counts.trim().split(/\s+/).map(Number);
      } catch {
        ahead = history ? 1 : 0;
      }
    }
  }
  return {
    path: root,
    name: identity.name,
    owner: identity.owner,
    branch: branch || "detached HEAD",
    remote,
    branches: branches.split("\n").filter(Boolean),
    files: parseStatus(status, statText, root),
    history: parseHistory(history),
    ahead: Number.isFinite(ahead) ? ahead : 0,
    behind: Number.isFinite(behind) ? behind : 0,
    hasUpstream,
  };
}

async function readRepositorySummary(repositoryPath) {
  const root = await git(repositoryPath, ["rev-parse", "--show-toplevel"]);
  const [branch, status, remote] = await Promise.all([
    git(root, ["branch", "--show-current"]).catch(() => ""),
    git(root, ["-c", "core.quotepath=false", "status", "--porcelain=v1", "--untracked-files=all"]),
    git(root, ["remote", "get-url", "origin"]).catch(() => ""),
  ]);
  const identity = repositoryIdentity(remote, root);
  return {
    path: root,
    name: identity.name,
    owner: identity.owner,
    branch: branch || "detached HEAD",
    changes: status.split("\n").filter(Boolean).length,
    lastOpened: new Date().toISOString(),
  };
}

async function getFileDiff(repositoryPath, filePath) {
  const absolutePath = path.join(repositoryPath, filePath);
  const status = await git(repositoryPath, ["-c", "core.quotepath=false", "status", "--porcelain=v1", "--", filePath]);
  if (status.startsWith("??")) {
    const buffer = fs.readFileSync(absolutePath);
    if (buffer.includes(0)) return "Binary file — preview unavailable";
    const lines = buffer.toString("utf8").split("\n");
    return [`--- /dev/null`, `+++ b/${filePath}`, `@@ -0,0 +1,${lines.length} @@`, ...lines.map((line) => `+${line}`)].join("\n");
  }
  try {
    return await git(repositoryPath, ["-c", "core.quotepath=false", "diff", "HEAD", "--no-ext-diff", "--unified=3", "--", filePath]);
  } catch {
    return git(repositoryPath, ["-c", "core.quotepath=false", "diff", "--cached", "--no-ext-diff", "--unified=3", "--", filePath]);
  }
}

async function commitFiles(repositoryPath, files, summary, description, account) {
  if (!Array.isArray(files) || files.length === 0) throw new Error("Select at least one changed file.");
  if (!String(summary || "").trim()) throw new Error("Enter a commit summary.");
  await git(repositoryPath, ["add", "--", ...files]);
  const message = [String(summary).trim(), String(description || "").trim()].filter(Boolean);
  const args = ["-c", `user.name=${account.name}`, "-c", `user.email=${account.email}`, "commit", "--only"];
  for (const paragraph of message) args.push("-m", paragraph);
  args.push("--", ...files);
  await git(repositoryPath, args, {
    GIT_AUTHOR_NAME: account.name,
    GIT_AUTHOR_EMAIL: account.email,
    GIT_COMMITTER_NAME: account.name,
    GIT_COMMITTER_EMAIL: account.email,
  });
}

async function fetchOrigin(repositoryPath, token, handle) {
  const remote = await git(repositoryPath, ["remote", "get-url", "origin"]).catch(() => "");
  if (!remote) throw new Error("This repository does not have an origin remote.");
  const args = [];
  const environment = { GIT_TERMINAL_PROMPT: "0" };
  if (token && /^https:\/\/github\.com\//i.test(remote)) {
    args.push(
      "-c", "credential.helper=",
      "-c", `credential.helper=!f() { echo username=${handle || "x-access-token"}; echo password=$RELAY_GIT_TOKEN; }; f`,
    );
    environment.RELAY_GIT_TOKEN = token;
  }
  args.push("fetch", "origin", "--prune");
  await git(repositoryPath, args, environment);
}

async function pushOrigin(repositoryPath, token, handle) {
  const remote = await git(repositoryPath, ["remote", "get-url", "origin"]).catch(() => "");
  if (!remote) throw new Error("This repository does not have an origin remote.");
  const branch = await git(repositoryPath, ["branch", "--show-current"]);
  if (!branch) throw new Error("Switch to a branch before pushing.");
  const args = [];
  const environment = { GIT_TERMINAL_PROMPT: "0" };
  if (token && /^https:\/\/github\.com\//i.test(remote)) {
    args.push(
      "-c", "credential.helper=",
      "-c", `credential.helper=!f() { echo username=${handle || "x-access-token"}; echo password=$RELAY_GIT_TOKEN; }; f`,
    );
    environment.RELAY_GIT_TOKEN = token;
  }
  args.push("push", "--set-upstream", "origin", "HEAD");
  await git(repositoryPath, args, environment);
}

async function cloneRepository(remoteUrl, destinationPath, token, handle) {
  const args = [];
  const environment = { GIT_TERMINAL_PROMPT: "0" };
  if (token && /^https:\/\/github\.com\//i.test(remoteUrl)) {
    args.push(
      "-c", "credential.helper=",
      "-c", `credential.helper=!f() { echo username=${handle || "x-access-token"}; echo password=$RELAY_GIT_TOKEN; }; f`,
    );
    environment.RELAY_GIT_TOKEN = token;
  }
  args.push("clone", "--progress", "--", remoteUrl, destinationPath);
  await gitWithoutRepository(args, environment, path.dirname(destinationPath));
  return readRepository(destinationPath);
}

async function switchBranch(repositoryPath, branch) {
  if (!branch || /[^\w./-]/.test(branch)) throw new Error("Invalid branch name.");
  await git(repositoryPath, ["switch", branch]);
}

module.exports = {
  cloneRepository,
  commitFiles,
  fetchOrigin,
  getFileDiff,
  pushOrigin,
  readRepository,
  readRepositorySummary,
  switchBranch,
};
