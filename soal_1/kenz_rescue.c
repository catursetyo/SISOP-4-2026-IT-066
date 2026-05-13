#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>

static char source_dir[PATH_MAX];

static int is_tujuan(const char *path) {
    return strcmp(path, "/tujuan.txt") == 0;
}

static void build_path(char fpath[PATH_MAX], const char *path) {
    snprintf(fpath, PATH_MAX, "%s%s", source_dir, path);
}

static char *read_whole_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    char *buffer = malloc(size + 1);
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buffer, 1, size, fp);
    buffer[n] = '\0';

    fclose(fp);
    return buffer;
}

static char *extract_koord(const char *content) {
    const char *marker = "KOORD:";
    char *start = strstr(content, marker);

    if (start == NULL) {
        return strdup("");
    }

    start += strlen(marker);

    while (*start == ' ' || *start == '\t') {
        start++;
    }

    const char *end = start;

    while (*end != '\0' && *end != '\n' && *end != '\r') {
        end++;
    }

    size_t len = end - start;

    char *fragment = malloc(len + 1);
    if (fragment == NULL) return NULL;

    memcpy(fragment, start, len);
    fragment[len] = '\0';

    return fragment;
}

static int append_text(char **result, size_t *len, size_t *cap, const char *text) {
    size_t text_len = strlen(text);

    if (*len + text_len + 1 > *cap) {
        while (*len + text_len + 1 > *cap) {
            *cap *= 2;
        }

        char *tmp = realloc(*result, *cap);
        if (tmp == NULL) return -1;

        *result = tmp;
    }

    memcpy(*result + *len, text, text_len);
    *len += text_len;
    (*result)[*len] = '\0';

    return 0;
}

static char *generate_tujuan(void) {
    size_t cap = 1024;
    size_t len = 0;

    char *result = malloc(cap);
    if (result == NULL) return NULL;

    result[0] = '\0';

    if (append_text(&result, &len, &cap, "Tujuan Mas Amba: ") == -1) {
        free(result);
        return NULL;
    }

    for (int i = 1; i <= 7; i++) {
        char filepath[PATH_MAX];

        snprintf(filepath, PATH_MAX, "%s/%d.txt", source_dir, i);

        char *content = read_whole_file(filepath);
        if (content == NULL) {
            free(result);
            return NULL;
        }

        char *fragment = extract_koord(content);
        free(content);

        if (fragment == NULL) {
            free(result);
            return NULL;
        }

        if (append_text(&result, &len, &cap, fragment) == -1) {
            free(fragment);
            free(result);
            return NULL;
        }

        free(fragment);
    }

    if (append_text(&result, &len, &cap, "\n") == -1) {
        free(result);
        return NULL;
    }

    return result;
}

static int kenz_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;

    memset(stbuf, 0, sizeof(struct stat));

    if (is_tujuan(path)) {
        char *content = generate_tujuan();
        if (content == NULL) return -ENOENT;

        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(content);

        free(content);
        return 0;
    }

    char fpath[PATH_MAX];
    build_path(fpath, path);

    int res = lstat(fpath, stbuf);

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int kenz_readdir(
    const char *path,
    void *buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    (void) offset;
    (void) fi;
    (void) flags;

    char fpath[PATH_MAX];
    build_path(fpath, path);

    DIR *dp = opendir(fpath);
    if (dp == NULL) {
        return -errno;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    struct dirent *de;

    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (strcmp(de->d_name, "tujuan.txt") == 0) {
            continue;
        }

        struct stat st;
        memset(&st, 0, sizeof(st));

        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        if (filler(buf, de->d_name, &st, 0, 0) != 0) {
            break;
        }
    }

    if (strcmp(path, "/") == 0) {
        struct stat st;
        memset(&st, 0, sizeof(st));

        st.st_mode = S_IFREG | 0444;
        st.st_nlink = 1;

        filler(buf, "tujuan.txt", &st, 0, 0);
    }

    closedir(dp);
    return 0;
}

static int kenz_open(const char *path, struct fuse_file_info *fi) {
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }

    if (is_tujuan(path)) {
        return 0;
    }

    char fpath[PATH_MAX];
    build_path(fpath, path);

    int fd = open(fpath, O_RDONLY);

    if (fd == -1) {
        return -errno;
    }

    close(fd);
    return 0;
}

static int kenz_read(
    const char *path,
    char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    (void) fi;

    if (is_tujuan(path)) {
        char *content = generate_tujuan();
        if (content == NULL) return -ENOENT;

        size_t len = strlen(content);

        if ((size_t) offset < len) {
            if ((size_t) offset + size > len) {
                size = len - offset;
            }

            memcpy(buf, content + offset, size);
        } else {
            size = 0;
        }

        free(content);
        return size;
    }

    char fpath[PATH_MAX];
    build_path(fpath, path);

    int fd = open(fpath, O_RDONLY);

    if (fd == -1) {
        return -errno;
    }

    int res = pread(fd, buf, size, offset);

    if (res == -1) {
        res = -errno;
    }

    close(fd);
    return res;
}

static struct fuse_operations kenz_oper = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .open    = kenz_open,
    .read    = kenz_read,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory> [FUSE options]\n", argv[0]);
        return 1;
    }

    if (realpath(argv[1], source_dir) == NULL) {
        perror("realpath");
        return 1;
    }

    /*
     * Hapus argumen source_directory sebelum masuk ke fuse_main.
     *
     * Contoh:
     * ./kenz_rescue amba_files mnt
     * menjadi:
     * ./kenz_rescue mnt
     *
     * Contoh debug:
     * ./kenz_rescue amba_files -f mnt
     * menjadi:
     * ./kenz_rescue -f mnt
     */
    for (int i = 1; i < argc - 1; i++) {
        argv[i] = argv[i + 1];
    }

    argc--;

    umask(0);
    return fuse_main(argc, argv, &kenz_oper, NULL);
}