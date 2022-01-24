// dragon - very lightweight DnD file source/target
// Copyright 2014 Michael Homer.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define VERSION "1.1.2"

GtkWidget *window;
GtkWidget *vbox;
GtkIconTheme *icon_theme;

char *progname;
bool verbose = false;
int mode = 0;
int thumb_size = 96;
bool and_exit = false;
bool keep = false;
bool print_path = false;
bool icons_only = false;
bool always_on_top = false;

bool center = false;
bool center_screen = false;
bool move = false;
bool resize = false;
bool fit = false;
int x = 0, y = 0,
    w = 0, h = 0;   

static char *stdin_files;

#define MODE_HELP 1
#define MODE_TARGET 2
#define MODE_VERSION 4

#define TARGET_TYPE_TEXT 1
#define TARGET_TYPE_URI 2

struct draggable_thing {
    char *text;
    char *uri;
};

// MODE_ALL
#define MAX_SIZE 100
char** uri_collection;
int uri_count = 0;
bool drag_all = false;
// ---

#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

static void sigintHandler(int sig) {gtk_main_quit(); exit(EXIT_SUCCESS);};

bool can_run_cmd(const char *cmd) {
    if (strchr(cmd, '/')) {
        // if cmd includes a slash, no path search must be performed,
        // go straight to checking if it's executable
        return (access(cmd, X_OK) == 0);
    }
    const char *path = getenv("PATH");
    if (!path) {
        return false;
    }
    char *buf = malloc(strlen(path)+strlen(cmd)+3);
    if (!buf) {
        return false;
    }
    // loop as long as we have stuff to examine in path
    for (; *path; ++path) {
        // start from the beginning of the buffer
        char *p = buf;
        // copy in buf the current path element
        for (; *path && *path != ':'; ++path, ++p) {
            *p = *path;
        }
        // empty path entries are treated like "."
        if (p == buf) {
            *p++ = '.';
        }
        // slash and command name
        if (p[-1] != '/') {
            *p++ = '/';
        }
        strcpy(p, cmd);
        // check if we can execute it
        if (access(buf, X_OK) == 0) {
            free(buf);
            return true;
        }
        // quit at last cycle
        if (!*path) {
            break;
        }
    }
    // not found
    free(buf);
    return false;
}

void add_target_button();

void do_quit(GtkWidget *widget, gpointer data) {
    exit(0);
}

void button_clicked(GtkWidget *widget, gpointer user_data) {
    struct draggable_thing *dd = (struct draggable_thing *)user_data;
    if (0 == fork()) {
        execlp("xdg-open", "xdg-open", dd->uri, NULL);
    }
}

void drag_data_get(GtkWidget    *widget,
               GdkDragContext   *context,
               GtkSelectionData *data,
               guint             info,
               guint             time,
               gpointer          user_data) {
    struct draggable_thing *dd = (struct draggable_thing *)user_data;
    if (info == TARGET_TYPE_URI) {

        char** uris;
        char* single_uri_data[2] = {dd->uri, NULL};
        if (drag_all) {
            uri_collection[uri_count] = NULL;
            uris = uri_collection;
        } else {
            uris = single_uri_data;
        }
        if (verbose) {
            if (drag_all)
                fputs("Sending all as URI\n", stderr);
            else
                fprintf(stderr, "Sending as URI: %s\n", dd->uri);
        }

        gtk_selection_data_set_uris(data, uris);
        g_signal_stop_emission_by_name(widget, "drag-data-get");
    } else if (info == TARGET_TYPE_TEXT) {
        if (verbose)
            fprintf(stderr, "Sending as TEXT: %s\n", dd->text);
        gtk_selection_data_set_text(data, dd->text, -1);
    } else {
        fprintf(stderr, "Error: bad target type %i\n", info);
    }
}

void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data) {
    if (verbose) {
        gboolean succeeded = gdk_drag_drop_succeeded(context);
        GdkDragAction action = gdk_drag_context_get_selected_action (context);
        char* action_str;
        switch (action) {
            case GDK_ACTION_COPY:
                action_str = "COPY"; break;
            case GDK_ACTION_MOVE:
                action_str = "MOVE"; break;
            case GDK_ACTION_LINK:
                action_str = "LINK"; break;
            case GDK_ACTION_ASK:
                action_str = "ASK"; break;
            default:
                action_str = malloc(sizeof(char) * 20);
                snprintf(action_str, 20, "invalid (%d)", action);
                break;
        }
        fprintf(stderr, "Selected drop action: %s; Succeeded: %d\n", action_str, succeeded);
        if (action_str[0] == 'i')
            free(action_str);
    }
    if (and_exit)
        gtk_main_quit();
}

GtkButton *add_button(char *label, struct draggable_thing *dragdata, int type) {
    GtkWidget *button;

    if (icons_only) {
        button = gtk_button_new();
    } else {
        button = gtk_button_new_with_label(label);
    }

    GtkTargetList *targetlist = gtk_drag_source_get_target_list(GTK_WIDGET(button));
    if (targetlist)
        gtk_target_list_ref(targetlist);
    else
        targetlist = gtk_target_list_new(NULL, 0);
    if (type == TARGET_TYPE_URI)
        gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
    else
        gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);

    gtk_drag_source_set(GTK_WIDGET(button), GDK_BUTTON1_MASK, NULL, 0,
            GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
    gtk_drag_source_set_target_list(GTK_WIDGET(button), targetlist);
    g_signal_connect(GTK_WIDGET(button), "drag-data-get",
            G_CALLBACK(drag_data_get), dragdata);
    g_signal_connect(GTK_WIDGET(button), "clicked",
            G_CALLBACK(button_clicked), dragdata);
    g_signal_connect(GTK_WIDGET(button), "drag-end",
            G_CALLBACK(drag_end), dragdata);

    gtk_container_add(GTK_CONTAINER(vbox), button);

    if (drag_all) {
        if (uri_count < MAX_SIZE) {
            uri_collection[uri_count] = dragdata->uri;
        } else {
            fprintf(stderr, "Exceeded maximum number of files for drag_all (%d)\n", MAX_SIZE);
        }
    }
    uri_count++;

    return (GtkButton *)button;
}

void left_align_button(GtkButton *button) {
    GList *child = g_list_first(
            gtk_container_get_children(GTK_CONTAINER(button)));
    if (child)
        gtk_widget_set_halign(GTK_WIDGET(child->data), GTK_ALIGN_START);
}

GtkIconInfo* icon_info_from_content_type(char *content_type) {
    GIcon *icon = g_content_type_get_icon(content_type);
    return gtk_icon_theme_lookup_by_gicon(icon_theme, icon, 48, 0);
}

void add_file_button(GFile *file) {
    char *filename = g_file_get_path(file);
    if(!g_file_query_exists(file, NULL)) {
        fprintf(stderr, "The file `%s' does not exist.\n",
                filename);
        exit(1);
    }
    char *uri = g_file_get_uri(file);
    struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
    dragdata->text = filename;
    dragdata->uri = uri;

    GtkButton *button = add_button(filename, dragdata, TARGET_TYPE_URI);
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(filename, thumb_size, thumb_size, NULL);
    if (pb) {
        GtkWidget *image = gtk_image_new_from_pixbuf(pb);
        gtk_button_set_always_show_image(button, true);
        gtk_button_set_image(button, image);
        gtk_button_set_always_show_image(button, true);
    } else {
        GFileInfo *fileinfo = g_file_query_info(file, "*", 0, NULL, NULL);
        GIcon *icon = g_file_info_get_icon(fileinfo);
        GtkIconInfo *icon_info = gtk_icon_theme_lookup_by_gicon(icon_theme,
                icon, 48, 0);

        // Try a few fallback mimetypes if no icon can be found
        if (!icon_info)
            icon_info = icon_info_from_content_type("application/octet-stream");
        if (!icon_info)
            icon_info = icon_info_from_content_type("text/x-generic");
        if (!icon_info)
            icon_info = icon_info_from_content_type("text/plain");

        if (icon_info) {
            GtkWidget *image = gtk_image_new_from_pixbuf(
                    gtk_icon_info_load_icon(icon_info, NULL));
            gtk_button_set_image(button, image);
            gtk_button_set_always_show_image(button, true);
        }
    }

    if (!icons_only)
        left_align_button(button);
}

void add_filename_button(char *filename) {
    GFile *file = g_file_new_for_path(filename);
    add_file_button(file);
}

void add_uri_button(char *uri) {
    struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
    dragdata->text = uri;
    dragdata->uri = uri;
    GtkButton *button = add_button(uri, dragdata, TARGET_TYPE_URI);
    left_align_button(button);
}

bool is_uri(char *uri) {
    for (int i=0; uri[i]; i++)
        if (uri[i] == '/')
            return false;
        else if (uri[i] == ':')
            return true;
    return false;
}

bool is_file_uri(char *uri) {
    char *prefix = "file:";
    return strncmp(prefix, uri, strlen(prefix)) == 0;
}

gboolean drag_drop (GtkWidget *widget,
               GdkDragContext *context,
               gint            x,
               gint            y,
               guint           time,
               gpointer        user_data) {
    GtkTargetList *targetlist = gtk_drag_dest_get_target_list(widget);
    GList *list = gdk_drag_context_list_targets(context);
    if (list) {
        while (list) {
            GdkAtom atom = (GdkAtom)g_list_nth_data(list, 0);
            if (gtk_target_list_find(targetlist,
                        GDK_POINTER_TO_ATOM(g_list_nth_data(list, 0)), NULL)) {
                gtk_drag_get_data(widget, context, atom, time);
                return true;
            }
            list = g_list_next(list);
        }
    }
    gtk_drag_finish(context, false, false, time);
    return true;
}

void
drag_data_received (GtkWidget          *widget,
                    GdkDragContext     *context,
                    gint                x,
                    gint                y,
                    GtkSelectionData   *data,
                    guint               info,
                    guint               time) {
    gchar **uris = gtk_selection_data_get_uris(data);
    unsigned char *text = gtk_selection_data_get_text(data);
    if (!uris && !text)
        gtk_drag_finish (context, FALSE, FALSE, time);
    if (uris) {
        if (verbose)
            fputs("Received URIs\n", stderr);
        gtk_container_remove(GTK_CONTAINER(vbox), widget);
        for (; *uris; uris++) {
            if (is_file_uri(*uris)) {
                GFile *file = g_file_new_for_uri(*uris);
                if (print_path) {
                    char *filename = g_file_get_path(file);
                    printf("%s\n", filename);
                } else
                    printf("%s\n", *uris);
                if (keep)
                    add_file_button(file);

            } else {
                printf("%s\n", *uris);
                if (keep)
                    add_uri_button(*uris);
            }
        }
        add_target_button();
        gtk_widget_show_all(window);
    } else if (text) {
        if (verbose)
            fputs("Received Text\n", stderr);
        printf("%s\n", text);
    } else if (verbose)
        fputs("Received nothing\n", stderr);
    gtk_drag_finish (context, TRUE, FALSE, time);
    if (and_exit)
        gtk_main_quit();
}

void add_target_button() {
    GtkWidget *label = gtk_button_new();
    gtk_button_set_label(GTK_BUTTON(label), "Drag something here...");
    gtk_button_set_alignment(label, 0.5, 0.5);
    gtk_box_pack_start(GTK_CONTAINER(vbox),label, 1, 1, 0); 
    GtkTargetList *targetlist = gtk_drag_dest_get_target_list(GTK_WIDGET(label));
    if (targetlist)
        gtk_target_list_ref(targetlist);
    else
        targetlist = gtk_target_list_new(NULL, 0);
    gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);
    gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
    gtk_drag_dest_set(GTK_WIDGET(label),
            GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT, NULL, 0,
            GDK_ACTION_COPY);
    gtk_drag_dest_set_target_list(GTK_WIDGET(label), targetlist);
    g_signal_connect(GTK_WIDGET(label), "drag-drop",
            G_CALLBACK(drag_drop), NULL);
    g_signal_connect(GTK_WIDGET(label), "drag-data-received",
            G_CALLBACK(drag_data_received), NULL);
}

void make_btn(char *filename) {
    if (!is_uri(filename)) {
        add_filename_button(filename);
    } else if (is_file_uri(filename)) {
        GFile *file = g_file_new_for_uri(filename);
        add_file_button(file);
    } else {
        add_uri_button(filename);
    }
}

static void readstdin(void) {
    char *write_pos = stdin_files, *newline;
    size_t max_size = BUFSIZ * 2, cur_size = 0;
    // read each line from stdin and add it to the item list
    while (fgets(write_pos, BUFSIZ, stdin)) {
            if (write_pos[0] == '-')
                    continue;
            if ((newline = strchr(write_pos, '\n')))
                    *newline = '\0';
            else
                    break;
            make_btn(write_pos);
            cur_size = newline - stdin_files + 1;
            if (max_size < cur_size + BUFSIZ) {
                    if (!(stdin_files = realloc(stdin_files, (max_size += BUFSIZ))))
                            fprintf(stderr, "%s: cannot realloc %lu bytes.\n", progname, max_size);
                    newline = stdin_files + cur_size - 1;
            }
            write_pos = newline + 1;
    }
}

int main (int argc, char **argv) {
    signal(SIGINT, sigintHandler);

    bool from_stdin = false;
    stdin_files = malloc(BUFSIZ * 2);
    progname = argv[0];
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-h") == 0
        || strcmp(argv[i], "--help") == 0) {
            mode = MODE_HELP;
            printf("dragon - lightweight DnD source/target\n");
            printf("Usage: %s [OPTION] [FILENAME]\n", argv[0]);
            printf("  --and-exit,      -x  exit after a single completed drop\n");
            printf("  --target,        -t  act as a target instead of source\n");
            printf("  --keep,          -k  with --target, keep files to drag out\n");
            printf("  --print-path,    -p  with --target, print file paths"
                    " instead of URIs\n");
            printf("  --all,           -a  drag all files at once\n");
            printf("  --icon-only,     -i  only show icons in drag-and-drop"
                    " windows\n");
            printf("  --on-top,        -T  make window always-on-top\n");
            printf("  --fit,           -f  open window in the center of the parent window and resize to match it's size \n");
            printf("  --center,        -c  open window in the center of the parent window (e.g. terminal)\n");
            printf("  --center-screen, -C  open window in the center of the screen\n");
            printf("  --stdin,         -I  read input from stdin\n");
            printf("  --thumb-size,    -s  set thumbnail size (default 96)\n");
            printf("  --verbose,       -v  be verbose\n");
            printf("  --help           -h  show help\n");
            printf("  --version            show version details\n");
            exit(0);
        } else if (strcmp(argv[i], "--version") == 0) {
            mode = MODE_VERSION;
            puts("dragon " VERSION);
            puts("Copyright (C) 2014-2018 Michael Homer");
            puts("This program comes with ABSOLUTELY NO WARRANTY.");
            puts("See the source for copying conditions.");
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0
                || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-t") == 0
                || strcmp(argv[i], "--target") == 0) {
            mode = MODE_TARGET;
        } else if (strcmp(argv[i], "-x") == 0
                || strcmp(argv[i], "--and-exit") == 0) {
            and_exit = true;
        } else if (strcmp(argv[i], "-k") == 0
                || strcmp(argv[i], "--keep") == 0) {
            keep = true;
        } else if (strcmp(argv[i], "-p") == 0
                || strcmp(argv[i], "--print-path") == 0) {
            print_path = true;
        } else if (strcmp(argv[i], "-a") == 0
                || strcmp(argv[i], "--all") == 0) {
            drag_all = true;
        } else if (strcmp(argv[i], "-i") == 0
                || strcmp(argv[i], "--icon-only") == 0) {
            icons_only = true;
        } else if (strcmp(argv[i], "-T") == 0
                || strcmp(argv[i], "--on-top") == 0) {
            always_on_top = true;
        } else if (strcmp(argv[i], "-m") == 0
                || strcmp(argv[i], "--move") == 0) {
            move = true;
        } else if (strcmp(argv[i], "-f") == 0
                || strcmp(argv[i], "--fit") == 0) {
            fit = true;
        } else if (strcmp(argv[i], "-c") == 0
                || strcmp(argv[i], "--center") == 0) {
            center = true;
        } else if (strcmp(argv[i], "-C") == 0
                || strcmp(argv[i], "--center-screen") == 0) {
            center_screen = true;
        } else if (strcmp(argv[i], "-I") == 0
                || strcmp(argv[i], "--stdin") == 0) {
            from_stdin = true;
        } else if (strcmp(argv[i], "-s") == 0
                || strcmp(argv[i], "--thumb-size") == 0) {
            if (argv[++i] == NULL || (thumb_size = atoi(argv[i])) <= 0) {
                fprintf(stderr, "%s: error: bad argument for %s `%s'.\n",
                        progname, argv[i-1], argv[i]);
                exit(1);
            }
            argv[i][0] = '\0';
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: error: unknown option `%s'.\n",
                    progname, argv[i]);
        }
    }
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

    GtkAccelGroup *accelgroup;
    GClosure *closure;

    gtk_init(&argc, &argv);

    icon_theme = gtk_icon_theme_get_default();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
    accelgroup = gtk_accel_group_new();
    gtk_accel_group_connect(accelgroup, GDK_KEY_Escape, 0, 0, closure);
    closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
    gtk_accel_group_connect(accelgroup, GDK_KEY_q, 0, 0, closure);
    gtk_window_add_accel_group(GTK_WINDOW(window), accelgroup);

    gtk_window_set_title(GTK_WINDOW(window), "Run");
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), always_on_top);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    gtk_container_add(GTK_CONTAINER(window), vbox);

    gtk_window_set_title(GTK_WINDOW(window), "dragon");
    gtk_window_resize(GTK_WINDOW(window), 170, 34);

    if ((fit || center) && !center_screen) {
        if (can_run_cmd("xdotool") == false)
        {
            fprintf(stderr, "Please install 'xdotool' to use this feature"\n);
        } else {
            FILE *fp;
            char path[1035];

            char proc[81];
            pid_t ppid = getppid();
            pid_t pppid = -1;

            snprintf(proc, 81, "/proc/%d/stat", (int)getppid());
            fp = fopen(proc, "r");
            if (fp)
            {
                fscanf(fp,"%*d %*s %*c %d", &pppid);
                fclose(fp);
            
                int pppid_len = snprintf(NULL, 0, "%d", pppid);
                int pppid_str_len = 70 + pppid_len + 1;
                char* pppid_str = malloc(pppid_str_len);
                snprintf(pppid_str, pppid_str_len, "xdotool getwindowgeometry --shell $(xdotool search --pid %d | tail -n 1)", pppid);

                //printf("%d %s\n",pppid, pppid_str);

                fp = popen(pppid_str, "r");
                if (fp == NULL) {
                    fprintf(stderr, "Failed to run xdotool\n" );
                } else {
                    int i = 0;
                    while (fgets(path, sizeof(path), fp) != NULL) 
                    {
                        char* ptr = &path;
                        if (i == 1) {x = strtol(path+2, NULL, 10);}
                        if (i == 2) {y = strtol(path+2, NULL, 10);}
                        if (i == 3) {w = strtol(path+6, NULL, 10);}
                        if (i == 4) {h = strtol(path+7, NULL, 10);}
                        ++i;
                    }
                    //printf("%d %d %d %d\n", x,y,w,h);
                    pclose(fp);
                }
                free(pppid_str);

                if (fit) {
                    x = x + 27;
                    y = y + 23;
                    w = w - 53;
                    h = h - 90;
                }
                else if (center) {
                    x = x + w/2 - 170/2;
                    y = y + h/2 - 34/2;
                }
            }
        }
    }

    if (center_screen) {
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
        fit = false;
        center = false;
    }


    if (mode == MODE_TARGET) {
        add_target_button();
    } else {
        if (from_stdin)
            uri_collection = malloc(sizeof(char*) * (MAX_SIZE  + 1));
        else if (drag_all)
            uri_collection = malloc(sizeof(char*) * ((argc > MAX_SIZE ? argc : MAX_SIZE) + 1));

        for (int i=1; i<argc; i++) {
            if (argv[i][0] != '-' && argv[i][0] != '\0')
            make_btn(argv[i]);
        }
        if (from_stdin)
            readstdin();

        if (!uri_count) {
            printf("Usage: %s [OPTIONS] FILENAME\n", progname);
            exit(0);
        }
    }
        
    gtk_widget_show_all(window);

    if ((fit || resize) && !center_screen) {
        gtk_window_resize(GTK_WINDOW(window), w, h);
    }
    if ((fit || move || center) && !center_screen) {
        gtk_window_move(GTK_WINDOW(window), x, y);
    }

    gtk_main();

    exit(0);
}
