"use strict";

/**
 * One definition of Relay's menus, used twice.
 *
 * On Windows the native menu bar occupies its own row below the title bar,
 * which is one row more of chrome than Relay wants. The window there draws its
 * own title row instead, so the renderer needs the same menu structure to
 * render on that row. Describing the menus once and deriving both the native
 * Electron template and the renderer descriptor from it keeps the two from
 * drifting apart.
 *
 * The native menu stays installed on Windows even while hidden, because that
 * is what keeps the accelerators working.
 */

// Windows labels carry "&" mnemonics; macOS labels never do.
const MENU_DEFINITION = [
  {
    id: "file",
    label: "File",
    mnemonic: "F",
    items: [
      { id: "open-repository", label: "Add Local Repository…", mnemonic: "L", accelerator: "CmdOrCtrl+O", action: "menu-action" },
      { id: "clone-repository", label: "Clone Repository…", mnemonic: "C", accelerator: "CmdOrCtrl+Shift+O", action: "menu-action" },
      { id: "scan-folder", label: "Scan Folder for Repositories…", mnemonic: "S", action: "menu-action" },
      { type: "separator" },
      { id: "remove-repository", label: "Remove Current Repository from Relay", mnemonic: "R", action: "menu-action" },
      { type: "separator", windowsOnly: true },
      { id: "quit", label: "Exit", mnemonic: "x", role: "quit", windowsOnly: true },
    ],
  },
  {
    id: "edit",
    label: "Edit",
    mnemonic: "E",
    items: [
      { id: "undo", label: "Undo", accelerator: "CmdOrCtrl+Z", role: "undo" },
      { id: "redo", label: "Redo", accelerator: "CmdOrCtrl+Shift+Z", role: "redo" },
      { type: "separator" },
      { id: "cut", label: "Cut", accelerator: "CmdOrCtrl+X", role: "cut" },
      { id: "copy", label: "Copy", accelerator: "CmdOrCtrl+C", role: "copy" },
      { id: "paste", label: "Paste", accelerator: "CmdOrCtrl+V", role: "paste" },
      { id: "selectAll", label: "Select All", accelerator: "CmdOrCtrl+A", role: "selectAll" },
    ],
  },
  {
    id: "view",
    label: "View",
    mnemonic: "V",
    items: [
      { id: "reload", label: "Reload", accelerator: "CmdOrCtrl+R", role: "reload" },
      { id: "forceReload", label: "Force Reload", accelerator: "CmdOrCtrl+Shift+R", role: "forceReload" },
      { id: "toggleDevTools", label: "Toggle Developer Tools", role: "toggleDevTools" },
      { type: "separator" },
      { id: "resetZoom", label: "Actual Size", accelerator: "CmdOrCtrl+0", role: "resetZoom" },
      { id: "zoomIn", label: "Zoom In", accelerator: "CmdOrCtrl+Plus", role: "zoomIn" },
      { id: "zoomOut", label: "Zoom Out", accelerator: "CmdOrCtrl+-", role: "zoomOut" },
      { type: "separator" },
      { id: "togglefullscreen", label: "Toggle Full Screen", role: "togglefullscreen" },
    ],
  },
  {
    id: "window",
    label: "Window",
    mnemonic: "W",
    items: [
      { id: "minimize", label: "Minimize", accelerator: "CmdOrCtrl+M", role: "minimize" },
      { id: "close", label: "Close", accelerator: "CmdOrCtrl+W", role: "close" },
    ],
  },
];

/** Inserts a "&" before the mnemonic letter, the way Windows menus expect. */
function withMnemonic(label, mnemonic) {
  if (!mnemonic) return label;
  const index = label.indexOf(mnemonic);
  return index < 0 ? label : `${label.slice(0, index)}&${label.slice(index)}`;
}

function visibleItems(items, isMac) {
  return items.filter((item) => !(item.windowsOnly && isMac));
}

/**
 * Builds the Electron menu template.
 * `onAction` receives the action id for items Relay handles in the renderer.
 */
function menuTemplate({ isMac, appName, onAction }) {
  const template = [];
  if (isMac) {
    template.push({
      label: appName,
      submenu: [
        { role: "about" }, { type: "separator" }, { role: "services" }, { type: "separator" },
        { role: "hide" }, { role: "hideOthers" }, { role: "unhide" }, { type: "separator" }, { role: "quit" },
      ],
    });
  }

  for (const menu of MENU_DEFINITION) {
    template.push({
      label: isMac ? menu.label : withMnemonic(menu.label, menu.mnemonic),
      submenu: visibleItems(menu.items, isMac).map((item) => {
        if (item.type === "separator") return { type: "separator" };
        const entry = { label: isMac ? item.label : withMnemonic(item.label, item.mnemonic) };
        if (item.accelerator) entry.accelerator = item.accelerator;
        if (item.role) entry.role = item.role;
        else entry.click = () => onAction(item.id);
        return entry;
      }),
    });
  }
  return template;
}

/**
 * The same menus as plain data for the renderer's in-window menu bar.
 * Accelerators are rendered as display text only; the native menu still owns
 * the real key handling.
 */
function menuDescriptor({ isMac }) {
  return MENU_DEFINITION.map((menu) => ({
    id: menu.id,
    label: menu.label,
    mnemonic: menu.mnemonic,
    items: visibleItems(menu.items, isMac).map((item) => item.type === "separator"
      ? { type: "separator" }
      : {
        id: item.id,
        label: item.label,
        mnemonic: item.mnemonic || null,
        accelerator: item.accelerator ? displayAccelerator(item.accelerator, isMac) : null,
        role: item.role || null,
      }),
  }));
}

function displayAccelerator(accelerator, isMac) {
  return accelerator
    .replace(/CmdOrCtrl/g, isMac ? "Cmd" : "Ctrl")
    .replace(/Plus/g, "+")
    .replace(/\+/g, isMac ? "" : "+")
    .replace(/^Ctrl(?!\+)/, "Ctrl+");
}

/** Every command id the renderer is allowed to ask the main process to run. */
function commandIds() {
  const ids = new Set();
  for (const menu of MENU_DEFINITION) {
    for (const item of menu.items) if (item.id) ids.add(item.id);
  }
  return ids;
}

module.exports = { MENU_DEFINITION, commandIds, menuDescriptor, menuTemplate, withMnemonic };
