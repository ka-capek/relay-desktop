"use client";

import { useEffect, useMemo, useRef, useState } from "react";

type Account = {
  id: string;
  name: string;
  handle: string;
  email: string;
  initials: string;
  tone: string;
  status: string;
};

type RepositorySummary = {
  path: string;
  name: string;
  owner: string;
  branch: string;
  changes: number;
  lastOpened: string;
  /** When Relay first remembered this repository, not a filesystem timestamp. */
  addedAt: string;
  /** Committer date of the current HEAD, or null in a repository with no commits. */
  latestCommit: string | null;
  /** Date of the repository's first commit. This is what "age" sorts by. */
  firstCommit: string | null;
};

/**
 * An SSH identity for a non-GitHub host.
 *
 * This is not a GitHub account and never involves a token. It holds no key
 * material either: `identityFile` is a path, and OpenSSH and the SSH agent
 * handle the key and any passphrase.
 */
type SshProfile = {
  id: string;
  label: string;
  host: string;
  user: string | null;
  port: number | null;
  identityFile: string | null;
  identitiesOnly: boolean;
};

type RepositoryOrderMode = "manual" | "age" | "name" | "latest";
type RepositoryOrderDirection = "asc" | "desc";
type RepositoryOrder = { mode: RepositoryOrderMode; direction: RepositoryOrderDirection };

type GitHubRepository = {
  id: string;
  name: string;
  fullName: string;
  owner: string;
  description: string;
  private: boolean;
  archived: boolean;
  fork: boolean;
  cloneUrl: string;
  updatedAt: string;
};

type ChangedFile = {
  path: string;
  name: string;
  directory: string;
  status: "A" | "M" | "D";
  tone: "added" | "modified" | "deleted";
  added: number;
  removed: number;
};

type HistoryItem = {
  fullHash: string;
  hash: string;
  title: string;
  author: string;
  email: string;
  date: string;
};

type HistoryCommit = {
  fullHash: string;
  hash: string;
  parents: string[];
  title: string;
  author: string;
  email: string;
  date: string;
  /** Branch, remote and tag decorations, as Git reports them. */
  refs: string[];
};

type HistoryPage = {
  commits: HistoryCommit[];
  head: string | null;
  /** The commit paging is anchored to, so later pages stay consistent. */
  anchor: string | null;
  endOfHistory: boolean;
};

type CommitFile = {
  path: string;
  name: string;
  directory: string;
  status: "A" | "M" | "D";
  tone: "added" | "modified" | "deleted";
  added: number;
  removed: number;
  binary: boolean;
};

type CommitDetail = {
  fullHash: string;
  hash: string;
  title: string;
  body: string;
  author: string;
  authorEmail: string;
  authorDate: string;
  committer: string;
  committerEmail: string;
  committerDate: string;
  parents: string[];
  refs: string[];
  isRoot: boolean;
  isMerge: boolean;
  files: CommitFile[];
  added: number;
  removed: number;
};

type Repository = {
  path: string;
  name: string;
  owner: string;
  branch: string;
  remote: string;
  branches: string[];
  files: ChangedFile[];
  history: HistoryItem[];
  ahead: number;
  behind: number;
  hasUpstream: boolean;
  latestCommit: string | null;
  firstCommit: string | null;
};

type AppState = {
  accounts: Account[];
  activeAccountId: string | null;
  repositories: RepositorySummary[];
  selectedRepositoryPath: string | null;
  repositoryAccounts: Record<string, string>;
  repositoryOrder: RepositoryOrder;
  manualOrder: string[];
  sshProfiles: SshProfile[];
  repositorySshProfiles: Record<string, string>;
};

type CommitInput = {
  repositoryPath: string;
  files: string[];
  summary: string;
  description: string;
  accountId: string;
};

type RelayDesktop = {
  platform: string;
  isDesktop: boolean;
  getState: () => Promise<AppState>;
  selectRepository: () => Promise<Repository | null>;
  chooseCloneDirectory: () => Promise<string | null>;
  listGitHubRepositories: (accountId: string) => Promise<{ repositories: GitHubRepository[]; hidden: number }>;
  cloneRepository: (input: { remoteUrl: string; parentPath: string; repositoryName: string; accountId: string | null; sshProfileId: string | null }) => Promise<{ repository: Repository; state: AppState }>;
  scanFolder: () => Promise<{ state: AppState; found: number; added: number; readable: number; folderPath: string } | null>;
  removeRepository: (repositoryPath: string) => Promise<AppState>;
  openRepository: (repositoryPath: string) => Promise<Repository>;
  refreshRepository: (repositoryPath: string) => Promise<Repository>;
  getFileDiff: (repositoryPath: string, filePath: string) => Promise<string>;
  readHistory: (repositoryPath: string, options: { skip?: number; limit?: number; anchor?: string | null }) => Promise<HistoryPage>;
  readCommit: (repositoryPath: string, hash: string) => Promise<CommitDetail>;
  readCommitDiff: (repositoryPath: string, hash: string, filePath: string) => Promise<string>;
  commit: (input: CommitInput) => Promise<Repository>;
  fetchOrigin: (repositoryPath: string, accountId: string | null) => Promise<Repository>;
  pushOrigin: (repositoryPath: string, accountId: string | null) => Promise<Repository>;
  switchBranch: (repositoryPath: string, branch: string) => Promise<Repository>;
  connectAccount: () => Promise<AppState>;
  onGitHubLoginProgress: (callback: (progress: GitHubLoginProgress) => void) => () => void;
  setActiveAccount: (accountId: string) => Promise<AppState>;
  setAccountEmail: (accountId: string, email: string) => Promise<AppState>;
  setRepositoryAccount: (repositoryPath: string, accountId: string | null) => Promise<AppState>;
  setRepositoryOrder: (mode: RepositoryOrderMode, direction: RepositoryOrderDirection) => Promise<AppState>;
  setManualOrder: (repositoryPaths: string[]) => Promise<AppState>;
  backfillRepositoryMetadata: () => Promise<{ state: AppState; updated: number }>;
  saveSshProfile: (profile: Partial<SshProfile>) => Promise<AppState>;
  removeSshProfile: (profileId: string) => Promise<AppState>;
  setRepositorySshProfile: (repositoryPath: string, profileId: string | null) => Promise<AppState>;
  testSshProfile: (profile: Partial<SshProfile>) => Promise<{ ok: boolean; message: string }>;
  removeAccount: (accountId: string) => Promise<AppState>;
  onMenuAction: (callback: (action: MenuAction) => void) => () => void;
  openExternal: (url: string) => Promise<void>;
  getMenu: () => Promise<{ isMac: boolean; menus: MenuDescriptor[] }>;
  runMenuCommand: (command: string) => Promise<void>;
};

type MenuAction = "open-repository" | "clone-repository" | "scan-folder" | "remove-repository";

type MenuDescriptorItem =
  | { type: "separator" }
  | { id: string; label: string; mnemonic: string | null; accelerator: string | null; role: string | null };

type MenuDescriptor = { id: string; label: string; mnemonic: string; items: MenuDescriptorItem[] };

type GitHubLoginProgress = {
  code: string | null;
  verificationUrl: string | null;
  message: string;
  browserOpenFailed?: boolean;
};

type DeviceCodeCopyStatus = "idle" | "copied" | "error";

declare global {
  interface Window {
    relayDesktop?: RelayDesktop;
  }
}

type DiffLine = {
  old: string;
  next: string;
  kind: "plain" | "add" | "remove" | "hunk";
  text: string;
};

const EMPTY_SSH_FORM = { id: "", label: "", host: "", user: "", port: "", identityFile: "", identitiesOnly: true };

/** Mirrors electron/ssh-service.cjs so the UI can tell the user what will happen. */
function isSshRemoteUrl(remote: string) {
  const value = remote.trim();
  return /^ssh:\/\//i.test(value) || (/^[^@\s]+@[^@:\s/\\]+:/.test(value) && !/^[a-zA-Z]:[\\/]/.test(value));
}

function isGitHubRemoteUrl(remote: string) {
  const value = remote.trim();
  if (/^https:\/\/github\.com\//i.test(value)) return true;
  return /^(?:ssh:\/\/)?(?:[^@\s]+@)?github\.com[:/]/i.test(value);
}

function sshHostFromRemote(remote: string) {
  const value = remote.trim();
  const explicit = value.match(/^ssh:\/\/(?:[^@/]+@)?([^:/]+)/i);
  if (explicit) return explicit[1];
  const scp = value.match(/^(?:[^@\s]+@)?([^@:\s/\\]+):(?!\/\/)/);
  return scp && !/^[a-zA-Z]$/.test(scp[1]) ? scp[1] : null;
}

type IconName = "alert" | "branch" | "check" | "chevron" | "clone" | "close" | "edit" | "external" | "folder" | "github" | "grip" | "info" | "key" | "lock" | "more" | "plus" | "refresh" | "repository" | "route" | "search" | "settings" | "sort" | "trash" | "upload";

function RelayMark({ size = 30 }: { size?: number }) {
  return (
    <svg className="relay-mark" width={size} height={size} viewBox="0 0 1024 1024" aria-hidden="true">
      <rect x="20" y="20" width="984" height="984" rx="228" fill="#1d5f45" />
      <path d="M338 296v432M338 430h176c96 0 174 78 174 174v124" fill="none" stroke="#f7f2e2" strokeWidth="116" strokeLinecap="round" strokeLinejoin="round" />
      <circle cx="338" cy="296" r="94" fill="#ef8a4c" />
      <circle cx="338" cy="728" r="94" fill="#7cc199" />
      <circle cx="688" cy="728" r="94" fill="#ef8a4c" />
    </svg>
  );
}

function Icon({ name, size = 16, className = "" }: { name: IconName; size?: number; className?: string }) {
  let content;
  switch (name) {
    case "alert": content = <><circle cx="12" cy="12" r="9" /><path d="M12 7v6" /><path d="M12 17h.01" /></>; break;
    case "branch": content = <><circle cx="6" cy="5" r="2" /><circle cx="6" cy="19" r="2" /><circle cx="18" cy="7" r="2" /><path d="M6 7v10" /><path d="M8 15h3a7 7 0 0 0 7-7" /></>; break;
    case "check": content = <path d="m5 12 4 4L19 6" />; break;
    case "chevron": content = <path d="m7 9 5 5 5-5" />; break;
    case "clone": content = <><rect x="8" y="8" width="11" height="11" rx="2" /><path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2" /></>; break;
    case "close": content = <><path d="m7 7 10 10" /><path d="m17 7-10 10" /></>; break;
    case "edit": content = <><path d="M12 20h9" /><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L8 18l-4 1 1-4Z" /></>; break;
    case "external": content = <><path d="M14 5h5v5" /><path d="m10 14 9-9" /><path d="M19 13v6H5V5h6" /></>; break;
    case "folder": content = <><path d="M3 7h6l2 2h10v10H3Z" /><path d="M3 7V5h6l2 2" /></>; break;
    case "github": content = <path fill="currentColor" stroke="none" d="M12 .7a11.5 11.5 0 0 0-3.64 22.4c.58.1.79-.25.79-.56v-2.23c-3.23.7-3.91-1.37-3.91-1.37-.53-1.34-1.29-1.7-1.29-1.7-1.05-.72.08-.71.08-.71 1.17.08 1.78 1.2 1.78 1.2 1.04 1.78 2.72 1.27 3.38.97.1-.75.4-1.27.74-1.56-2.58-.29-5.29-1.29-5.29-5.69 0-1.26.45-2.28 1.19-3.09-.12-.29-.52-1.47.11-3.05 0 0 .97-.31 3.16 1.18a10.9 10.9 0 0 1 5.76 0c2.2-1.49 3.16-1.18 3.16-1.18.63 1.58.23 2.76.11 3.05.74.81 1.19 1.83 1.19 3.09 0 4.42-2.72 5.39-5.31 5.68.42.36.79 1.07.79 2.16v3.21c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .7Z" />; break;
    case "grip": content = <><path d="M9 6h.01M9 12h.01M9 18h.01" /><path d="M15 6h.01M15 12h.01M15 18h.01" /></>; break;
    case "info": content = <><circle cx="12" cy="12" r="9" /><path d="M12 11v6" /><path d="M12 7h.01" /></>; break;
    case "key": content = <><circle cx="8" cy="15" r="4" /><path d="m11 12 8-8" /><path d="m17 6 2 2" /><path d="m14 9 2 2" /></>; break;
    case "lock": content = <><rect x="5" y="10" width="14" height="10" rx="2" /><path d="M8 10V7a4 4 0 0 1 8 0v3" /></>; break;
    case "more": content = <><circle cx="5" cy="12" r="1" fill="currentColor" stroke="none" /><circle cx="12" cy="12" r="1" fill="currentColor" stroke="none" /><circle cx="19" cy="12" r="1" fill="currentColor" stroke="none" /></>; break;
    case "plus": content = <><path d="M12 5v14" /><path d="M5 12h14" /></>; break;
    case "refresh": content = <><path d="M20 6v5h-5" /><path d="M19 11a7 7 0 1 0 1 5" /></>; break;
    case "repository": content = <><path d="M5 4h12a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2Z" /><path d="M8 4v16" /><path d="M5 17a3 3 0 0 1 3-3h11" /></>; break;
    case "route": content = <><path d="M5 7h11" /><path d="m13 4 3 3-3 3" /><path d="M19 17H8" /><path d="m11 14-3 3 3 3" /></>; break;
    case "search": content = <><circle cx="11" cy="11" r="7" /><path d="m20 20-4-4" /></>; break;
    case "settings": content = <><circle cx="12" cy="12" r="3" /><path d="M19 13.5v-3l-2-.7-.7-1.7.9-1.9-2.1-2.1-1.9.9-1.7-.7L10.5 2h-3l-.7 2-1.7.7-1.9-.9-2.1 2.1.9 1.9-.7 1.7-2 .7v3l2 .7.7 1.7-.9 1.9 2.1 2.1 1.9-.9 1.7.7.7 2h3l.7-2 1.7-.7 1.9.9 2.1-2.1-.9-1.9.7-1.7Z" transform="scale(.82) translate(2.6 2.6)" /></>; break;
    case "sort": content = <><path d="M4 7h13" /><path d="M4 12h9" /><path d="M4 17h5" /><path d="M18 10v9" /><path d="m15 16 3 3 3-3" /></>; break;
    case "trash": content = <><path d="M4 7h16" /><path d="M9 7V4h6v3" /><path d="m7 7 1 13h8l1-13" /><path d="M10 11v5M14 11v5" /></>; break;
    case "upload": content = <><path d="M12 16V4" /><path d="m7 9 5-5 5 5" /><path d="M5 20h14" /></>; break;
  }
  return <svg className={`icon ${className}`} width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{content}</svg>;
}

const emptyAppState: AppState = {
  accounts: [],
  activeAccountId: null,
  repositories: [],
  selectedRepositoryPath: null,
  repositoryAccounts: {},
  repositoryOrder: { mode: "manual", direction: "asc" },
  manualOrder: [],
  sshProfiles: [],
  repositorySshProfiles: {},
};

const HISTORY_PAGE_SIZE = 50;

const repositoryCollator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });

function timestamp(value: string | null) {
  const parsed = value ? Date.parse(value) : Number.NaN;
  return Number.isFinite(parsed) ? parsed : null;
}

// Equal sort values must not let repositories swap places between renders, so
// every mode falls through to the same stable tie-breaker.
function compareByName(a: RepositorySummary, b: RepositorySummary) {
  return repositoryCollator.compare(a.name, b.name) || repositoryCollator.compare(a.path, b.path);
}

function sortRepositories(repositories: RepositorySummary[], order: RepositoryOrder, manualOrder: string[]) {
  const sorted = [...repositories];
  const sign = order.direction === "asc" ? 1 : -1;

  if (order.mode === "manual") {
    const position = new Map(manualOrder.map((repositoryPath, index) => [repositoryPath, index]));
    // Anything missing from the stored order sorts after what the user placed.
    return sorted.sort((a, b) =>
      (position.get(a.path) ?? Number.MAX_SAFE_INTEGER) - (position.get(b.path) ?? Number.MAX_SAFE_INTEGER)
      || compareByName(a, b));
  }

  if (order.mode === "name") {
    return sorted.sort((a, b) => (repositoryCollator.compare(a.name, b.name) * sign) || compareByName(a, b));
  }

  if (order.mode === "age") {
    return sorted.sort((a, b) => {
      // The repository's own age, not when Relay first saw it. A folder scan
      // stamps every repository it finds with the same instant, so addedAt
      // orders by the scan's read order and looks random.
      const left = timestamp(a.firstCommit);
      const right = timestamp(b.firstCommit);
      // A repository with no commits has no age, so it sorts last either way.
      if ((left === null) !== (right === null)) return left === null ? 1 : -1;
      if (left !== null && right !== null && left !== right) return (left - right) * sign;
      return compareByName(a, b);
    });
  }

  return sorted.sort((a, b) => {
    const left = timestamp(a.latestCommit);
    const right = timestamp(b.latestCommit);
    // A repository with no commits sorts last in both directions.
    if ((left === null) !== (right === null)) return left === null ? 1 : -1;
    if (left !== null && right !== null && left !== right) return (left - right) * sign;
    return compareByName(a, b);
  });
}

const ORDER_MODE_LABELS: Record<RepositoryOrderMode, string> = {
  manual: "Manual",
  age: "Age",
  name: "Name",
  latest: "Latest commit",
};

function directionLabel(mode: RepositoryOrderMode, direction: RepositoryOrderDirection) {
  if (mode === "name") return direction === "asc" ? "A–Z" : "Z–A";
  return direction === "asc" ? "Oldest first" : "Newest first";
}

function messageFrom(error: unknown) {
  const value = error instanceof Error ? error.message : String(error);
  return value
    .replace(/^Error invoking remote method '[^']+':\s*/i, "")
    .replace(/^Error:\s*/i, "")
    .trim();
}

function initials(name: string) {
  return name.split(/\s+/).filter(Boolean).slice(0, 2).map((part) => part[0]?.toUpperCase()).join("") || "G";
}

function relativeTime(value: string) {
  const date = new Date(value);
  const seconds = Math.max(0, Math.round((Date.now() - date.getTime()) / 1000));
  if (seconds < 60) return "Just now";
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
  if (seconds < 604800) return `${Math.floor(seconds / 86400)}d ago`;
  return date.toLocaleDateString(undefined, { month: "short", day: "numeric" });
}

function absoluteDate(value: string) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
}

// History is grouped by calendar day, which is the label people scan for, with
// relative time kept on the row itself.
function dayLabel(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "Unknown date";
  return date.toLocaleDateString(undefined, { weekday: "long", year: "numeric", month: "long", day: "numeric" });
}

function groupCommitsByDay(commits: HistoryCommit[]) {
  const groups: { label: string; commits: HistoryCommit[] }[] = [];
  for (const commit of commits) {
    const label = dayLabel(commit.date);
    const last = groups.at(-1);
    if (last?.label === label) last.commits.push(commit);
    else groups.push({ label, commits: [commit] });
  }
  return groups;
}

// Only a recognizable GitHub origin gets a link; anything else hides the action
// rather than guessing a URL.
function githubCommitUrl(remote: string, fullHash: string) {
  const match = remote.replace(/\\/g, "/").match(/github\.com[/:]([^/]+)\/([^/]+?)(?:\.git)?$/i);
  return match ? `https://github.com/${match[1]}/${match[2]}/commit/${fullHash}` : null;
}

const DIFF_HEADER_PREFIXES = [
  "diff --git", "index ", "--- ", "+++ ",
  "new file mode", "deleted file mode", "old mode", "new mode",
  "similarity index", "dissimilarity index",
  "rename from", "rename to", "copy from", "copy to",
];

function parseDiff(diff: string): DiffLine[] {
  if (!diff) return [];
  const lines = diff.split("\n");
  const parsed: DiffLine[] = [];
  let oldLine = 0;
  let nextLine = 0;

  for (const line of lines) {
    const hunk = line.match(/^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@/);
    if (hunk) {
      oldLine = Number(hunk[1]);
      nextLine = Number(hunk[2]);
      parsed.push({ old: "", next: "", kind: "hunk", text: line });
      continue;
    }
    // Git's file headers are metadata, not content. Rendering them as code
    // lines gives them meaningless line numbers in the gutter.
    if (DIFF_HEADER_PREFIXES.some((prefix) => line.startsWith(prefix))) continue;
    if (line.startsWith("+") && !line.startsWith("+++")) {
      parsed.push({ old: "", next: String(nextLine++), kind: "add", text: line });
    } else if (line.startsWith("-") && !line.startsWith("---")) {
      parsed.push({ old: String(oldLine++), next: "", kind: "remove", text: line });
    } else if (line.startsWith("\\")) {
      parsed.push({ old: "", next: "", kind: "plain", text: line });
    } else {
      parsed.push({ old: String(oldLine++), next: String(nextLine++), kind: "plain", text: line.startsWith(" ") ? line.slice(1) : line });
    }
  }
  return parsed;
}

export default function Home() {
  const [appState, setAppState] = useState<AppState>(emptyAppState);
  const [repository, setRepository] = useState<Repository | null>(null);
  const [activeTab, setActiveTab] = useState<"changes" | "history">("changes");
  const [activeFile, setActiveFile] = useState("");
  const [checkedFiles, setCheckedFiles] = useState<string[]>([]);
  const [diffText, setDiffText] = useState("");
  const [summary, setSummary] = useState("");
  const [description, setDescription] = useState("");
  const [search, setSearch] = useState("");
  const [notice, setNotice] = useState<{ message: string; error?: boolean } | null>(null);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState("");
  const [accountMenuOpen, setAccountMenuOpen] = useState(false);
  const [accountModalOpen, setAccountModalOpen] = useState(false);
  const [manageAccountsOpen, setManageAccountsOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [cloneModalOpen, setCloneModalOpen] = useState(false);
  const [cloneSource, setCloneSource] = useState<"github" | "url">("github");
  const [cloneForm, setCloneForm] = useState({ remoteUrl: "", parentPath: "", repositoryName: "" });
  const [githubRepositories, setGitHubRepositories] = useState<GitHubRepository[]>([]);
  const [githubRepositorySearch, setGitHubRepositorySearch] = useState("");
  const [selectedGitHubRepositoryId, setSelectedGitHubRepositoryId] = useState<string | null>(null);
  const [githubRepositoriesLoading, setGitHubRepositoriesLoading] = useState(false);
  const [githubRepositoriesError, setGitHubRepositoriesError] = useState("");
  const [hiddenRepositoryCount, setHiddenRepositoryCount] = useState(0);
  const [editingAccountId, setEditingAccountId] = useState<string | null>(null);
  const [accountEmail, setAccountEmail] = useState("");
  const [routingChoice, setRoutingChoice] = useState("follow");
  const [loginProgress, setLoginProgress] = useState<GitHubLoginProgress | null>(null);
  const [deviceCodeCopyStatus, setDeviceCodeCopyStatus] = useState<DeviceCodeCopyStatus>("idle");
  const accountMenuRef = useRef<HTMLDivElement>(null);
  const menuActionsRef = useRef<Record<MenuAction, () => void> | null>(null);
  const savingEmailRef = useRef(false);
  const [sshModalOpen, setSshModalOpen] = useState(false);
  const [sshForm, setSshForm] = useState({ ...EMPTY_SSH_FORM });
  const [sshTestResult, setSshTestResult] = useState<{ ok: boolean; message: string } | null>(null);
  const [cloneSshProfileId, setCloneSshProfileId] = useState<string>("");
  const [appMenus, setAppMenus] = useState<MenuDescriptor[]>([]);
  const [openMenuId, setOpenMenuId] = useState<string | null>(null);
  const menuBarRef = useRef<HTMLDivElement>(null);
  const [historyCommits, setHistoryCommits] = useState<HistoryCommit[]>([]);
  const [historyAnchor, setHistoryAnchor] = useState<string | null>(null);
  const [historyEnd, setHistoryEnd] = useState(false);
  const [historyLoading, setHistoryLoading] = useState(false);
  const [historyLoadingMore, setHistoryLoadingMore] = useState(false);
  const [historyError, setHistoryError] = useState("");
  const [historySearch, setHistorySearch] = useState("");
  const [selectedCommitHash, setSelectedCommitHash] = useState<string | null>(null);
  const [commitDetail, setCommitDetail] = useState<CommitDetail | null>(null);
  const [commitDetailLoading, setCommitDetailLoading] = useState(false);
  const [commitDetailError, setCommitDetailError] = useState("");
  const [commitFile, setCommitFile] = useState("");
  const [commitDiff, setCommitDiff] = useState("");
  const [commitDiffLoading, setCommitDiffLoading] = useState(false);
  const [copiedHash, setCopiedHash] = useState(false);
  // Every history request carries an id; a response whose id is stale is
  // dropped, so overlapping requests cannot interleave into the list.
  const historyRequestRef = useRef(0);
  const historyCountRef = useRef(0);
  const [draggingRepositoryPath, setDraggingRepositoryPath] = useState<string | null>(null);
  const [dropTargetPath, setDropTargetPath] = useState<string | null>(null);
  const loginCodeRef = useRef<string | null>(null);
  const deviceCodeCopyTimerRef = useRef<number | null>(null);

  const accounts = appState.accounts;
  const activeAccount = accounts.find((account) => account.id === appState.activeAccountId) ?? null;
  const boundAccountId = repository ? appState.repositoryAccounts[repository.path] : null;
  const repositoryAccount = accounts.find((account) => account.id === boundAccountId) ?? activeAccount;
  const diffLines = useMemo(() => parseDiff(diffText), [diffText]);
  const orderedRepositories = useMemo(
    () => sortRepositories(appState.repositories, appState.repositoryOrder, appState.manualOrder),
    [appState.repositories, appState.repositoryOrder, appState.manualOrder],
  );
  const visibleRepositories = useMemo(() => {
    const needle = search.trim().toLowerCase();
    if (!needle) return orderedRepositories;
    return orderedRepositories.filter((repo) => `${repo.owner}/${repo.name} ${repo.path}`.toLowerCase().includes(needle));
  }, [orderedRepositories, search]);
  // A non-GitHub SSH clone is the case where an explicit identity can help.
  const cloneUsesSsh = isSshRemoteUrl(cloneForm.remoteUrl) && !isGitHubRemoteUrl(cloneForm.remoteUrl);
  const visibleGitHubRepositories = useMemo(() => {
    const needle = githubRepositorySearch.trim().toLowerCase();
    if (!needle) return githubRepositories;
    return githubRepositories.filter((item) => `${item.fullName} ${item.description}`.toLowerCase().includes(needle));
  }, [githubRepositories, githubRepositorySearch]);

  useEffect(() => {
    let cancelled = false;
    async function start() {
      const api = window.relayDesktop;
      if (!api) {
        setLoading(false);
        showNotice("Relay must be opened as the installed desktop app.", true);
        return;
      }
      try {
        const state = await api.getState();
        if (cancelled) return;
        setAppState(state);

        // Repositories remembered before firstCommit/latestCommit existed have
        // neither, which would leave the age and latest-commit sorts ordering a
        // list of nulls. Filling them in is a Git read per repository, so it
        // runs after the first paint rather than blocking startup, and a
        // failure only means those sorts stay degraded.
        api.backfillRepositoryMetadata()
          .then((result) => { if (!cancelled && result.updated > 0) setAppState(result.state); })
          .catch(() => undefined);
      } catch (error) {
        if (!cancelled) showNotice(messageFrom(error), true);
      } finally {
        if (!cancelled) setLoading(false);
      }
    }
    start();
    return () => { cancelled = true; };
  }, []);

  useEffect(() => {
    const api = window.relayDesktop;
    if (!api) return;
    return api.onGitHubLoginProgress((progress) => {
      if (progress.code !== loginCodeRef.current) {
        loginCodeRef.current = progress.code;
        if (deviceCodeCopyTimerRef.current !== null) window.clearTimeout(deviceCodeCopyTimerRef.current);
        deviceCodeCopyTimerRef.current = null;
        setDeviceCodeCopyStatus("idle");
      }
      setLoginProgress(progress);
    });
  }, []);

  useEffect(() => () => {
    if (deviceCodeCopyTimerRef.current !== null) window.clearTimeout(deviceCodeCopyTimerRef.current);
  }, []);

  useEffect(() => {
    if (!accountMenuOpen) return;
    function dismissAccountMenu(event: PointerEvent) {
      if (!accountMenuRef.current?.contains(event.target as Node)) setAccountMenuOpen(false);
    }
    document.addEventListener("pointerdown", dismissAccountMenu);
    return () => document.removeEventListener("pointerdown", dismissAccountMenu);
  }, [accountMenuOpen]);

  useEffect(() => {
    let cancelled = false;
    if (!repository || !activeFile || !window.relayDesktop) {
      queueMicrotask(() => { if (!cancelled) setDiffText(""); });
      return;
    }
    queueMicrotask(() => { if (!cancelled) setDiffText(""); });
    window.relayDesktop.getFileDiff(repository.path, activeFile)
      .then((value) => { if (!cancelled) setDiffText(value); })
      .catch((error) => { if (!cancelled) showNotice(messageFrom(error), true); });
    return () => { cancelled = true; };
  }, [repository, activeFile]);

  useEffect(() => {
    function refreshOnFocus() {
      if (!repository || busy || !window.relayDesktop) return;
      window.relayDesktop.refreshRepository(repository.path)
        .then((next) => applyRepository(next, true))
        .catch(() => undefined);
    }
    window.addEventListener("focus", refreshOnFocus);
    return () => window.removeEventListener("focus", refreshOnFocus);
  }, [repository, busy]);

  // Only Windows renders a menu bar in the window; macOS keeps its system menu.
  useEffect(() => {
    const api = window.relayDesktop;
    if (!api?.getMenu) return;
    let cancelled = false;
    api.getMenu()
      .then((result) => { if (!cancelled && !result.isMac) setAppMenus(result.menus); })
      .catch(() => undefined);
    return () => { cancelled = true; };
  }, []);

  useEffect(() => {
    if (!openMenuId) return;
    function dismiss(event: PointerEvent) {
      if (!menuBarRef.current?.contains(event.target as Node)) setOpenMenuId(null);
    }
    function onKey(event: KeyboardEvent) {
      if (event.key === "Escape") setOpenMenuId(null);
    }
    document.addEventListener("pointerdown", dismiss);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("pointerdown", dismiss);
      document.removeEventListener("keydown", onKey);
    };
  }, [openMenuId]);

  // Alt focuses the menu bar, matching how Windows menus normally behave.
  useEffect(() => {
    if (appMenus.length === 0) return;
    function onKeyDown(event: KeyboardEvent) {
      if (event.key !== "Alt" || event.repeat) return;
      event.preventDefault();
      setOpenMenuId((current) => (current ? null : appMenus[0].id));
      menuBarRef.current?.querySelector<HTMLButtonElement>("button")?.focus();
    }
    document.addEventListener("keydown", onKeyDown);
    return () => document.removeEventListener("keydown", onKeyDown);
  }, [appMenus]);

  const openRepositoryPath = repository?.path ?? null;
  // Used to re-anchor history when HEAD moves under an open repository.
  const repositoryHead = repository?.history[0]?.fullHash ?? null;

  // Switching repositories must not leave another repository's commits, detail
  // or diff on screen for even one frame.
  useEffect(() => {
    let cancelled = false;
    historyRequestRef.current += 1;
    historyCountRef.current = 0;
    // Deferred the same way the working-tree diff effect defers its reset. The
    // microtask still runs before paint, so no stale commit is ever displayed.
    queueMicrotask(() => {
      if (cancelled) return;
      setHistoryCommits([]);
      setHistoryAnchor(null);
      setHistoryEnd(false);
      setHistoryError("");
      setHistorySearch("");
      setSelectedCommitHash(null);
      setCommitDetail(null);
      setCommitDetailError("");
      setCommitFile("");
      setCommitDiff("");
    });
    return () => { cancelled = true; };
  }, [openRepositoryPath]);

  // Re-anchors when HEAD moves, so a commit, fetch or branch switch reloads
  // from the current tip instead of paging through a history that shifted.
  useEffect(() => {
    if (!repository || activeTab !== "history") return;
    loadHistory(true);
    // loadHistory closes over state that would re-run this on every keystroke.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [openRepositoryPath, repositoryHead, activeTab]);

  useEffect(() => {
    let cancelled = false;
    if (!openRepositoryPath || !selectedCommitHash || !window.relayDesktop) {
      // The superseded request is cancelled, so its finally block cannot clear
      // these. Without clearing them here the pane stays on "Loading commit…"
      // forever after switching repositories.
      queueMicrotask(() => {
        if (cancelled) return;
        setCommitDetail(null);
        setCommitDetailLoading(false);
        setCommitDetailError("");
      });
      return () => { cancelled = true; };
    }
    queueMicrotask(() => {
      if (cancelled) return;
      setCommitDetailLoading(true);
      setCommitDetailError("");
    });
    window.relayDesktop.readCommit(openRepositoryPath, selectedCommitHash)
      .then((detail) => {
        if (cancelled) return;
        setCommitDetail(detail);
        setCommitFile((current) => (detail.files.some((file) => file.path === current) ? current : detail.files[0]?.path || ""));
      })
      .catch((error) => { if (!cancelled) { setCommitDetail(null); setCommitDetailError(messageFrom(error)); } })
      .finally(() => { if (!cancelled) setCommitDetailLoading(false); });
    return () => { cancelled = true; };
  }, [openRepositoryPath, selectedCommitHash]);

  useEffect(() => {
    let cancelled = false;
    queueMicrotask(() => { if (!cancelled) setCommitDiff(""); });
    if (!openRepositoryPath || !selectedCommitHash || !commitFile || !window.relayDesktop) {
      queueMicrotask(() => { if (!cancelled) setCommitDiffLoading(false); });
      return () => { cancelled = true; };
    }
    queueMicrotask(() => { if (!cancelled) setCommitDiffLoading(true); });
    window.relayDesktop.readCommitDiff(openRepositoryPath, selectedCommitHash, commitFile)
      .then((value) => { if (!cancelled) setCommitDiff(value); })
      .catch((error) => { if (!cancelled) showNotice(messageFrom(error), true); })
      .finally(() => { if (!cancelled) setCommitDiffLoading(false); });
    return () => { cancelled = true; };
  }, [openRepositoryPath, selectedCommitHash, commitFile]);

  function applyRepository(next: Repository, preserveSelection = true) {
    setRepository(next);
    setAppState((current) => {
      const known = current.repositories.find((item) => item.path === next.path);
      const summary: RepositorySummary = {
        path: next.path,
        name: next.name,
        owner: next.owner,
        branch: next.branch,
        changes: next.files.length,
        lastOpened: new Date().toISOString(),
        // Mirrors the main process: opening or refreshing a repository must not
        // reset the age it is sorted by, or its place in the manual order.
        addedAt: known?.addedAt || new Date().toISOString(),
        latestCommit: next.latestCommit ?? known?.latestCommit ?? null,
        firstCommit: next.firstCommit ?? known?.firstCommit ?? null,
      };
      return {
        ...current,
        selectedRepositoryPath: next.path,
        repositories: [summary, ...current.repositories.filter((item) => item.path !== next.path)].slice(0, 5000),
        manualOrder: current.manualOrder.includes(next.path) ? current.manualOrder : [...current.manualOrder, next.path],
      };
    });
    setCheckedFiles((current) => preserveSelection ? current.filter((file) => next.files.some((item) => item.path === file)) : next.files.map((file) => file.path));
    setActiveFile((current) => preserveSelection && next.files.some((file) => file.path === current) ? current : next.files[0]?.path || "");
  }

  function showNotice(message: string, error = false) {
    setNotice({ message, error });
    window.setTimeout(() => setNotice(null), error ? 4400 : 2800);
  }

  function resetDeviceCodeCopyFeedback() {
    if (deviceCodeCopyTimerRef.current !== null) window.clearTimeout(deviceCodeCopyTimerRef.current);
    deviceCodeCopyTimerRef.current = null;
    setDeviceCodeCopyStatus("idle");
  }

  function openAccountModal() {
    resetDeviceCodeCopyFeedback();
    setAccountModalOpen(true);
  }

  function closeAccountModal() {
    resetDeviceCodeCopyFeedback();
    loginCodeRef.current = null;
    setAccountModalOpen(false);
  }

  async function chooseRepository() {
    if (!window.relayDesktop) return;
    try {
      setBusy("opening");
      const next = await window.relayDesktop.selectRepository();
      if (next) {
        applyRepository(next, false);
        setSummary("");
        setDescription("");
      }
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function openRepository(repositoryPath: string) {
    if (!window.relayDesktop || repositoryPath === repository?.path) return;
    try {
      setBusy("opening");
      const next = await window.relayDesktop.openRepository(repositoryPath);
      applyRepository(next, false);
      setSummary("");
      setDescription("");
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  function repositoryNameFromUrl(remoteUrl: string) {
    const normalized = remoteUrl.trim().replace(/[\\/]+$/, "");
    return normalized.split(/[/:]/).at(-1)?.replace(/\.git$/i, "") || "";
  }

  function openCloneModal() {
    setCloneSource(activeAccount ? "github" : "url");
    setCloneForm({ remoteUrl: "", parentPath: "", repositoryName: "" });
    setGitHubRepositorySearch("");
    setSelectedGitHubRepositoryId(null);
    setGitHubRepositoriesError("");
    setCloneModalOpen(true);
    if (activeAccount) loadGitHubRepositories(activeAccount.id);
  }

  async function loadGitHubRepositories(accountId: string) {
    if (!window.relayDesktop) return;
    setGitHubRepositories([]);
    setGitHubRepositoriesError("");
    setHiddenRepositoryCount(0);
    setGitHubRepositoriesLoading(true);
    try {
      const result = await window.relayDesktop.listGitHubRepositories(accountId);
      setGitHubRepositories(result.repositories);
      setHiddenRepositoryCount(result.hidden);
    } catch (error) {
      setGitHubRepositoriesError(messageFrom(error));
    } finally {
      setGitHubRepositoriesLoading(false);
    }
  }

  function changeCloneSource(source: "github" | "url") {
    setCloneSource(source);
    setSelectedGitHubRepositoryId(null);
    setCloneForm((current) => ({ ...current, remoteUrl: "", repositoryName: "" }));
    if (source === "github" && activeAccount && githubRepositories.length === 0 && !githubRepositoriesLoading) loadGitHubRepositories(activeAccount.id);
  }

  function selectGitHubRepository(item: GitHubRepository) {
    setSelectedGitHubRepositoryId(item.id);
    setCloneForm((current) => ({ ...current, remoteUrl: item.cloneUrl, repositoryName: item.name }));
  }

  async function chooseCloneDirectory() {
    if (!window.relayDesktop) return;
    const parentPath = await window.relayDesktop.chooseCloneDirectory();
    if (parentPath) setCloneForm((current) => ({ ...current, parentPath }));
  }

  async function cloneRemoteRepository() {
    if (!window.relayDesktop) return;
    try {
      setBusy("clone");
      const result = await window.relayDesktop.cloneRepository({
        ...cloneForm,
        accountId: activeAccount?.id || null,
        // A clone has no path yet, so the identity is chosen here and bound to
        // the repository once it exists.
        sshProfileId: cloneUsesSsh ? cloneSshProfileId || null : null,
      });
      setAppState(result.state);
      applyRepository(result.repository, false);
      setCloneModalOpen(false);
      setCloneForm({ remoteUrl: "", parentPath: "", repositoryName: "" });
      showNotice(`Cloned ${result.repository.owner}/${result.repository.name}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function scanRepositoryFolder() {
    if (!window.relayDesktop) return;
    try {
      setBusy("scan");
      const result = await window.relayDesktop.scanFolder();
      if (!result) return;
      setAppState(result.state);
      const unreadable = result.found - result.readable;
      const suffix = unreadable > 0 ? ` ${unreadable} could not be read.` : "";
      showNotice(`Found ${result.found} repositories and added ${result.added} new.${suffix}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function removeRepositoryFromRelay(repositoryPath?: string) {
    const targetPath = repositoryPath || repository?.path;
    if (!window.relayDesktop || !targetPath) {
      showNotice("Open or choose a repository to remove from Relay.", true);
      return;
    }
    try {
      const removedName = appState.repositories.find((item) => item.path === targetPath)?.name || repository?.name || "Repository";
      const state = await window.relayDesktop.removeRepository(targetPath);
      setAppState(state);
      if (repository?.path === targetPath) {
        setRepository(null);
        setActiveFile("");
        setCheckedFiles([]);
        setDiffText("");
      }
      showNotice(`${removedName} was removed from Relay. Files were left untouched.`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  async function switchAccount(accountId: string) {
    if (!window.relayDesktop) return;
    try {
      const state = await window.relayDesktop.setActiveAccount(accountId);
      setAppState(state);
      setAccountMenuOpen(false);
      const next = state.accounts.find((account) => account.id === accountId);
      showNotice(`Switched to @${next?.handle}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  async function connectAccount() {
    if (!window.relayDesktop) return;
    try {
      setBusy("account");
      loginCodeRef.current = null;
      if (deviceCodeCopyTimerRef.current !== null) window.clearTimeout(deviceCodeCopyTimerRef.current);
      deviceCodeCopyTimerRef.current = null;
      setDeviceCodeCopyStatus("idle");
      setLoginProgress({ code: null, verificationUrl: null, message: "Preparing GitHub sign-in…" });
      const state = await window.relayDesktop.connectAccount();
      setAppState(state);
      setLoginProgress(null);
      closeAccountModal();
      showNotice(`Connected @${state.accounts.find((account) => account.id === state.activeAccountId)?.handle}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function copyDeviceCode() {
    const code = loginProgress?.code;
    if (!code) return;
    if (deviceCodeCopyTimerRef.current !== null) window.clearTimeout(deviceCodeCopyTimerRef.current);
    deviceCodeCopyTimerRef.current = null;
    try {
      await navigator.clipboard.writeText(code);
      setDeviceCodeCopyStatus("copied");
      deviceCodeCopyTimerRef.current = window.setTimeout(() => {
        setDeviceCodeCopyStatus("idle");
        deviceCodeCopyTimerRef.current = null;
      }, 2200);
    } catch {
      setDeviceCodeCopyStatus("error");
    }
  }

  async function openGitHubDevicePage() {
    try {
      await window.relayDesktop?.openExternal("https://github.com/login/device");
    } catch (error) {
      showNotice(messageFrom(error) || "GitHub could not be opened in your browser.", true);
    }
  }

  async function removeAccount(accountId: string) {
    if (!window.relayDesktop) return;
    try {
      const state = await window.relayDesktop.removeAccount(accountId);
      setAppState(state);
      showNotice("Account removed from Relay");
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  function editAccountEmail(account: Account) {
    setEditingAccountId(account.id);
    setAccountEmail(account.email);
  }

  async function saveAccountEmail(event?: React.FormEvent<HTMLFormElement>) {
    event?.preventDefault();
    if (!window.relayDesktop || !editingAccountId) return;
    // Enter can fire again before React re-renders with the busy state, so the
    // in-flight guard has to be a ref rather than the busy string.
    if (savingEmailRef.current) return;
    savingEmailRef.current = true;
    try {
      setBusy("email");
      const state = await window.relayDesktop.setAccountEmail(editingAccountId, accountEmail);
      setAppState(state);
      setEditingAccountId(null);
      setAccountEmail("");
      showNotice("Commit email updated");
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      savingEmailRef.current = false;
      setBusy("");
    }
  }

  const visibleCommits = useMemo(() => {
    const needle = historySearch.trim().toLowerCase();
    if (!needle) return historyCommits;
    // Searches what has been loaded so far, by message, author, email or hash.
    return historyCommits.filter((commit) =>
      `${commit.title} ${commit.author} ${commit.email} ${commit.fullHash}`.toLowerCase().includes(needle));
  }, [historyCommits, historySearch]);
  const commitGroups = useMemo(() => groupCommitsByDay(visibleCommits), [visibleCommits]);
  const commitDiffLines = useMemo(() => parseDiff(commitDiff), [commitDiff]);
  const commitUrl = repository && commitDetail ? githubCommitUrl(repository.remote, commitDetail.fullHash) : null;

  async function loadHistory(reset: boolean) {
    const api = window.relayDesktop;
    if (!api || !repository) return;
    if (!reset && (historyEnd || historyLoadingMore || historyLoading)) return;

    const requestId = ++historyRequestRef.current;
    if (reset) {
      setHistoryLoading(true);
      setHistoryError("");
    } else {
      setHistoryLoadingMore(true);
    }

    try {
      const page = await api.readHistory(repository.path, {
        skip: reset ? 0 : historyCountRef.current,
        limit: HISTORY_PAGE_SIZE,
        anchor: reset ? null : historyAnchor,
      });
      if (requestId !== historyRequestRef.current) return;
      setHistoryAnchor(page.anchor);
      setHistoryEnd(page.endOfHistory);
      setHistoryCommits((current) => {
        // Deduplicates at the batch boundary in case history shifted underneath.
        const merged = reset
          ? page.commits
          : [...current, ...page.commits.filter((commit) => !current.some((item) => item.fullHash === commit.fullHash))];
        historyCountRef.current = merged.length;
        return merged;
      });
    } catch (error) {
      if (requestId !== historyRequestRef.current) return;
      setHistoryError(messageFrom(error));
    } finally {
      if (requestId === historyRequestRef.current) {
        setHistoryLoading(false);
        setHistoryLoadingMore(false);
      }
    }
  }

  function selectCommit(hash: string) {
    setSelectedCommitHash(hash);
    setCommitFile("");
    setCommitDiff("");
  }

  function commitKeyDown(event: React.KeyboardEvent<HTMLButtonElement>, index: number) {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    const next = visibleCommits[index + (event.key === "ArrowUp" ? -1 : 1)];
    if (!next) return;
    selectCommit(next.fullHash);
    // Moving focus with the selection is what makes the list usable without a
    // pointer; the row is addressed by hash so it survives re-ordering.
    document.querySelector<HTMLButtonElement>(`[data-commit="${next.fullHash}"]`)?.focus();
  }

  function commitFileKeyDown(event: React.KeyboardEvent<HTMLButtonElement>, index: number) {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    const next = commitDetail?.files[index + (event.key === "ArrowUp" ? -1 : 1)];
    if (!next) return;
    setCommitFile(next.path);
    document.querySelector<HTMLButtonElement>(`[data-commit-file="${CSS.escape(next.path)}"]`)?.focus();
  }

  async function copyCommitHash(fullHash: string) {
    try {
      await navigator.clipboard.writeText(fullHash);
      setCopiedHash(true);
      window.setTimeout(() => setCopiedHash(false), 1600);
    } catch {
      showNotice("Relay could not write to the clipboard. Select the hash to copy it manually.", true);
    }
  }

  const repositoryOrder = appState.repositoryOrder;
  // Reordering acts on the whole remembered list, so it is only offered when
  // the sidebar is showing that whole list.
  const canReorder = repositoryOrder.mode === "manual" && !search.trim();

  async function changeRepositoryOrder(mode: RepositoryOrderMode, direction: RepositoryOrderDirection) {
    const previous = appState.repositoryOrder;
    setAppState((current) => ({ ...current, repositoryOrder: { mode, direction } }));
    try {
      setAppState(await window.relayDesktop!.setRepositoryOrder(mode, direction));
    } catch (error) {
      setAppState((current) => ({ ...current, repositoryOrder: previous }));
      showNotice(messageFrom(error), true);
    }
  }

  async function persistManualOrder(manualOrder: string[]) {
    const previous = appState.manualOrder;
    setAppState((current) => ({ ...current, manualOrder }));
    try {
      setAppState(await window.relayDesktop!.setManualOrder(manualOrder));
    } catch (error) {
      setAppState((current) => ({ ...current, manualOrder: previous }));
      showNotice(messageFrom(error), true);
    }
  }

  // Both drag-and-drop and the keyboard controls funnel through here so the two
  // paths can never disagree about what the resulting order is.
  function moveRepository(repositoryPath: string, targetPath: string, placeAfter = false) {
    if (!window.relayDesktop || repositoryPath === targetPath) return;
    const order = orderedRepositories.map((repo) => repo.path);
    const from = order.indexOf(repositoryPath);
    const to = order.indexOf(targetPath);
    if (from < 0 || to < 0) return;
    order.splice(from, 1);
    const insertAt = order.indexOf(targetPath) + (placeAfter ? 1 : 0);
    order.splice(insertAt, 0, repositoryPath);
    persistManualOrder(order);
  }

  function nudgeRepository(repositoryPath: string, offset: number) {
    if (!window.relayDesktop) return;
    const order = orderedRepositories.map((repo) => repo.path);
    const from = order.indexOf(repositoryPath);
    const to = from + offset;
    if (from < 0 || to < 0 || to >= order.length) return;
    order.splice(from, 1);
    order.splice(to, 0, repositoryPath);
    persistManualOrder(order);
  }

  function reorderKeyDown(event: React.KeyboardEvent<HTMLButtonElement>, repositoryPath: string) {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    nudgeRepository(repositoryPath, event.key === "ArrowUp" ? -1 : 1);
  }

  const sshProfiles = appState.sshProfiles;
  const repositorySshProfileId = repository ? appState.repositorySshProfiles[repository.path] || "" : "";
  const repositoryUsesSsh = Boolean(repository?.remote && isSshRemoteUrl(repository.remote) && !isGitHubRemoteUrl(repository.remote));

  function editSshProfile(profile?: SshProfile) {
    setSshTestResult(null);
    setSshForm(profile
      ? {
        id: profile.id,
        label: profile.label,
        host: profile.host,
        user: profile.user || "",
        port: profile.port ? String(profile.port) : "",
        identityFile: profile.identityFile || "",
        identitiesOnly: profile.identitiesOnly,
      }
      : { ...EMPTY_SSH_FORM, host: repository?.remote ? sshHostFromRemote(repository.remote) || "" : "" });
    setSshModalOpen(true);
  }

  function sshProfileFromForm() {
    return {
      id: sshForm.id || undefined,
      label: sshForm.label.trim(),
      host: sshForm.host.trim(),
      user: sshForm.user.trim(),
      port: sshForm.port.trim() ? Number(sshForm.port.trim()) : null,
      identityFile: sshForm.identityFile.trim(),
      identitiesOnly: sshForm.identitiesOnly,
    };
  }

  async function saveSshProfile() {
    if (!window.relayDesktop) return;
    try {
      setBusy("ssh");
      setAppState(await window.relayDesktop.saveSshProfile(sshProfileFromForm()));
      setSshModalOpen(false);
      setSshForm({ ...EMPTY_SSH_FORM });
      showNotice("SSH identity saved");
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function testSshProfile() {
    if (!window.relayDesktop) return;
    setSshTestResult(null);
    try {
      setBusy("ssh-test");
      setSshTestResult(await window.relayDesktop.testSshProfile(sshProfileFromForm()));
    } catch (error) {
      setSshTestResult({ ok: false, message: messageFrom(error) });
    } finally {
      setBusy("");
    }
  }

  async function removeSshProfile(profileId: string) {
    if (!window.relayDesktop) return;
    try {
      setAppState(await window.relayDesktop.removeSshProfile(profileId));
      showNotice("SSH identity removed. No key files were changed.");
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  async function chooseRepositorySshProfile(profileId: string) {
    if (!window.relayDesktop || !repository) return;
    try {
      setAppState(await window.relayDesktop.setRepositorySshProfile(repository.path, profileId || null));
      showNotice(profileId ? "This repository will use the chosen SSH identity." : "This repository will use your default SSH configuration.");
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  async function runMenuCommand(command: string) {
    setOpenMenuId(null);
    try {
      await window.relayDesktop?.runMenuCommand(command);
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  function menuBarKeyDown(event: React.KeyboardEvent<HTMLButtonElement>, index: number) {
    if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
    event.preventDefault();
    const next = appMenus[(index + (event.key === "ArrowLeft" ? -1 : 1) + appMenus.length) % appMenus.length];
    setOpenMenuId((current) => (current ? next.id : current));
    menuBarRef.current?.querySelector<HTMLButtonElement>(`[data-menu="${next.id}"]`)?.focus();
  }

  async function saveRepositoryAccount() {
    if (!window.relayDesktop || !repository) return;
    try {
      const state = await window.relayDesktop.setRepositoryAccount(repository.path, routingChoice === "follow" ? null : routingChoice);
      setAppState(state);
      setSettingsOpen(false);
      showNotice("Repository account preference saved");
    } catch (error) {
      showNotice(messageFrom(error), true);
    }
  }

  async function fetchRemote() {
    if (!window.relayDesktop || !repository) return;
    try {
      setBusy("fetch");
      const next = await window.relayDesktop.fetchOrigin(repository.path, repositoryAccount?.id || null);
      applyRepository(next, true);
      showNotice("Origin is up to date");
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function syncRemote() {
    if (!window.relayDesktop || !repository) return;
    const shouldPush = Boolean(repository.remote && (!repository.hasUpstream || repository.ahead > 0));
    if (!shouldPush) {
      await fetchRemote();
      return;
    }
    try {
      setBusy("push");
      const next = await window.relayDesktop.pushOrigin(repository.path, repositoryAccount?.id || null);
      applyRepository(next, true);
      showNotice("Changes pushed to origin");
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function changeBranch(branch: string) {
    if (!window.relayDesktop || !repository || branch === repository.branch) return;
    try {
      setBusy("branch");
      const next = await window.relayDesktop.switchBranch(repository.path, branch);
      applyRepository(next, false);
      showNotice(`Switched to ${branch}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  async function commit() {
    if (!window.relayDesktop || !repository) return;
    if (!repositoryAccount) {
      openAccountModal();
      return;
    }
    try {
      setBusy("commit");
      const next = await window.relayDesktop.commit({
        repositoryPath: repository.path,
        files: checkedFiles,
        summary,
        description,
        accountId: repositoryAccount.id,
      });
      applyRepository(next, false);
      setSummary("");
      setDescription("");
      showNotice(`Committed as @${repositoryAccount.handle}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
    }
  }

  function toggleFile(filePath: string) {
    setCheckedFiles((current) => current.includes(filePath) ? current.filter((file) => file !== filePath) : [...current, filePath]);
  }

  function openRepositorySettings() {
    if (!repository) return;
    setRoutingChoice(appState.repositoryAccounts[repository.path] || "follow");
    setSettingsOpen(true);
  }

  useEffect(() => {
    menuActionsRef.current = {
      "open-repository": chooseRepository,
      "clone-repository": openCloneModal,
      "scan-folder": scanRepositoryFolder,
      "remove-repository": () => removeRepositoryFromRelay(),
    };
  });

  useEffect(() => {
    const api = window.relayDesktop;
    if (!api) return;
    return api.onMenuAction((action) => menuActionsRef.current?.[action]?.());
  }, []);

  const canCommit = Boolean(repository && checkedFiles.length > 0 && summary.trim() && !busy);
  const shouldPush = Boolean(repository?.remote && (!repository.hasUpstream || repository.ahead > 0));
  const syncLabel = busy === "push" ? "Pushing…" : busy === "fetch" ? "Fetching…" : shouldPush ? (repository?.hasUpstream ? `Push origin${repository.ahead > 1 ? ` (${repository.ahead})` : ""}` : "Publish branch") : "Fetch origin";

  return (
    <main className="app-shell">
      <header className="titlebar">
        <div className="window-controls" aria-hidden="true"><i /><i /><i /></div>
        <span className="app-title">Relay</span>
        {appMenus.length > 0 && (
          <div className="app-menubar" role="menubar" aria-label="Application" ref={menuBarRef}>
            {appMenus.map((menu, index) => (
              <div className="app-menu" key={menu.id}>
                <button
                  data-menu={menu.id}
                  role="menuitem"
                  aria-haspopup="menu"
                  aria-expanded={openMenuId === menu.id}
                  className={openMenuId === menu.id ? "open" : ""}
                  onClick={() => setOpenMenuId(openMenuId === menu.id ? null : menu.id)}
                  onMouseEnter={() => openMenuId && setOpenMenuId(menu.id)}
                  onKeyDown={(event) => menuBarKeyDown(event, index)}
                >
                  {/* The mnemonic letter is underlined the way Windows shows it. */}
                  {menu.mnemonic && menu.label.includes(menu.mnemonic)
                    ? <>{menu.label.slice(0, menu.label.indexOf(menu.mnemonic))}<u>{menu.mnemonic}</u>{menu.label.slice(menu.label.indexOf(menu.mnemonic) + 1)}</>
                    : menu.label}
                </button>
                {openMenuId === menu.id && (
                  <div className="app-menu-popup panel-float" role="menu" aria-label={menu.label}>
                    {menu.items.map((item, itemIndex) => ("type" in item
                      ? <div className="menu-divider" key={`separator-${itemIndex}`} role="separator" />
                      : (
                        <button key={item.id} role="menuitem" onClick={() => runMenuCommand(item.id)}>
                          <span>{item.label}</span>
                          {item.accelerator && <em>{item.accelerator}</em>}
                        </button>
                      )
                    ))}
                  </div>
                )}
              </div>
            ))}
          </div>
        )}
      </header>

      <section className="repo-bar">
        <button className="repo-picker" aria-label="Open repository" onClick={chooseRepository}>
          <RelayMark size={30} />
          <span>{repository ? <>{repository.owner} / <strong>{repository.name}</strong></> : <strong>Open a repository</strong>}</span>
          <Icon name="chevron" className="chevron" />
        </button>
        <label className={`branch-control ${!repository ? "disabled" : ""}`}>
          <Icon name="branch" className="branch-symbol" size={20} />
          <span><small>Current branch</small><strong>{repository?.branch || "No repository open"}</strong></span>
          <Icon name="chevron" className="chevron" />
          {repository && <select value={repository.branch} onChange={(event) => changeBranch(event.target.value)} disabled={Boolean(busy)} aria-label="Switch branch">
            {repository.branches.map((branch) => <option key={branch} value={branch}>{branch}</option>)}
          </select>}
        </label>
        {/* The remote control stays with the branch rather than being pushed
            to the far right by the spacer. */}
        <button className="fetch-button" onClick={syncRemote} disabled={!repository || !repository.remote || Boolean(busy)}><Icon name={shouldPush ? "upload" : "refresh"} className={busy === "fetch" ? "spin" : ""} size={16} />{syncLabel}</button>
        <div className="toolbar-spacer" />
        <div className="account-wrap" ref={accountMenuRef}>
          <button className={`account-trigger ${accountMenuOpen ? "active" : ""}`} onClick={() => setAccountMenuOpen(!accountMenuOpen)} aria-expanded={accountMenuOpen} aria-label="Switch GitHub account">
            {activeAccount ? <span className={`avatar ${activeAccount.tone}`}>{activeAccount.initials}</span> : <span className="avatar empty-avatar"><Icon name="plus" size={15} /></span>}
            <span className="account-copy"><small>Active account</small><strong>{activeAccount ? `@${activeAccount.handle}` : "Add account"}</strong></span>
            <Icon name="chevron" className="chevron" />
          </button>
          {accountMenuOpen && (
            <div className="account-menu panel-float">
              <div className="menu-heading"><span>Switch account</span></div>
              {accounts.length === 0 && <div className="menu-empty">No GitHub accounts connected</div>}
              {accounts.map((account) => (
                <button key={account.id} className={`account-option ${account.id === appState.activeAccountId ? "selected" : ""}`} onClick={() => switchAccount(account.id)}>
                  <span className={`avatar large ${account.tone}`}>{account.initials}</span>
                  <span><strong>{account.status}</strong><small>@{account.handle} · {account.email}</small></span>
                  {account.id === appState.activeAccountId && <Icon name="check" className="check" size={17} />}
                </button>
              ))}
              <div className="menu-divider" />
              <button className="menu-action" onClick={() => { setAccountMenuOpen(false); openAccountModal(); }}><Icon name="plus" size={17} /> Add another account</button>
              {accounts.length > 0 && <button className="menu-action" onClick={() => { setAccountMenuOpen(false); setManageAccountsOpen(true); }}><Icon name="settings" size={16} /> Manage accounts</button>}
            </div>
          )}
        </div>
      </section>

      <div className="workspace">
        <aside className="repo-sidebar">
          <div className="sidebar-heading"><span>Repositories</span><button aria-label="Add repository" onClick={chooseRepository}><Icon name="plus" size={18} /></button></div>
          <label className="search-box"><Icon name="search" size={15} /><input value={search} onChange={(event) => setSearch(event.target.value)} placeholder="Filter repositories" aria-label="Filter repositories" /></label>
          <div className="repo-order">
            <label className="repo-order-mode">
              <Icon name="sort" size={14} />
              <span>{ORDER_MODE_LABELS[repositoryOrder.mode]}</span>
              <select
                value={repositoryOrder.mode}
                aria-label="Order repositories by"
                onChange={(event) => changeRepositoryOrder(event.target.value as RepositoryOrderMode, repositoryOrder.direction)}
              >
                {(Object.keys(ORDER_MODE_LABELS) as RepositoryOrderMode[]).map((mode) => (
                  <option key={mode} value={mode}>{ORDER_MODE_LABELS[mode]}</option>
                ))}
              </select>
            </label>
            {repositoryOrder.mode === "manual" ? (
              <span className="repo-order-hint">{search.trim() ? "Clear the filter to reorder" : "Drag to reorder"}</span>
            ) : (
              <button
                className="repo-order-direction"
                onClick={() => changeRepositoryOrder(repositoryOrder.mode, repositoryOrder.direction === "asc" ? "desc" : "asc")}
                aria-label={`Sort direction: ${directionLabel(repositoryOrder.mode, repositoryOrder.direction)}. Activate to reverse.`}
              >
                {directionLabel(repositoryOrder.mode, repositoryOrder.direction)}
              </button>
            )}
          </div>
          <div className="repo-list">
            {visibleRepositories.length === 0 && <div className="repo-empty"><strong>No repositories yet</strong><span>Open, clone, or scan a folder for repositories.</span><div className="repo-empty-actions"><button onClick={chooseRepository}><Icon name="folder" size={14} />Open</button><button onClick={openCloneModal}><Icon name="clone" size={14} />Clone</button><button onClick={scanRepositoryFolder}><Icon name="search" size={14} />Scan</button></div></div>}
            {visibleRepositories.map((repo, index) => {
              const pinned = accounts.find((account) => account.id === appState.repositoryAccounts[repo.path]);
              return (
                <div
                  key={repo.path}
                  className={`repo-row ${repo.path === repository?.path ? "selected" : ""} ${canReorder ? "reorderable" : ""} ${draggingRepositoryPath === repo.path ? "dragging" : ""} ${dropTargetPath === repo.path ? "drop-target" : ""}`}
                  draggable={canReorder}
                  onDragStart={(event) => {
                    if (!canReorder) return;
                    setDraggingRepositoryPath(repo.path);
                    event.dataTransfer.effectAllowed = "move";
                    // Firefox and Chromium both refuse to start a drag without payload.
                    event.dataTransfer.setData("text/plain", repo.path);
                  }}
                  onDragOver={(event) => {
                    if (!canReorder || !draggingRepositoryPath || draggingRepositoryPath === repo.path) return;
                    event.preventDefault();
                    event.dataTransfer.dropEffect = "move";
                    setDropTargetPath(repo.path);
                  }}
                  onDragLeave={() => setDropTargetPath((current) => (current === repo.path ? null : current))}
                  onDrop={(event) => {
                    if (!canReorder || !draggingRepositoryPath) return;
                    event.preventDefault();
                    // Dropping below the row the drag started on means "after it".
                    const placeAfter = orderedRepositories.findIndex((item) => item.path === draggingRepositoryPath) < index;
                    moveRepository(draggingRepositoryPath, repo.path, placeAfter);
                    setDraggingRepositoryPath(null);
                    setDropTargetPath(null);
                  }}
                  onDragEnd={() => { setDraggingRepositoryPath(null); setDropTargetPath(null); }}
                >
                  {canReorder && (
                    <button
                      className="repo-grip"
                      onKeyDown={(event) => reorderKeyDown(event, repo.path)}
                      aria-label={`Reorder ${repo.name}, position ${index + 1} of ${visibleRepositories.length}. Press the up or down arrow key to move it.`}
                      title="Drag, or focus and use the arrow keys, to reorder"
                    >
                      <Icon name="grip" size={14} />
                    </button>
                  )}
                  <button className="repo-item" onClick={() => openRepository(repo.path)} title={repo.path}>
                    <Icon name="repository" className="repo-icon" size={17} />
                    <span className="repo-text"><strong>{repo.name}</strong><small>{repo.owner}</small></span>
                    {pinned && <span className={`mini-avatar ${pinned.tone}`}>{pinned.initials}</span>}
                    {repo.changes > 0 && <span className="change-count">{repo.changes}</span>}
                  </button>
                  <button className="repo-remove" onClick={() => removeRepositoryFromRelay(repo.path)} aria-label={`Remove ${repo.name} from Relay`} title="Remove from Relay"><Icon name="trash" size={14} /></button>
                </div>
              );
            })}
          </div>
        </aside>

        <section className="main-panel">
          <div className="content-tabs">
            <button className={activeTab === "changes" ? "active" : ""} onClick={() => setActiveTab("changes")}>Changes {repository && <span>{repository.files.length}</span>}</button>
            <button className={activeTab === "history" ? "active" : ""} onClick={() => setActiveTab("history")}>History</button>
            <button className="more-button" aria-label="Refresh repository" onClick={() => repository && window.relayDesktop?.refreshRepository(repository.path).then((next) => applyRepository(next, true)).catch((error) => showNotice(messageFrom(error), true))}><Icon name="refresh" size={16} /></button>
          </div>

          {loading ? (
            <div className="empty-workspace"><span className="empty-glyph"><Icon name="refresh" className="spin" size={24} /></span><h2>Opening Relay</h2></div>
          ) : !repository ? (
            <div className="empty-workspace"><span className="empty-glyph"><Icon name="repository" size={25} /></span><h2>Open a Git repository</h2><p>Choose an existing repository, clone one, or scan a folder containing many repositories.</p><div className="empty-actions"><button onClick={chooseRepository}><Icon name="folder" size={15} />Choose repository</button><button onClick={openCloneModal}><Icon name="clone" size={15} />Clone repository</button><button onClick={scanRepositoryFolder}><Icon name="search" size={15} />Scan folder</button></div></div>
          ) : activeTab === "changes" ? (
            <div className="changes-layout">
              <section className="file-pane">
                <div className="file-pane-heading">
                  <label><input type="checkbox" checked={repository.files.length > 0 && checkedFiles.length === repository.files.length} onChange={() => setCheckedFiles(checkedFiles.length === repository.files.length ? [] : repository.files.map((file) => file.path))} /> <span>{checkedFiles.length} of {repository.files.length} files</span></label>
                  <button aria-label="Refresh changes" onClick={() => window.relayDesktop?.refreshRepository(repository.path).then((next) => applyRepository(next, true))}><Icon name="refresh" size={15} /></button>
                </div>
                <div className="file-list">
                  {repository.files.length === 0 && <div className="clean-state"><span><Icon name="check" size={18} /></span><strong>No local changes</strong><small>Your working tree is clean.</small></div>}
                  {repository.files.map((file) => (
                    <button key={file.path} className={`file-item ${activeFile === file.path ? "selected" : ""}`} onClick={() => setActiveFile(file.path)}>
                      <input aria-label={`Include ${file.path}`} type="checkbox" checked={checkedFiles.includes(file.path)} onChange={() => toggleFile(file.path)} onClick={(event) => event.stopPropagation()} />
                      <span className="file-copy"><strong>{file.name}</strong><small>{file.directory}</small></span>
                      <span className="delta">{file.added > 0 && <i>+{file.added}</i>}{file.removed > 0 && <b>−{file.removed}</b>}</span>
                      <span className={`file-state ${file.tone}`}>{file.status}</span>
                    </button>
                  ))}
                </div>
                <div className="commit-box">
                  <div className="commit-identity">
                    {repositoryAccount ? <><span className={`mini-avatar ${repositoryAccount.tone}`}>{repositoryAccount.initials}</span><span>Commit as <strong>@{repositoryAccount.handle}</strong></span></> : <><span className="mini-avatar empty-avatar"><Icon name="plus" size={12} /></span><span>No GitHub account connected</span></>}
                  </div>
                  <input className="summary-input" value={summary} onChange={(event) => setSummary(event.target.value)} placeholder="Summary (required)" aria-label="Commit summary" />
                  <textarea value={description} onChange={(event) => setDescription(event.target.value)} placeholder="Description" aria-label="Commit description" />
                  <button className="commit-button" disabled={repositoryAccount ? !canCommit : Boolean(busy)} onClick={commit}>{repositoryAccount ? `${busy === "commit" ? "Committing…" : `Commit ${checkedFiles.length} file${checkedFiles.length === 1 ? "" : "s"}`} to ${repository.branch}` : "Connect an account to commit"}</button>
                </div>
              </section>

              <section className="diff-pane">
                {activeFile ? <>
                  <div className="diff-heading">
                    <span><strong>{repository.files.find((file) => file.path === activeFile)?.name || activeFile}</strong><small>{activeFile}</small></span>
                    <div className="diff-stats"><i /><i /><i /><i className="removed" /><button aria-label="More diff options"><Icon name="more" size={17} /></button></div>
                  </div>
                  <div className="code-view">
                    {diffLines.length === 0 && <div className="diff-empty">No textual diff available for this file.</div>}
                    {diffLines.map((line, index) => line.kind === "hunk"
                      ? <div key={index} className="diff-hunk">{line.text}</div>
                      : <div key={index} className={`code-line ${line.kind}`}><span>{line.old}</span><span>{line.next}</span><code>{line.text || " "}</code></div>
                    )}
                  </div>
                </> : <div className="empty-workspace diff-empty-state"><span className="empty-glyph"><Icon name="check" size={24} /></span><h2>Nothing to compare</h2><p>Select a changed file to view its diff.</p></div>}
              </section>
            </div>
          ) : (
            <div className="history-layout">
              <section className="commit-pane">
                <label className="commit-search">
                  <Icon name="search" size={15} />
                  <input value={historySearch} onChange={(event) => setHistorySearch(event.target.value)} placeholder="Search message, author, or hash" aria-label="Search commits" />
                </label>
                <div
                  className="commit-list"
                  onScroll={(event) => {
                    const list = event.currentTarget;
                    if (list.scrollHeight - list.scrollTop - list.clientHeight < 240) loadHistory(false);
                  }}
                >
                  {historyLoading && <div className="history-state"><Icon name="refresh" className="spin" size={17} />Loading history…</div>}
                  {!historyLoading && historyError && (
                    <div className="history-state error">
                      <Icon name="alert" size={17} />
                      <span>{historyError}</span>
                      <button onClick={() => loadHistory(true)}>Try again</button>
                    </div>
                  )}
                  {!historyLoading && !historyError && visibleCommits.length === 0 && (
                    <div className="history-state">{historySearch.trim() ? "No commits match that search." : "This repository does not have any commits yet."}</div>
                  )}
                  {commitGroups.map((group) => (
                    <div className="commit-group" key={group.label}>
                      <div className="commit-day">{group.label}</div>
                      {group.commits.map((commit) => (
                        <button
                          key={commit.fullHash}
                          data-commit={commit.fullHash}
                          className={`commit-row ${selectedCommitHash === commit.fullHash ? "selected" : ""}`}
                          aria-current={selectedCommitHash === commit.fullHash}
                          onClick={() => selectCommit(commit.fullHash)}
                          onKeyDown={(event) => commitKeyDown(event, visibleCommits.indexOf(commit))}
                        >
                          <span className="commit-copy">
                            <strong>{commit.title}</strong>
                            <small>{commit.hash} · {relativeTime(commit.date)} · {commit.author}</small>
                            {commit.refs.length > 0 && (
                              <span className="commit-refs">{commit.refs.map((ref) => <i key={ref}>{ref.replace(/^tag: /, "")}</i>)}</span>
                            )}
                          </span>
                          <span className="history-author" title={commit.email}>{initials(commit.author)}</span>
                        </button>
                      ))}
                    </div>
                  ))}
                  {historyLoadingMore && <div className="history-state compact"><Icon name="refresh" className="spin" size={15} />Loading more commits…</div>}
                  {historyEnd && !historyLoading && historyCommits.length > 0 && (
                    <div className="history-end">End of history · {historyCommits.length} commit{historyCommits.length === 1 ? "" : "s"}</div>
                  )}
                </div>
              </section>

              <section className="commit-detail">
                {commitDetailLoading && !commitDetail && <div className="history-state"><Icon name="refresh" className="spin" size={17} />Loading commit…</div>}
                {!commitDetailLoading && commitDetailError && <div className="history-state error"><Icon name="alert" size={17} /><span>{commitDetailError}</span></div>}
                {!commitDetail && !commitDetailLoading && !commitDetailError && (
                  <div className="empty-workspace"><span className="empty-glyph"><Icon name="branch" size={24} /></span><h2>Select a commit</h2><p>Choose a commit to see its message, changed files, and diffs.</p></div>
                )}
                {commitDetail && (
                  <>
                    <div className="commit-heading">
                      <div className="commit-heading-top">
                        <h3>{commitDetail.title}</h3>
                        <div className="commit-actions">
                          <button onClick={() => copyCommitHash(commitDetail.fullHash)} aria-label={`Copy the full commit hash for ${commitDetail.hash}`}>
                            <Icon name={copiedHash ? "check" : "clone"} size={14} />{copiedHash ? "Copied" : "Copy hash"}
                          </button>
                          {commitUrl && (
                            <button onClick={() => window.relayDesktop?.openExternal(commitUrl)} aria-label="Open this commit on GitHub">
                              <Icon name="external" size={14} />GitHub
                            </button>
                          )}
                        </div>
                      </div>
                      <span aria-live="polite" className="visually-hidden">{copiedHash ? "Commit hash copied to the clipboard." : ""}</span>
                      {commitDetail.body && <pre className="commit-body">{commitDetail.body}</pre>}
                      <dl className="commit-meta">
                        <div><dt>Commit</dt><dd><code>{commitDetail.fullHash}</code></dd></div>
                        <div><dt>Author</dt><dd>{commitDetail.author} &lt;{commitDetail.authorEmail}&gt; · {absoluteDate(commitDetail.authorDate)}</dd></div>
                        <div><dt>Committer</dt><dd>{commitDetail.committer} &lt;{commitDetail.committerEmail}&gt; · {absoluteDate(commitDetail.committerDate)}</dd></div>
                        <div>
                          <dt>Parents</dt>
                          <dd>{commitDetail.isRoot ? "None — this is the root commit" : commitDetail.parents.map((parent) => parent.slice(0, 7)).join(", ")}</dd>
                        </div>
                        {commitDetail.refs.length > 0 && <div><dt>Refs</dt><dd>{commitDetail.refs.join(", ")}</dd></div>}
                      </dl>
                      {commitDetail.isMerge && <div className="modal-note"><Icon name="info" size={16} />Merge commit. Changes are shown against its first parent.</div>}
                    </div>

                    <div className="commit-files-heading">
                      <span>{commitDetail.files.length} changed file{commitDetail.files.length === 1 ? "" : "s"}</span>
                      <span className="delta"><i>+{commitDetail.added}</i><b>−{commitDetail.removed}</b></span>
                    </div>
                    <div className="commit-files">
                      {commitDetail.files.length === 0 && <div className="history-state compact">This commit does not change any files.</div>}
                      {commitDetail.files.map((file, index) => (
                        <button
                          key={file.path}
                          data-commit-file={file.path}
                          className={`file-item ${commitFile === file.path ? "selected" : ""}`}
                          aria-current={commitFile === file.path}
                          onClick={() => setCommitFile(file.path)}
                          onKeyDown={(event) => commitFileKeyDown(event, index)}
                        >
                          <span className="file-copy"><strong>{file.name}</strong><small>{file.directory}</small></span>
                          <span className="delta">{file.binary ? <i>binary</i> : <>{file.added > 0 && <i>+{file.added}</i>}{file.removed > 0 && <b>−{file.removed}</b>}</>}</span>
                          <span className={`file-state ${file.tone}`}>{file.status}</span>
                        </button>
                      ))}
                    </div>

                    <div className="commit-diff">
                      {commitDiffLoading && <div className="history-state compact"><Icon name="refresh" className="spin" size={15} />Loading diff…</div>}
                      {!commitDiffLoading && commitDiffLines.length === 0 && commitFile && <div className="diff-empty">No textual diff available for this file.</div>}
                      {!commitDiffLoading && commitDiffLines.map((line, index) => line.kind === "hunk"
                        ? <div key={index} className="diff-hunk">{line.text}</div>
                        : <div key={index} className={`code-line ${line.kind}`}><span>{line.old}</span><span>{line.next}</span><code>{line.text || " "}</code></div>
                      )}
                    </div>
                  </>
                )}
              </section>
            </div>
          )}
        </section>
      </div>

      <footer className="statusbar">
        <span>{repositoryAccount ? <>Signed in as <strong>@{repositoryAccount.handle}</strong></> : "No GitHub account selected"}</span>
        <button onClick={openRepositorySettings} disabled={!repository || accounts.length === 0}>Repository account settings</button>
      </footer>

      {settingsOpen && repository && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) setSettingsOpen(false); }}>
          <section className="modal" role="dialog" aria-modal="true" aria-labelledby="settings-title">
            <button className="modal-close" onClick={() => setSettingsOpen(false)} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon"><Icon name="route" size={21} /></div>
            <h2 id="settings-title">Account for {repository.name}</h2>
            <p>Choose the identity Relay uses for commits and authenticated GitHub fetches in this repository.</p>
            <div className="routing-options">
              <label className={routingChoice === "follow" ? "selected" : ""}>
                <input type="radio" name="routing" value="follow" checked={routingChoice === "follow"} onChange={(event) => setRoutingChoice(event.target.value)} />
                <span className="routing-symbol"><Icon name="refresh" size={17} /></span>
                <span><strong>Follow active account</strong><small>{activeAccount ? `Currently @${activeAccount.handle}` : "No active account"}</small></span>
              </label>
              {accounts.map((account) => (
                <label key={account.id} className={routingChoice === account.id ? "selected" : ""}>
                  <input type="radio" name="routing" value={account.id} checked={routingChoice === account.id} onChange={(event) => setRoutingChoice(event.target.value)} />
                  <span className={`avatar ${account.tone}`}>{account.initials}</span>
                  <span><strong>{account.status}</strong><small>@{account.handle}</small></span>
                </label>
              ))}
            </div>
            <div className="modal-note"><Icon name="info" size={17} />GitHub CLI keeps OAuth credentials in the operating system credential store.</div>

            <div className="ssh-section">
              <div className="ssh-section-heading">
                <span><Icon name="key" size={15} />SSH identity</span>
                <button onClick={() => editSshProfile()}><Icon name="plus" size={13} />Add identity</button>
              </div>
              {repositoryUsesSsh ? (
                <>
                  <p>
                    This repository uses <code>{repository.remote}</code>. Commit name and email still come from the account above;
                    this only chooses which key is offered to the host.
                  </p>
                  <label className="form-field">
                    <span>Identity for this repository</span>
                    <select value={repositorySshProfileId} onChange={(event) => chooseRepositorySshProfile(event.target.value)}>
                      <option value="">Use my SSH agent and ~/.ssh/config</option>
                      {sshProfiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.label} · {profile.host}</option>)}
                    </select>
                  </label>
                </>
              ) : (
                <p>
                  {repository.remote
                    ? "This repository's remote is not a non-GitHub SSH remote, so Relay uses the account above for it."
                    : "This repository has no origin remote yet."}
                  {" "}SSH identities apply to Git hosts other than GitHub.com.
                </p>
              )}
              {sshProfiles.length > 0 && (
                <div className="ssh-profiles">
                  {sshProfiles.map((profile) => (
                    <div className="ssh-profile" key={profile.id}>
                      <span className="ssh-profile-mark"><Icon name="key" size={14} /></span>
                      <span className="ssh-profile-copy">
                        <strong>{profile.label}</strong>
                        <small>{profile.user ? `${profile.user}@` : ""}{profile.host}{profile.port ? `:${profile.port}` : ""}{profile.identityFile ? ` · ${profile.identityFile}` : " · SSH agent"}</small>
                      </span>
                      <div className="ssh-profile-actions">
                        <button onClick={() => editSshProfile(profile)}><Icon name="edit" size={13} />Edit</button>
                        <button className="danger" onClick={() => removeSshProfile(profile.id)}><Icon name="trash" size={13} />Remove</button>
                      </div>
                    </div>
                  ))}
                </div>
              )}
              <div className="modal-note"><Icon name="info" size={17} />Relay stores only the host and the key&apos;s location. Private keys and passphrases stay with OpenSSH and your SSH agent, and a GitHub token is never sent to another host.</div>
            </div>

            <button className="primary-modal-button" onClick={saveRepositoryAccount}>Save preference</button>
          </section>
        </div>
      )}

      {accountModalOpen && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) closeAccountModal(); }}>
          <section className="modal account-form-modal" role="dialog" aria-modal="true" aria-labelledby="add-account-title">
            <button className="modal-close" onClick={closeAccountModal} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon github-mark"><Icon name="github" size={22} /></div>
            <h2 id="add-account-title">Connect a GitHub account</h2>
            <p>Relay opens GitHub in your browser for an official one-time OAuth login. No personal access token is needed.</p>
            {loginProgress?.code && <div className={`device-code ${deviceCodeCopyStatus}`}><span>One-time code</span><input className="device-code-value" value={loginProgress.code} readOnly onFocus={(event) => event.currentTarget.select()} aria-label="One-time GitHub device code" /><button type="button" onClick={copyDeviceCode} aria-label={`Copy one-time code ${loginProgress.code}`}>{deviceCodeCopyStatus === "copied" ? <><Icon name="check" size={14} />Copied</> : "Copy code"}</button><small aria-live="polite">{deviceCodeCopyStatus === "copied" ? "Code copied to the clipboard." : deviceCodeCopyStatus === "error" ? "Couldn’t copy automatically. Select the code and copy it manually." : "Paste this code into the GitHub page."}</small></div>}
            {busy === "account" && <div className="login-progress"><Icon name="refresh" className="spin" size={17} /><span>{loginProgress?.message || "Waiting for GitHub…"}</span></div>}
            <div className="modal-note"><Icon name="info" size={17} />GitHub CLI stores the OAuth credential in your operating system credential store. Relay never asks for or saves a PAT.</div>
            <button className="primary-modal-button" disabled={busy === "account"} onClick={connectAccount}>{busy === "account" ? "Waiting for browser sign-in…" : "Continue with GitHub"}</button>
            {busy === "account" && <button className="token-help" onClick={openGitHubDevicePage}>{loginProgress?.browserOpenFailed ? "Open the GitHub device page" : "Open the GitHub device page again"} <Icon name="external" size={13} /></button>}
            <button className="secondary-modal-button" onClick={closeAccountModal}>Cancel</button>
          </section>
        </div>
      )}

      {manageAccountsOpen && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) setManageAccountsOpen(false); }}>
          <section className="modal" role="dialog" aria-modal="true" aria-labelledby="manage-title">
            <button className="modal-close" onClick={() => setManageAccountsOpen(false)} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon"><Icon name="settings" size={21} /></div>
            <h2 id="manage-title">Connected accounts</h2>
            <p>Removing an account signs it out locally and removes its credential from this computer.</p>
            <div className="managed-accounts">
              {accounts.map((account) => <div className="managed-account" key={account.id}><span className={`avatar ${account.tone}`}>{account.initials}</span><span><strong>{account.status}</strong><small>@{account.handle} · {account.email}</small></span><div className="managed-account-actions"><button onClick={() => editAccountEmail(account)}><Icon name="edit" size={14} />Email</button><button className="danger" onClick={() => removeAccount(account.id)}><Icon name="trash" size={14} />Remove</button></div></div>)}
            </div>
            <button className="primary-modal-button" onClick={() => { setManageAccountsOpen(false); openAccountModal(); }}>Add account</button>
          </section>
        </div>
      )}

      {cloneModalOpen && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget && busy !== "clone") setCloneModalOpen(false); }}>
          <section className="modal clone-modal" role="dialog" aria-modal="true" aria-labelledby="clone-title">
            <button className="modal-close" onClick={() => setCloneModalOpen(false)} aria-label="Close" disabled={busy === "clone"}><Icon name="close" size={17} /></button>
            <div className="modal-icon"><Icon name="clone" size={21} /></div>
            <h2 id="clone-title">Clone a repository</h2>
            <p>Choose a repository from the active GitHub account, or paste any HTTPS or SSH URL.</p>
            <div className="clone-source-tabs" role="tablist" aria-label="Clone source">
              <button role="tab" aria-selected={cloneSource === "github"} className={cloneSource === "github" ? "active" : ""} onClick={() => changeCloneSource("github")} disabled={!activeAccount}><Icon name="github" size={15} />GitHub repositories</button>
              <button role="tab" aria-selected={cloneSource === "url"} className={cloneSource === "url" ? "active" : ""} onClick={() => changeCloneSource("url")}><Icon name="route" size={15} />URL</button>
            </div>
            {cloneSource === "github" && activeAccount ? (
              <div className="github-repository-picker">
                <div className="github-repository-heading"><span>Writable by <strong>@{activeAccount.handle}</strong></span><span>{githubRepositoriesLoading ? "Loading…" : `${visibleGitHubRepositories.length} repositories`}</span></div>
                <label className="github-repository-search"><Icon name="search" size={15} /><input value={githubRepositorySearch} onChange={(event) => setGitHubRepositorySearch(event.target.value)} placeholder="Filter repositories" aria-label="Filter GitHub repositories" /></label>
                <div className="github-repository-list">
                  {githubRepositoriesLoading && <div className="repository-list-state"><Icon name="refresh" className="spin" size={18} />Loading repositories from GitHub…</div>}
                  {!githubRepositoriesLoading && githubRepositoriesError && <div className="repository-list-state error"><Icon name="alert" size={18} />{githubRepositoriesError}</div>}
                  {!githubRepositoriesLoading && !githubRepositoriesError && visibleGitHubRepositories.length === 0 && <div className="repository-list-state">No matching repositories.</div>}
                  {!githubRepositoriesLoading && !githubRepositoriesError && visibleGitHubRepositories.slice(0, 250).map((item) => (
                    <button key={item.id} className={selectedGitHubRepositoryId === item.id ? "selected" : ""} onClick={() => selectGitHubRepository(item)}>
                      <Icon name="repository" size={17} />
                      <span><strong>{item.fullName}</strong><small>{item.description || (item.fork ? "Forked repository" : "No description")}</small></span>
                      <span className="repository-badges">{item.private && <i><Icon name="lock" size={11} />Private</i>}{item.archived && <i>Archived</i>}</span>
                      {selectedGitHubRepositoryId === item.id && <Icon name="check" className="selected-check" size={17} />}
                    </button>
                  ))}
                </div>
                {visibleGitHubRepositories.length > 250 && <div className="repository-list-limit">Showing the first 250 results. Filter by name to find another repository.</div>}
                {!githubRepositoriesLoading && !githubRepositoriesError && hiddenRepositoryCount > 0 && (
                  <div className="repository-list-limit">
                    {hiddenRepositoryCount} repositor{hiddenRepositoryCount === 1 ? "y is" : "ies are"} hidden because @{activeAccount.handle} cannot push to {hiddenRepositoryCount === 1 ? "it" : "them"}, or {hiddenRepositoryCount === 1 ? "it is" : "they are"} archived. Paste its URL on the URL tab to clone it read-only.
                  </div>
                )}
              </div>
            ) : (
              <label className="form-field"><span>Repository URL</span><input value={cloneForm.remoteUrl} onChange={(event) => { const remoteUrl = event.target.value; setCloneForm((current) => ({ ...current, remoteUrl, repositoryName: repositoryNameFromUrl(remoteUrl) })); }} placeholder="https://github.com/owner/repository.git" /></label>
            )}
            <label className="form-field"><span>Repository name</span><input value={cloneForm.repositoryName} onChange={(event) => setCloneForm((current) => ({ ...current, repositoryName: event.target.value }))} placeholder="repository" /></label>
            <label className="form-field"><span>Clone into folder</span><div className="path-picker"><input value={cloneForm.parentPath} readOnly placeholder="Choose a local folder" /><button onClick={chooseCloneDirectory}><Icon name="folder" size={15} />Choose</button></div></label>
            {cloneUsesSsh && (
              <label className="form-field">
                <span>SSH identity <small>Optional</small></span>
                <select value={cloneSshProfileId} onChange={(event) => setCloneSshProfileId(event.target.value)}>
                  <option value="">Use my SSH agent and ~/.ssh/config</option>
                  {sshProfiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.label} · {profile.host}</option>)}
                </select>
              </label>
            )}
            <div className="modal-note"><Icon name="info" size={17} />{activeAccount ? `Private GitHub repositories use @${activeAccount.handle}.` : "Connect a GitHub account to browse and clone private repositories."} SSH URLs use your existing SSH configuration; a GitHub token is never sent to another host.</div>
            <button className="primary-modal-button" disabled={!cloneForm.remoteUrl.trim() || !cloneForm.parentPath || !cloneForm.repositoryName.trim() || busy === "clone"} onClick={cloneRemoteRepository}>{busy === "clone" ? "Cloning repository…" : "Clone repository"}</button>
            <button className="secondary-modal-button" disabled={busy === "clone"} onClick={() => setCloneModalOpen(false)}>Cancel</button>
          </section>
        </div>
      )}

      {editingAccountId && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) setEditingAccountId(null); }}>
          <section className="modal email-modal" role="dialog" aria-modal="true" aria-labelledby="email-title">
            <button className="modal-close" onClick={() => setEditingAccountId(null)} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon"><Icon name="edit" size={21} /></div>
            <h2 id="email-title">Change commit email</h2>
            <p>This email is written into new commits made with @{accounts.find((account) => account.id === editingAccountId)?.handle}. Existing commits are not changed.</p>
            {/* noValidate keeps validation in the main process, so Enter and the
                button both surface the same message a click surfaces today. */}
            <form onSubmit={saveAccountEmail} noValidate>
              <label className="form-field"><span>Commit email</span><input type="email" value={accountEmail} onChange={(event) => setAccountEmail(event.target.value)} placeholder="Leave blank to use GitHub noreply" /></label>
              <button type="submit" className="primary-modal-button" disabled={busy === "email"}>{busy === "email" ? "Saving…" : "Save email"}</button>
              <button type="button" className="secondary-modal-button" onClick={() => setEditingAccountId(null)}>Cancel</button>
            </form>
          </section>
        </div>
      )}

      {sshModalOpen && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget && busy !== "ssh-test") setSshModalOpen(false); }}>
          <section className="modal ssh-modal" role="dialog" aria-modal="true" aria-labelledby="ssh-title">
            <button className="modal-close" onClick={() => setSshModalOpen(false)} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon"><Icon name="key" size={21} /></div>
            <h2 id="ssh-title">{sshForm.id ? "Edit SSH identity" : "Add SSH identity"}</h2>
            <p>For Git hosts other than GitHub.com. Leave the key blank to use your SSH agent and <code>~/.ssh/config</code>, which is right for most setups.</p>
            <form onSubmit={(event) => { event.preventDefault(); saveSshProfile(); }} noValidate>
              <label className="form-field"><span>Name</span><input value={sshForm.label} onChange={(event) => setSshForm({ ...sshForm, label: event.target.value })} placeholder="Work GitLab" /></label>
              <label className="form-field"><span>Host <small>Required</small></span><input value={sshForm.host} onChange={(event) => setSshForm({ ...sshForm, host: event.target.value })} placeholder="gitlab.example.com" /></label>
              <div className="ssh-form-row">
                <label className="form-field"><span>User</span><input value={sshForm.user} onChange={(event) => setSshForm({ ...sshForm, user: event.target.value })} placeholder="git" /></label>
                <label className="form-field"><span>Port</span><input value={sshForm.port} onChange={(event) => setSshForm({ ...sshForm, port: event.target.value.replace(/\D/g, "") })} placeholder="22" inputMode="numeric" /></label>
              </div>
              <label className="form-field">
                <span>Private key file <small>Optional</small></span>
                <input value={sshForm.identityFile} onChange={(event) => setSshForm({ ...sshForm, identityFile: event.target.value })} placeholder="~/.ssh/id_ed25519" />
              </label>
              <label className="ssh-checkbox">
                <input type="checkbox" checked={sshForm.identitiesOnly} onChange={(event) => setSshForm({ ...sshForm, identitiesOnly: event.target.checked })} />
                <span>Offer only this key<small>Keep this on when several identities share one host.</small></span>
              </label>
              {sshTestResult && (
                <div className={`ssh-test-result ${sshTestResult.ok ? "ok" : "error"}`} role="status">
                  <Icon name={sshTestResult.ok ? "check" : "alert"} size={16} />{sshTestResult.message}
                </div>
              )}
              <button type="button" className="secondary-modal-button" disabled={!sshForm.host.trim() || busy === "ssh-test"} onClick={testSshProfile}>
                {busy === "ssh-test" ? "Testing connection…" : "Test connection"}
              </button>
              <button type="submit" className="primary-modal-button" disabled={!sshForm.host.trim() || busy === "ssh"}>{busy === "ssh" ? "Saving…" : "Save identity"}</button>
              <button type="button" className="secondary-modal-button" onClick={() => setSshModalOpen(false)}>Cancel</button>
            </form>
          </section>
        </div>
      )}

      {notice && <div className={`toast ${notice.error ? "error" : ""}`}><span><Icon name={notice.error ? "alert" : "check"} size={13} /></span>{notice.message}</div>}
    </main>
  );
}
