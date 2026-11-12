#define _GNU_SOURCE

#include "project.h"
#include "codegen.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

int
tr_argv_ok(const char *s)
{
    size_t i;
    if (!s || s[0] == '\0' || strlen(s) > 4000) return 0;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 32 || c > 126) return 0;
        if (c == '$' || c == '`' || c == ';' || c == '|' || c == '&' ||
            c == '<' || c == '>' || c == '\\' || c == '\'' || c == '"')
            return 0;
    }
    return 1;
}

int
tr_home_dir(char *dst, size_t cap)
{
    const char *env = getenv("TCCRAD_HOME");
    char exe[TR_PATH_MAX];
    ssize_t n;
    char *slash;
    if (!dst || cap < 2) return -1;
    if (env && env[0] != '\0') return tr_copy_cap(dst, cap, env);
    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 0) return tr_copy_cap(dst, cap, ".");
    exe[n] = '\0';
    slash = strrchr(exe, '/');
    if (!slash) return tr_copy_cap(dst, cap, ".");
    *slash = '\0';
    slash = strrchr(exe, '/');
    if (slash && strcmp(slash + 1, "build") == 0) *slash = '\0';
    return tr_copy_cap(dst, cap, exe);
}

static int
dir_parent(char *dst, size_t cap, const char *path)
{
    size_t n;
    if (!path || !dst || cap < 2) return -1;
    n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    while (n > 0 && path[n - 1] != '/') n--;
    if (n <= 1) return -1;
    if (n - 1 >= cap) return -1;
    memcpy(dst, path, n - 1);
    dst[n - 1] = '\0';
    return 0;
}

static int
offside_bin(char *dst, size_t cap)
{
    const char *env = getenv("TCCRAD_OFFSIDE");
    char home[TR_PATH_MAX];
    char parent[TR_PATH_MAX];
    char mid[TR_PATH_MAX];
    char bin[TR_PATH_MAX];
    if (env && env[0] != '\0' && access(env, X_OK) == 0) return tr_copy_cap(dst, cap, env);
    if (tr_home_dir(home, sizeof home) == 0 && dir_parent(parent, sizeof parent, home) == 0 &&
        tr_path_join(mid, sizeof mid, parent, "offside") == 0 &&
        tr_path_join(bin, sizeof bin, mid, "bin") == 0 &&
        tr_path_join(dst, cap, bin, "offside") == 0 &&
        access(dst, X_OK) == 0) {
        return 0;
    }
    if (tr_home_dir(home, sizeof home) == 0 && dir_parent(parent, sizeof parent, home) == 0 &&
        tr_path_join(mid, sizeof mid, parent, "offside") == 0 &&
        tr_path_join(bin, sizeof bin, mid, "c") == 0 &&
        tr_path_join(mid, sizeof mid, bin, "build") == 0 &&
        tr_path_join(dst, cap, mid, "offside") == 0 &&
        access(dst, X_OK) == 0) {
        return 0;
    }
    return tr_copy_cap(dst, cap, "offside");
}

int
tr_spaced_emit(const char *text, size_t n, char **out, size_t *out_len, char *err, size_t errcap)
{
    char bin[TR_PATH_MAX];
    char path[64];
    char *buf;
    char *av[8];
    int code = -1;
    size_t cap = 65536;
    int nw;
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (err && errcap) err[0] = '\0';
    if (!text || !out || n > 32768) {
        if (err && errcap) tr_copy_cap(err, errcap, "spaced C region is too large");
        return -1;
    }
    if (offside_bin(bin, sizeof bin) != 0) return -1;
    nw = snprintf(path, sizeof path, "/tmp/tccrad-space-%ld.csp", (long)getpid());
    if (nw < 0 || (size_t)nw >= sizeof path) return -1;
    {
        char *file;
        size_t fn = n;
        int extra = (n == 0 || text[n - 1] != '\n') ? 1 : 0;
        file = malloc(n + (size_t)extra + 1);
        if (!file) return -1;
        if (n > 0) memcpy(file, text, n);
        if (extra) file[n] = '\n';
        file[n + (size_t)extra] = '\0';
        fn = n + (size_t)extra;
        if (tr_write_file(path, file, fn) != 0) {
            free(file);
            if (err && errcap) tr_copy_cap(err, errcap, "could not write the spaced C fragment");
            return -1;
        }
        free(file);
    }
    buf = malloc(cap);
    if (!buf) {
        unlink(path);
        return -1;
    }
    buf[0] = '\0';
    av[0] = bin;
    av[1] = "emit";
    av[2] = path;
    av[3] = "--plain";
    av[4] = "--lang";
    av[5] = "c";
    av[6] = NULL;
    if (tr_spawn_wait(av, "/tmp", buf, cap, &code, 10000) != 0 || code != 0) {
        free(buf);
        unlink(path);
        if (err && errcap) tr_copy_cap(err, errcap, "spaced C needs the offside command");
        return -1;
    }
    unlink(path);
    if (buf[0] == '\0' || strlen(buf) + 1 >= cap) {
        free(buf);
        if (err && errcap) tr_copy_cap(err, errcap, "spaced C produced no host code");
        return -1;
    }
    *out = buf;
    if (out_len) *out_len = strlen(buf);
    return 0;
}

static int
spawn_common(char *const argv[], const char *dir, int logfd, pid_t *pid)
{
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t attr;
    int rc;
    int i;
    if (!argv || !argv[0] || !pid) return -1;
    for (i = 0; argv[i]; i++) {
        if (!tr_argv_ok(argv[i])) return -1;
    }
    if (posix_spawn_file_actions_init(&fa) != 0) return -1;
    if (posix_spawnattr_init(&attr) != 0) {
        posix_spawn_file_actions_destroy(&fa);
        return -1;
    }
    if (dir && dir[0] != '\0') {
        if (posix_spawn_file_actions_addchdir_np(&fa, dir) != 0) {
            posix_spawnattr_destroy(&attr);
            posix_spawn_file_actions_destroy(&fa);
            return -1;
        }
    }
    if (logfd >= 0) {
        if (posix_spawn_file_actions_adddup2(&fa, logfd, STDOUT_FILENO) != 0 ||
            posix_spawn_file_actions_adddup2(&fa, logfd, STDERR_FILENO) != 0) {
            posix_spawnattr_destroy(&attr);
            posix_spawn_file_actions_destroy(&fa);
            return -1;
        }
    }
    if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) != 0 ||
        posix_spawnattr_setpgroup(&attr, 0) != 0) {
        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&fa);
        return -1;
    }
    rc = posix_spawnp(pid, argv[0], &fa, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&fa);
    return rc == 0 ? 0 : -1;
}

int
tr_spawn_wait(char *const argv[], const char *dir, char *log, size_t logcap,
    int *exit_code, int timeout_ms)
{
    char tmpl[] = "/tmp/tccrad-log-XXXXXX";
    int fd;
    pid_t pid = 0;
    int waited = 0;
    int status = 0;
    if (exit_code) *exit_code = -1;
    if (log && logcap) log[0] = '\0';
    fd = mkstemp(tmpl);
    if (fd < 0) return -1;
    if (spawn_common(argv, dir, fd, &pid) != 0) {
        close(fd);
        unlink(tmpl);
        return -1;
    }
    if (timeout_ms < 1) timeout_ms = 1000;
    while (waited < timeout_ms) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        struct timespec ts;
        if (got == pid) break;
        if (got < 0 && errno != EINTR) {
            close(fd);
            unlink(tmpl);
            return -1;
        }
        ts.tv_sec = 0;
        ts.tv_nsec = 50000000L;
        nanosleep(&ts, NULL);
        waited += 50;
    }
    if (waited >= timeout_ms) {
        if (pid > 1) kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
        close(fd);
        unlink(tmpl);
        if (log && logcap) tr_copy_cap(log, logcap, "compile timed out");
        return -1;
    }
    if (exit_code) {
        if (WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
        else *exit_code = -1;
    }
    if (log && logcap > 1) {
        ssize_t n;
        if (lseek(fd, 0, SEEK_SET) >= 0) {
            n = read(fd, log, logcap - 1);
            if (n < 0) n = 0;
            log[n] = '\0';
        }
    }
    close(fd);
    unlink(tmpl);
    return 0;
}

int
tr_spawn_async(char *const argv[], const char *dir, const char *logfile, pid_t *pid)
{
    int fd = -1;
    pid_t child = 0;
    if (!pid) return -1;
    *pid = 0;
    if (logfile) {
        fd = open(logfile, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return -1;
    }
    if (spawn_common(argv, dir, fd, &child) != 0) {
        if (fd >= 0) close(fd);
        return -1;
    }
    if (fd >= 0) close(fd);
    if (child <= 1) return -1;
    *pid = child;
    return 0;
}

#define TR_LV_SRC_MAX 2048

struct LvList {
    char *path[TR_LV_SRC_MAX];
    char *obj[TR_LV_SRC_MAX];
    size_t n;
};

static void
lv_list_free(struct LvList *list)
{
    size_t i;
    if (!list) return;
    for (i = 0; i < list->n; i++) {
        free(list->path[i]);
        free(list->obj[i]);
        list->path[i] = NULL;
        list->obj[i] = NULL;
    }
    list->n = 0;
}

static int
lv_list_add(struct LvList *list, const char *path)
{
    size_t n;
    char *copy;
    if (!list || !path || list->n >= TR_LV_SRC_MAX) return -1;
    if (!tr_argv_ok(path)) return -1;
    n = strlen(path);
    if (n == 0 || n > 4000) return -1;
    copy = malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, path, n + 1);
    list->obj[list->n] = NULL;
    list->path[list->n] = copy;
    list->n++;
    return 0;
}

static int
lv_list_set_obj(struct LvList *list, size_t index, const char *obj)
{
    size_t n;
    char *copy;
    if (!list || index >= list->n || !obj || !tr_argv_ok(obj)) return -1;
    n = strlen(obj);
    if (n == 0 || n > 4000) return -1;
    copy = malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, obj, n + 1);
    free(list->obj[index]);
    list->obj[index] = copy;
    return 0;
}

/* core/lv_obj.c -> <cache>/core_lv_obj.o so two files can share a basename. */
static int
lv_flat_obj(char *dst, size_t cap, const char *cache, const char *srcroot, const char *src)
{
    size_t rootlen;
    size_t n;
    size_t i;
    const char *rel;
    char name[512];
    if (!dst || !cache || !srcroot || !src) return -1;
    rootlen = strlen(srcroot);
    if (strncmp(src, srcroot, rootlen) != 0) return -1;
    rel = src + rootlen;
    if (*rel == '/') rel++;
    n = strlen(rel);
    if (n < 3 || n >= sizeof name) return -1;
    if (rel[n - 2] != '.' || rel[n - 1] != 'c') return -1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)rel[i];
        if (c == '/') name[i] = '_';
        else if (c <= 32 || c > 126) return -1;
        else name[i] = (char)c;
    }
    name[n - 1] = 'o';
    name[n] = '\0';
    if (strstr(name, "..") != NULL) return -1;
    return tr_path_join(dst, cap, cache, name);
}

static int
lv_obj_fresh(const char *obj, const char *src, const char *conf)
{
    struct stat so;
    struct stat ss;
    struct stat sc;
    if (!obj || !src || stat(obj, &so) != 0 || !S_ISREG(so.st_mode)) return 0;
    if (stat(src, &ss) != 0 || so.st_mtime < ss.st_mtime) return 0;
    if (conf && stat(conf, &sc) == 0 && so.st_mtime < sc.st_mtime) return 0;
    return 1;
}

static int
lv_compile_obj(const char *compiler, const char *lvroot, const char *src, const char *obj,
    const char *cache, char *log, size_t logcap)
{
    char *av[12];
    int code = -1;
    av[0] = (char *)compiler;
    av[1] = "-std=c11";
    av[2] = "-c";
    av[3] = "-I";
    av[4] = (char *)lvroot;
    av[5] = "-DLV_CONF_INCLUDE_SIMPLE";
    av[6] = "-o";
    av[7] = (char *)obj;
    av[8] = (char *)src;
    av[9] = NULL;
    if (tr_spawn_wait(av, cache, log, logcap, &code, 60000) != 0 || code != 0) return -1;
    return 0;
}

static int
name_is_c_file(const char *name)
{
    size_t n;
    if (!name || name[0] == '\0' || name[0] == '.') return 0;
    n = strlen(name);
    if (n < 3 || n > 240) return 0;
    return name[n - 2] == '.' && name[n - 1] == 'c';
}

/* Read regular .c files under dir. Symlinks are skipped. Depth is bounded. */
static int
lv_walk(struct LvList *list, const char *dir, int depth)
{
    DIR *dp;
    struct dirent *ent;
    if (!list || !dir || depth < 0 || depth >= 16) return -1;
    if (!tr_argv_ok(dir)) return -1;
    dp = opendir(dir);
    if (!dp) return -1;
    while ((ent = readdir(dp)) != NULL) {
        char child[TR_PATH_MAX];
        struct stat st;
        if (ent->d_name[0] == '.') continue;
        if (strchr(ent->d_name, '/') != NULL || strstr(ent->d_name, "..") != NULL) continue;
        if (tr_path_join(child, sizeof child, dir, ent->d_name) != 0) {
            closedir(dp);
            return -1;
        }
        if (lstat(child, &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (lv_walk(list, child, depth + 1) != 0) {
                closedir(dp);
                return -1;
            }
        } else if (S_ISREG(st.st_mode) && name_is_c_file(ent->d_name)) {
            if (lv_list_add(list, child) != 0) {
                closedir(dp);
                return -1;
            }
        }
    }
    closedir(dp);
    return 0;
}

static int
lv_collect(struct LvList *list, const char *third_dir, char *root, size_t rootcap,
    char *err, size_t errcap)
{
    char src[TR_PATH_MAX];
    char header[TR_PATH_MAX];
    const char *env = getenv("TCCRAD_LVGL");
    if (err && errcap) err[0] = '\0';
    if (env && env[0] != '\0') {
        if (!tr_argv_ok(env) || tr_copy_cap(root, rootcap, env) != 0) {
            if (err && errcap) tr_copy_cap(err, errcap, "TCCRAD_LVGL is not a usable path");
            return -1;
        }
    } else if (tr_path_join(root, rootcap, third_dir, "lvgl") != 0) {
        if (err && errcap) tr_copy_cap(err, errcap, "could not build the LVGL path");
        return -1;
    }
    if (tr_path_join(header, sizeof header, root, "lvgl.h") != 0 || access(header, R_OK) != 0) {
        if (err && errcap) {
            if (snprintf(err, errcap, "lvgl.h not found in %s", root) < 0) err[0] = '\0';
        }
        return -1;
    }
    if (tr_path_join(src, sizeof src, root, "src") != 0 || lv_walk(list, src, 0) != 0) {
        if (err && errcap) tr_copy_cap(err, errcap, "could not read the LVGL sources");
        return -1;
    }
    if (list->n == 0) {
        if (err && errcap) tr_copy_cap(err, errcap, "no LVGL sources were found");
        return -1;
    }
    return 0;
}

static int
push_arg(char **argv, int *argc, int cap, char *s)
{
    if (!argv || !argc || *argc < 0 || *argc >= cap) return -1;
    argv[*argc] = s;
    *argc += 1;
    return 0;
}

static int
ensure_dir(const char *path)
{
    struct stat st;
    if (!path || path[0] == '\0') return -1;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

int
tr_project_build(const TrDoc *doc, const TrStyle *style, const char *out_dir,
    const char *runtime_dir, const char *third_dir, const char *tcc,
    char *log, size_t logcap, int also_run, pid_t *child)
{
    TrBuf h, c, m;
    char hpath[TR_PATH_MAX];
    char cpath[TR_PATH_MAX];
    char mpath[TR_PATH_MAX];
    char binpath[TR_PATH_MAX];
    char err[256];
    char *stack_argv[24];
    char **argv = stack_argv;
    char **heap_argv = NULL;
    struct LvList lvsrc;
    char lvroot[TR_PATH_MAX];
    char uidir[TR_PATH_MAX];
    char uisrc[TR_PATH_MAX];
    int argc = 0;
    int cap = 24;
    int code = -1;
    int def = 1;
    int timeout_ms = 60000;
    char defbuf[32];
    const char *compiler = (tcc && tcc[0]) ? tcc : "tcc";
    memset(&lvsrc, 0, sizeof lvsrc);
    memset(stack_argv, 0, sizeof stack_argv);
    if (child) *child = 0;
    if (!doc || !out_dir || !runtime_dir || !third_dir) return -1;
    if (!tr_argv_ok(compiler) || !tr_argv_ok(out_dir) || !tr_argv_ok(runtime_dir) ||
        !tr_argv_ok(third_dir))
        return -1;
    if (ensure_dir(out_dir) != 0) return -1;
    if (tr_buf_init(&h) != 0 || tr_buf_init(&c) != 0 || tr_buf_init(&m) != 0) {
        tr_buf_free(&h);
        tr_buf_free(&c);
        tr_buf_free(&m);
        return -1;
    }
    err[0] = '\0';
    if (tr_codegen(doc, style, "app.h", &h, &c, err, sizeof err) != 0 ||
        tr_codegen_main(doc, style, "app.h", &m, err, sizeof err) != 0) {
        if (log && logcap) tr_copy_cap(log, logcap, err[0] ? err : "codegen failed");
        tr_buf_free(&h);
        tr_buf_free(&c);
        tr_buf_free(&m);
        return -1;
    }
    if (tr_path_join(hpath, sizeof hpath, out_dir, "app.h") != 0 ||
        tr_path_join(cpath, sizeof cpath, out_dir, "app.c") != 0 ||
        tr_path_join(mpath, sizeof mpath, out_dir, "main.c") != 0 ||
        tr_path_join(binpath, sizeof binpath, out_dir, "app") != 0) {
        tr_buf_free(&h);
        tr_buf_free(&c);
        tr_buf_free(&m);
        return -1;
    }
    if (tr_write_file(hpath, h.data, h.len) != 0 ||
        tr_write_file(cpath, c.data, c.len) != 0 ||
        tr_write_file(mpath, m.data, m.len) != 0) {
        tr_buf_free(&h);
        tr_buf_free(&c);
        tr_buf_free(&m);
        if (log && logcap) tr_copy_cap(log, logcap, "could not write the build directory");
        return -1;
    }
    tr_buf_free(&h);
    tr_buf_free(&c);
    tr_buf_free(&m);
    if (doc->backend == TR_BACKEND_LIBUI) def = 2;
    if (doc->backend == TR_BACKEND_LVGL) def = 3;
    if (snprintf(defbuf, sizeof defbuf, "-DTCCRAD_BACKEND=%d", def) < 0) return -1;
    if (def == 3) {
        char why[512];
        char parent[TR_PATH_MAX];
        char cache[TR_PATH_MAX];
        char srcdir[TR_PATH_MAX];
        char conf[TR_PATH_MAX];
        char obj[TR_PATH_MAX];
        size_t slots;
        size_t si;
        timeout_ms = 180000;
        why[0] = '\0';
        if (lv_collect(&lvsrc, third_dir, lvroot, sizeof lvroot, why, sizeof why) != 0) {
            if (log && logcap) tr_copy_cap(log, logcap, why[0] ? why : "LVGL sources are missing");
            lv_list_free(&lvsrc);
            return -1;
        }
        if (tr_path_dirname(lvroot, parent, sizeof parent) != 0 ||
            tr_path_join(cache, sizeof cache, parent, "lvgl-cache") != 0 ||
            tr_path_join(srcdir, sizeof srcdir, lvroot, "src") != 0 ||
            tr_path_join(conf, sizeof conf, lvroot, "lv_conf.h") != 0 ||
            ensure_dir(cache) != 0) {
            lv_list_free(&lvsrc);
            if (log && logcap) tr_copy_cap(log, logcap, "could not prepare the LVGL object cache");
            return -1;
        }
        for (si = 0; si < lvsrc.n; si++) {
            if (lv_flat_obj(obj, sizeof obj, cache, srcdir, lvsrc.path[si]) != 0 ||
                lv_list_set_obj(&lvsrc, si, obj) != 0) {
                lv_list_free(&lvsrc);
                if (log && logcap) tr_copy_cap(log, logcap, "could not name an LVGL object");
                return -1;
            }
            if (!lv_obj_fresh(lvsrc.obj[si], lvsrc.path[si], conf) &&
                lv_compile_obj(compiler, lvroot, lvsrc.path[si], lvsrc.obj[si], cache,
                    log, logcap) != 0) {
                lv_list_free(&lvsrc);
                if (log && logcap && log[0] == '\0')
                    tr_copy_cap(log, logcap, "could not compile an LVGL source");
                return -1;
            }
        }
        if (lvsrc.n > 4096U - 32U) {
            lv_list_free(&lvsrc);
            if (log && logcap) tr_copy_cap(log, logcap, "too many LVGL sources");
            return -1;
        }
        slots = lvsrc.n + 32U;
        cap = (int)slots;
        heap_argv = calloc(slots, sizeof *heap_argv);
        if (!heap_argv) {
            lv_list_free(&lvsrc);
            if (log && logcap) tr_copy_cap(log, logcap, "out of memory");
            return -1;
        }
        argv = heap_argv;
        if (push_arg(argv, &argc, cap, (char *)compiler) != 0 ||
            push_arg(argv, &argc, cap, "-std=c11") != 0 ||
            push_arg(argv, &argc, cap, "-Wall") != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)runtime_dir) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)third_dir) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)out_dir) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, lvroot) != 0 ||
            push_arg(argv, &argc, cap, "-DLV_CONF_INCLUDE_SIMPLE") != 0 ||
            push_arg(argv, &argc, cap, defbuf) != 0 ||
            push_arg(argv, &argc, cap, "-o") != 0 ||
            push_arg(argv, &argc, cap, binpath) != 0 ||
            push_arg(argv, &argc, cap, mpath) != 0 ||
            push_arg(argv, &argc, cap, cpath) != 0)
        {
            free(heap_argv);
            lv_list_free(&lvsrc);
            if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
            return -1;
        }
        for (si = 0; si < lvsrc.n; si++) {
            if (push_arg(argv, &argc, cap, lvsrc.obj[si]) != 0) {
                free(heap_argv);
                lv_list_free(&lvsrc);
                if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
                return -1;
            }
        }
        if (push_arg(argv, &argc, cap, "-lX11") != 0 ||
            push_arg(argv, &argc, cap, "-lpthread") != 0 ||
            push_arg(argv, &argc, cap, "-lm") != 0 ||
            push_arg(argv, &argc, cap, NULL) != 0)
        {
            free(heap_argv);
            lv_list_free(&lvsrc);
            if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
            return -1;
        }
    } else {
        if (push_arg(argv, &argc, cap, (char *)compiler) != 0 ||
            push_arg(argv, &argc, cap, "-std=c11") != 0 ||
            push_arg(argv, &argc, cap, "-Wall") != 0 ||
            push_arg(argv, &argc, cap, "-o") != 0 ||
            push_arg(argv, &argc, cap, binpath) != 0 ||
            push_arg(argv, &argc, cap, mpath) != 0 ||
            push_arg(argv, &argc, cap, cpath) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)runtime_dir) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)third_dir) != 0 ||
            push_arg(argv, &argc, cap, "-I") != 0 ||
            push_arg(argv, &argc, cap, (char *)out_dir) != 0 ||
            push_arg(argv, &argc, cap, defbuf) != 0)
        {
            if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
            return -1;
        }
        if (def == 1) {
            if (push_arg(argv, &argc, cap, "-I") != 0 ||
                push_arg(argv, &argc, cap, "/usr/include/freetype2") != 0 ||
                push_arg(argv, &argc, cap, "-lX11") != 0 ||
                push_arg(argv, &argc, cap, "-lXft") != 0 ||
                push_arg(argv, &argc, cap, "-lm") != 0) {
                if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
                return -1;
            }
        } else {
            if (tr_path_join(uidir, sizeof uidir, third_dir, "libui") != 0 ||
                tr_path_join(uisrc, sizeof uisrc, uidir, "ui_x11.c") != 0 ||
                access(uisrc, R_OK) != 0) {
                if (log && logcap) tr_copy_cap(log, logcap, "ui.h not found in third_party/libui");
                return -1;
            }
            if (push_arg(argv, &argc, cap, "-I") != 0 ||
                push_arg(argv, &argc, cap, uidir) != 0 ||
                push_arg(argv, &argc, cap, uisrc) != 0 ||
                push_arg(argv, &argc, cap, "-lX11") != 0 ||
                push_arg(argv, &argc, cap, "-lm") != 0) {
                if (log && logcap) tr_copy_cap(log, logcap, "could not assemble the compiler command");
                return -1;
            }
        }
        if (push_arg(argv, &argc, cap, NULL) != 0) return -1;
    }
    if (tr_spawn_wait(argv, out_dir, log, logcap, &code, timeout_ms) != 0 || code != 0) {
        free(heap_argv);
        lv_list_free(&lvsrc);
        if (log && logcap && log[0] == '\0')
            tr_copy_cap(log, logcap, "could not start the compiler");
        return -1;
    }
    free(heap_argv);
    lv_list_free(&lvsrc);
    heap_argv = NULL;
    if (also_run) {
        char *runv[2];
        char logpath[TR_PATH_MAX];
        if (tr_path_join(logpath, sizeof logpath, out_dir, "run.log") != 0) return -1;
        runv[0] = binpath;
        runv[1] = NULL;
        if (!child || tr_spawn_async(runv, out_dir, logpath, child) != 0) return -1;
    }
    return 0;
}
