/*
 * Unit tests for geary-hide-sidebar.
 *
 * We #include the module's .c so the tests can reach its static functions
 * and configuration globals directly. The tests need a GDK display (most of
 * GTK refuses to run without one); run them headless via `make test`, which
 * uses xvfb-run when available. With no display the binary exits 77, the
 * automake convention for "skipped".
 *
 *   make test
 */
#include <gtk/gtk.h>

#include "geary-hide-sidebar.c"

/* ----------------------------------------------------------- fixtures */

/* Reset the config globals to their compiled defaults and clear every
 * env var parse_config() reads, so each test starts from a known state. */
static void reset_config(void) {
    g_mode        = MODE_AUTO;
    g_min_width   = DEFAULT_MIN_WIDTH;
    g_width_ratio = DEFAULT_WIDTH_RATIO;
    g_accel_key   = 0;
    g_accel_mods  = 0;
    g_unsetenv("GEARY_HIDE_SIDEBAR_MODE");
    g_unsetenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH");
    g_unsetenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO");
    g_unsetenv("GEARY_HIDE_SIDEBAR_KEY");
}

/* Build a small widget tree mirroring Geary's real structure:
 *
 *     window
 *       outer (GtkBox, stands in for the inner HdyLeaflet)
 *         folder_box (GtkBox)         <- the column we hide
 *           scrolled (GtkScrolledWindow, style class "geary-folder")
 *         sep (GtkSeparator)          <- the divider we hide too
 *
 * Returns the toplevel window (floating ref sunk by the caller via
 * gtk_widget_destroy). Out-params expose the interesting nodes.
 */
static GtkWidget *build_geary_like_tree(GtkWidget **out_folder_box,
                                        GtkWidget **out_sep,
                                        GtkWidget **out_scrolled) {
    GtkWidget *window     = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *outer      = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *folder_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scrolled   = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *sep        = gtk_separator_new(GTK_ORIENTATION_VERTICAL);

    gtk_style_context_add_class(gtk_widget_get_style_context(scrolled),
                                GEARY_FOLDER_STYLE_CLASS);

    gtk_container_add(GTK_CONTAINER(folder_box), scrolled);
    gtk_container_add(GTK_CONTAINER(outer), folder_box);
    gtk_container_add(GTK_CONTAINER(outer), sep);
    gtk_container_add(GTK_CONTAINER(window), outer);

    if (out_folder_box) *out_folder_box = folder_box;
    if (out_sep)        *out_sep        = sep;
    if (out_scrolled)   *out_scrolled   = scrolled;
    return window;
}

/* ------------------------------------------------------- parse_config */

static void test_config_defaults(void) {
    reset_config();
    parse_config();
    g_assert_cmpint(g_mode, ==, MODE_AUTO);
    g_assert_cmpint(g_min_width, ==, DEFAULT_MIN_WIDTH);
    g_assert_cmpfloat(g_width_ratio, ==, DEFAULT_WIDTH_RATIO);
    /* default accelerator: <Control><Shift>m */
    g_assert_cmpuint(g_accel_key, ==, gdk_keyval_to_lower(GDK_KEY_m));
    g_assert_cmpuint(g_accel_mods & GDK_CONTROL_MASK, ==, GDK_CONTROL_MASK);
    g_assert_cmpuint(g_accel_mods & GDK_SHIFT_MASK,   ==, GDK_SHIFT_MASK);
}

static void test_config_modes(void) {
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MODE", "always", TRUE);
    parse_config();
    g_assert_cmpint(g_mode, ==, MODE_ALWAYS);

    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MODE", "manual", TRUE);
    parse_config();
    g_assert_cmpint(g_mode, ==, MODE_MANUAL);

    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MODE", "auto", TRUE);
    parse_config();
    g_assert_cmpint(g_mode, ==, MODE_AUTO);

    /* unrecognised value falls back to auto */
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MODE", "wibble", TRUE);
    parse_config();
    g_assert_cmpint(g_mode, ==, MODE_AUTO);
}

static void test_config_min_width(void) {
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH", "1024", TRUE);
    parse_config();
    g_assert_cmpint(g_min_width, ==, 1024);

    /* non-positive and garbage values are rejected, default kept */
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH", "0", TRUE);
    parse_config();
    g_assert_cmpint(g_min_width, ==, DEFAULT_MIN_WIDTH);

    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH", "-50", TRUE);
    parse_config();
    g_assert_cmpint(g_min_width, ==, DEFAULT_MIN_WIDTH);

    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_MIN_WIDTH", "notanumber", TRUE);
    parse_config();
    g_assert_cmpint(g_min_width, ==, DEFAULT_MIN_WIDTH);
}

static void test_config_width_ratio(void) {
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO", "0.75", TRUE);
    parse_config();
    g_assert_cmpfloat(g_width_ratio, ==, 0.75);

    /* out-of-range (>1, <=0) rejected */
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO", "1.5", TRUE);
    parse_config();
    g_assert_cmpfloat(g_width_ratio, ==, DEFAULT_WIDTH_RATIO);

    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO", "0", TRUE);
    parse_config();
    g_assert_cmpfloat(g_width_ratio, ==, DEFAULT_WIDTH_RATIO);

    /* parsing is locale-independent: a comma is NOT a decimal point, so
     * "0,5" parses as 0.0 and is rejected. */
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_WIDTH_RATIO", "0,5", TRUE);
    parse_config();
    g_assert_cmpfloat(g_width_ratio, ==, DEFAULT_WIDTH_RATIO);
}

static void test_config_key(void) {
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_KEY", "F9", TRUE);
    parse_config();
    g_assert_cmpuint(g_accel_key, ==, GDK_KEY_F9);
    g_assert_cmpuint(g_accel_mods, ==, 0);

    /* an unparseable accelerator leaves the keybinding disabled (key 0) */
    reset_config();
    g_setenv("GEARY_HIDE_SIDEBAR_KEY", "not a valid accel @#", TRUE);
    parse_config();
    g_assert_cmpuint(g_accel_key, ==, 0);
}

/* --------------------------------------------------- widget helpers */

static void test_find_by_style_class(void) {
    GtkWidget *window, *scrolled;
    window = build_geary_like_tree(NULL, NULL, &scrolled);

    g_assert_true(find_by_style_class(window, GEARY_FOLDER_STYLE_CLASS)
                  == scrolled);
    g_assert_null(find_by_style_class(window, "no-such-class"));
    g_assert_null(find_by_style_class(NULL, GEARY_FOLDER_STYLE_CLASS));

    gtk_widget_destroy(window);
}

static void test_find_child_of_type(void) {
    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);
    GtkWidget *outer = gtk_widget_get_parent(folder_box);

    g_assert_true(find_child_of_type(outer, GTK_TYPE_SEPARATOR) == sep);
    /* folder_box holds a scrolled window, not a separator */
    g_assert_null(find_child_of_type(folder_box, GTK_TYPE_SEPARATOR));
    /* a non-container yields NULL rather than crashing */
    g_assert_null(find_child_of_type(sep, GTK_TYPE_SEPARATOR));

    gtk_widget_destroy(window);
}

static void test_acquire_sidebar(void) {
    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);

    GtkWidget *box = NULL, *found_sep = NULL;
    acquire_sidebar(window, &box, &found_sep);
    g_assert_true(box == folder_box);
    g_assert_true(found_sep == sep);

    gtk_widget_destroy(window);
}

static void test_acquire_sidebar_missing(void) {
    /* a tree without the anchor style class: both out-params stay NULL */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(window),
                      gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));

    GtkWidget *box = (GtkWidget *) 0x1, *sep = (GtkWidget *) 0x1;
    acquire_sidebar(window, &box, &sep);
    g_assert_null(box);
    g_assert_null(sep);

    gtk_widget_destroy(window);
}

/* ----------------------------------------------------- apply_state */

static void test_apply_state_toggles_visibility(void) {
    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);

    WinState st = { .folder_box = folder_box, .folder_separator = sep };

    st.collapsed = TRUE;
    apply_state(&st);
    g_assert_false(gtk_widget_get_visible(folder_box));
    g_assert_false(gtk_widget_get_child_visible(folder_box));
    g_assert_false(gtk_widget_get_visible(sep));
    g_assert_false(gtk_widget_get_child_visible(sep));

    st.collapsed = FALSE;
    apply_state(&st);
    g_assert_true(gtk_widget_get_visible(folder_box));
    g_assert_true(gtk_widget_get_child_visible(folder_box));
    g_assert_true(gtk_widget_get_visible(sep));
    g_assert_true(gtk_widget_get_child_visible(sep));

    gtk_widget_destroy(window);
}

static void test_apply_state_null_safe(void) {
    /* acquire failed: both widgets NULL. Must not crash. */
    WinState st = { 0 };
    st.collapsed = TRUE;
    apply_state(&st);
    set_widget_collapsed(NULL, TRUE); /* explicit NULL-safety check */
    g_assert_true(TRUE);
}

/* ------------------------------------------------ want_collapsed_auto */

static void test_want_collapsed_auto_narrow(void) {
    reset_config();
    g_min_width = 800;
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    /* unrealized window has no GdkWindow, so only the absolute-floor rule
     * applies; below the floor -> collapse, above -> expand. */
    g_assert_true(want_collapsed_auto(window, 500));
    g_assert_false(want_collapsed_auto(window, 3000));

    gtk_widget_destroy(window);
}

/* ------------------------------------------------------ on_configure */

/* on_configure drives auto mode off the size carried by the event. We
 * fabricate the event rather than pumping the real GDK loop. */
static GdkEventConfigure make_configure(int width) {
    GdkEventConfigure ev;
    memset(&ev, 0, sizeof ev);
    ev.type   = GDK_CONFIGURE;
    ev.width  = width;
    ev.height = 600;
    return ev;
}

static void test_on_configure_auto_collapses(void) {
    reset_config();
    g_mode = MODE_AUTO;
    g_min_width = 800;

    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);
    WinState st = { .folder_box = folder_box, .folder_separator = sep,
                    .collapsed = FALSE };

    GdkEventConfigure narrow = make_configure(500);
    on_configure(window, &narrow, &st);
    g_assert_true(st.collapsed);
    g_assert_false(gtk_widget_get_visible(folder_box));

    GdkEventConfigure wide = make_configure(3000);
    on_configure(window, &wide, &st);
    g_assert_false(st.collapsed);
    g_assert_true(gtk_widget_get_visible(folder_box));

    gtk_widget_destroy(window);
}

static void test_on_configure_respects_mode(void) {
    /* outside MODE_AUTO, configure events change nothing */
    reset_config();
    g_mode = MODE_MANUAL;

    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);
    WinState st = { .folder_box = folder_box, .folder_separator = sep,
                    .collapsed = FALSE };

    GdkEventConfigure narrow = make_configure(100);
    on_configure(window, &narrow, &st);
    g_assert_false(st.collapsed); /* untouched */

    gtk_widget_destroy(window);
}

static void test_on_configure_user_override(void) {
    reset_config();
    g_mode = MODE_AUTO;
    g_min_width = 800;

    GtkWidget *window, *folder_box, *sep;
    window = build_geary_like_tree(&folder_box, &sep, NULL);

    /* User toggled to expanded while auto also wanted expanded (a wide
     * window). The override holds as long as the size class is unchanged. */
    WinState st = { .folder_box = folder_box, .folder_separator = sep,
                    .collapsed = FALSE, .user_override = TRUE,
                    .override_auto_want = FALSE };

    GdkEventConfigure stillwide = make_configure(3000);
    on_configure(window, &stillwide, &st);
    g_assert_true(st.user_override);  /* still honoured */
    g_assert_false(st.collapsed);

    /* Crossing into the narrow size class flips what auto wants, which
     * drops the override and hands control back to auto. */
    GdkEventConfigure narrow = make_configure(500);
    on_configure(window, &narrow, &st);
    g_assert_false(st.user_override); /* dropped */
    g_assert_true(st.collapsed);      /* auto took over */

    gtk_widget_destroy(window);
}

/* --------------------------------------------------------------- main */

int main(int argc, char **argv) {
    /* Most of these tests need a display. Skip cleanly (automake code 77)
     * when none is available instead of aborting. */
    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("no display available; skipping GTK tests\n");
        return 77;
    }
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/config/defaults",     test_config_defaults);
    g_test_add_func("/config/modes",        test_config_modes);
    g_test_add_func("/config/min_width",    test_config_min_width);
    g_test_add_func("/config/width_ratio",  test_config_width_ratio);
    g_test_add_func("/config/key",          test_config_key);

    g_test_add_func("/widget/find_by_style_class", test_find_by_style_class);
    g_test_add_func("/widget/find_child_of_type",  test_find_child_of_type);
    g_test_add_func("/widget/acquire_sidebar",     test_acquire_sidebar);
    g_test_add_func("/widget/acquire_sidebar_missing",
                    test_acquire_sidebar_missing);

    g_test_add_func("/state/toggles_visibility",
                    test_apply_state_toggles_visibility);
    g_test_add_func("/state/null_safe", test_apply_state_null_safe);

    g_test_add_func("/auto/want_collapsed_narrow",
                    test_want_collapsed_auto_narrow);

    g_test_add_func("/configure/auto_collapses",
                    test_on_configure_auto_collapses);
    g_test_add_func("/configure/respects_mode",
                    test_on_configure_respects_mode);
    g_test_add_func("/configure/user_override",
                    test_on_configure_user_override);

    return g_test_run();
}
