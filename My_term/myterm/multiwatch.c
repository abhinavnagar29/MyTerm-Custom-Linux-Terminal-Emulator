#include "myterm.h"

void get_timestamp(char *buf, size_t sz) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    // UNIX epoch with milliseconds: e.g., 1698173311.123
    long long sec = (long long)ts.tv_sec;
    long ms = ts.tv_nsec / 1000000;
    snprintf(buf, sz, "%lld.%03ld", sec, ms);
}

int parse_multiwatch(const char *cmd_line, char cmds[][256], int *count) {
    *count = 0; const char *p = strchr(cmd_line, '['); if (!p) return 0; p++;
    while (*p && *p != ']') {
        // Skip leading whitespace and commas
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;
        
        if (*p == '"') {
            // Quoted command
            p++; char *out = cmds[*count]; size_t k = 0;
            while (*p && *p != '"' && k + 1 < 256) out[k++] = *p++;
            out[k] = '\0';
            if (*p == '"') p++;
            if (k > 0) (*count)++;
            if (*count >= MAX_WATCH_CMDS) break;
        }
        else {
            // Unquoted command (read until comma or ])
            char *out = cmds[*count]; size_t k = 0;
            while (*p && *p != ',' && *p != ']' && k + 1 < 256) out[k++] = *p++;
            out[k] = '\0';
            // Trim trailing whitespace
            while (k && isspace((unsigned char)out[k-1])) out[--k] = '\0';
            if (k > 0) (*count)++;
            if (*count >= MAX_WATCH_CMDS) break;
        }
    }
    return *count > 0;
}

void stop_multiwatch(Tab *t) {
    if (!t->watch.active) return;
    // Phase 1: ask children (process groups) to stop nicely (SIGINT)
    for (int i = 0; i < t->watch.n; i++) if (t->watch.pids[i] > 0) kill(-t->watch.pids[i], SIGINT);
    // Small grace period with reap attempts
    for (int attempt = 0; attempt < 10; attempt++) {
        int any_alive = 0;
        for (int i = 0; i < t->watch.n; i++) {
            if (t->watch.pids[i] > 0) {
                pid_t r = waitpid(t->watch.pids[i], NULL, WNOHANG);
                if (r == 0) any_alive = 1; else if (r == t->watch.pids[i]) t->watch.pids[i] = 0;
            }
        }
        if (!any_alive) break;
        struct timespec ts = {0, 20 * 1000 * 1000}; // 20ms
        nanosleep(&ts, NULL);
    }
    // Phase 2: escalate to SIGTERM if still alive
    for (int i = 0; i < t->watch.n; i++) if (t->watch.pids[i] > 0) kill(-t->watch.pids[i], SIGTERM);
    for (int attempt = 0; attempt < 10; attempt++) {
        int any_alive = 0;
        for (int i = 0; i < t->watch.n; i++) {
            if (t->watch.pids[i] > 0) {
                pid_t r = waitpid(t->watch.pids[i], NULL, WNOHANG);
                if (r == 0) any_alive = 1; else if (r == t->watch.pids[i]) t->watch.pids[i] = 0;
            }
        }
        if (!any_alive) break;
        struct timespec ts = {0, 20 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    // Phase 3: force kill
    for (int i = 0; i < t->watch.n; i++) if (t->watch.pids[i] > 0) kill(-t->watch.pids[i], SIGKILL);
    for (int i = 0; i < t->watch.n; i++) if (t->watch.pids[i] > 0) { waitpid(t->watch.pids[i], NULL, 0); t->watch.pids[i] = 0; }

    // Close and cleanup all descriptors and files
    for (int i = 0; i < t->watch.n; i++) {
        if (t->watch.pipes[i][0] != -1) { close(t->watch.pipes[i][0]); t->watch.pipes[i][0] = -1; }
        if (t->watch.pipes[i][1] != -1) { close(t->watch.pipes[i][1]); t->watch.pipes[i][1] = -1; }
        if (t->watch.tmpfds[i] != -1) { close(t->watch.tmpfds[i]); t->watch.tmpfds[i] = -1; }
        if (t->watch.tmppaths[i][0]) { unlink(t->watch.tmppaths[i]); t->watch.tmppaths[i][0] = '\0'; }
    }
    t->watch.active = 0; append_line(t, "[multiWatch stopped]");
}

void start_multiwatch(Tab *t, char cmds[][256], int n) {
    if (t->watch.active) { append_line(t, "multiWatch already running"); return; }
    memset(&t->watch, 0, sizeof(t->watch)); t->watch.active = 1; t->watch.n = n;
    for (int i = 0; i < n; i++) {
        strncpy(t->watch.cmds[i], cmds[i], sizeof(t->watch.cmds[i]) - 1);
        t->watch.pipes[i][0] = t->watch.pipes[i][1] = -1;
        t->watch.tmpfds[i] = -1; t->watch.tmppaths[i][0] = '\0';
        t->watch.linepos[i] = 0; t->watch.linebuf[i][0] = '\0';
        if (pipe(t->watch.pipes[i]) == -1) { append_line(t, "pipe failed"); stop_multiwatch(t); return; }
        pid_t pid = fork();
        if (pid == 0) {
            // Child: make its own process group so signals via -pgid affect entire tree
            setpgid(0, 0);
            close(t->watch.pipes[i][0]);
            dup2(t->watch.pipes[i][1], STDOUT_FILENO);
            dup2(t->watch.pipes[i][1], STDERR_FILENO);
            close(t->watch.pipes[i][1]);
            execl("/bin/sh", "sh", "-c", t->watch.cmds[i], (char*)NULL);
            perror("execl"); _exit(127);
        } else if (pid > 0) {
            t->watch.pids[i] = pid;
            // Parent: ensure the child is leader of its own process group
            setpgid(pid, pid);
            close(t->watch.pipes[i][1]);
            snprintf(t->watch.tmppaths[i], sizeof(t->watch.tmppaths[i]), ".temp.%d.txt", (int)pid);
            int fd = open(t->watch.tmppaths[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { append_line(t, "temp file open failed"); stop_multiwatch(t); return; }
            t->watch.tmpfds[i] = fd;
        } else { append_line(t, "fork failed"); stop_multiwatch(t); return; }
    }
    append_line(t, "[multiWatch started]");
}

void process_watch(Tab *t) {
    if (!t->watch.active || t->watch.n <= 0) return;
    
    // Check if all processes have finished
    int all_finished = 1;
    for (int i = 0; i < t->watch.n; i++) {
        if (t->watch.pids[i] > 0) {
            int status;
            pid_t result = waitpid(t->watch.pids[i], &status, WNOHANG);
            if (result == t->watch.pids[i]) {
                // Process finished - print closing separator if header was printed
                if (t->watch.header_printed[i]) {
                    append_line(t, "----------------------------------------------------");
                    t->watch.header_printed[i] = 0;  // Reset for next run
                }
                t->watch.pids[i] = 0;
            } else if (result == 0) {
                // Process still running
                all_finished = 0;
            }
        }
    }
    
    // If all processes finished, stop multiWatch
    if (all_finished) {
        // Read any remaining output before stopping
        for (int i = 0; i < t->watch.n; i++) {
            if (t->watch.pipes[i][0] != -1) {
                char buf[512];
                ssize_t n;
                while ((n = read(t->watch.pipes[i][0], buf, sizeof(buf))) > 0) {
                    if (t->watch.tmpfds[i] != -1) write(t->watch.tmpfds[i], buf, (size_t)n);
                    for (ssize_t j = 0; j < n; j++) {
                        char c = buf[j]; if (c == '\r') continue; if (c == '\n') {
                            t->watch.linebuf[i][t->watch.linepos[i]] = '\0'; if (t->watch.linepos[i] > 0) {
                                // Print header only once when first output appears
                                if (!t->watch.header_printed[i]) {
                                    char ts[64]; get_timestamp(ts, sizeof(ts));
                                    char hdr[MAX_LINE_LEN]; snprintf(hdr, sizeof(hdr), "\"%s\" , %s :", t->watch.cmds[i], ts);
                                    append_line(t, hdr); 
                                    append_line(t, "----------------------------------------------------");
                                    t->watch.header_printed[i] = 1;
                                }
                                append_line(t, t->watch.linebuf[i]);
                            }
                            t->watch.linepos[i] = 0; t->watch.linebuf[i][0] = '\0';
                        } else if (t->watch.linepos[i] + 1 < sizeof(t->watch.linebuf[i])) { t->watch.linebuf[i][t->watch.linepos[i]++] = c; }
                    }
                }
            }
        }
        stop_multiwatch(t);
        append_line(t, "[multiWatch completed - all commands finished]");
        draw_terminal();
        return;
    }
    
    struct pollfd pfds[MAX_WATCH_CMDS]; int m = 0;
    for (int i = 0; i < t->watch.n; i++) { if (t->watch.pipes[i][0] != -1) { pfds[m].fd = t->watch.pipes[i][0]; pfds[m].events = POLLIN; pfds[m].revents = 0; m++; } }
    if (m == 0) { stop_multiwatch(t); return; }
    int ret = poll(pfds, m, 0); if (ret <= 0) return;
    for (int k = 0; k < m; k++) {
        if (pfds[k].revents & POLLIN) {
            int idx = -1; for (int i = 0; i < t->watch.n; i++) { if (t->watch.pipes[i][0] == pfds[k].fd) { idx = i; break; } }
            if (idx == -1) continue; char buf[512]; ssize_t n = read(pfds[k].fd, buf, sizeof(buf));
            if (n > 0) {
                if (t->watch.tmpfds[idx] != -1) write(t->watch.tmpfds[idx], buf, (size_t)n);
                for (ssize_t i = 0; i < n; i++) {
                    char c = buf[i]; if (c == '\r') continue; if (c == '\n') {
                        t->watch.linebuf[idx][t->watch.linepos[idx]] = '\0'; if (t->watch.linepos[idx] > 0) {
                            // Print header only once when first output appears
                            if (!t->watch.header_printed[idx]) {
                                char ts[64]; get_timestamp(ts, sizeof(ts));
                                char hdr[MAX_LINE_LEN]; snprintf(hdr, sizeof(hdr), "\"%s\" , %s :", t->watch.cmds[idx], ts);
                                append_line(t, hdr);
                                append_line(t, "----------------------------------------------------");
                                t->watch.header_printed[idx] = 1;
                            }
                            append_line(t, t->watch.linebuf[idx]);
                        }
                        t->watch.linepos[idx] = 0; t->watch.linebuf[idx][0] = '\0';
                    } else if (t->watch.linepos[idx] + 1 < sizeof(t->watch.linebuf[idx])) { t->watch.linebuf[idx][t->watch.linepos[idx]++] = c; }
                }
            } else if (n == 0) { close(t->watch.pipes[idx][0]); t->watch.pipes[idx][0] = -1; if (t->watch.tmpfds[idx] != -1) { close(t->watch.tmpfds[idx]); t->watch.tmpfds[idx] = -1; } }
        }
    }
}
