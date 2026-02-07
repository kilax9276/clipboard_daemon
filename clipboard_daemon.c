
//copyright "Kilax @kilax9276"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* =========================================================
 * CLIPBOARD DATA TYPES
 * ========================================================= */

typedef enum {
    CLIP_TYPE_FILE,
    CLIP_TYPE_TEXT
} ClipboardType;

typedef struct {
    ClipboardType type;

    /* FILE / IMAGE */
    gchar  *uri;
    gchar  *gnome;
    GBytes *image;

    /* TEXT */
    gchar *text;
} ClipboardData;

/* =========================================================
 * GLOBALS
 * ========================================================= */

static GtkClipboard *cb_clipboard = NULL; /* CLIPBOARD */
static GtkClipboard *cb_primary   = NULL; /* PRIMARY */

/* --- multiline text state --- */
static gboolean text_mode = FALSE;
static GString *text_buffer = NULL;

/* =========================================================
 * HELPERS
 * ========================================================= */

static void clipboard_flush(void)
{
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);

    GdkDisplay *dpy = gdk_display_get_default();
    if (dpy)
        gdk_display_flush(dpy);
}

/* =========================================================
 * CLIPBOARD CALLBACKS
 * ========================================================= */

static void clipboard_get_cb(GtkClipboard *cb,
                             GtkSelectionData *sd,
                             guint info,
                             gpointer user_data)
{
    ClipboardData *d = (ClipboardData *)user_data;

    /* ===== TEXT MODE ===== */
    if (d->type == CLIP_TYPE_TEXT) {
        GdkAtom target = gtk_selection_data_get_target(sd);

        GdkAtom a_targets = gdk_atom_intern_static_string("TARGETS");
        GdkAtom a_atom    = gdk_atom_intern_static_string("ATOM");

        GdkAtom a_utf8    = gdk_atom_intern_static_string("UTF8_STRING");
        GdkAtom a_string  = gdk_atom_intern_static_string("STRING");
        GdkAtom a_text    = gdk_atom_intern_static_string("TEXT");

        GdkAtom a_plain   = gdk_atom_intern_static_string("text/plain");
        GdkAtom a_plain_u = gdk_atom_intern_static_string("text/plain;charset=utf-8");

        const char *txt = d->text ? d->text : "";
        gsize n = strlen(txt);

        /* 1) Браузеры часто сначала запрашивают TARGETS */
        if (target == a_targets) {
            /* список поддерживаемых targets */
            GdkAtom supported[] = {
                a_targets,
                a_utf8,
                a_plain_u,
                a_plain,
                a_string,
                a_text
            };
            gtk_selection_data_set(sd, a_atom, 32,
                                   (const guchar *)supported,
                                   (gint)(sizeof(supported)));
            return;
        }

        /* 2) Текстовые MIME targets: отдаём UTF-8 bytes, type ставим UTF8_STRING */
        if (target == a_plain || target == a_plain_u || target == a_utf8) {
            gtk_selection_data_set(sd, a_utf8, 8, (const guchar *)txt, (gint)n);
            return;
        }

        /* 3) STRING/TEXT: часто ожидают NUL-терминатор */
        if (target == a_string || target == a_text) {
            guchar *buf = g_malloc(n + 1);
            memcpy(buf, txt, n);
            buf[n] = '\0';
            gtk_selection_data_set(sd, target, 8, buf, (gint)(n + 1));
            g_free(buf);
            return;
        }

        /* 4) Fallback */
        gtk_selection_data_set(sd, a_utf8, 8, (const guchar *)txt, (gint)n);
        return;
    }

    /* ===== FILE / IMAGE MODE ===== */
    switch (info) {
    case 0: /* x-special/gnome-copied-files */
        gtk_selection_data_set(
            sd,
            gdk_atom_intern_static_string("x-special/gnome-copied-files"),
            8,
            (const guchar *)d->gnome,
            (gint)strlen(d->gnome)
        );
        break;

    case 1: /* text/uri-list */
        gtk_selection_data_set(
            sd,
            gdk_atom_intern_static_string("text/uri-list"),
            8,
            (const guchar *)d->uri,
            (gint)strlen(d->uri)
        );
        break;

    case 2: /* image/png */
        if (!d->image)
            return;

        gsize size = 0;
        const guchar *buf = g_bytes_get_data(d->image, &size);

        gtk_selection_data_set(
            sd,
            gdk_atom_intern_static_string("image/png"),
            8,
            buf,
            (gint)size
        );
        break;
    }
}

static void clipboard_clear_cb(GtkClipboard *cb, gpointer user_data)
{
    ClipboardData *d = (ClipboardData *)user_data;

    g_free(d->uri);
    g_free(d->gnome);
    g_free(d->text);

    if (d->image)
        g_bytes_unref(d->image);

    g_free(d);
}

/* =========================================================
 * CLIPBOARD SETTERS
 * ========================================================= */

static ClipboardData *clipboard_data_new_file(const char *path)
{
    ClipboardData *d = g_new0(ClipboardData, 1);
    d->type = CLIP_TYPE_FILE;

    d->uri   = g_strdup_printf("file://%s\n", path);
    d->gnome = g_strdup_printf("copy\nfile://%s\n", path);

    guchar *buf = NULL;
    gsize size = 0;
    if (g_file_get_contents(path, (gchar **)&buf, &size, NULL)) {
        d->image = g_bytes_new_take(buf, size);
    }

    return d;
}

static void set_clipboard_from_file(const char *path)
{
    static GtkTargetEntry targets[] = {
        { "x-special/gnome-copied-files", 0, 0 },
        { "text/uri-list",                0, 1 },
        { "image/png",                    0, 2 }
    };

    gtk_clipboard_clear(cb_clipboard);
    gtk_clipboard_clear(cb_primary);

    ClipboardData *d_clip = clipboard_data_new_file(path);
    ClipboardData *d_prim = clipboard_data_new_file(path);

    gtk_clipboard_set_with_data(
        cb_clipboard,
        targets, G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_clip
    );

    gtk_clipboard_set_with_data(
        cb_primary,
        targets, G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_prim
    );

    clipboard_flush();
    g_message("FILE clipboard updated: %s", path);
}

static void set_clipboard_text(const char *text)
{
    static GtkTargetEntry targets[] = {
        { "TARGETS",                  0, 100 },
        { "UTF8_STRING",              0, 0 },
        { "text/plain;charset=utf-8", 0, 1 },
        { "text/plain",               0, 2 },
        { "STRING",                   0, 3 },
        { "TEXT",                     0, 4 }
    };

    gtk_clipboard_clear(cb_clipboard);
    gtk_clipboard_clear(cb_primary);

    ClipboardData *d_clip = g_new0(ClipboardData, 1);
    ClipboardData *d_prim = g_new0(ClipboardData, 1);

    d_clip->type = CLIP_TYPE_TEXT;
    d_prim->type = CLIP_TYPE_TEXT;

    d_clip->text = g_strdup(text ? text : "");
    d_prim->text = g_strdup(text ? text : "");

    gtk_clipboard_set_with_data(
        cb_clipboard,
        targets, G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_clip
    );

    gtk_clipboard_set_with_data(
        cb_primary,
        targets, G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_prim
    );

    clipboard_flush();
    g_message("TEXT clipboard updated (browser-compatible targets)");
}

/* =========================================================
 * CONTROL SOCKET
 * ========================================================= */

static gboolean on_client_io(GIOChannel *ch,
                             GIOCondition cond,
                             gpointer user_data)
{
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        g_io_channel_shutdown(ch, TRUE, NULL);
        g_io_channel_unref(ch);
        return FALSE;
    }

    gchar *line = NULL;
    gsize len = 0;

    if (g_io_channel_read_line(ch, &line, &len, NULL, NULL)
        == G_IO_STATUS_NORMAL) {

        g_strchomp(line);

        /* ===== MULTILINE TEXT MODE ===== */
        if (text_mode) {
            if (g_strcmp0(line, "text-end") == 0) {

                /* убрать финальный '\n' */
                if (text_buffer && text_buffer->len > 0 &&
                    text_buffer->str[text_buffer->len - 1] == '\n') {
                    g_string_truncate(text_buffer, text_buffer->len - 1);
                }

                set_clipboard_text(text_buffer ? text_buffer->str : "");

                g_string_free(text_buffer, TRUE);
                text_buffer = NULL;
                text_mode = FALSE;

            } else {
                g_string_append(text_buffer, line);
                g_string_append_c(text_buffer, '\n');
            }
        }
        else if (g_strcmp0(line, "text-begin") == 0) {
            text_mode = TRUE;
            text_buffer = g_string_new(NULL);
        }
        else if (g_str_has_prefix(line, "copy ")) {
            set_clipboard_from_file(line + 5);
        }
        else if (g_str_has_prefix(line, "text ")) {
            set_clipboard_text(line + 5);
        }
    }

    g_free(line);

    if (text_mode)
        return TRUE;

    g_io_channel_shutdown(ch, TRUE, NULL);
    g_io_channel_unref(ch);
    return FALSE;
}

static gboolean on_accept(GSocket *listener,
                          GIOCondition cond,
                          gpointer user_data)
{
    GSocket *client = g_socket_accept(listener, NULL, NULL);
    if (!client)
        return TRUE;

    GIOChannel *ch = g_io_channel_unix_new(g_socket_get_fd(client));
    g_io_channel_set_close_on_unref(ch, TRUE);

    g_io_add_watch(
        ch,
        G_IO_IN | G_IO_HUP | G_IO_ERR,
        on_client_io,
        NULL
    );

    return TRUE;
}

static void start_control_socket(void)
{
    const char *sock = "/run/gtk-clipboard-daemon.sock";
    unlink(sock);

    GSocket *listener = g_socket_new(
        G_SOCKET_FAMILY_UNIX,
        G_SOCKET_TYPE_STREAM,
        0,
        NULL
    );

    g_socket_bind(
        listener,
        g_unix_socket_address_new(sock),
        TRUE,
        NULL
    );

    g_socket_listen(listener, NULL);

    GSource *src = g_socket_create_source(listener, G_IO_IN, NULL);
    g_source_set_callback(src, (GSourceFunc)on_accept, NULL, NULL);
    g_source_attach(src, NULL);

    g_message("Control socket listening on %s", sock);
}

/* =========================================================
 * MAIN
 * ========================================================= */

int main(int argc, char **argv)
{
    setenv("GDK_BACKEND", "x11", 1);
    setenv("DISPLAY", ":3", 1);

    gtk_init(&argc, &argv);

    cb_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    cb_primary   = gtk_clipboard_get(GDK_SELECTION_PRIMARY);

    start_control_socket();

    g_message("GTK clipboard daemon (FILE + IMAGE + TEXT + MULTILINE) started");

    gtk_main();
    return 0;
}
