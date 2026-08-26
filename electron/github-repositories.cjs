"use strict";

/**
 * Shaping of the GitHub repository list used by the clone browser.
 *
 * Kept out of main.cjs so it can be exercised without an Electron runtime.
 */

/**
 * Whether the active account could actually push to this repository.
 *
 * `/user/repos` already returns `permissions`, so this costs no extra request.
 * A missing permissions object means an unexpected response shape; treat that
 * as pushable rather than silently hiding a repository the user owns.
 */
function canPush(repository) {
  const permissions = repository?.permissions;
  if (!permissions) return true;
  return Boolean(permissions.push || permissions.maintain || permissions.admin);
}

/**
 * An archived repository is read-only on GitHub's side no matter what the
 * permissions say, so pushing to it always fails. It is hidden with the
 * read-only ones rather than offered and then rejected on push.
 */
function isPushable(repository) {
  return canPush(repository) && !repository?.archived;
}

function sanitize(repository) {
  return {
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
  };
}

/**
 * Splits a raw GitHub page into what the clone browser shows and a count of
 * what was withheld, so the UI can say so instead of silently shrinking.
 */
function partitionRepositories(rawRepositories) {
  const pushable = [];
  let hidden = 0;
  for (const repository of rawRepositories) {
    if (isPushable(repository)) pushable.push(sanitize(repository));
    else hidden += 1;
  }
  return { repositories: pushable, hidden };
}

module.exports = { canPush, isPushable, partitionRepositories, sanitize };
