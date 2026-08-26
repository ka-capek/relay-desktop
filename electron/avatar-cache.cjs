"use strict";

/**
 * On-disk cache for GitHub profile pictures.
 *
 * The renderer has no network access and must not gain any, so avatars are
 * fetched in the main process and handed over as data URLs. They are cached on
 * disk so a launch costs no requests and works offline; an account whose avatar
 * has never been fetched simply falls back to its initials.
 *
 * An avatar URL is public profile data, not a credential, and no request
 * carries the account's OAuth token.
 */

const fs = require("fs");
const path = require("path");

// GitHub avatars are small; this is a sanity bound, not a target.
const MAX_AVATAR_BYTES = 512 * 1024;

const ALLOWED_TYPES = new Map([
  ["image/png", "png"],
  ["image/jpeg", "jpg"],
  ["image/gif", "gif"],
  ["image/webp", "webp"],
]);

function cacheDirectory(userDataPath) {
  return path.join(userDataPath, "avatars");
}

/** Account ids come from GitHub, but the value still ends up in a path. */
function cacheFile(userDataPath, accountId, extension) {
  const safe = String(accountId).replace(/[^a-zA-Z0-9._-]/g, "_");
  return path.join(cacheDirectory(userDataPath), `${safe}.${extension}`);
}

function findCached(userDataPath, accountId) {
  for (const extension of ALLOWED_TYPES.values()) {
    const file = cacheFile(userDataPath, accountId, extension);
    if (fs.existsSync(file)) return { file, extension };
  }
  return null;
}

function dataUrl(buffer, extension) {
  const type = [...ALLOWED_TYPES.entries()].find(([, value]) => value === extension)?.[0] || "image/png";
  return `data:${type};base64,${buffer.toString("base64")}`;
}

/** Reads a cached avatar as a data URL, or null when nothing is cached. */
function readCachedAvatar(userDataPath, accountId) {
  try {
    const cached = findCached(userDataPath, accountId);
    if (!cached) return null;
    return dataUrl(fs.readFileSync(cached.file), cached.extension);
  } catch {
    return null;
  }
}

/**
 * Only GitHub's own avatar hosts are ever fetched, so a tampered store cannot
 * turn this into a request to an arbitrary address.
 */
function isGitHubAvatarUrl(url) {
  try {
    const parsed = new URL(String(url));
    return parsed.protocol === "https:"
      && (parsed.hostname === "avatars.githubusercontent.com" || parsed.hostname.endsWith(".githubusercontent.com"));
  } catch {
    return false;
  }
}

/**
 * Fetches and caches an avatar, returning a data URL.
 *
 * Returns null on any failure. A missing avatar is a cosmetic problem and must
 * never break account synchronization or startup.
 */
async function fetchAvatar(userDataPath, accountId, avatarUrl, fetchImpl = fetch) {
  if (!isGitHubAvatarUrl(avatarUrl)) return null;
  try {
    const response = await fetchImpl(avatarUrl, { headers: { "User-Agent": "Relay-Desktop" } });
    if (!response.ok) return null;

    const contentType = String(response.headers.get("content-type") || "").split(";")[0].trim().toLowerCase();
    const extension = ALLOWED_TYPES.get(contentType);
    if (!extension) return null;

    const buffer = Buffer.from(await response.arrayBuffer());
    if (buffer.length === 0 || buffer.length > MAX_AVATAR_BYTES) return null;

    fs.mkdirSync(cacheDirectory(userDataPath), { recursive: true });
    // Replace any previously cached format for this account.
    for (const value of ALLOWED_TYPES.values()) {
      const stale = cacheFile(userDataPath, accountId, value);
      if (value !== extension && fs.existsSync(stale)) fs.rmSync(stale, { force: true });
    }
    fs.writeFileSync(cacheFile(userDataPath, accountId, extension), buffer, { mode: 0o600 });
    return dataUrl(buffer, extension);
  } catch {
    return null;
  }
}

/** Cached avatar if present, otherwise fetch it. Never throws. */
async function resolveAvatar(userDataPath, accountId, avatarUrl, fetchImpl = fetch) {
  return readCachedAvatar(userDataPath, accountId) || fetchAvatar(userDataPath, accountId, avatarUrl, fetchImpl);
}

/** Drops an account's cached avatar when the account is removed. */
function forgetAvatar(userDataPath, accountId) {
  for (const extension of ALLOWED_TYPES.values()) {
    const file = cacheFile(userDataPath, accountId, extension);
    try {
      if (fs.existsSync(file)) fs.rmSync(file, { force: true });
    } catch {
      // A cached image that cannot be removed is not worth failing a logout.
    }
  }
}

module.exports = {
  MAX_AVATAR_BYTES,
  cacheDirectory,
  fetchAvatar,
  forgetAvatar,
  isGitHubAvatarUrl,
  readCachedAvatar,
  resolveAvatar,
};
