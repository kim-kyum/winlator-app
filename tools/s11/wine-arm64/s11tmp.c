/*
 * libs11tmp.so: "/tmp" for a Wine built for Linux, inside S11 Workspace's rootfs on Android.
 *
 * Android has no /tmp. Wine's Linux builds keep the wineserver's socket under
 * /tmp/.wine-<uid>/server-<dev>-<ino> (Winlator's own Wine has the rootfs's path compiled in
 * instead). Preloaded into the ARM64 Wine's processes (LD_PRELOAD), this sends every path
 * Wine gives to these calls under /tmp to $TMPDIR (the rootfs's tmp), and leaves the others.
 *
 * Calls inside glibc (fopen's open, for one) are not seen; Wine's server directory goes through
 * mkdir, lstat, stat, chdir, open and unlink, which are.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *tmp_dir(void)
{
    static const char *dir;
    if (!dir) {
        const char *env = getenv("TMPDIR");
        dir = env && env[0] == '/' ? env : "/tmp";
    }
    return dir;
}

/* "/tmp" or "/tmp/..." becomes "$TMPDIR..." (in buf); any other path is returned as it is */
static const char *fix(const char *path, char *buf, size_t size)
{
    if (!path || strncmp(path, "/tmp", 4) != 0 || (path[4] != '/' && path[4] != '\0')) return path;
    const char *dir = tmp_dir();
    size_t len = strlen(dir);
    if (strcmp(dir, "/tmp") == 0 || strncmp(path, dir, len) == 0) return path;
    int n = snprintf(buf, size, "%s%s", dir, path + 4);
    return n > 0 && (size_t)n < size ? buf : path;
}

#define REAL(name) \
    static __typeof__(name) *real; \
    if (!real) real = (__typeof__(name) *)dlsym(RTLD_NEXT, #name)

int mkdir(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    REAL(mkdir);
    return real(fix(path, buf, sizeof(buf)), mode);
}

int rmdir(const char *path)
{
    char buf[PATH_MAX];
    REAL(rmdir);
    return real(fix(path, buf, sizeof(buf)));
}

int chdir(const char *path)
{
    char buf[PATH_MAX];
    REAL(chdir);
    return real(fix(path, buf, sizeof(buf)));
}

int unlink(const char *path)
{
    char buf[PATH_MAX];
    REAL(unlink);
    return real(fix(path, buf, sizeof(buf)));
}

int access(const char *path, int mode)
{
    char buf[PATH_MAX];
    REAL(access);
    return real(fix(path, buf, sizeof(buf)), mode);
}

int stat(const char *path, struct stat *st)
{
    char buf[PATH_MAX];
    REAL(stat);
    return real(fix(path, buf, sizeof(buf)), st);
}

int lstat(const char *path, struct stat *st)
{
    char buf[PATH_MAX];
    REAL(lstat);
    return real(fix(path, buf, sizeof(buf)), st);
}

int stat64(const char *path, struct stat64 *st)
{
    char buf[PATH_MAX];
    REAL(stat64);
    return real(fix(path, buf, sizeof(buf)), st);
}

int lstat64(const char *path, struct stat64 *st)
{
    char buf[PATH_MAX];
    REAL(lstat64);
    return real(fix(path, buf, sizeof(buf)), st);
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    char buf[PATH_MAX];
    REAL(fstatat);
    return real(dirfd, fix(path, buf, sizeof(buf)), st, flags);
}

int fstatat64(int dirfd, const char *path, struct stat64 *st, int flags)
{
    char buf[PATH_MAX];
    REAL(fstatat64);
    return real(dirfd, fix(path, buf, sizeof(buf)), st, flags);
}

static mode_t mode_arg(int flags, va_list ap)
{
    return (flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE ? (mode_t)va_arg(ap, int) : 0;
}

int open(const char *path, int flags, ...)
{
    char buf[PATH_MAX];
    va_list ap;
    va_start(ap, flags);
    mode_t mode = mode_arg(flags, ap);
    va_end(ap);
    REAL(open);
    return real(fix(path, buf, sizeof(buf)), flags, mode);
}

int open64(const char *path, int flags, ...)
{
    char buf[PATH_MAX];
    va_list ap;
    va_start(ap, flags);
    mode_t mode = mode_arg(flags, ap);
    va_end(ap);
    REAL(open64);
    return real(fix(path, buf, sizeof(buf)), flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...)
{
    char buf[PATH_MAX];
    va_list ap;
    va_start(ap, flags);
    mode_t mode = mode_arg(flags, ap);
    va_end(ap);
    REAL(openat);
    return real(dirfd, fix(path, buf, sizeof(buf)), flags, mode);
}

DIR *opendir(const char *path)
{
    char buf[PATH_MAX];
    REAL(opendir);
    return real(fix(path, buf, sizeof(buf)));
}
