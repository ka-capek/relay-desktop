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
};

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
};

type AppState = {
  accounts: Account[];
  activeAccountId: string | null;
  repositories: RepositorySummary[];
  selectedRepositoryPath: string | null;
  repositoryAccounts: Record<string, string>;
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
  listGitHubRepositories: (accountId: string) => Promise<GitHubRepository[]>;
  cloneRepository: (input: { remoteUrl: string; parentPath: string; repositoryName: string; accountId: string | null }) => Promise<{ repository: Repository; state: AppState }>;
  scanFolder: () => Promise<{ state: AppState; found: number; added: number; readable: number; folderPath: string } | null>;
  removeRepository: (repositoryPath: string) => Promise<AppState>;
  openRepository: (repositoryPath: string) => Promise<Repository>;
  refreshRepository: (repositoryPath: string) => Promise<Repository>;
  getFileDiff: (repositoryPath: string, filePath: string) => Promise<string>;
  commit: (input: CommitInput) => Promise<Repository>;
  fetchOrigin: (repositoryPath: string, accountId: string | null) => Promise<Repository>;
  pushOrigin: (repositoryPath: string, accountId: string | null) => Promise<Repository>;
  switchBranch: (repositoryPath: string, branch: string) => Promise<Repository>;
  connectAccount: () => Promise<AppState>;
  onGitHubLoginProgress: (callback: (progress: GitHubLoginProgress) => void) => () => void;
  setActiveAccount: (accountId: string) => Promise<AppState>;
  setAccountEmail: (accountId: string, email: string) => Promise<AppState>;
  setRepositoryAccount: (repositoryPath: string, accountId: string | null) => Promise<AppState>;
  removeAccount: (accountId: string) => Promise<AppState>;
  onMenuAction: (callback: (action: MenuAction) => void) => () => void;
  openExternal: (url: string) => Promise<void>;
};

type MenuAction = "open-repository" | "clone-repository" | "scan-folder" | "remove-repository";

type GitHubLoginProgress = {
  code: string | null;
  message: string;
};

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

type IconName = "alert" | "branch" | "check" | "chevron" | "clone" | "close" | "edit" | "external" | "folder" | "github" | "info" | "lock" | "more" | "plus" | "refresh" | "repository" | "route" | "search" | "settings" | "trash" | "upload";

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
    case "info": content = <><circle cx="12" cy="12" r="9" /><path d="M12 11v6" /><path d="M12 7h.01" /></>; break;
    case "lock": content = <><rect x="5" y="10" width="14" height="10" rx="2" /><path d="M8 10V7a4 4 0 0 1 8 0v3" /></>; break;
    case "more": content = <><circle cx="5" cy="12" r="1" fill="currentColor" stroke="none" /><circle cx="12" cy="12" r="1" fill="currentColor" stroke="none" /><circle cx="19" cy="12" r="1" fill="currentColor" stroke="none" /></>; break;
    case "plus": content = <><path d="M12 5v14" /><path d="M5 12h14" /></>; break;
    case "refresh": content = <><path d="M20 6v5h-5" /><path d="M19 11a7 7 0 1 0 1 5" /></>; break;
    case "repository": content = <><path d="M5 4h12a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2Z" /><path d="M8 4v16" /><path d="M5 17a3 3 0 0 1 3-3h11" /></>; break;
    case "route": content = <><path d="M5 7h11" /><path d="m13 4 3 3-3 3" /><path d="M19 17H8" /><path d="m11 14-3 3 3 3" /></>; break;
    case "search": content = <><circle cx="11" cy="11" r="7" /><path d="m20 20-4-4" /></>; break;
    case "settings": content = <><circle cx="12" cy="12" r="3" /><path d="M19 13.5v-3l-2-.7-.7-1.7.9-1.9-2.1-2.1-1.9.9-1.7-.7L10.5 2h-3l-.7 2-1.7.7-1.9-.9-2.1 2.1.9 1.9-.7 1.7-2 .7v3l2 .7.7 1.7-.9 1.9 2.1 2.1 1.9-.9 1.7.7.7 2h3l.7-2 1.7-.7 1.9.9 2.1-2.1-.9-1.9.7-1.7Z" transform="scale(.82) translate(2.6 2.6)" /></>; break;
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
};

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
    if (line.startsWith("diff --git") || line.startsWith("index ") || line.startsWith("--- ") || line.startsWith("+++ ")) continue;
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
  const [editingAccountId, setEditingAccountId] = useState<string | null>(null);
  const [accountEmail, setAccountEmail] = useState("");
  const [routingChoice, setRoutingChoice] = useState("follow");
  const [loginProgress, setLoginProgress] = useState<GitHubLoginProgress | null>(null);
  const accountMenuRef = useRef<HTMLDivElement>(null);
  const menuActionsRef = useRef<Record<MenuAction, () => void> | null>(null);

  const accounts = appState.accounts;
  const activeAccount = accounts.find((account) => account.id === appState.activeAccountId) ?? null;
  const boundAccountId = repository ? appState.repositoryAccounts[repository.path] : null;
  const repositoryAccount = accounts.find((account) => account.id === boundAccountId) ?? activeAccount;
  const diffLines = useMemo(() => parseDiff(diffText), [diffText]);
  const visibleRepositories = useMemo(() => {
    const needle = search.trim().toLowerCase();
    if (!needle) return appState.repositories;
    return appState.repositories.filter((repo) => `${repo.owner}/${repo.name} ${repo.path}`.toLowerCase().includes(needle));
  }, [appState.repositories, search]);
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
    return api.onGitHubLoginProgress((progress) => setLoginProgress(progress));
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

  function applyRepository(next: Repository, preserveSelection = true) {
    setRepository(next);
    setAppState((current) => ({
      ...current,
      selectedRepositoryPath: next.path,
      repositories: [
        { path: next.path, name: next.name, owner: next.owner, branch: next.branch, changes: next.files.length, lastOpened: new Date().toISOString() },
        ...current.repositories.filter((item) => item.path !== next.path),
      ].slice(0, 5000),
    }));
    setCheckedFiles((current) => preserveSelection ? current.filter((file) => next.files.some((item) => item.path === file)) : next.files.map((file) => file.path));
    setActiveFile((current) => preserveSelection && next.files.some((file) => file.path === current) ? current : next.files[0]?.path || "");
  }

  function showNotice(message: string, error = false) {
    setNotice({ message, error });
    window.setTimeout(() => setNotice(null), error ? 4400 : 2800);
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
    setGitHubRepositoriesLoading(true);
    try {
      setGitHubRepositories(await window.relayDesktop.listGitHubRepositories(accountId));
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
      setLoginProgress({ code: null, message: "Opening GitHub in your browser…" });
      const state = await window.relayDesktop.connectAccount();
      setAppState(state);
      setLoginProgress(null);
      setAccountModalOpen(false);
      showNotice(`Connected @${state.accounts.find((account) => account.id === state.activeAccountId)?.handle}`);
    } catch (error) {
      showNotice(messageFrom(error), true);
    } finally {
      setBusy("");
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

  async function saveAccountEmail() {
    if (!window.relayDesktop || !editingAccountId) return;
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
      setBusy("");
    }
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
      setAccountModalOpen(true);
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
        <button className="repo-picker" aria-label="Open repository" onClick={chooseRepository}>
          <span className="repo-mark">R</span>
          <span><small>Current repository</small>{repository ? <>{repository.owner} / <strong>{repository.name}</strong></> : <strong>Open a repository</strong>}</span>
          <Icon name="chevron" className="chevron" />
        </button>
        <div className="titlebar-center" aria-hidden="true">Relay</div>
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
              <button className="menu-action" onClick={() => { setAccountMenuOpen(false); setAccountModalOpen(true); }}><Icon name="plus" size={17} /> Add another account</button>
              {accounts.length > 0 && <button className="menu-action" onClick={() => { setAccountMenuOpen(false); setManageAccountsOpen(true); }}><Icon name="settings" size={16} /> Manage accounts</button>}
            </div>
          )}
        </div>
      </header>

      <section className="toolbar">
        <label className={`branch-control ${!repository ? "disabled" : ""}`}>
          <Icon name="branch" className="branch-symbol" size={22} />
          <span><small>Current branch</small><strong>{repository?.branch || "No repository open"}</strong></span>
          <Icon name="chevron" className="chevron" />
          {repository && <select value={repository.branch} onChange={(event) => changeBranch(event.target.value)} disabled={Boolean(busy)} aria-label="Switch branch">
            {repository.branches.map((branch) => <option key={branch} value={branch}>{branch}</option>)}
          </select>}
        </label>
        <div className="toolbar-spacer" />
        {repository && repositoryAccount && <div className="identity-pill" title="The identity used for this repository">
          <span className={`mini-avatar ${repositoryAccount.tone}`}>{repositoryAccount.initials}</span>
          <span>Using <strong>@{repositoryAccount.handle}</strong></span>
          {boundAccountId && <em>Pinned</em>}
        </div>}
        <button className="fetch-button" onClick={syncRemote} disabled={!repository || !repository.remote || Boolean(busy)}><Icon name={shouldPush ? "upload" : "refresh"} className={busy === "fetch" ? "spin" : ""} size={16} />{syncLabel}</button>
      </section>

      <div className="workspace">
        <aside className="repo-sidebar">
          <div className="sidebar-heading"><span>Repositories</span><button aria-label="Add repository" onClick={chooseRepository}><Icon name="plus" size={18} /></button></div>
          <label className="search-box"><Icon name="search" size={15} /><input value={search} onChange={(event) => setSearch(event.target.value)} placeholder="Filter repositories" aria-label="Filter repositories" /></label>
          <div className="repo-list">
            {visibleRepositories.length === 0 && <div className="repo-empty"><strong>No repositories yet</strong><span>Open, clone, or scan a folder for repositories.</span><div className="repo-empty-actions"><button onClick={chooseRepository}><Icon name="folder" size={14} />Open</button><button onClick={openCloneModal}><Icon name="clone" size={14} />Clone</button><button onClick={scanRepositoryFolder}><Icon name="search" size={14} />Scan</button></div></div>}
            {visibleRepositories.map((repo) => {
              const pinned = accounts.find((account) => account.id === appState.repositoryAccounts[repo.path]);
              return (
                <div key={repo.path} className={`repo-row ${repo.path === repository?.path ? "selected" : ""}`}>
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
            <div className="history-view">
              <div className="history-date">Recent commits on <strong>{repository.branch}</strong></div>
              {repository.history.length === 0 && <div className="history-empty">This repository does not have any commits yet.</div>}
              {repository.history.map((item) => (
                <div className="history-item" key={item.fullHash}>
                  <span className="timeline"><i /></span>
                  <span className="history-copy"><strong>{item.title}</strong><small>{item.hash} · {relativeTime(item.date)} · {item.author}</small></span>
                  <span className="history-author" title={item.email}>{initials(item.author)}</span>
                </div>
              ))}
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
            <button className="primary-modal-button" onClick={saveRepositoryAccount}>Save preference</button>
          </section>
        </div>
      )}

      {accountModalOpen && (
        <div className="modal-backdrop" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) setAccountModalOpen(false); }}>
          <section className="modal account-form-modal" role="dialog" aria-modal="true" aria-labelledby="add-account-title">
            <button className="modal-close" onClick={() => setAccountModalOpen(false)} aria-label="Close"><Icon name="close" size={17} /></button>
            <div className="modal-icon github-mark"><Icon name="github" size={22} /></div>
            <h2 id="add-account-title">Connect a GitHub account</h2>
            <p>Relay opens GitHub in your browser for an official one-time OAuth login. No personal access token is needed.</p>
            {loginProgress?.code && <button className="device-code" onClick={() => navigator.clipboard.writeText(loginProgress.code || "")} title="Copy code"><span>One-time code</span><strong>{loginProgress.code}</strong><small>Click to copy</small></button>}
            {busy === "account" && <div className="login-progress"><Icon name="refresh" className="spin" size={17} /><span>{loginProgress?.message || "Waiting for GitHub…"}</span></div>}
            <div className="modal-note"><Icon name="info" size={17} />GitHub CLI stores the OAuth credential in your operating system credential store. Relay never asks for or saves a PAT.</div>
            <button className="primary-modal-button" disabled={busy === "account"} onClick={connectAccount}>{busy === "account" ? "Waiting for browser sign-in…" : "Continue with GitHub"}</button>
            {busy === "account" && <button className="token-help" onClick={() => window.relayDesktop?.openExternal("https://github.com/login/device")}>Open the GitHub device page again <Icon name="external" size={13} /></button>}
            <button className="secondary-modal-button" onClick={() => setAccountModalOpen(false)}>Cancel</button>
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
            <button className="primary-modal-button" onClick={() => { setManageAccountsOpen(false); setAccountModalOpen(true); }}>Add account</button>
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
                <div className="github-repository-heading"><span>Available to <strong>@{activeAccount.handle}</strong></span><span>{githubRepositoriesLoading ? "Loading…" : `${visibleGitHubRepositories.length} repositories`}</span></div>
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
              </div>
            ) : (
              <label className="form-field"><span>Repository URL</span><input value={cloneForm.remoteUrl} onChange={(event) => { const remoteUrl = event.target.value; setCloneForm((current) => ({ ...current, remoteUrl, repositoryName: repositoryNameFromUrl(remoteUrl) })); }} placeholder="https://github.com/owner/repository.git" /></label>
            )}
            <label className="form-field"><span>Repository name</span><input value={cloneForm.repositoryName} onChange={(event) => setCloneForm((current) => ({ ...current, repositoryName: event.target.value }))} placeholder="repository" /></label>
            <label className="form-field"><span>Clone into folder</span><div className="path-picker"><input value={cloneForm.parentPath} readOnly placeholder="Choose a local folder" /><button onClick={chooseCloneDirectory}><Icon name="folder" size={15} />Choose</button></div></label>
            <div className="modal-note"><Icon name="info" size={17} />{activeAccount ? `Private GitHub repositories use @${activeAccount.handle}.` : "Connect a GitHub account to browse and clone private repositories."} SSH URLs use your existing SSH configuration.</div>
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
            <label className="form-field"><span>Commit email</span><input type="email" value={accountEmail} onChange={(event) => setAccountEmail(event.target.value)} placeholder="Leave blank to use GitHub noreply" /></label>
            <button className="primary-modal-button" disabled={busy === "email"} onClick={saveAccountEmail}>{busy === "email" ? "Saving…" : "Save email"}</button>
            <button className="secondary-modal-button" onClick={() => setEditingAccountId(null)}>Cancel</button>
          </section>
        </div>
      )}

      {notice && <div className={`toast ${notice.error ? "error" : ""}`}><span><Icon name={notice.error ? "alert" : "check"} size={13} /></span>{notice.message}</div>}
    </main>
  );
}
