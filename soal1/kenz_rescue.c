#define FUSE_USE_VERSION 28
#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>


static int extract_koord(const char *filepath, char *out, size_t outlen)
{
    FILE *fp = fopen(filepath, "r");
    if (!fp) return -1;

    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "KOORD: ", 7) == 0) {
            char *fragment = line + 7;
            size_t len = strlen(fragment);
            if (len > 0 && fragment[len - 1] == '\n')
                fragment[len - 1] = '\0';
            strncpy(out, fragment, outlen - 1);
            out[outlen - 1] = '\0';
            found = strlen(out);
            break;
        }
    }
    fclose(fp);
    return found;
}

static size_t build_tujuan_content(char *buf, size_t buflen)
{
    char fragment[1024];
    char fpath[64];
    size_t pos = 0;
    const char *prefix = "Tujuan Mas Amba: ";
    size_t prefix_len = strlen(prefix);
    int i;

    if (pos + prefix_len < buflen) {
        memcpy(buf + pos, prefix, prefix_len);
        pos += prefix_len;
    }

    for (i = 1; i <= 7; i++) {
        sprintf(fpath, "amba_files/%d.txt", i);
        int fraglen = extract_koord(fpath, fragment, sizeof(fragment));
        if (fraglen > 0 && pos + (size_t)fraglen < buflen) {
            memcpy(buf + pos, fragment, fraglen);
            pos += fraglen;
        }
        if (i < 7 && pos + 1 < buflen) {
            buf[pos++] = ',';
        }
    }

    if (pos + 1 < buflen)
        buf[pos++] = '\n';
    buf[pos] = '\0';
    return pos;
}

static size_t compute_tujuan_size(void)
{
    char tmp[4096];
    return build_tujuan_content(tmp, sizeof(tmp));
}

static int kenz_getattr(const char *path, struct stat *stbuf)
{
    if (strcmp(path, "/tujuan.txt") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = compute_tujuan_size();
        return 0;
    }

    char fpath[1024];
    sprintf(fpath, "amba_files%s", path);

    if (lstat(fpath, stbuf) == -1)
        return -errno;
    return 0;
}

static int kenz_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    char fpath[1024];
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    sprintf(fpath, "amba_files%s", path);

    dp = opendir(fpath);
    if (dp == NULL)
        return -errno;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        filler(buf, de->d_name, &st, 0);
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFREG | 0444;
    filler(buf, "tujuan.txt", &st, 0);

    closedir(dp);
    return 0;
}

static int kenz_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void) fi;

    if (strcmp(path, "/tujuan.txt") == 0) {
        static char content[4096];
        static size_t content_len = 0;
        static int built = 0;

        if (!built) {
            content_len = build_tujuan_content(content, sizeof(content));
            built = 1;
        }

        if (offset >= (off_t)content_len)
            return 0;

        if (offset + size > content_len)
            size = content_len - offset;

        memcpy(buf, content + offset, size);
        return size;
    }

    char fpath[1024];
    int fd;
    int res;

    sprintf(fpath, "amba_files%s", path);

    fd = open(fpath, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static struct fuse_operations kenz_oper = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .read    = kenz_read,
};

int main(int argc, char *argv[])
{
    umask(0);
    return fuse_main(argc, argv, &kenz_oper, NULL);
}
