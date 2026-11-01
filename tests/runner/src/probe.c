#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RENDEZVOUS_LIMIT_MS = 20000, RENDEZVOUS_POLL_MS = 5 };

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int create_file(const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

/* Creates self, then returns 0 once other exists or 3 if it never does. */
static int rendezvous(const char *self, const char *other) {
    if (!create_file(self)) return 3;
    for (int waited = 0; waited < RENDEZVOUS_LIMIT_MS; waited += RENDEZVOUS_POLL_MS) {
        if (file_exists(other)) return 0;
        sleep_ms(RENDEZVOUS_POLL_MS);
    }
    printf("rendezvous: %s never appeared\n", other);
    return 3;
}

/* Returns nonzero if another holder has path. */
static int hold_exclusive(const char *path, int ms) {
    FILE *file = fopen(path, "wx");
    if (!file) {
        printf("exclusive: %s already held\n", path);
        return 4;
    }
    fclose(file);
    sleep_ms(ms);
    return remove(path) == 0 ? 0 : 4;
}

/* Starts a copy of this program that creates path after ms, and waits for it. */
static int spawn_delayed_touch(const char *self, const char *ms, const char *path) {
    char command[1024];
    snprintf(command, sizeof(command), "%s --sleep %s --touch %s", self, ms, path);
    return system(command) == 0 ? 0 : 7;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const char *action = argv[i];
        int rc = 0;
        if (strcmp(action, "--out") == 0) {
            fputs(argv[++i], stdout);
            fflush(stdout);
        } else if (strcmp(action, "--err") == 0) {
            fputs(argv[++i], stderr);
            fflush(stderr);
        } else if (strcmp(action, "--exit") == 0) {
            return atoi(argv[i + 1]);
        } else if (strcmp(action, "--sleep") == 0) {
            sleep_ms(atoi(argv[++i]));
        } else if (strcmp(action, "--rendezvous") == 0) {
            rc = rendezvous(argv[i + 1], argv[i + 2]);
            i += 2;
        } else if (strcmp(action, "--exclusive") == 0) {
            rc = hold_exclusive(argv[i + 1], atoi(argv[i + 2]));
            i += 2;
        } else if (strcmp(action, "--require") == 0) {
            rc = file_exists(argv[++i]) ? 0 : 6;
        } else if (strcmp(action, "--touch") == 0) {
            rc = create_file(argv[++i]) ? 0 : 8;
        } else if (strcmp(action, "--spawn-touch") == 0) {
            rc = spawn_delayed_touch(argv[0], argv[i + 1], argv[i + 2]);
            i += 2;
        } else if (strcmp(action, "--stdin-empty") == 0) {
            rc = getchar() == EOF ? 0 : 5;
        } else if (strcmp(action, "--env") == 0) {
            const char *value = getenv(argv[++i]);
            printf("env=%s\n", value ? value : "(unset)");
            fflush(stdout);
        } else if (strcmp(action, "--terminate") == 0) {
            raise(SIGTERM);
        } else if (strcmp(action, "--echo-args") == 0) {
            for (++i; i < argc; ++i) printf("[%s]\n", argv[i]);
            fflush(stdout);
        } else {
            printf("unknown action %s\n", action);
            return 2;
        }
        if (rc != 0) return rc;
    }
    return 0;
}
