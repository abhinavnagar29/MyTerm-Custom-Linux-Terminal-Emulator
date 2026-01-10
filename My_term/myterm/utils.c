#include "myterm.h"
#include <limits.h>

// Define globals
Display *display = NULL; Window window; GC gc; XFontSet fontset = NULL;
cairo_surface_t *cairo_surface = NULL; cairo_t *cairo_cr = NULL; int LINE_HEIGHT = FONT_HEIGHT; int LINE_GAP = 8; PangoFontDescription *font_desc = NULL; int LEFT_MARGIN = 0; int INPUT_LEFT_PAD = 14;
Atom XA_UTF8_STRING_ATOM = None; Atom XA_CLIPBOARD_ATOM = None; Atom XA_TARGETS_ATOM = None; Atom XA_TEXT_ATOM = None; Atom XA_STRING_ATOM = None; Atom XA_PASTE_PROP_ATOM = None;
char clipboard_text[MAX_LINE_LEN * 2] = {0};
Tab tabs[MAX_TABS]; int tab_count = 0; int current_tab = 0; int next_tab_id = 1;
int app_running = 1;
int dragging_scroll = 0; int drag_track_y = 0; int drag_track_h = 0; int drag_knob_h = 0;
int current_fg_pgid = 0;

// Get formatted prompt with current directory
void get_prompt(char *buf, size_t size) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(buf, size, "~$ ");
        return;
    }
    
    // Try to replace home directory with ~
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        if (cwd[strlen(home)] == '\0') {
            snprintf(buf, size, "~$ ");
        } else {
            snprintf(buf, size, "~%s$ ", cwd + strlen(home));
        }
    } else {
        // Show full path
        snprintf(buf, size, "%s$ ", cwd);
    }
}

void append_line(Tab *t, const char *s) {
    if (!t || !s) return;
    if (t->line_count < MAX_LINES) {
        strncpy(t->buffer[t->line_count++], s, MAX_LINE_LEN - 1);
        t->buffer[t->line_count - 1][MAX_LINE_LEN - 1] = '\0';
    } else {
        memmove(t->buffer, t->buffer + 1, sizeof(t->buffer[0]) * (MAX_LINES - 1));
        strncpy(t->buffer[MAX_LINES - 1], s, MAX_LINE_LEN - 1);
        t->buffer[MAX_LINES - 1][MAX_LINE_LEN - 1] = '\0';
    }
}

int count_lines(const char *s) {
    if (!s || !*s) return 1;
    int n = 1; for (const char *p = s; *p; ++p) if (*p == '\n') n++;
    return n;
}

int has_unquoted_newline(const char *s) {
    int in_s=0, in_d=0, esc=0; for (const char *p=s; *p; ++p) {
        char c=*p; if (esc) { esc=0; continue; }
        if (c=='\\') { esc=1; continue; }
        if (!in_d && c=='\'') { in_s = !in_s; continue; }
        if (!in_s && c=='"') { in_d = !in_d; continue; }
        if (!in_s && !in_d && c=='\n') return 1;
    }
    return 0;
}

void run_lines_split_unquoted(Tab *t, const char *s) {
    extern void run_command(const char *cmd_line, Tab *t);
    int in_s=0, in_d=0, esc=0; const char *seg=s; for (const char *p=s; ; ++p) {
        char c=*p; int end = (c=='\0'); if (!end) {
            if (esc) { esc=0; continue; }
            if (c=='\\') { esc=1; continue; }
            if (!in_d && c=='\'') { in_s = !in_s; continue; }
            if (!in_s && c=='"') { in_d = !in_d; continue; }
        }
        if (end || (!in_s && !in_d && c=='\n')) {
            const char *a=seg, *b=p; while (a<b && (*a==' '||*a=='\t')) a++; while (b>a && (b[-1]==' '||b[-1]=='\t')) b--; if (b>a) {
                char line[MAX_LINE_LEN]; size_t L=(size_t)(b-a); if (L>sizeof(line)-1) L=sizeof(line)-1; memcpy(line,a,L); line[L]='\0'; run_command(line, t);
            }
            if (end) break; seg = p+1;
        }
    }
}

void remove_proc(Tab *t, pid_t pid) {
    for (int i = 0; i < t->proc_count; i++) if (t->procs[i].pid == pid) { for (int j = i; j < t->proc_count - 1; j++) t->procs[j] = t->procs[j + 1]; t->proc_count--; return; }
}

void list_jobs(Tab *t) {
    // First, clean up any dead processes by trying to reap them
    // Collect dead PIDs first to avoid iterator issues
    pid_t dead_pids[MAX_JOBS];
    int dead_count = 0;
    
    for (int i = 0; i < t->proc_count; i++) {
        pid_t pid = t->procs[i].pid;
        int status;
        // Try to reap the process (non-blocking)
        // Use -pid to check the whole process group
        pid_t result = waitpid(-pid, &status, WNOHANG);
        if (result > 0 || (result == -1 && errno == ECHILD)) {
            // Process has exited or no children exist
            dead_pids[dead_count++] = pid;
        } else if (kill(-pid, 0) == -1 && errno == ESRCH) {
            // Process group doesn't exist
            dead_pids[dead_count++] = pid;
        }
    }
    
    // Now remove all dead processes
    for (int i = 0; i < dead_count; i++) {
        remove_proc(t, dead_pids[i]);
    }
    
    if (!t->proc_count) { append_line(t, "No processes running"); return; }
    append_line(t, "Running processes:"); char line[320];
    for (int i = 0; i < t->proc_count; i++) { snprintf(line, sizeof(line), "  PID %d: %s", (int)t->procs[i].pid, t->procs[i].cmd); append_line(t, line); }
}

void reaper(int sig) {
    (void)sig; int status; pid_t p; while ((p = waitpid(-1, &status, WNOHANG)) > 0) { for (int i = 0; i < tab_count; i++) remove_proc(&tabs[i], p); }
}
