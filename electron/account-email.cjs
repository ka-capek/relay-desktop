"use strict";

/**
 * Commit-email choices offered when an account is connected.
 *
 * The commit email is a commit-identity setting, not an authentication one:
 * changing it never changes the GitHub account and never rewrites history.
 *
 * Kept out of main.cjs so it can be exercised without an Electron runtime.
 */

/** GitHub's numeric-ID noreply address, which always works and never leaks a real address. */
function noreplyAddress(githubId, handle) {
  return `${githubId}+${handle}@users.noreply.github.com`;
}

function isValidEmail(value) {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(String(value || "").trim());
}

/**
 * Builds the list to offer, from GitHub's /user/emails response.
 *
 * The noreply address is always present and always first: it is the safe
 * default and the value a blank submission resolves to. Unverified addresses
 * are dropped, because committing with one is not useful. If the response is
 * missing or unusable, which happens when the token lacks the user:email
 * scope, the noreply address alone is offered and the caller can still type
 * something, so a missing scope degrades rather than fails.
 */
function emailChoices(account, rawEmails) {
  const noreply = noreplyAddress(account.githubId, account.handle);
  const choices = [{ email: noreply, label: "GitHub noreply", primary: false, noreply: true }];
  const seen = new Set([noreply.toLowerCase()]);

  for (const entry of Array.isArray(rawEmails) ? rawEmails : []) {
    const email = String(entry?.email || "").trim();
    if (!email || !isValidEmail(email) || !entry?.verified) continue;
    if (seen.has(email.toLowerCase())) continue;
    seen.add(email.toLowerCase());
    choices.push({ email, label: entry.primary ? "Primary" : "Verified", primary: Boolean(entry.primary), noreply: false });
  }

  return choices;
}

/**
 * Resolves what to store, given whatever the user submitted.
 * Blank means the noreply address, matching the existing modal's behaviour.
 */
function resolveCommitEmail(account, requested) {
  const value = String(requested || "").trim();
  if (!value) return noreplyAddress(account.githubId, account.handle);
  if (!isValidEmail(value)) throw new Error("Enter a valid email address.");
  return value;
}

module.exports = { emailChoices, isValidEmail, noreplyAddress, resolveCommitEmail };
