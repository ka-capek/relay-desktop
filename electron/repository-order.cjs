"use strict";

/**
 * Sidebar ordering rules for Relay's remembered repositories.
 *
 * This lives outside main.cjs so it can be exercised without an Electron
 * runtime, and because the normalization has to stay backward compatible with
 * stores written before ordering existed.
 */

const REPOSITORY_ORDER_MODES = ["manual", "age", "name", "latest"];
const REPOSITORY_ORDER_DIRECTIONS = ["asc", "desc"];

const DEFAULT_ORDER = { mode: "manual", direction: "asc" };

/**
 * Fills in repositoryOrder, manualOrder, and per-repository addedAt in place.
 *
 * Stores written by an earlier Relay have none of these. Normalizing on every
 * read means the rest of the main process can assume they are present, and the
 * file stays readable by an older build because nothing is removed.
 */
function normalizeOrdering(store) {
  const requested = store.repositoryOrder || {};
  store.repositoryOrder = {
    mode: REPOSITORY_ORDER_MODES.includes(requested.mode) ? requested.mode : DEFAULT_ORDER.mode,
    direction: REPOSITORY_ORDER_DIRECTIONS.includes(requested.direction) ? requested.direction : DEFAULT_ORDER.direction,
  };

  const known = new Set(store.repositories.map((repository) => repository.path));
  const seen = new Set();
  const manualOrder = [];

  // Drops paths for repositories that are no longer remembered, and any
  // duplicate a crash or a concurrent write could have left behind.
  for (const repositoryPath of Array.isArray(store.manualOrder) ? store.manualOrder : []) {
    if (typeof repositoryPath !== "string" || seen.has(repositoryPath) || !known.has(repositoryPath)) continue;
    seen.add(repositoryPath);
    manualOrder.push(repositoryPath);
  }

  // Anything remembered before ordering existed, or added since the order was
  // last written, joins the end so an existing arrangement is left alone.
  for (const repository of store.repositories) {
    if (seen.has(repository.path)) continue;
    seen.add(repository.path);
    manualOrder.push(repository.path);
  }
  store.manualOrder = manualOrder;

  for (const repository of store.repositories) {
    // lastOpened is the best available stand-in for a repository that predates
    // addedAt, and it is never later than the real first-added time.
    if (!repository.addedAt) repository.addedAt = repository.lastOpened || new Date().toISOString();
    if (repository.latestCommit === undefined) repository.latestCommit = null;
    // Age sorts by this. Stores written before it existed backfill lazily; see
    // needsMetadataBackfill.
    if (repository.firstCommit === undefined) repository.firstCommit = null;
  }

  return store;
}

/**
 * Repositories still missing the fields the sidebar sorts by.
 *
 * A store written before those fields existed would otherwise leave "age" and
 * "latest commit" sorting a list of nulls until every repository happened to be
 * opened, which reads as the feature being broken.
 */
function needsMetadataBackfill(store) {
  return store.repositories
    .filter((repository) => !repository.firstCommit && repository.firstCommit !== false)
    .map((repository) => repository.path);
}

/** Applies a renderer-supplied manual order, ignoring unknown and duplicate paths. */
function applyManualOrder(store, repositoryPaths) {
  if (!Array.isArray(repositoryPaths)) throw new Error("Manual order must be a list of repository paths.");
  const known = new Set(store.repositories.map((repository) => repository.path));
  const seen = new Set();
  const manualOrder = [];
  for (const value of repositoryPaths) {
    if (typeof value !== "string" || !known.has(value) || seen.has(value)) continue;
    seen.add(value);
    manualOrder.push(value);
  }
  store.manualOrder = manualOrder;
  // Any repository the renderer left out keeps a position rather than
  // disappearing from the sidebar.
  return normalizeOrdering(store);
}

module.exports = {
  DEFAULT_ORDER,
  needsMetadataBackfill,
  REPOSITORY_ORDER_DIRECTIONS,
  REPOSITORY_ORDER_MODES,
  applyManualOrder,
  normalizeOrdering,
};
