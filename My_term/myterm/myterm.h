#ifndef MYTERM_H
#define MYTERM_H

#define _XOPEN_SOURCE 700
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <locale.h>

// Constants
#define WIDTH 1600
#define HEIGHT 900
#define FONT_HEIGHT 18
#define TAB_HEIGHT 30
#define MAX_TABS 8
#define MAX_LINES 1000
#define MAX_LINE_LEN 512
#define MAX_ARGS 64
#define MAX_STAGES 16
#define BUFFER_SIZE 8192
#define HISTORY_MAX 10000
#define HISTORY_SHOW 1000
#define MAX_WATCH_CMDS 16
#define MAX_JOBS 64

// Types
typedef struct Proc { pid_t pid; char cmd[256]; } Proc;

typedef struct WatchCtx {
    int active; int n;
    pid_t pids[MAX_WATCH_CMDS];
    char cmds[MAX_WATCH_CMDS][256];
    int pipes[MAX_WATCH_CMDS][2];
    int tmpfds[MAX_WATCH_CMDS];
    char tmppaths[MAX_WATCH_CMDS][128];
    char linebuf[MAX_WATCH_CMDS][1024]; size_t linepos[MAX_WATCH_CMDS];
    int header_printed[MAX_WATCH_CMDS];  // Track if header was printed for each command
} WatchCtx;

typedef struct Tab {
    int id; char name[32]; int is_active;
    char buffer[MAX_LINES][MAX_LINE_LEN]; int line_count;
    int scroll;
    char input_line[MAX_LINE_LEN]; size_t cursor;
    char *history[HISTORY_MAX]; int hist_count; int hist_head;
    int search_mode; char search_term[256]; size_t search_len;
    Proc procs[MAX_JOBS]; int proc_count;
    WatchCtx watch;
    char ac_choices[256][NAME_MAX+1]; int ac_count; int ac_tok_start;
} Tab;

// Globals (defined in utils.c)
extern Display *display; extern Window window; extern GC gc; extern XFontSet fontset;
extern cairo_surface_t *cairo_surface; extern cairo_t *cairo_cr; extern int LINE_HEIGHT; extern int LINE_GAP; extern PangoFontDescription *font_desc; extern int LEFT_MARGIN; extern int INPUT_LEFT_PAD;
extern Atom XA_UTF8_STRING_ATOM; extern Atom XA_CLIPBOARD_ATOM; extern Atom XA_TARGETS_ATOM; extern Atom XA_TEXT_ATOM; extern Atom XA_STRING_ATOM; extern Atom XA_PASTE_PROP_ATOM;
extern char clipboard_text[MAX_LINE_LEN * 2];
extern Tab tabs[MAX_TABS]; extern int tab_count; extern int current_tab; extern int next_tab_id;
extern int app_running;
extern int dragging_scroll; extern int drag_track_y; extern int drag_track_h; extern int drag_knob_h;
// Track current foreground process group for Ctrl+C
extern int current_fg_pgid;

// Utils
void get_prompt(char *buf, size_t size);
void append_line(Tab *t, const char *s);
void run_lines_split_unquoted(Tab *t, const char *input);
void remove_proc(Tab *t, pid_t pid);
void list_jobs(Tab *t);

// GUI
void draw_text(int x, int y, const char *s);
void get_text_size(const char *s, int *out_w, int *out_h);
void draw_tabs();
void draw_input(Tab *t, int y);
void draw_terminal();

// History
void history_add(Tab *t, const char *cmd);
void show_history(Tab *t);
int lcslen(const char *a, const char *b);
void history_search(Tab *t);

// Multiwatch
int parse_multiwatch(const char *cmd_line, char cmds[][256], int *count);
void stop_multiwatch(Tab *t);
void start_multiwatch(Tab *t, char cmds[][256], int n);
void process_watch(Tab *t);

// Tabs
void create_new_tab();
void switch_to_tab(int idx);
void close_tab(int idx);

// Input handlers
void handle_tab_click(int x, int y, unsigned int button);
void handle_motion_drag(int x, int y);
void autocomplete(Tab *t);

// Shell
void execute_simple(Tab *t, char **argv, int background);
int split_pipeline(const char *cmd_line, char stages[][MAX_LINE_LEN], int *count);
void execute_pipeline(Tab *t, const char *cmd_line, int background);
void execute_sh(Tab *t, const char *script);
void run_command(const char *cmd_line, Tab *t);

// Main
void reaper(int sig);

#endif // MYTERM_H
