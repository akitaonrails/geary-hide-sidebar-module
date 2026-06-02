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

## Configuration & modes

All config is via environment variables, parsed once in `parse_config()` (globals
`g_mode`, `g_min_width`, `g_width_ratio`, `g_accel_key`/`g_accel_mods`). Modes:
`auto` (default — collapse on portrait monitor, or window < `WIDTH_RATIO` of the
monitor's usable width, or < `MIN_WIDTH` px floor; re-evaluated on every
`configure-event` via `on_configure`), `always`, `manual`. A manual keypress
overrides auto only within the current size class — crossing the collapse
threshold (maximize ↔ half-tile) resumes auto. See README.md for the full env-var
table and examples.
