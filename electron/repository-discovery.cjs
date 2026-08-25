const fs = require("fs");
const path = require("path");

const ignoredDirectories = new Set([".git", "node_modules"]);

async function isGitWorktree(directoryPath, entries) {
  const marker = entries.find((entry) => entry.name === ".git");
  if (!marker) return false;
  if (marker.isDirectory()) return true;
  if (!marker.isFile()) return false;
  try {
    const value = await fs.promises.readFile(path.join(directoryPath, ".git"), "utf8");
    return /^gitdir:\s*.+/im.test(value);
  } catch {
    return false;
  }
}

async function scanForRepositories(rootPath, maximum = 5000) {
  const root = path.resolve(rootPath);
  const queue = [root];
  let queueIndex = 0;
  const repositories = [];
  const visited = new Set();

  while (queueIndex < queue.length && repositories.length < maximum) {
    const directoryPath = queue[queueIndex++];
    let realPath;
    try {
      realPath = await fs.promises.realpath(directoryPath);
    } catch {
      continue;
    }
    if (visited.has(realPath)) continue;
    visited.add(realPath);

    let entries;
    try {
      entries = await fs.promises.readdir(directoryPath, { withFileTypes: true });
    } catch {
      continue;
    }
    if (await isGitWorktree(directoryPath, entries)) {
      repositories.push(directoryPath);
      continue;
    }

    for (const entry of entries) {
      if (!entry.isDirectory() || entry.isSymbolicLink() || ignoredDirectories.has(entry.name)) continue;
      queue.push(path.join(directoryPath, entry.name));
    }
  }

  return repositories.sort((left, right) => left.localeCompare(right));
}

module.exports = { scanForRepositories };
