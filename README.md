# geary-hide-sidebar

A tiny **GTK3 module** that collapses/expands Geary's left **"Mail"
sidebar** (the account/folder-list column) at runtime — **without forking
or rebuilding Geary**, and without touching any file under `/usr`.

- **Keybinding** to toggle the sidebar (default **Ctrl+Shift+M**).
- **Auto mode**: collapses by default when the window is **narrow** or on a
  **portrait (vertical) monitor**; expands when wide on a landscape
  monitor. Re-evaluated whenever you resize or drag between monitors.

## Screenshots

<table>
<tr>
<td width="50%" align="center">

**Default Geary** — the left "Mail" column (account/folder list) takes up a
big slice of a narrow window.

<img src="docs/images/sidebar-shown.png" alt="Geary with the Mail sidebar shown" width="100%">

</td>
<td width="50%" align="center">

**With the module** — the sidebar is collapsed, handing all the width to the
message list and reading pane. Toggle back any time with **Ctrl+Shift+M**.

<img src="docs/images/sidebar-hidden.png" alt="Geary with the Mail sidebar hidden" width="100%">

</td>
</tr>
</table>

> Email addresses in the screenshots are placeholders.

Written against Geary `1:46.0` (Arch package), which links the system
GTK3 + libhandy-1.

## Install on Arch (AUR)

**Pick the package that matches your Geary** — pacman has no "either/or"
dependency, and `geary-git` conflicts with `geary` without providing it, so
each module variant pairs with one Geary:

```sh
yay -S geary-hide-sidebar       # source build; depends on geary-git
yay -S geary-hide-sidebar-bin   # prebuilt .so (x86_64); depends on geary (stable)
```

- **`geary-hide-sidebar-bin`** → stable **`geary`**. Prebuilt, x86_64-only, no
  compile.
- **`geary-hide-sidebar`** → **`geary-git`** (the development branch, which also
  ships native dark-mode email previews). Builds from source, so it's the
  option for aarch64 too.

The two module packages `provides`/`conflicts` each other (you can only run one
Geary anyway). Both install to `/usr/lib/geary-hide-sidebar/`, and the install
scriptlet patches **both** Geary launch paths to load the module via
`GTK_MODULES`:

- the desktop entry `/usr/share/applications/org.gnome.Geary.desktop`, and
- the D-Bus activation service `/usr/share/dbus-1/services/org.gnome.Geary.service`
  — required because Geary is `DBusActivatable`, so GNOME often starts it
  through D-Bus rather than the desktop `Exec=` line.

A bundled pacman hook re-applies the patch after any future Geary upgrade —
stable `geary` or `geary-git` — (which would otherwise restore the pristine
launchers). Removing the package strips the injection back out, restoring
Geary's launchers **byte-for-byte**:

```sh
yay -R geary-hide-sidebar      # Geary keeps working, unpatched
```

Restart any running Geary instance after install/removal. Tune behavior by
editing the `GEARY_HIDE_SIDEBAR_*` env vars in the patched `Exec=` lines (see
*Configuration* below).

> Unlike the manual setup below (which touches nothing under `/usr`), the AUR
> package edits Geary's system launcher files in place. The edit is idempotent
> and exactly reversible, so uninstalling leaves Geary as it was.

## Dark mode for email previews (geary-git)

This is **not** part of this module — it's a built-in Geary feature, but only on
`geary-git` (the development branch). It was added after the 46.0 release, so
stable `geary 1:46.0` does **not** have it; it'll arrive in a future stable
release. Since the source package already pairs with `geary-git`, you get it for
free there.

It's easy to miss because Geary doesn't label it "dark mode" — it's a color
override that uses the bundled [Dark Reader](https://darkreader.org/) engine,
tuned to Adwaita colors. To turn it on:

1. Run your desktop in dark mode — Dark Reader only darkens when the app prefers
   dark (`gsettings get org.gnome.desktop.interface color-scheme` →
   `'prefer-dark'`).
2. Enable the setting, either in **Preferences → "Override the original colors in
   HTML emails"** (last toggle on the page), or on the command line:

   ```sh
   gsettings set org.gnome.Geary unset-html-colors true
   ```

3. **Fully restart Geary** (see the gotcha below).

> **Gotcha — closing the window is not a restart.** With "Watch for new mail when
> closed" enabled (the default), Geary keeps running as a background D-Bus
> service, so the old process — and its old UI — stays alive after you close the
> window or even after an upgrade. If a setting or a newly installed version
> doesn't show up, the running process is stale. Force a real restart:
>
> ```sh
> geary --quit         # ask the background service to exit
> pkill -x geary       # only if it's still running
> geary &              # relaunch
> ```
>
> Tip: `readlink -f /proc/$(pgrep -x geary)/exe` ending in `(deleted)` means the
> running binary was replaced on disk by an upgrade — a sure sign you're still on
> the old process.

To revert: `gsettings set org.gnome.Geary unset-html-colors false`, then restart.

### The unreadable-composer fix (handled automatically)

Geary has a dark-mode bug: when you compose a message, its `composer-web-view.css`
hardcodes a **white** background on the focused editing area, so with dark mode on
you get light text on white — almost unreadable. This module fixes it
automatically: at launch it ensures a small counter-rule is present in Geary's own
user stylesheet (`~/.config/geary/user-style.css`), which Geary loads into every
webview. The write is marker-delimited and idempotent, so it never clobbers your
own CSS; set `GEARY_HIDE_SIDEBAR_COMPOSER_FIX=0` to skip it.

To apply it by hand instead (no module), drop this into
`~/.config/geary/user-style.css` and restart Geary:

```css
body > div:focus-within { background-color: transparent !important; }
```

## Why a GTK module instead of a Geary plugin?

Geary's plugin API never exposes the main-window layout. The only
extension point that can reach those widgets (`TrustedExtension`) is
refused for any plugin not installed in Geary's *system* plugins
directory, and Geary scans no user plugin directory at all. So a normal
plugin physically cannot touch the sidebar.

A GTK module sidesteps that: GTK loads any library named in the
`GTK_MODULES` environment variable and calls its `gtk_module_init()` right
after GTK starts — so we run *inside Geary's process* and manipulate the
live widget tree directly.

### Why not just send a PR upstream?

We deliberately didn't open a pull request for this. The ability to
hide/collapse the folder-list column has been requested upstream several
times over the years, and those issues have gone unaddressed — which reads
less like a backlog and more like a deliberate design stance that this
isn't a direction the maintainers want Geary to take. Rather than push a
change they've effectively already declined, this module gives the same
result entirely on the user's side, without forking or modifying Geary.

## How it works

1. On load we install an emission hook on `GtkWidget::map` and a global
   GTK key snooper.
2. When a window of GType `ApplicationMainWindow` (Geary's main window, per
   `<template class="ApplicationMainWindow">` in
   `ui/application-main-window.ui`) is mapped, we locate the "Mail" column
   **by structure**, not by name. Geary's main window is a composite
   template, so GtkBuilder does *not* copy object ids into widget names;
   instead we anchor on a CSS style class that the `.ui` reliably applies:
   - find the widget with style class `geary-folder` (the folder-list
     scrolled window),
   - its parent is the "Mail" column box we hide,
   - and that box's sibling separator (a `GtkSeparator`) is hidden too.
3. To collapse we set the box invisible *and* `child-visible` false (an
   unfolded `HdyLeaflet` tracks the two separately), mark it `no-show-all`,
   and re-hide it if Geary shows it again.
4. The key snooper catches the toggle accelerator regardless of focus
   (per-window `key-press-event` was unreliable here). A `configure-event`
   handler re-applies the auto policy on resize / monitor move.

**The only coupling to Geary internals** is the window GType name and the
`geary-folder` style class — edit the `#define`s at the top of
`geary-hide-sidebar.c` if a future release renames them.

A manual keypress overrides auto **only within the current size class**:
your choice holds through minor resizes, but once the window crosses the
collapse threshold (e.g. you maximize or tile it to half) auto resumes.

## Build

```sh
make
```

Produces `libgeary-hide-sidebar.so`. Needs the GTK3 dev headers
(`pkg-config gtk+-3.0 gmodule-2.0`).

## Test

```sh
make test           # builds and runs the unit tests
```

The tests need a GDK display, so `make test` runs them under `xvfb-run` when
it's installed (and skips, exit 77, if no display is reachable).

## Run

```sh
make run            # GTK_MODULES=<abs path>/libgeary-hide-sidebar.so geary
make debug          # same + GEARY_HIDE_SIDEBAR_DEBUG=1 verbose logging
```

or manually (the var accepts an absolute path, so the `.so` can live
anywhere in your home dir):

```sh
GTK_MODULES=$PWD/libgeary-hide-sidebar.so geary
```

Press **Ctrl+Shift+M** to toggle the sidebar.

## Configuration (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `GEARY_HIDE_SIDEBAR_MODE` | `auto` | `auto`, `always`, or `manual` (see below) |
| `GEARY_HIDE_SIDEBAR_WIDTH_RATIO` | `0.62` | `auto` collapses when the window covers less than this fraction of the monitor's usable width |
| `GEARY_HIDE_SIDEBAR_MIN_WIDTH` | `800` | Absolute px floor; `auto` always collapses below this |
| `GEARY_HIDE_SIDEBAR_KEY` | `<Control><Shift>m` | GTK accelerator, e.g. `<Control><Shift>m`, `<Control>backslash`, `F9` |
| `GEARY_HIDE_SIDEBAR_COMPOSER_FIX` | _on_ | `0`/`false`/`no` to skip the composer dark-mode CSS fix (see [Dark mode](#dark-mode-for-email-previews-geary-git)) |
| `GEARY_HIDE_SIDEBAR_DEBUG` | _unset_ | `1` to log decisions to stderr |

**Modes**

- `auto` — collapse when **any** of these hold, re-checked on every resize
  or monitor move:
  - the monitor is **portrait** (height > width), or
  - the window covers less than `WIDTH_RATIO` of the monitor's usable width
    (so a half-tiled window collapses, a maximized one expands —
    independent of the monitor's resolution), or
  - the window is narrower than the absolute `MIN_WIDTH` floor.

  The keybinding still works; a manual toggle holds only until the window
  crosses the collapse threshold (e.g. maximize ↔ half-tile), then auto
  resumes.
- `always` — start collapsed and stay collapsed (size ignored); the key still
  toggles.
- `manual` — start expanded; only the keybinding changes it.

Examples:

```sh
# Be more eager: collapse until the window covers 75% of the monitor:
GEARY_HIDE_SIDEBAR_WIDTH_RATIO=0.75 GTK_MODULES=$PWD/libgeary-hide-sidebar.so geary

# Use F9 instead of the default, never auto-collapse:
GEARY_HIDE_SIDEBAR_MODE=manual GEARY_HIDE_SIDEBAR_KEY='F9' \
  GTK_MODULES=$PWD/libgeary-hide-sidebar.so geary
```

> Heads-up: when collapsed, Geary's only folder switcher is hidden. Press
> Ctrl+Shift+M (or move to a wider window) to bring it back and pick a folder. The
> last selected folder is remembered across launches.

## Make it permanent (per-user launcher)

A `org.gnome.Geary.desktop` is included that overrides the system launcher
for your user only. **Edit its `Exec=` line** to set the absolute module
path and any env vars you want, then:

```sh
cp org.gnome.Geary.desktop ~/.local/share/applications/
update-desktop-database ~/.local/share/applications 2>/dev/null || true
```

Remove that copy to revert.

## Quick proof without building anything

```sh
GTK_DEBUG=interactive geary
```

In the GTK Inspector, find the `folder_box` object and toggle its
**Visible** property. The module just automates that, plus the keybinding
and size policy.

## Uninstall / revert

- Launch Geary without `GTK_MODULES` (or remove the `.desktop` copy).
- `make clean` removes the built `.so`.
- Nothing is written outside this project directory and (if you copied it)
  `~/.local/share/applications/org.gnome.Geary.desktop`.

## Files

| File | Purpose |
|---|---|
| `geary-hide-sidebar.c` | The GTK module source |
| `test_geary_hide_sidebar.c` | Unit tests (`make test`, headless via xvfb-run) |
| `Makefile` | Build / run / debug / test / install targets |
| `org.gnome.Geary.desktop` | Optional per-user launcher with the hack |
| `compile_commands.json` | Lets your editor resolve GTK headers (generated) |
| `README.md` | This file |
