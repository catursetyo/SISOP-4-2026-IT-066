# Laporan Praktikum Modul 4 Sistem Operasi

**Nama:** Catur Setyo Ragil\
**NRP:** 5027251066\
**Kelas:** Sistem Operasi B\
**Kode Asisten:** SCRA

---

## Struktur Repository
```
.
├── soal_1/
│   └── kenz_rescue.c
├── soal_2/
│   ├── client.c
│   ├── Dockerfile
│   ├── fuse.c
│   └── server
└── soal_3/
    ├── Dockerfile
    ├── docker-compose.yml
    ├── entrypoint.sh
    ├── smb.conf
    └── logs/
        └── libraryit.log
```
## Pembahasan Soal

## Soal 1: Kenz Rescue

Pada soal nomor 1, dibuat sebuah filesystem berbasis FUSE bernama `kenz_rescue.c`. Filesystem ini akan melakukan mount terhadap sebuah direktori sumber, lalu menambahkan sebuah file virtual bernama `tujuan.txt`. File `tujuan.txt` tidak benar-benar berada di direktori sumber, tetapi dibentuk secara dinamis ketika user membuka atau membaca file tersebut.

File `tujuan.txt` berisi gabungan koordinat yang diambil dari file `1.txt` sampai `7.txt`. Setiap file sumber dibaca, lalu program mengambil bagian setelah marker `KOORD:` dan menggabungkannya ke dalam satu baris dengan awalan:

```text
Tujuan Mas Amba:
```

### Inisialisasi FUSE

Program menggunakan FUSE versi 31 dan menyimpan path absolut direktori sumber ke dalam variabel global `source_dir`.

```c
#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <limits.h>

static char source_dir[PATH_MAX];
```

`source_dir` digunakan sebagai root asli dari filesystem. Nantinya seluruh request dari mount point akan diterjemahkan ke direktori sumber tersebut.

### Deteksi File Virtual `tujuan.txt`

File `tujuan.txt` dibuat sebagai file khusus yang hanya muncul pada mount point FUSE.

```c
static int is_tujuan(const char *path) {
    return strcmp(path, "/tujuan.txt") == 0;
}
```

Fungsi ini digunakan pada beberapa operasi FUSE seperti `getattr`, `open`, dan `read`. Jika path yang diminta adalah `/tujuan.txt`, maka program tidak akan mencari file tersebut di direktori asli, melainkan membuat kontennya secara dinamis.

### Membentuk Path Asli

```c
static void build_path(char fpath[PATH_MAX], const char *path) {
    snprintf(fpath, PATH_MAX, "%s%s", source_dir, path);
}
```

Fungsi `build_path()` digunakan untuk menggabungkan `source_dir` dengan path yang diminta dari mount point. Misalnya, jika user membuka `/1.txt`, maka path tersebut akan diubah menjadi:

```text
/source/directory/1.txt
```

Dengan cara ini, filesystem FUSE tetap mengarah ke data asli yang berada di direktori sumber.

### Membaca Isi File Sumber

```c
static char *read_whole_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, fp);
    buffer[size] = '\0';

    fclose(fp);
    return buffer;
}
```

Fungsi ini membaca seluruh isi file sumber ke dalam buffer. Isi file tersebut kemudian digunakan untuk mencari marker `KOORD:`.

### Mengambil Bagian Koordinat

```c
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
    memcpy(fragment, start, len);
    fragment[len] = '\0';

    return fragment;
}
```

Fungsi `extract_koord()` mencari string `KOORD:` dari isi file. Jika marker ditemukan, pointer akan digeser ke posisi setelah `KOORD:` lalu program mengambil teks sampai akhir baris. Bagian inilah yang dianggap sebagai fragmen koordinat.

### Membentuk Isi `tujuan.txt`

```c
static char *generate_tujuan(void) {
    size_t cap = 1024;
    size_t len = 0;

    char *result = malloc(cap);
    result[0] = '\0';

    append_text(&result, &len, &cap, "Tujuan Mas Amba: ");

    for (int i = 1; i <= 7; i++) {
        char filepath[PATH_MAX];

        snprintf(filepath, PATH_MAX, "%s/%d.txt", source_dir, i);

        char *content = read_whole_file(filepath);
        char *fragment = extract_koord(content);

        append_text(&result, &len, &cap, fragment);

        free(content);
        free(fragment);
    }

    append_text(&result, &len, &cap, "\n");

    return result;
}
```

Fungsi `generate_tujuan()` melakukan loop dari file `1.txt` sampai `7.txt`, mengambil fragmen koordinat dari masing-masing file, lalu menyatukannya dalam satu buffer. Buffer inilah yang akan dikembalikan ketika user membaca `tujuan.txt`.

### Menampilkan File Virtual pada Root Directory

```c
static int kenz_readdir(
    const char *path,
    void *buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info *fi,
    enum fuse_readdir_flags flags
) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    ...

    if (strcmp(path, "/") == 0) {
        struct stat st;
        memset(&st, 0, sizeof(st));

        st.st_mode = S_IFREG | 0444;
        st.st_nlink = 1;

        filler(buf, "tujuan.txt", &st, 0, 0);
    }

    return 0;
}
```

Pada fungsi `readdir`, program tetap menampilkan file dan folder asli dari direktori sumber. Namun jika path yang sedang dibaca adalah root `/`, program juga menambahkan file virtual `tujuan.txt`.

Permission file virtual dibuat `0444`, artinya hanya dapat dibaca dan tidak dapat ditulis.

### Membaca File Virtual

```c
static int kenz_read(
    const char *path,
    char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    if (is_tujuan(path)) {
        char *content = generate_tujuan();
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

    ...
}
```

Jika path yang dibaca adalah `/tujuan.txt`, maka isi file dibuat menggunakan `generate_tujuan()`. Program juga memperhatikan `offset` dan `size` agar pembacaan sesuai dengan mekanisme FUSE.

---

## Soal 2: MooFS dan Dockerized DB Server

Pada soal nomor 2, dibuat dua komponen utama, yaitu filesystem FUSE sederhana dan server database sederhana yang dapat dijalankan melalui Docker.

Komponen FUSE berada pada file `fuse.c`, sedangkan komponen server menggunakan binary `server`, file `client.c`, dan `Dockerfile`. Program FUSE bertugas menyediakan filesystem yang melakukan enkripsi sederhana terhadap file yang ditulis, sedangkan server database menyediakan command sederhana seperti `CREATE DATABASE`, `CREATE TABLE`, `INSERT`, `SELECT`, `LIST`, dan `DROP DATABASE`.

### Struktur File Soal 2

```
soal_2/
├── client.c
├── Dockerfile
├── fuse.c
└── server
```

### Filesystem Enkripsi pada `fuse.c`

Filesystem pada `fuse.c` menggunakan FUSE dan melakukan enkripsi sederhana menggunakan XOR. Setiap data yang ditulis akan di-XOR dengan key tertentu, lalu disimpan dengan ekstensi `.enc`.

```c
#define XOR_KEY 0x76

static char storage_root[PATH_MAX];
```

`XOR_KEY` digunakan untuk proses enkripsi dan dekripsi. Karena XOR bersifat simetris, proses yang sama dapat digunakan untuk mengubah plaintext menjadi ciphertext dan mengembalikan ciphertext menjadi plaintext.

### Menyembunyikan Ekstensi `.enc`

```c
static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;

    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) return 0;

    return strcmp(str + len_str - len_suffix, suffix) == 0;
}
```

Fungsi `ends_with()` digunakan untuk mengecek apakah file asli di storage memiliki ekstensi `.enc`. Pada saat ditampilkan melalui mount point, ekstensi tersebut akan disembunyikan.

```c
if (ends_with(shown_name, ".enc")) {
    shown_name[strlen(shown_name) - 4] = '\0';
}
```

Dengan demikian, file yang secara fisik tersimpan sebagai:

```text
catatan.txt.enc
```

akan terlihat oleh user sebagai:

```text
catatan.txt
```

### Membentuk Path Plain dan Encrypted

```c
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
```

Fungsi `make_plain_path()` digunakan untuk mengakses file biasa, sedangkan `make_enc_path()` digunakan untuk mengakses file terenkripsi dengan tambahan ekstensi `.enc`.

### Resolusi File yang Sudah Ada

```c
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
```

Fungsi ini mencoba mencari file dalam dua bentuk, yaitu path biasa dan path terenkripsi. Jika file biasa tidak ditemukan, maka program mencoba mencari file dengan ekstensi `.enc`.

### Membaca File dengan Dekripsi

```c
static int moo_read(
    const char *path,
    char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        return -ENOENT;
    }

    int fd = open(fpath, O_RDONLY);
    int res = pread(fd, buf, size, offset);

    for (int i = 0; i < res; i++) {
        buf[i] ^= XOR_KEY;
    }

    close(fd);
    return res;
}
```

Ketika user membaca file dari mount point, program membaca isi file asli dari storage, lalu setiap byte di-XOR dengan `XOR_KEY`. Dengan demikian, user akan melihat isi file dalam bentuk asli walaupun file yang tersimpan di storage dalam bentuk terenkripsi.

### Menulis File dengan Enkripsi

```c
static int moo_write(
    const char *path,
    const char *buf,
    size_t size,
    off_t offset,
    struct fuse_file_info *fi
) {
    char fpath[PATH_MAX];

    if (resolve_existing_path(path, fpath) != 0) {
        make_enc_path(fpath, path);
    }

    int fd = open(fpath, O_WRONLY | O_CREAT, 0644);

    char *enc_buf = malloc(size);

    for (size_t i = 0; i < size; i++) {
        enc_buf[i] = buf[i] ^ XOR_KEY;
    }

    int res = pwrite(fd, enc_buf, size, offset);

    free(enc_buf);
    close(fd);
    return res;
}
```

Saat user menulis file melalui mount point, isi file akan dienkripsi terlebih dahulu sebelum disimpan ke storage. File baru akan disimpan dalam bentuk `.enc`.

### Operasi FUSE yang Digunakan

```c
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
```

Struktur `fuse_operations` berisi daftar operasi yang akan ditangani oleh filesystem. Pada implementasi ini, filesystem mendukung operasi dasar seperti membaca, menulis, membuat file, menghapus file, membuat direktori, dan membaca isi direktori.

### Menentukan Storage Root

```c
int main(int argc, char *argv[]) {
    umask(0);

    const char *base = getenv("MOO_STORAGE");
    if (!base) base = "encrypted_storage";

    if (access(base, F_OK) == -1) {
        mkdir(base, 0755);
    }

    realpath(base, storage_root);

    return fuse_main(argc, argv, &moo_oper, NULL);
}
```

Program mengambil path storage dari environment variable `MOO_STORAGE`. Jika tidak ada, maka default storage yang digunakan adalah `encrypted_storage`.

### Dockerized DB Server

Selain FUSE, terdapat server database sederhana yang dijalankan melalui Docker. Server berjalan pada port `9000` dan menyimpan data pada direktori `/app/db`.

Dari hasil pengecekan command pada server, fitur yang tersedia antara lain:

```text
HELP
CREATE DATABASE <db>
CREATE TABLE <db> <table> <col1> <col2> ...
INSERT <db> <table> <value1> <value2> ...
SELECT <db> <table>
DELETE <db> <table> <key>
UPDATE <db> <table> <old> <new>
LIST DATABASE
LIST TABLE <db>
DROP DATABASE <db>
```

### Dockerfile

```dockerfile
FROM ubuntu:latest

WORKDIR /app

COPY server /app/server

RUN chmod +x /app/server

EXPOSE 9000

CMD ["./server"]
```

Dockerfile tersebut menggunakan `ubuntu:latest` sebagai base image, menyalin binary `server` ke `/app/server`, memberikan permission executable, membuka port `9000`, lalu menjalankan server.

### Client TCP

File `client.c` digunakan sebagai client TCP untuk berkomunikasi dengan server. Client menggunakan host default `127.0.0.1` dan port default `9000`.

```c
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9000
#define BUFFER_SIZE 4096
```

Client membuat socket, menghubungkannya ke server, lalu menggunakan `select()` agar input dari terminal dan response dari server dapat dibaca secara bersamaan.

```c
FD_SET(STDIN_FILENO, &readfds);
FD_SET(sockfd, &readfds);

int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
```

Jika ada input dari terminal, client akan mengirimkan input tersebut ke server. Jika ada response dari server, client akan menampilkan response ke terminal.

Contoh command:

```text
HELP
CREATE DATABASE sisop
CREATE TABLE sisop mahasiswa nrp nama
INSERT sisop mahasiswa 5027251066 Catur
SELECT sisop mahasiswa
LIST DATABASE
LIST TABLE sisop
DROP DATABASE sisop
```

---

## Soal 3: LibraryIT

Pada soal nomor 3, diminta untuk membangun infrastruktur file server **LibraryIT** menggunakan Docker dan Samba. Server harus menyediakan empat koleksi utama, yaitu `ebooks`, `papers`, `sourcecode`, dan `docs`, yang semuanya berada pada direktori `/libraryit/` di dalam container.

Selain itu, server harus memiliki tiga user Samba:

| User | Password | Grup |
| --- | --- | --- |
| `member` | `member123` | `readonly` |
| `contributor` | `contrib456` | `staff` |
| `librarian` | `lib789` | `staff` |

Ketentuan akses yang harus dipenuhi:

- `ebooks` dan `papers` dapat dibaca oleh `staff` dan `readonly`, tetapi hanya `staff` yang dapat menulis.
- `sourcecode` hanya dapat diakses oleh `staff`, tetapi tidak boleh ditulis melalui Samba.
- `docs` dapat dibaca oleh semua grup, tetapi hanya `librarian` yang dapat menulis melalui Samba.
- Semua koleksi harus persisten di host melalui bind mount.
- Folder `sourcecode` pada host harus memiliki permission `750`.
- Folder `docs` tidak boleh bisa ditulis langsung dari host oleh user biasa.
- Semua aktivitas Samba dicatat ke `logs/libraryit.log` dan dapat dipantau melalui service `libraryit-logger`.

### Struktur File Soal 3

```
soal_3/
├── Dockerfile
├── docker-compose.yml
├── entrypoint.sh
├── smb.conf
├── data/
│   ├── docs/
│   ├── ebooks/
│   ├── papers/
│   └── sourcecode/
└── logs/
    └── libraryit.log
```

### Dockerfile

```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    samba \
    samba-vfs-modules \
    smbclient \
    rsyslog \
    python3 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /libraryit /logs /var/log/samba /run/samba /var/run/samba /var/cache/samba /var/lib/samba/private

COPY smb.conf /etc/samba/smb.conf
COPY entrypoint.sh /entrypoint.sh

RUN python3 -c "from pathlib import Path; p=Path('/entrypoint.sh'); s=p.read_bytes(); s=s.replace(b'\xef\xbb\xbf', b''); s=s.replace(b'\r\n', b'\n').replace(b'\r', b'\n'); p.write_bytes(s)"
RUN chmod +x /entrypoint.sh

EXPOSE 445

ENTRYPOINT ["/bin/bash", "/entrypoint.sh"]
```

Dockerfile menggunakan `ubuntu:22.04` sebagai base image karena Samba, `smbclient`, `rsyslog`, dan `python3` dapat diinstall langsung melalui `apt`.

Bagian berikut digunakan untuk menginstall dependency:

```dockerfile
RUN apt-get update && apt-get install -y \
    samba \
    samba-vfs-modules \
    smbclient \
    rsyslog \
    python3 \
    && rm -rf /var/lib/apt/lists/*
```

Package `samba` digunakan sebagai file server, `samba-vfs-modules` digunakan agar modul audit Samba tersedia, `smbclient` digunakan untuk pengujian dari client, `rsyslog` digunakan untuk menerima audit log, dan `python3` digunakan untuk parser log.

```dockerfile
ENTRYPOINT ["/bin/bash", "/entrypoint.sh"]
```

`ENTRYPOINT` dijalankan melalui `/bin/bash` agar script tetap bisa dieksekusi meskipun terdapat masalah shebang atau format line ending.

### docker-compose.yml

```yaml
services:
  libraryit-server:
    build: .
    container_name: libraryit-server
    ports:
      - "1445:445"
    volumes:
      - ./data:/libraryit:z
      - ./logs:/logs:z
    restart: unless-stopped

  libraryit-logger:
    image: alpine:3.20
    container_name: libraryit-logger
    depends_on:
      - libraryit-server
    volumes:
      - ./logs:/logs:z
    command: sh -c "touch /logs/libraryit.log || true; chmod 666 /logs/libraryit.log || true; exec tail -f /logs/libraryit.log"
    restart: unless-stopped
```

Terdapat dua service:

1. `libraryit-server`, yaitu container utama yang menjalankan Samba.
2. `libraryit-logger`, yaitu container kecil berbasis Alpine yang melakukan `tail -f` terhadap file log.

Port Samba di dalam container adalah `445`, lalu dipetakan ke port `1445` pada host:

```yaml
ports:
  - "1445:445"
```

Bind mount digunakan agar data tetap berada di host:

```yaml
volumes:
  - ./data:/libraryit:z
  - ./logs:/logs:z
```

Opsi `:z` digunakan agar direktori dapat dipakai bersama oleh lebih dari satu container, terutama ketika environment membatasi akses bind mount.

### Pembuatan User dan Grup

Pada `entrypoint.sh`, dibuat fungsi `create_group()` dan `create_user()`.

```bash
create_group() {
  local group_name="$1"

  if ! getent group "$group_name" >/dev/null; then
    groupadd "$group_name"
  fi
}
```

Fungsi `create_group()` mengecek apakah grup sudah ada. Jika belum ada, maka grup akan dibuat menggunakan `groupadd`.

```bash
create_user() {
  local username="$1"
  local password="$2"
  local extra_group="$3"
  local uid="$4"

  if ! id "$username" >/dev/null 2>&1; then
    useradd -m -u "$uid" -s /usr/sbin/nologin "$username"
  fi

  usermod -aG "$extra_group" "$username"

  if pdbedit -L 2>/dev/null | cut -d: -f1 | grep -qx "$username"; then
    printf '%s\n%s\n' "$password" "$password" | smbpasswd -s "$username" >/dev/null
  else
    printf '%s\n%s\n' "$password" "$password" | smbpasswd -a -s "$username" >/dev/null
  fi

  smbpasswd -e "$username" >/dev/null
}
```

User dibuat sebagai user Linux terlebih dahulu karena Samba hanya dapat menambahkan user yang sudah ada di sistem. Setelah itu, user dimasukkan ke grup yang sesuai dan didaftarkan ke database Samba menggunakan `smbpasswd`.

```bash
create_group readonly
create_group staff

create_user member member123 readonly 1000
create_user contributor contrib456 staff 1001
create_user librarian lib789 staff 1002
```

### Inisialisasi Direktori

```bash
mkdir -p /libraryit/ebooks
mkdir -p /libraryit/papers
mkdir -p /libraryit/sourcecode
mkdir -p /libraryit/docs
mkdir -p /logs
mkdir -p /var/log/samba
mkdir -p /run/samba
mkdir -p /var/run/samba
mkdir -p /var/cache/samba
mkdir -p /var/lib/samba/private
```

Direktori `/libraryit` digunakan sebagai root koleksi. Sedangkan `/logs` digunakan sebagai tempat menyimpan `libraryit.log`.

### Permission Koleksi

```bash
chown root:staff /libraryit/ebooks || true
chmod 2775 /libraryit/ebooks || true

chown root:staff /libraryit/papers || true
chmod 2775 /libraryit/papers || true

chown root:staff /libraryit/sourcecode || true
chmod 0750 /libraryit/sourcecode || true

chown root:staff /libraryit/docs || true
chmod 2775 /libraryit/docs || true
```

Permission untuk `ebooks` dan `papers` dibuat `2775`. Angka `2` di depan merupakan setgid bit, sehingga file baru yang dibuat di dalam folder tersebut akan mengikuti grup folder, yaitu `staff`.

Folder `sourcecode` menggunakan permission `0750`, sehingga hanya owner dan grup yang dapat mengakses. Hal ini sesuai dengan ketentuan bahwa direktori `sourcecode` di host hanya dapat diakses oleh owner dan grup.

Folder `docs` dibuat `2775` agar user `librarian` dapat menulis melalui Samba, tetapi akses langsung dari host tetap dapat dibatasi karena folder dimiliki oleh `root:staff`.

### Konfigurasi Samba Global

```ini
[global]
   workgroup = WORKGROUP
   server string = LibraryIT Server
   security = user
   map to guest = never

   log file = /var/log/samba/log.%m
   max log size = 1000

   access based share enum = yes

   vfs objects = full_audit
   full_audit:prefix = %u|%S
   full_audit:success = connect disconnect open opendir read pread write pwrite mkdir rmdir unlink rename
   full_audit:failure = connect open opendir read pread write pwrite mkdir rmdir unlink rename
   full_audit:facility = LOCAL7
   full_audit:priority = NOTICE
```

`security = user` digunakan agar setiap akses Samba harus melalui autentikasi user. `access based share enum = yes` digunakan agar share yang tidak boleh diakses oleh suatu user tidak ditampilkan pada daftar share.

Modul `full_audit` digunakan untuk mencatat aktivitas Samba. Log audit dikirim ke facility `LOCAL7`, lalu diterima oleh `rsyslog`.

### Share `ebooks` dan `papers`

```ini
[ebooks]
   path = /libraryit/ebooks
   browseable = yes
   read only = yes
   valid users = @staff @readonly
   write list = @staff
   force group = staff
   create mask = 0664
   directory mask = 2775
```

```ini
[papers]
   path = /libraryit/papers
   browseable = yes
   read only = yes
   valid users = @staff @readonly
   write list = @staff
   force group = staff
   create mask = 0664
   directory mask = 2775
```

`valid users = @staff @readonly` membuat kedua grup dapat membaca koleksi. Namun `write list = @staff` membuat hanya grup `staff` yang dapat menulis.

### Share `sourcecode`

```ini
[sourcecode]
   path = /libraryit/sourcecode
   browseable = yes
   read only = yes
   valid users = @staff
   force group = staff
   create mask = 0640
   directory mask = 0750
```

Share `sourcecode` hanya dapat diakses oleh grup `staff`. Karena `read only = yes` dan tidak ada `write list`, maka user `contributor` maupun `librarian` tidak dapat menulis ke share ini melalui Samba.

### Share `docs`

```ini
[docs]
   path = /libraryit/docs
   browseable = yes
   read only = yes
   valid users = @staff @readonly
   write list = librarian
   force group = staff
   create mask = 0664
   directory mask = 2775
```

Share `docs` dapat dibaca oleh `staff` dan `readonly`, tetapi hanya user `librarian` yang berada pada `write list`, sehingga hanya `librarian` yang dapat menulis.

### Parser Log

Log mentah dari Samba ditulis ke `/var/log/samba/audit.raw`, lalu diproses oleh parser Python yang dijalankan dari `entrypoint.sh`.

```bash
touch /logs/libraryit.log || true
chmod 0666 /logs/libraryit.log || true

touch /var/log/samba/audit.raw || true
chmod 0666 /var/log/samba/audit.raw || true
```

File `libraryit.log` dibuat di folder `/logs`, sehingga dapat dibaca oleh container `libraryit-logger` dan juga terlihat dari host melalui bind mount.

```bash
cat >/etc/rsyslog.d/50-samba-audit.conf <<'RSYSLOG'
local7.*    /var/log/samba/audit.raw
& stop
RSYSLOG
```

Konfigurasi tersebut membuat semua log dari facility `local7` masuk ke file `audit.raw`.

Parser Python kemudian membaca `audit.raw` dan menulis ulang log ke format yang diminta soal:

```text
[YYYY-MM-DD HH:MM:SS] [LEVEL] [USERNAME] [AKSI] [NAMA FILE/SHARE]
```

Aktivitas normal seperti `CONNECT` dan `WRITE` dicatat sebagai `INFO`, sedangkan percobaan akses yang gagal dicatat sebagai `WARNING`.

### Pengujian Sub-soal A

Sub-soal A menguji apakah user, grup, dan struktur direktori berhasil dibuat otomatis.

```bash
docker exec -it libraryit-server pdbedit -L
docker exec -it libraryit-server getent group staff readonly
docker exec -it libraryit-server ls /libraryit/
```

Output yang diharapkan:

```text
member:1000:
librarian:1002:
contributor:1001:
staff:x:50:contributor,librarian
readonly:x:1000:member
docs ebooks papers sourcecode
```

Perintah `pdbedit -L` digunakan untuk memastikan user Samba sudah terdaftar. Perintah `getent group staff readonly` digunakan untuk memastikan pembagian grup sudah benar. Perintah `ls /libraryit/` digunakan untuk memastikan empat direktori koleksi sudah tersedia.

### Pengujian Sub-soal B

Sub-soal B menguji aturan akses Samba untuk masing-masing koleksi.

Cek daftar share sebagai `member`:

```bash
smbclient -L //localhost -p 1445 -U member%member123
```

Pada output, `member` seharusnya dapat melihat `ebooks`, `papers`, dan `docs`, tetapi tidak dapat mengakses `sourcecode`.

Tes akses `sourcecode` sebagai `member`:

```bash
smbclient //localhost/sourcecode -p 1445 -U member%member123
```

Output yang diharapkan:

```text
tree connect failed: NT_STATUS_ACCESS_DENIED
```

Tes menulis ke `docs` sebagai `contributor`:

```bash
echo "test" > test.txt
smbclient //localhost/docs -p 1445 -U contributor%contrib456 -c "put test.txt"
```

Output yang diharapkan:

```text
NT_STATUS_ACCESS_DENIED opening remote file \test.txt
```

Tes menulis ke `docs` sebagai `librarian`:

```bash
smbclient //localhost/docs -p 1445 -U librarian%lib789 -c "put test.txt"
```

Output yang diharapkan:

```text
putting file test.txt as \test.txt
```

Dengan pengujian tersebut, terbukti bahwa `contributor` yang termasuk grup `staff` tetap tidak dapat menulis ke `docs`, sedangkan `librarian` secara spesifik dapat menulis karena berada pada `write list`.

### Pengujian Sub-soal C

Sub-soal C menguji persistensi data dan permission pada host.

Cek struktur `data` pada host:

```bash
ls -la ./data/
```

Cek permission `sourcecode`:

```bash
ls -ld ./data/sourcecode
```

Output yang diharapkan:

```text
drwxr-x--- ... root staff ... ./data/sourcecode
```

Atau dapat dicek menggunakan:

```bash
stat -c "%a %U %G %n" ./data/sourcecode
```

Output yang diharapkan:

```text
750 root staff ./data/sourcecode
```

Tes apakah `docs` dapat ditulis langsung dari host:

```bash
touch ./data/docs/test_dari_host.txt
```

Output yang diharapkan:

```text
touch: cannot touch './data/docs/test_dari_host.txt': Permission denied
```

Tes apakah `sourcecode` dapat ditulis melalui Samba:

```bash
smbclient //localhost/sourcecode -p 1445 -U contributor%contrib456 -c "put /etc/hostname hello_world.py"
```

Output yang diharapkan:

```text
NT_STATUS_ACCESS_DENIED opening remote file \hello_world.py
```

Dengan demikian, data tetap berada di host melalui bind mount, tetapi akses langsung dari host dan akses melalui Samba tetap mengikuti aturan yang diminta.

### Pengujian Sub-soal D

Sub-soal D menguji pencatatan log aktivitas Samba.

Buka terminal pertama untuk memantau log:

```bash
docker logs -f libraryit-logger
```

Buka terminal kedua lalu lakukan beberapa aktivitas:

```bash
echo "test log" > test.txt

smbclient //localhost/ebooks -p 1445 -U contributor%contrib456 -c "put test.txt"

smbclient //localhost/docs -p 1445 -U contributor%contrib456 -c "put test.txt"

smbclient //localhost/docs -p 1445 -U librarian%lib789 -c "put test.txt"
```

Output log yang diharapkan:

```text
[2025-05-13 15:30:01] [INFO] [contributor] [WRITE] [test.txt]
[2025-05-13 15:30:12] [WARNING] [contributor] [DENIED] [test.txt]
[2025-05-13 15:30:25] [INFO] [librarian] [WRITE] [test.txt]
```

File log juga dapat dicek langsung dari host:

```bash
cat ./logs/libraryit.log
```

atau:

```bash
tail -f ./logs/libraryit.log
```

### Kendala dan Revisi

Selama pengerjaan, terdapat beberapa kendala yang perlu diperbaiki.

#### 1. Perintah `docker compose` tidak dikenali

Pada environment yang digunakan, perintah:

```bash
docker compose up --build -d
```

menghasilkan error:

```text
unknown flag: --build
```

Solusinya adalah menggunakan Docker Compose versi lama:

```bash
docker-compose up --build -d
```

#### 2. Permission denied pada bind mount

Container sempat restart karena tidak dapat mengubah permission folder `/libraryit` dan `/logs`.

```text
touch: cannot touch '/logs/libraryit.log': Permission denied
chown: changing ownership of '/libraryit/ebooks': Permission denied
```

Solusi yang diterapkan adalah:

```yaml
volumes:
  - ./data:/libraryit:z
  - ./logs:/logs:z
```

Selain itu, pada `entrypoint.sh`, beberapa perintah permission dibuat aman dengan `|| true` agar container tidak langsung berhenti ketika filesystem host tidak mengizinkan `chown`.

#### 3. `exec format error` pada `entrypoint.sh`

Container sempat gagal dengan error:

```text
exec /entrypoint.sh: exec format error
```

Masalah ini disebabkan oleh format line ending atau shebang yang tidak terbaca. Solusinya adalah menjalankan entrypoint melalui `/bin/bash` dan membersihkan line ending pada Dockerfile.

```dockerfile
ENTRYPOINT ["/bin/bash", "/entrypoint.sh"]
```

#### 4. `librarian` tidak bisa menulis ke `docs`

Awalnya folder `docs` memiliki permission yang tidak memberikan write permission ke grup, sehingga `librarian` terkena `NT_STATUS_ACCESS_DENIED`.

Solusi:

```bash
chmod 2775 /libraryit/docs
```

dan pada `entrypoint.sh`:

```bash
chmod 2775 /libraryit/docs || true
```

#### 5. Samba berhenti setelah koneksi

Saat menggunakan:

```bash
exec smbd -i
```

container dapat berhenti setelah koneksi `smbclient`, sehingga muncul error `NT_STATUS_CONNECTION_DISCONNECTED`. Solusinya adalah menjalankan Samba dalam foreground daemon mode:

```bash
exec smbd -F --no-process-group
```

### Membersihkan Repository Sebelum Commit

Karena folder `data/` dan `logs/` merupakan hasil runtime, keduanya sebaiknya tidak ikut di-commit.

Contoh `.gitignore`:

```gitignore
soal_3/data/
soal_3/logs/
soal_3/test.txt
```

Jika folder tersebut sudah pernah masuk tracking Git, hapus dari tracking tanpa menghapus file lokal:

```bash
git rm -r --cached soal_3/data soal_3/logs 2>/dev/null || true
git add .
```
