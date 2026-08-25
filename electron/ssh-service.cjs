"use strict";

/**
 * SSH identities for Git hosts that are not GitHub.com.
 *
 * These are deliberately not modelled as GitHub accounts. A GitHub account is
 * an OAuth identity held by the GitHub CLI; an SSH profile is only a hint about
 * which key to offer a host. No PAT is involved either way.
 *
 * Nothing secret is stored. A profile holds a host, an optional user, and an
 * optional path to a private key. Key contents and passphrases are never read,
 * copied, or persisted; OpenSSH and the SSH agent handle those, exactly as
 * they do for the user's own `git` on the command line.
 *
 * With no profile selected Relay sets nothing, so the user's ~/.ssh/config and
 * running agent behave normally. That is the default and covers most setups.
 */

const { execFile } = require("child_process");
const fs = require("fs");
const path = require("path");
const { promisify } = require("util");

const execFileAsync = promisify(execFile);

/** Matches `ssh://user@host:port/path` and the scp-like `user@host:path`. */
function parseSshRemote(remote) {
  const value = String(remote || "").trim();
  if (!value) return null;

  const explicit = value.match(/^ssh:\/\/(?:([^@/]+)@)?([^:/]+)(?::(\d+))?(\/.*)?$/i);
  if (explicit) {
    return { user: explicit[1] || null, host: explicit[2], port: explicit[3] ? Number(explicit[3]) : null, path: explicit[4] || "" };
  }

  // The scp-like form has no scheme and separates the path with a colon.
  // A Windows drive letter such as C:\repo must not be mistaken for one.
  const scp = value.match(/^(?:([^@\s]+)@)?([^@:\s/\\]+):(?!\/\/)(.+)$/);
  if (scp && !/^[a-zA-Z]$/.test(scp[2])) {
    return { user: scp[1] || null, host: scp[2], port: null, path: scp[3] };
  }
  return null;
}

function isGitHubRemote(remote) {
  const value = String(remote || "");
  if (/^https:\/\/github\.com\//i.test(value)) return true;
  const parsed = parseSshRemote(value);
  return parsed ? parsed.host.toLowerCase() === "github.com" : false;
}

/** POSIX single-quoting. Git hands GIT_SSH_COMMAND to a shell, including the bundled sh on Windows. */
function shellQuote(value) {
  return `'${String(value).replace(/'/g, "'\\''")}'`;
}

function sshExecutable() {
  if (process.platform !== "win32") return "ssh";
  // Git for Windows ships OpenSSH beside it.
  const roots = [
    process.resourcesPath && path.join(process.resourcesPath, "git"),
    path.join(__dirname, "..", "runtime", "git", "win-x64"),
  ].filter(Boolean);
  for (const root of roots) {
    const candidate = path.join(root, "usr", "bin", "ssh.exe");
    if (fs.existsSync(candidate)) return candidate;
  }
  return "ssh.exe";
}

function normalizeProfile(profile) {
  if (!profile || typeof profile !== "object") return null;
  const host = String(profile.host || "").trim().toLowerCase();
  if (!host || /[\s/\\]/.test(host)) return null;
  return {
    id: String(profile.id || "").trim() || `ssh-${host}-${Date.now()}`,
    label: String(profile.label || "").trim() || host,
    host,
    user: String(profile.user || "").trim() || null,
    port: Number.isInteger(profile.port) && profile.port > 0 && profile.port < 65536 ? profile.port : null,
    identityFile: String(profile.identityFile || "").trim() || null,
    // Offering only the named key is what makes several identities on one host
    // work; without it OpenSSH tries every agent key and the server rejects
    // the connection after too many attempts.
    identitiesOnly: profile.identitiesOnly !== false,
  };
}

/**
 * Builds the GIT_SSH_COMMAND for a profile, or null to leave Git alone.
 * A profile with no key only pins the port, so the agent still does the work.
 */
function sshCommandFor(profile) {
  const normalized = normalizeProfile(profile);
  if (!normalized) return null;

  const parts = [shellQuote(sshExecutable())];
  if (normalized.identityFile) {
    parts.push("-i", shellQuote(normalized.identityFile));
    if (normalized.identitiesOnly) parts.push("-o", shellQuote("IdentitiesOnly=yes"));
  }
  if (normalized.port) parts.push("-p", String(normalized.port));
  if (parts.length === 1) return null;
  return parts.join(" ");
}

/** Turns OpenSSH output into one actionable sentence that names the host. */
function describeSshResult(host, code, output) {
  const text = String(output || "");
  if (/successfully authenticated|you've successfully|welcome to|logged in as/i.test(text)) {
    return { ok: true, message: `${host} accepted the key.` };
  }
  if (/permission denied/i.test(text)) {
    return { ok: false, message: `${host} refused the key. Add the matching public key to your account on ${host}, or choose a different identity.` };
  }
  if (/could not resolve hostname|name or service not known|nodename nor servname/i.test(text)) {
    return { ok: false, message: `${host} could not be resolved. Check the host name and your network connection.` };
  }
  if (/host key verification failed|remote host identification has changed/i.test(text)) {
    return { ok: false, message: `The host key for ${host} is not trusted yet. Connect once with ssh in a terminal to review and accept it.` };
  }
  if (/connection timed out|operation timed out|connection refused/i.test(text)) {
    return { ok: false, message: `${host} did not answer on the SSH port. Check the port and whether the host is reachable.` };
  }
  if (/passphrase|agent has no identities|no such identity|unprotected private key/i.test(text)) {
    return { ok: false, message: `${host} needs a key your SSH agent is not offering. Add it with ssh-add in a terminal; Relay never handles passphrases.` };
  }
  // Many hosts answer a shell-less login with a non-zero code and a banner.
  if (code === 0) return { ok: true, message: `${host} accepted the connection.` };
  return { ok: false, message: `Relay could not authenticate to ${host}. Try the same remote with ssh in a terminal to see the full response.` };
}

/**
 * Tries an authentication-only connection.
 *
 * BatchMode stops OpenSSH prompting for a passphrase or a password behind an
 * invisible prompt, which would otherwise hang the app.
 */
async function testConnection(profile) {
  const normalized = normalizeProfile(profile);
  if (!normalized) throw new Error("Enter a host name for this SSH identity.");
  if (normalized.identityFile && !fs.existsSync(normalized.identityFile)) {
    throw new Error("That private key file does not exist. Check the path, or clear it to use your SSH agent.");
  }

  const args = ["-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=accept-new"];
  if (normalized.identityFile) {
    args.push("-i", normalized.identityFile);
    if (normalized.identitiesOnly) args.push("-o", "IdentitiesOnly=yes");
  }
  if (normalized.port) args.push("-p", String(normalized.port));
  args.push(`${normalized.user || "git"}@${normalized.host}`);

  try {
    const { stdout, stderr } = await execFileAsync(sshExecutable(), args, {
      encoding: "utf8",
      timeout: 20000,
      windowsHide: true,
    });
    return describeSshResult(normalized.host, 0, `${stdout}\n${stderr}`);
  } catch (error) {
    if (error.killed || error.signal) {
      return { ok: false, message: `${normalized.host} did not answer within 20 seconds.` };
    }
    return describeSshResult(normalized.host, error.code, `${error.stdout || ""}\n${error.stderr || ""}`);
  }
}

module.exports = {
  describeSshResult,
  isGitHubRemote,
  normalizeProfile,
  parseSshRemote,
  shellQuote,
  sshCommandFor,
  sshExecutable,
  testConnection,
};
