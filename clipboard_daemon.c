#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>

/* =========================================================
 * CLIPBOARD DATA (GTK OWNS LIFETIME)
 * ========================================================= */

typedef struct {
    gchar  *uri;
    gchar  *gnome;
    GBytes *image;
} ClipboardData;

/* =========================================================
 * GLOBALS
 * ========================================================= */

static GtkClipboard *cb_clipboard = NULL; /* CLIPBOARD */
static GtkClipboard *cb_primary   = NULL; /* PRIMARY */

/* =========================================================
 * CLIPBOARD CALLBACKS
 * ========================================================= */

static void clipboard_get_cb(GtkClipboard *cb,
                             GtkSelectionData *sd,
                             guint info,
                             gpointer user_data)
{
    ClipboardData *d = (ClipboardData *)user_data;

    switch (info) {

    case 0: /* x-special/gnome-copied-files */
        gtk_selection_data_set(
            sd,
            gdk_atom_intern_static_string("x-special/gnome-copied-files"),
            8,
            (const guchar *)d->gnome,
            strlen(d->gnome)
        );
        break;

    case 1: /* text/uri-list */
        gtk_selection_data_set(
            sd,
            gdk_atom_intern_static_string("text/uri-list"),
            8,
            (const guchar *)d->uri,
            strlen(d->uri)
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
            size
        );
        break;
    }
}

static void clipboard_clear_cb(GtkClipboard *cb, gpointer user_data)
{
    ClipboardData *d = (ClipboardData *)user_data;

    g_free(d->uri);
    g_free(d->gnome);
    if (d->image)
        g_bytes_unref(d->image);

    g_free(d);
}

static ClipboardData *clipboard_data_new(const char *path)
{
    ClipboardData *d = g_new0(ClipboardData, 1);

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

    /* ❗ ДВА РАЗНЫХ ОБЪЕКТА */
    ClipboardData *d_clipboard = clipboard_data_new(path);
    ClipboardData *d_primary   = clipboard_data_new(path);

    gtk_clipboard_set_with_data(
        cb_clipboard,
        targets,
        G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_clipboard
    );

    gtk_clipboard_set_with_data(
        cb_primary,
        targets,
        G_N_ELEMENTS(targets),
        clipboard_get_cb,
        clipboard_clear_cb,
        d_primary
    );

    gtk_clipboard_store(cb_clipboard);
    gtk_clipboard_store(cb_primary);

    g_message("Clipboard (CLIPBOARD + PRIMARY) updated: %s", path);
}


/* =========================================================
 * CONTROL SOCKET (MULTI-COMMAND)
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

        g_strstrip(line);

        if (g_str_has_prefix(line, "copy ")) {
            set_clipboard_from_file(line + 5);
        }
    }

    g_free(line);

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
    setenv("DISPLAY", ":3", 1);

    gtk_init(&argc, &argv);

    cb_clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    cb_primary   = gtk_clipboard_get(GDK_SELECTION_PRIMARY);

    start_control_socket();

    g_message("GTK clipboard daemon (CLIPBOARD + PRIMARY) started");

    gtk_main();
    return 0;
}
