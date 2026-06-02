/*
 * geary-hide-sidebar — a GTK3 module that collapses/expands Geary's left
 * "Mail" sidebar (the account/folder-list column) at runtime, with no
 * fork and no changes to Geary's installed files.
 *
 * Features
 * --------
 *   - A keybinding (default Ctrl+Shift+M) toggles the sidebar, regardless
 *     of which widget has focus.
 *   - "auto" mode collapses the sidebar by default when the window is
 *     narrow OR sitting on a portrait (vertical) monitor, and expands it
 *     when the window is wide on a landscape monitor. Re-evaluated on
 *     every resize / move between monitors.
 *
 * How it works
 * ------------
 * GTK loads any module named in the GTK_MODULES environment variable and
 * calls gtk_module_init() right after GTK starts, so we run inside Geary's
 * process. We install an emission hook on GtkWidget::map; when a window
 * whose GType name is "ApplicationMainWindow" (Geary's main window, per
 * <template class="..."> in ui/application-main-window.ui) is mapped, we
 * locate the "Mail" column box and its separator by structure and hide
 * them (see acquire_sidebar and the coupling notes by the #defines below).
 * The window GType name and one CSS style class are the ONLY coupling to
 * Geary internals.
 *
 * A global GTK key snooper handles the toggle accelerator (focus-proof),
 * and a per-window "configure-event" re-applies the auto policy on
 * resize/move.
 *
 * Configuration (environment variables)
 * -------------------------------------
 *   GEARY_HIDE_SIDEBAR_MODE         auto | always | manual (default auto)
 *       auto    collapse on a portrait monitor, below WIDTH_RATIO of the
 *               monitor's usable width, or below the MIN_WIDTH floor
 *       always  start collapsed, ignore size
 *       manual  start expanded, only the keybinding changes it
 *   GEARY_HIDE_SIDEBAR_WIDTH_RATIO  fraction of monitor width below which
 *                                   auto collapses        (default 0.62)
 *   GEARY_HIDE_SIDEBAR_MIN_WIDTH    absolute px floor      (default 800)
 *   GEARY_HIDE_SIDEBAR_KEY          GTK accelerator string
 *                                   (default "<Control><Shift>m")
 *   GEARY_HIDE_SIDEBAR_DEBUG        1 to log to stderr
 *
 * A manual keypress overrides auto only within the current size class: your
 * choice holds through minor jitter, but once the window crosses the
 * collapse threshold (e.g. you maximize or tile it) auto takes over again.
 *
 * Build:  make
 * Run:    GTK_MODULES=$PWD/libgeary-hide-sidebar.so geary
 */

/* gtk_key_snooper_* is deprecated but still functional in GTK3 and is the
 * most reliable way to catch a global toggle key regardless of focus. */
#define GTK_DISABLE_DEPRECATION_WARNINGS
#include <gtk/gtk.h>
#include <gmodule.h>
#include <stdarg.h>
#include <stdlib.h>

/* ---- coupling to Geary internals: keep these in sync with the .ui ----
 *
 * Geary's main window is a GtkWidget composite template, so GtkBuilder does
 * NOT copy object ids into widget names. Instead we anchor on a CSS style
 * class that the .ui reliably applies, then navigate by structure:
 *
 *   folder_list_scrolled  has style class "geary-folder"   (ui line ~52)
 *     -> its parent is "folder_box": the "Mail" column we hide
 *        -> whose parent is the inner HdyLeaflet, which also holds
 *           "folder_separator" (a GtkSeparator) that we hide too.
 */
#define GEARY_MAIN_WINDOW_TYPE "ApplicationMainWindow"
#define GEARY_FOLDER_STYLE_CLASS "geary-folder"

/* ---- defaults (each overridable via the environment, see parse_config) ---- */
#define DEFAULT_MIN_WIDTH    800                 /* absolute px floor */
#define DEFAULT_WIDTH_RATIO  0.62                /* fraction of monitor width */
#define DEFAULT_ACCEL        "<Control><Shift>m" /* toggle keybinding */

/* A widget that has not yet been allocated a real size reports a width of
 * 0 or 1px; treat anything this small as "no allocation yet" and fall back
 * to another size source. */
#define UNALLOCATED_WIDTH 1

/* Key under which each main window stores its WinState (g_object_set_data). */
#define WIN_STATE_KEY "ghs-window"

/* ---- modes ---- */
typedef enum { MODE_AUTO, MODE_ALWAYS, MODE_MANUAL } SidebarMode;

/* ---- parsed configuration (read once at module init) ---- */
static SidebarMode g_mode        = MODE_AUTO;
static int         g_min_width   = DEFAULT_MIN_WIDTH;
static double      g_width_ratio = DEFAULT_WIDTH_RATIO;
static guint       g_accel_key   = 0;     /* GDK keyval */
static GdkModifierType g_accel_mods = 0;

/* ---- per-window state ---- */
typedef struct {
    GtkWidget *folder_box;
    GtkWidget *folder_separator;
    gboolean   collapsed;
    gboolean   user_override;     /* a keypress is currently overriding auto */
    gboolean   override_auto_want; /* what auto wanted when the user toggled;
                                    * the override is dropped once auto's
                                    * decision differs from this (i.e. the
                                    * window crossed a size class). */
} WinState;

/* ---------------------------------------------------------------- debug */

static gboolean debug_enabled(void) {
    const char *v = g_getenv("GEARY_HIDE_SIDEBAR_DEBUG");
    return v != NULL && *v != '\0' && g_strcmp0(v, "0") != 0;
}

static void dbg(const char *fmt, ...) {
    if (!debug_enabled())
        return;
    va_list ap;
    va_start(ap, fmt);
    char *msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    g_printerr("[geary-hide-sidebar] %s\n", msg);
    g_free(msg);
}

/* ------------------------------------------------------- widget helpers */

/* Depth-first search for a descendant carrying the given CSS style class. */
static GtkWidget *find_by_style_class(GtkWidget *root, const char *cls) {
    if (root == NULL)
        return NULL;
    GtkStyleContext *ctx = gtk_widget_get_style_context(root);
    if (ctx != NULL && gtk_style_context_has_class(ctx, cls))
        return root;

    if (!GTK_IS_CONTAINER(root))
        return NULL;

    GtkWidget *found = NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    for (GList *l = children; l != NULL && found == NULL; l = l->next)
        found = find_by_style_class(GTK_WIDGET(l->data), cls);
    g_list_free(children);
    return found;
}

/* First direct child of `parent` that is an instance of `type`, or NULL. */
static GtkWidget *find_child_of_type(GtkWidget *parent, GType type) {
    if (!GTK_IS_CONTAINER(parent))
        return NULL;

    GtkWidget *found = NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
    for (GList *l = children; l != NULL && found == NULL; l = l->next)
        if (G_TYPE_CHECK_INSTANCE_TYPE(l->data, type))
            found = GTK_WIDGET(l->data);
    g_list_free(children);
    return found;
}

/* Locate the "Mail" column box and its separator by structure.
 * Sets *box and *sep (either may be NULL if not found). */
static void acquire_sidebar(GtkWidget *top,
                            GtkWidget **box,
                            GtkWidget **sep) {
    *box = NULL;
    *sep = NULL;

    GtkWidget *anchor = find_by_style_class(top, GEARY_FOLDER_STYLE_CLASS);
    if (anchor == NULL) {
        dbg("style class '%s' not found - Geary layout may have changed",
            GEARY_FOLDER_STYLE_CLASS);
        return;
    }

    GtkWidget *folder_box = gtk_widget_get_parent(anchor);
    if (folder_box == NULL)
        return;
    *box = folder_box;

    GtkWidget *leaflet = gtk_widget_get_parent(folder_box);
    if (leaflet != NULL)
        *sep = find_child_of_type(leaflet, GTK_TYPE_SEPARATOR);

    dbg("found sidebar: box=%s sep=%s",
        G_OBJECT_TYPE_NAME(*box),
        *sep ? G_OBJECT_TYPE_NAME(*sep) : "(none)");
}

/* Show or hide one sidebar widget. HdyLeaflet manages child_visible
 * separately from the widget's own visibility, so set both — otherwise an
 * unfolded leaflet keeps allocating space to a hidden column. NULL-safe. */
static void set_widget_collapsed(GtkWidget *w, gboolean visible) {
    if (w == NULL)
        return;
    gtk_widget_set_visible(w, visible);
    gtk_widget_set_child_visible(w, visible);
}

static void apply_state(WinState *st) {
    gboolean visible = !st->collapsed;
    set_widget_collapsed(st->folder_box, visible);
    set_widget_collapsed(st->folder_separator, visible);

    if (st->folder_box != NULL) {
        GtkWidget *parent = gtk_widget_get_parent(st->folder_box);
        if (parent != NULL)
            gtk_widget_queue_resize(parent);

        dbg("sidebar %s (box visible=%d is_visible=%d child_visible=%d)",
            st->collapsed ? "collapsed" : "expanded",
            gtk_widget_get_visible(st->folder_box),
            gtk_widget_is_visible(st->folder_box),
            gtk_widget_get_child_visible(st->folder_box));
    }
}

/* Decide, from window size and the monitor it sits on, whether auto mode
 * wants the sidebar collapsed. Resolution-independent: we collapse when the
 * window is on a portrait monitor, or below an absolute pixel floor, or
 * occupies less than a configurable fraction of the monitor's usable width
 * (so a half-tiled window collapses while a maximized one expands). */
static gboolean want_collapsed_auto(GtkWidget *top, int width_hint) {
    /* Prefer the size carried by a configure-event: the widget's allocation
     * is not yet updated when that event fires. */
    int width = width_hint;
    if (width <= UNALLOCATED_WIDTH)
        width = gtk_widget_get_allocated_width(top);
    if (width <= UNALLOCATED_WIDTH)
        gtk_window_get_size(GTK_WINDOW(top), &width, NULL);

    gboolean narrow_abs = (width > 0 && width < g_min_width);

    gboolean portrait = FALSE;
    gboolean narrow_ratio = FALSE;
    int mon_w = 0;
    double ratio = 0.0;

    GdkWindow *gw = gtk_widget_get_window(top);
    if (gw != NULL) {
        GdkDisplay *display = gtk_widget_get_display(top);
        GdkMonitor *monitor =
            gdk_display_get_monitor_at_window(display, gw);
        if (monitor != NULL) {
            GdkRectangle geo, area;
            gdk_monitor_get_geometry(monitor, &geo);
            gdk_monitor_get_workarea(monitor, &area);
            portrait = (geo.height > geo.width);
            mon_w = (area.width > 0) ? area.width : geo.width;
            if (mon_w > 0 && width > 0) {
                ratio = (double) width / (double) mon_w;
                narrow_ratio = (ratio < g_width_ratio);
            }
        }
    }

    gboolean collapse = portrait || narrow_abs || narrow_ratio;
    dbg("auto check: width=%d mon_w=%d ratio=%.2f "
        "narrow_abs=%d narrow_ratio=%d portrait=%d -> %s",
        width, mon_w, ratio, narrow_abs, narrow_ratio, portrait,
        collapse ? "collapse" : "expand");
    return collapse;
}

/* --------------------------------------------------------- signal hooks */

static gboolean on_configure(GtkWidget *top,
                             GdkEventConfigure *event,
                             gpointer user_data) {
    WinState *st = (WinState *) user_data;
    if (g_mode != MODE_AUTO) {
        return GDK_EVENT_PROPAGATE;
    }

    gboolean want = want_collapsed_auto(top, event->width);

    if (st->user_override) {
        /* Honour the manual choice until the window crosses a size class
         * (auto's decision flips), then hand control back to auto. */
        if (want == st->override_auto_want) {
            return GDK_EVENT_PROPAGATE;
        }
        dbg("configure: size class changed, auto resumes");
        st->user_override = FALSE;
    }

    if (want != st->collapsed) {
        st->collapsed = want;
        apply_state(st);
    }
    return GDK_EVENT_PROPAGATE; /* let Geary/GTK keep handling it */
}

/* Re-hide the column if Geary (e.g. an HdyLeaflet relayout or a show_all)
 * shows it again while we want it collapsed. */
static void on_show_rehide(GtkWidget *widget, gpointer user_data) {
    WinState *st = (WinState *) user_data;
    if (st->collapsed) {
        dbg("re-hiding '%s' after a show", G_OBJECT_TYPE_NAME(widget));
        gtk_widget_hide(widget);
    }
}

/* Global key snooper: fires for every key press before normal delivery, so
 * it works no matter which widget has focus. */
static gint key_snooper(GtkWidget *grab_widget,
                        GdkEventKey *event,
                        gpointer data) {
    (void) data;
    if (g_accel_key == 0 || event->type != GDK_KEY_PRESS || grab_widget == NULL)
        return FALSE;

    GtkWidget *top = gtk_widget_get_toplevel(grab_widget);
    if (top == NULL ||
        g_strcmp0(G_OBJECT_TYPE_NAME(top), GEARY_MAIN_WINDOW_TYPE) != 0)
        return FALSE;

    WinState *st = g_object_get_data(G_OBJECT(top), WIN_STATE_KEY);
    if (st == NULL)
        return FALSE;

    GdkModifierType mask = gtk_accelerator_get_default_mod_mask();
    guint keyval = gdk_keyval_to_lower(event->keyval);
    if (keyval == g_accel_key && (event->state & mask) == g_accel_mods) {
        st->collapsed = !st->collapsed;
        /* Override auto only within the current size class; remember what
         * auto wants now so on_configure can tell when it has changed. */
        st->user_override = TRUE;
        st->override_auto_want = want_collapsed_auto(top, 0);
        apply_state(st);
        dbg("toggle via keybinding -> %s",
            st->collapsed ? "collapsed" : "expanded");
        return TRUE; /* consume the key */
    }
    return FALSE;
}

/* One-time setup the first time we see Geary's main window. */
static void setup_window(GtkWidget *top) {
    if (g_object_get_data(G_OBJECT(top), WIN_STATE_KEY) != NULL)
        return; /* already wired */

    WinState *st = g_new0(WinState, 1);
    acquire_sidebar(top, &st->folder_box, &st->folder_separator);

    switch (g_mode) {
        case MODE_ALWAYS: st->collapsed = TRUE;  break;
        case MODE_MANUAL: st->collapsed = FALSE; break;
        case MODE_AUTO:   st->collapsed = want_collapsed_auto(top, 0); break;
    }

    /* Free the state automatically when the window is destroyed. */
    g_object_set_data_full(G_OBJECT(top), WIN_STATE_KEY, st, g_free);

    /* Prevent a later show_all() from revealing the column, and re-hide if
     * Geary explicitly shows it again. */
    if (st->folder_box != NULL) {
        gtk_widget_set_no_show_all(st->folder_box, TRUE);
        g_signal_connect(st->folder_box, "show",
                         G_CALLBACK(on_show_rehide), st);
    }
    if (st->folder_separator != NULL) {
        gtk_widget_set_no_show_all(st->folder_separator, TRUE);
        g_signal_connect(st->folder_separator, "show",
                         G_CALLBACK(on_show_rehide), st);
    }

    g_signal_connect(top, "configure-event",
                     G_CALLBACK(on_configure), st);

    apply_state(st);
    dbg("main window wired (mode=%d, min_width=%d)", g_mode, g_min_width);
}

/* Emission hook on GtkWidget::map. params[0] is the widget being mapped. */
static gboolean on_widget_map(GSignalInvocationHint *ihint,
                              guint n_params,
                              const GValue *params,
                              gpointer user_data) {
    (void) ihint;
    (void) n_params;
    (void) user_data;

    GtkWidget *widget = GTK_WIDGET(g_value_get_object(&params[0]));
    GtkWidget *top = gtk_widget_get_toplevel(widget);
    if (top != NULL &&
        g_strcmp0(G_OBJECT_TYPE_NAME(top), GEARY_MAIN_WINDOW_TYPE) == 0) {
        setup_window(top);
    }
    return TRUE; /* keep the hook installed for future windows */
}

/* ------------------------------------------------------------- config */

static void parse_config(void) {
    const char *mode = g_getenv("GEARY_HIDE_SIDEBAR_MODE");
    if (mode != NULL) {
        if (g_strcmp0(mode, "always") == 0)      g_mode = MODE_ALWAYS;
        else if (g_strcmp0(mode, "manual") == 0) g_mode = MODE_MANUAL;
        else                                     g_mode = MODE_AUTO;
    }

    const char *mw = g_getenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH");
    if (mw != NULL && *mw != '\0') {
        int v = atoi(mw);
        if (v > 0)
            g_min_width = v;
    }

    const char *wr = g_getenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO");
    if (wr != NULL && *wr != '\0') {
        double v = g_ascii_strtod(wr, NULL); /* locale-independent */
        if (v > 0.0 && v <= 1.0)
            g_width_ratio = v;
    }

    const char *key = g_getenv("GEARY_HIDE_SIDEBAR_KEY");
    if (key == NULL || *key == '\0')
        key = DEFAULT_ACCEL;
    gtk_accelerator_parse(key, &g_accel_key, &g_accel_mods);
    if (g_accel_key != 0) {
        g_accel_key = gdk_keyval_to_lower(g_accel_key);
        dbg("keybinding = '%s' (keyval=0x%x mods=0x%x)",
            key, g_accel_key, g_accel_mods);
    } else {
        dbg("could not parse key '%s'; keybinding disabled", key);
    }
}

/* ------------------------------------------------------ module entry */

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv) {
    (void) argc;
    (void) argv;

    parse_config();

    /* gtk_module_init() runs during gtk_init(), before any widget has been
     * created. A class's signals aren't registered until its class_init has
     * run, so force GtkWidget's class to initialize before we look up its
     * "map" signal (kept ref'd for the process lifetime). */
    g_type_class_ref(GTK_TYPE_WIDGET);

    guint map_signal = g_signal_lookup("map", GTK_TYPE_WIDGET);
    if (map_signal == 0) {
        dbg("could not look up GtkWidget::map signal");
        return;
    }
    g_signal_add_emission_hook(map_signal, 0, on_widget_map, NULL, NULL);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_key_snooper_install(key_snooper, NULL);
#pragma GCC diagnostic pop

    dbg("module loaded; watching for '%s'", GEARY_MAIN_WINDOW_TYPE);
}

G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module) {
    (void) module;
    return NULL;
}
