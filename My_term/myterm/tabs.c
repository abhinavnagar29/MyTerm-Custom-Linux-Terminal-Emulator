#include "myterm.h"

void create_new_tab() {
    if (tab_count >= MAX_TABS) return;
    for (int i = 0; i < tab_count; i++) tabs[i].is_active = 0;
    Tab *t = &tabs[tab_count]; memset(t, 0, sizeof(*t));
    t->id = next_tab_id++;
    snprintf(t->name, sizeof(t->name), "Tab");
    t->is_active = 1; t->cursor=0; t->scroll=0;
    append_line(t, "Welcome to MyTerm");
    append_line(t, "Interface: Click '+' or Ctrl+T for new tab | Click 'X' or Ctrl+W to close tab");
    append_line(t, "Quick Start: Type 'help' for all commands | Ctrl+L or 'clear' to clear screen");
    current_tab = tab_count; tab_count++;
    draw_terminal();
}

void switch_to_tab(int idx) {
    if (idx < 0 || idx >= tab_count) return;
    for (int i = 0; i < tab_count; i++) tabs[i].is_active = 0;
    tabs[idx].is_active = 1; current_tab = idx; draw_terminal();
}

void close_tab(int idx) {
    if (tab_count <= 1 || idx < 0 || idx >= tab_count) return;
    Tab *t = &tabs[idx];
    if (t->watch.active) stop_multiwatch(t);
    for (int i = 0; i < t->proc_count; i++) { kill(t->procs[i].pid, SIGTERM); waitpid(t->procs[i].pid, NULL, 0); }
    for (int i = idx; i < tab_count - 1; i++) tabs[i] = tabs[i + 1];
    tab_count--; if (current_tab >= tab_count) current_tab = tab_count - 1; tabs[current_tab].is_active = 1; draw_terminal();
}
