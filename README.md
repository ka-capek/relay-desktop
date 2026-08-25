# Relay

Relay is a small GitHub Desktop-style client focused on easy multi-account
switching. It is a real Electron app for Apple silicon Macs and 64-bit Windows,
with Git and GitHub CLI bundled in the installer.

## Download

Download the ready-to-use installers from the
[latest GitHub release](https://github.com/ka-capek/relay-desktop/releases/latest):

- Windows 64-bit: `Relay-Setup-0.4.0-x64.exe`
- macOS Apple Silicon: `Relay-0.4.0-arm64.dmg`

The builds are currently unsigned, so Windows SmartScreen or macOS Gatekeeper
may ask for confirmation the first time Relay is opened.

## Prerequisites

- Node.js `>=22.13.0` (development only)
- Git and the official GitHub CLI when running from source

## Quick Start

```bash
npm install
npm run desktop:open
```

## Desktop builds

Release installers bundle their own Git and GitHub CLI runtimes. Those
third-party binaries are intentionally excluded from Git history; local release
packaging expects them under `runtime/git` and `runtime/gh`.

- `npm run desktop:mac` builds an Apple silicon DMG.
- `npm run desktop:windows` builds a Windows x64 installer.
- `npm run desktop:release` builds both.

## GitHub sign-in

Relay uses the official GitHub CLI browser/device OAuth flow. The CLI stores each
OAuth credential in the operating system credential store; Relay does not ask
for or persist personal access tokens. Relay keeps a separate GitHub CLI config
directory inside its app data so it does not change the active account in a
developer's normal terminal session.

Relay starts with no repository selected. Previously opened repositories remain
in the sidebar as recent shortcuts until the user chooses one.

## Repository workflow

- Clone from a searchable list of repositories available to the active GitHub
  account, including private and organization repositories, or paste an HTTPS or
  SSH URL.
- Use **File → Scan Folder for Repositories…** to recursively add existing local
  repositories in bulk.
- Remove a repository from the Relay sidebar with its trash button or the File
  menu. Relay only removes its shortcut and never deletes repository files.
- Change each account's commit email under **Manage accounts**. Leaving it blank
  restores that account's GitHub noreply address.
