#define _GNU_SOURCE
#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define XOR_KEY 0x76

static char storage_root[PATH_MAX];

static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;

    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) return 0;

    return strcmp(str + len_str - len_suffix, suffix) == 0;
}

static void make_plain_path(char out[PATH_MAX], const char *path) {
    if (strcmp(path, "/") == 0) {
        snprintf(out, PATH_MAX, "%s", storage_root);
    } else {
        snprintf(out, PATH_MAX, "%s%s", storage_root, path);
    }
}

static void make_enc_path(char out[PATH_MAX], const char *path) {
    snprintf(out, PATH_MAX, "%s%s.enc", storage_root, path);
}

static int resolve_existing_path(const char *path, char out[PATH_MAX]) {
    struct stat st;

    make_plain_path(out, path);
    if (lstat(out, &st) == 0) {
        return 0;
    }

    make_enc_path(out, path);
    if (lstat(out, &st) == 0) {
        return 0;
    }

    return -ENOENT;
}

static int moo_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    int res;
    char fpath[PATH_MAX];

    memset(stbuf, 0, sizeof(struct stat));

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    res = lstat(fpath, stbuf);
    if (res == -1) return -errno;

    return 0;
}

static int moo_access(const char *path, int mask) {
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int res = access(fpath, mask);
    if (res == -1) return -errno;

    return 0;
}

static int moo_readdir(
    const char *path,
    void *buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    (void) flags;
    (void) offset;
    (void) fi;

    char fpath[PATH_MAX];
    make_plain_path(fpath, path);

    DIR *dp = opendir(fpath);
    if (dp == NULL) return -errno;

    struct dirent *de;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        char shown_name[NAME_MAX + 1];
        char real_child[PATH_MAX];

        memset(&st, 0, sizeof(st));

        snprintf(real_child, PATH_MAX, "%s/%s", fpath, de->d_name);

        if (lstat(real_child, &st) == -1) {
            continue;
        }

        snprintf(shown_name, sizeof(shown_name), "%s", de->d_name);

        if (ends_with(shown_name, ".enc")) {
            shown_name[strlen(shown_name) - 4] = '\0';
        }

        if (filler(buf, shown_name, &st, 0, 0)) break;
    }

    closedir(dp);
    return 0;
}

static int moo_mkdir(const char *path, mode_t mode) {
    char fpath[PATH_MAX];
    make_plain_path(fpath, path);

    int res = mkdir(fpath, mode);
    if (res == -1) return -errno;

    return 0;
}

static int moo_rmdir(const char *path) {
    char fpath[PATH_MAX];
    make_plain_path(fpath, path);

    int res = rmdir(fpath);
    if (res == -1) return -errno;

    return 0;
}

static int moo_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char fpath[PATH_MAX];
    make_enc_path(fpath, path);

    int fd = open(fpath, fi->flags | O_CREAT, mode);
    if (fd == -1) return -errno;

    close(fd);
    return 0;
}

static int moo_open(const char *path, struct fuse_file_info *fi) {
    (void) fi;

    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    close(fd);
    return 0;
}

static int moo_read(
    const char *path,
    char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    (void) fi;

    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1) {
        int err = errno;
        close(fd);
        return -err;
    }

    for (int i = 0; i < res; i++) {
        buf[i] ^= XOR_KEY;
    }

    close(fd);
    return res;
}

static int moo_write(
    const char *path,
    const char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    (void) fi;

    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        make_enc_path(fpath, path);
    }

    int fd = open(fpath, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) return -errno;

    char *enc_buf = malloc(size);
    if (!enc_buf) {
        close(fd);
        return -ENOMEM;
    }

    for (size_t i = 0; i < size; i++) {
        enc_buf[i] = buf[i] ^ XOR_KEY;
    }

    int res = pwrite(fd, enc_buf, size, offset);
    if (res == -1) {
        int err = errno;
        free(enc_buf);
        close(fd);
        return -err;
    }

    free(enc_buf);
    close(fd);
    return res;
}

static int moo_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        make_enc_path(fpath, path);
    }

    int res = truncate(fpath, size);
    if (res == -1) return -errno;

    return 0;
}

static int moo_unlink(const char *path) {
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int res = unlink(fpath);
    if (res == -1) return -errno;

    return 0;
}

static int moo_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void) fi;
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int res = utimensat(AT_FDCWD, fpath, tv, AT_SYMLINK_NOFOLLOW);
    if (res == -1) return -errno;

    return 0;
}

static struct fuse_operations moo_oper = {
    .getattr = moo_getattr,
    .readdir = moo_readdir,
    .mkdir = moo_mkdir,
    .rmdir = moo_rmdir,
    .create = moo_create,
    .open = moo_open,
    .read = moo_read,
    .write = moo_write,
    .truncate = moo_truncate,
    .unlink = moo_unlink,
    .access = moo_access,
    .utimens = moo_utimens,
};

int main(int argc, char *argv[]) {
    umask(0);

    const char *base = getenv("MOO_STORAGE");
    if (!base) base = "encrypted_storage";

    if (access(base, F_OK) == -1) {
        if (mkdir(base, 0755) == -1) {
            perror("mkdir encrypted_storage");
            return 1;
        }
    }

    if (realpath(base, storage_root) == NULL) {
        perror("realpath encrypted_storage");
        return 1;
    }

    return fuse_main(argc, argv, &moo_oper, NULL);
}
