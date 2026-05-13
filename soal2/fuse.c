#define FUSE_USE_VERSION 28
#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#define XOR_KEY 0x76

static char backend[1024] = "encrypted_storage";

static void xor_buf(char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] ^= XOR_KEY;
}

static void build_real_path(const char *path, char *out)
{
    char candidate[2048];
    struct stat st;

    sprintf(candidate, "%s%s", backend, path);

    if (lstat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
        strcpy(out, candidate);
        return;
    }

    sprintf(out, "%s%s.enc", backend, path);
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
    char fpath[2048];
    build_real_path(path, fpath);

    if (lstat(fpath, stbuf) == -1)
        return -errno;

    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    char fpath[2048];
    DIR *dp;
    struct dirent *de;
    (void) offset;
    (void) fi;

    sprintf(fpath, "%s%s", backend, path);

    dp = opendir(fpath);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        char name[256];
        strncpy(name, de->d_name, sizeof(name));
        name[sizeof(name) - 1] = '\0';

        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".enc") == 0)
            name[len - 4] = '\0';

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        filler(buf, name, &st, 0);
    }

    closedir(dp);
    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    char fpath[2048];
    sprintf(fpath, "%s%s", backend, path);

    if (mkdir(fpath, mode) == -1)
        return -errno;
    return 0;
}

static int xmp_rmdir(const char *path)
{
    char fpath[2048];
    sprintf(fpath, "%s%s", backend, path);

    if (rmdir(fpath) == -1)
        return -errno;
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    int fd = open(fpath, fi->flags | O_CREAT, mode);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    int fd = open(fpath, fi->flags);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    int fd;
    int opened_here = 0;

    if (fi != NULL && fi->fh > 0) {
        fd = fi->fh;
    } else {
        fd = open(fpath, O_RDONLY);
        opened_here = 1;
    }

    if (fd == -1)
        return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;
    else
        xor_buf(buf, res);

    if (opened_here)
        close(fd);

    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    int fd;
    int opened_here = 0;

    if (fi != NULL && fi->fh > 0) {
        fd = fi->fh;
    } else {
        fd = open(fpath, O_WRONLY);
        opened_here = 1;
    }

    if (fd == -1)
        return -errno;

    char *tmp = malloc(size);
    if (!tmp) {
        if (opened_here) close(fd);
        return -ENOMEM;
    }
    memcpy(tmp, buf, size);
    xor_buf(tmp, size);

    int res = pwrite(fd, tmp, size, offset);
    if (res == -1)
        res = -errno;

    free(tmp);
    if (opened_here)
        close(fd);

    return res;
}

static int xmp_truncate(const char *path, off_t size)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    if (truncate(fpath, size) == -1)
        return -errno;
    return 0;
}

static int xmp_unlink(const char *path)
{
    char fpath[2048];
    sprintf(fpath, "%s%s.enc", backend, path);

    if (unlink(fpath) == -1)
        return -errno;
    return 0;
}

static int xmp_access(const char *path, int mask)
{
    char fpath[2048];
    build_real_path(path, fpath);

    if (access(fpath, mask) == -1)
        return -errno;
    return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
    char fpath[2048];
    build_real_path(path, fpath);

    if (utimensat(AT_FDCWD, fpath, ts, AT_SYMLINK_NOFOLLOW) == -1)
        return -errno;
    return 0;
}

static struct fuse_operations xmp_oper = {
    .getattr  = xmp_getattr,
    .readdir  = xmp_readdir,
    .mkdir    = xmp_mkdir,
    .rmdir    = xmp_rmdir,
    .create   = xmp_create,
    .open     = xmp_open,
    .read     = xmp_read,
    .write    = xmp_write,
    .truncate = xmp_truncate,
    .unlink   = xmp_unlink,
    .access   = xmp_access,
    .utimens  = xmp_utimens,
};

int main(int argc, char *argv[])
{
    umask(0);

    char abs_backend[1024];
    if (realpath("encrypted_storage", abs_backend) == NULL) {
        fprintf(stderr, "ERROR: encrypted_storage directory not found in cwd: %s\n",
                strerror(errno));
        return 1;
    }
    strncpy(backend, abs_backend, sizeof(backend) - 1);
    backend[sizeof(backend) - 1] = '\0';

    return fuse_main(argc, argv, &xmp_oper, NULL);
}
