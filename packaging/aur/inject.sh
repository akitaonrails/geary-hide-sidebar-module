#!/bin/sh
#
# inject.sh — patch (or un-patch) Geary's launchers so they load the
# geary-hide-sidebar GTK module via GTK_MODULES.
#
#   inject.sh apply     prepend `env GTK_MODULES=<module>` to Geary's Exec lines
#   inject.sh remove    strip that prefix back out
#
# Two launch paths must be covered, because Geary's desktop entry sets
# DBusActivatable=true: a launcher may exec the .desktop's Exec= line OR
# start Geary through its D-Bus activation service. We patch both.
#
# The patch is idempotent (apply never injects twice) and exactly reversible
# (remove strips only our own prefix), so uninstalling restores Geary's
# launchers byte-for-byte. Both files are owned by the `geary` package, so a
# Geary upgrade overwrites them with pristine copies — the accompanying
# pacman hook re-runs `apply` after any geary transaction to re-patch.
#
# Run as root (pacman scriptlets/hooks already are).

set -eu

MODULE="/usr/lib/geary-hide-sidebar/libgeary-hide-sidebar.so"
INJECT="env GTK_MODULES=${MODULE} "

TARGETS="
/usr/share/applications/org.gnome.Geary.desktop
/usr/share/dbus-1/services/org.gnome.Geary.service
"

patch_file() {
    # $1 = apply|remove   $2 = file
    [ -f "$2" ] || return 0
    tmp="$2.ghs.tmp"

    if [ "$1" = apply ]; then
        # Prefix every Exec= line that does not already carry the module.
        # index()==0 keeps it idempotent; sub() replaces only the literal
        # `Exec=` anchor (the module path contains no awk-special chars).
        awk -v mod="$MODULE" -v inj="$INJECT" '
            /^Exec=/ && index($0, mod) == 0 { sub(/^Exec=/, "Exec=" inj) }
            { print }
        ' "$2" > "$tmp"
    else
        # Strip our exact `Exec=env GTK_MODULES=<module> ` prefix, leaving
        # whatever command followed it untouched.
        awk -v pfx="Exec=${INJECT}" '
            index($0, pfx) == 1 { $0 = "Exec=" substr($0, length(pfx) + 1) }
            { print }
        ' "$2" > "$tmp"
    fi

    # Overwrite contents in place so the file keeps its original owner/mode.
    cat "$tmp" > "$2"
    rm -f "$tmp"
}

action="${1:-apply}"
case "$action" in
    apply|remove) ;;
    *) echo "usage: $0 {apply|remove}" >&2; exit 2 ;;
esac

for f in $TARGETS; do
    patch_file "$action" "$f"
done

# Let launchers pick up the changed Exec= lines.
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications 2>/dev/null || true
fi
