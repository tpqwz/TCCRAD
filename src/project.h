#ifndef TCCRAD_PROJECT_H
#define TCCRAD_PROJECT_H

#include "model.h"
#include "style.h"

#include <stddef.h>
#include <sys/types.h>

int tr_argv_ok(const char *s);
int tr_home_dir(char *dst, size_t cap);

/* Run argv without a shell. timeout_ms bounds the wait. log receives captured text. */
int tr_spawn_wait(char *const argv[], const char *dir, char *log, size_t logcap,
    int *exit_code, int timeout_ms);

/* Start argv and return. logfile receives the child's stdout and stderr. */
int tr_spawn_async(char *const argv[], const char *dir, const char *logfile, pid_t *pid);

/* Turn a spaced-C fragment into host C with the offside command. Caller frees *out. */
int tr_spaced_emit(const char *text, size_t n, char **out, size_t *out_len,
    char *err, size_t errcap);

int tr_project_build(const TrDoc *doc, const TrStyle *style, const char *out_dir,
    const char *runtime_dir, const char *third_dir, const char *tcc,
    char *log, size_t logcap, int also_run, pid_t *child);

#endif
