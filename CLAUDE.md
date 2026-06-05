# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A single-file GTK3 module (`geary-hide-sidebar.c`, ~450 lines) that collapses
Geary's left "Mail" sidebar at runtime by being loaded into Geary's process via
`GTK_MODULES`. There is no Geary fork and nothing is written under `/usr`. Built
against Geary `1:46.0` (Arch) on system GTK3 + libhandy-1.

Geary's upstream source is checked out at `../geary` — consult it (e.g.
`ui/application-main-window.ui`) to verify the GType name / style class the module
couples to. The initial commit (`3c45d57`) has been tested live and works as
intended; treat it as the known-good baseline.

## Commands

```sh
make          # build libgeary-hide-sidebar.so
make run      # build, then launch geary with the module loaded
make debug    # build + launch with GEARY_HIDE_SIDEBAR_DEBUG=1 (verbose stderr)
make test     # build + run the unit tests (headless via xvfb-run)
make clean    # remove the .so and test binary
```

Build needs GTK3 dev headers via `pkg-config gtk+-3.0 gmodule-2.0`.

`test_geary_hide_sidebar.c` is a GLib-test suite that `#include`s the module
source so it can reach the static functions and config globals. It needs a GDK
display, so `make test` runs it under `xvfb-run`; with no display the binary
exits 77 (skipped). It covers `parse_config` (env parsing + validation),
`acquire_sidebar`/the widget-tree helpers (against a synthetic Geary-like tree),
`apply_state`, and the `on_configure` auto/override logic (driven by fabricated
`GdkEventConfigure`s rather than the real event loop). To run a single test:
`xvfb-run -a ./test_geary_hide_sidebar -p /config/key`.

For end-to-end checks against real Geary, `make debug` and watch stderr, or
inspect the live widget tree with `GTK_DEBUG=interactive geary`.

## How the runtime injection works (the non-obvious part)

A Geary *plugin* cannot reach the main-window layout: the only extension point
that could (`TrustedExtension`) is refused for plugins outside Geary's system
plugins dir, and Geary scans no user plugin dir. So this is a **GTK module**
instead — GTK calls `gtk_module_init()` right after `gtk_init()`, inside Geary's
process, giving direct access to the live widget tree.

`gtk_module_init()` runs *before any widget exists*, so it cannot find the window
directly. Instead it installs:
- an emission hook on `GtkWidget::map` (`on_widget_map`) that fires `setup_window`
  the first time a window of GType `ApplicationMainWindow` is mapped, and
- a global GTK key snooper (`key_snooper`) for the toggle accelerator —
  per-window `key-press-event` proved unreliable, so the snooper is intentional.

## How the sidebar is located (the fragile coupling)

Geary's main window is a composite template, so GtkBuilder does **not** copy
object ids into widget names — you cannot find the sidebar by name. The module
anchors on structure instead (`acquire_sidebar`): find the widget carrying CSS
style class `geary-folder`, take its parent (the "Mail" column box), and that
box's sibling `GtkSeparator`.

The **only** two couplings to Geary internals are the two `#define`s near the top
of `geary-hide-sidebar.c`:
- `GEARY_MAIN_WINDOW_TYPE` = `"ApplicationMainWindow"`
- `GEARY_FOLDER_STYLE_CLASS` = `"geary-folder"`

If a future Geary release renames either, the module silently does nothing — edit
these defines first when it stops working.

Collapsing is more than `gtk_widget_hide`: an unfolded `HdyLeaflet` tracks
visibility and `child-visible` separately, so `apply_state` sets both, marks the
box `no-show-all`, and a `show`-signal handler (`on_show_rehide`) re-hides it if
Geary tries to show it again.

## Packaging & release (AUR)

`packaging/aur/` holds two AUR packages. Each pairs with one Geary variant,
because pacman has no "either/or" dependency and `geary-git` `conflicts=(geary)`
without `provides=(geary)` — so a single OR-dependency is impossible:
- **`geary-hide-sidebar`** (`PKGBUILD`) — builds from the GitHub tag archive;
  `depends=('geary-git' …)`. Note the archive top-dir is
  `geary-hide-sidebar-module-$pkgver` because the *repo* name differs from the
  *package* name.
- **`geary-hide-sidebar-bin`** (`PKGBUILD-bin`) — x86_64-only; downloads the
  prebuilt `.so` tarball attached to the GitHub release; `depends=('geary' …)`
  (stable). The `.so` is built on ubuntu-22.04 (older glibc → forward-compatible
  on Arch) and links GTK3/glib2 by stable SONAME, so unversioned gtk3/glib2 is
  enough. The two packages `provides`/`conflicts` each other and install to
  identical paths, sharing the same `.install` scriptlet and hook (the hook
  triggers on both `geary` and `geary-git`).

Both install the launcher-injection machinery, all driven by
`packaging/aur/inject.sh`:

- `inject.sh apply|remove` prepends/strips `env GTK_MODULES=<module> ` on the
  `Exec=` lines of **both** Geary launch paths: the `.desktop` entry *and* the
  D-Bus activation service (`org.gnome.Geary.service`). Patching only the
  desktop file is a bug — `DBusActivatable=true` means GNOME usually starts
  Geary via D-Bus. `apply` is idempotent; `remove` is byte-exact reversible.
- `geary-hide-sidebar.install` calls `apply` on install/upgrade and `remove`
  on `pre_remove` (while inject.sh still exists).
- `geary-hide-sidebar.hook` is a pacman PostTransaction hook on the `geary`
  package — re-applies after a Geary upgrade restores its pristine launchers.

Both launcher files are owned by the `geary` package; we edit them in place
(pacman will report them as modified). `inject.sh` lives at
`/usr/lib/geary-hide-sidebar/` next to the `.so`.

Release is tag-driven (`.github/workflows/release.yml`, on `v*`): build+test +
stage the prebuilt tarball → create GitHub release (attaching the tarball +
sha256) → pin both checksums (source = tag-archive hash, bin = tarball hash) +
sync `pkgver` into both PKGBUILDs → publish both packages to AUR via
`KSXGitHub/github-actions-deploy-aur` (the `.install` ships through the action's
`assets:` input; `.SRCINFO` is regenerated by the action). The in-tree PKGBUILDs
keep `SKIP` checksums between releases by convention. Bump the in-tree `pkgver`
in both PKGBUILDs before tagging.

Gotcha: deploy-aur copies the `pkgbuild` file preserving its name, but each AUR
repo tracks a file literally named `PKGBUILD` — so the workflow stages
`PKGBUILD-bin` as `staging/bin/PKGBUILD` before pointing the bin step at it.

AUR push is skipped if the `AUR_SSH_KEY` secret is unset. `ci.yml` runs
build+test on push/PR.

To validate the package locally without a tag: `makepkg` with the `source=`
overridden to a `file://` tarball (geary/gtk3/xorg-server-xvfb must be present).

## Configuration & modes

All config is via environment variables, parsed once in `parse_config()` (globals
`g_mode`, `g_min_width`, `g_width_ratio`, `g_accel_key`/`g_accel_mods`). Modes:
`auto` (default — collapse on portrait monitor, or window < `WIDTH_RATIO` of the
monitor's usable width, or < `MIN_WIDTH` px floor; re-evaluated on every
`configure-event` via `on_configure`), `always`, `manual`. A manual keypress
overrides auto only within the current size class — crossing the collapse
threshold (maximize ↔ half-tile) resumes auto. See README.md for the full env-var
table and examples.

## Composer dark-mode fix (a second, separate feature)

Beyond hiding the sidebar, the module also patches a Geary dark-mode bug: Geary's
`composer-web-view.css` hardcodes a white background on the focused editing area
(`body > div:focus-within`), which is unreadable once the "Override the original
colors in HTML emails" preference lightens the text. `ensure_composer_css()` (run
from `gtk_module_init`, as the user, *before* Geary loads its stylesheet) appends a
counter-rule to Geary's own per-user stylesheet `~/.config/geary/user-style.css`
(or legacy `user-message.css` if that's the one Geary will read). It's
marker-delimited (`COMPOSER_FIX_BEGIN`/`END`) and idempotent — never clobbers a
user's CSS — and opt-out via `GEARY_HIDE_SIDEBAR_COMPOSER_FIX=0`. This reuses
Geary's documented user-stylesheet hook, so no WebKit linking is needed.
