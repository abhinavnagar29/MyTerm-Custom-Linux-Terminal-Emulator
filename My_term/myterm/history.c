#include "myterm.h"

// Global, shared history across all tabs (10,000 max)
static char *g_history[HISTORY_MAX];
static int g_hist_count = 0;
static int g_hist_head = 0;

void history_add(Tab *t, const char *cmd) {
    (void)t; // history is global; tab is unused
    if (!cmd || !*cmd) return;
    if (g_hist_count) {
        int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
        if (g_history[last] && strcmp(g_history[last], cmd) == 0) return;
    }
    char *copy = strdup(cmd); if (!copy) return;
    free(g_history[g_hist_head]);
    g_history[g_hist_head] = copy;
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX) g_hist_count++;
}

void show_history(Tab *t) {
    int to_show = g_hist_count < HISTORY_SHOW ? g_hist_count : HISTORY_SHOW;
    // Iterate from oldest to newest
    for (int i = to_show - 1; i >= 0; i--) {
        int idx = (g_hist_head - 1 - i + HISTORY_MAX) % HISTORY_MAX;
        if (g_history[idx]) {
            char numbered[MAX_LINE_LEN];
            snprintf(numbered, sizeof(numbered), "%4d  %s", to_show - i, g_history[idx]);
            append_line(t, numbered);
        }
    }
}

int lcslen(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b); int best = 0;
    for (int i = 0; i < la; i++) {
        for (int j = 0; j < lb; j++) {
            int k = 0; while (i+k < la && j+k < lb && a[i+k] == b[j+k]) k++;
            if (k > best) best = k;
        }
    }
    return best;
}

void history_search(Tab *t) {
    const char *term = t->search_term;
    if (!*term) { append_line(t, "No match for search term in history"); return; }
    for (int i = 0; i < g_hist_count; i++) {
        int idx = (g_hist_head - 1 - i + HISTORY_MAX) % HISTORY_MAX;
        if (g_history[idx] && strcmp(g_history[idx], term) == 0) { append_line(t, g_history[idx]); return; }
    }
    int best = 0;
    for (int i = 0; i < g_hist_count; i++) {
        int idx = (g_hist_head - 1 - i + HISTORY_MAX) % HISTORY_MAX;
        if (!g_history[idx]) continue; int l = lcslen(g_history[idx], term); if (l > best) best = l;
    }
    if (best > 2) {
        for (int i = 0; i < g_hist_count; i++) {
            int idx = (g_hist_head - 1 - i + HISTORY_MAX) % HISTORY_MAX;
            if (!g_history[idx]) continue; int l = lcslen(g_history[idx], term); if (l == best) append_line(t, g_history[idx]);
        }
    } else {
        append_line(t, "No match for search term in history");
    }
}
