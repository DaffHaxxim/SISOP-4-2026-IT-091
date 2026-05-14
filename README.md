# Laporan Praktikum - Modul 4 Sistem Operasi

---

# Soal 1 - Save Asisten Kenz

## Penjelasan Singkat Masalah

Mas Amba sedang mencari Asisten Kenz yang hilang. Di tengah ekspedisinya, Mas Amba menemukan sebuah flashdisk berisi catatan harian (file `1.txt` sampai `7.txt`) yang tersimpan di dalam folder `amba_files/`. Untuk menemukan koordinat ritual yang bisa membawanya ke Asisten Kenz, Mas Amba harus menyelami isi flashdisk tersebut melalui sebuah filesystem virtual.

Tugasmu adalah membangun sebuah **FUSE (Filesystem in Userspace)** yang mampu:

1. **(Poin a)** Mengekstrak isi zip ke dalam direktori `amba_files/` dan menghapus file zip-nya.
2. **(Poin b)** Membuat program FUSE `kenz_rescue.c` yang me-mount direktori `amba_files/` ke sebuah mount point (misalnya `mnt/`). File yang ada di dalam mount harus identik byte-per-byte dengan file sumbernya (passthrough).
3. **(Poin c)** Menambahkan file virtual `tujuan.txt` di root mount directory. File ini harus muncul saat `ls mnt/`, ukurannya konsisten saat `stat`, dan **tidak boleh** memiliki kesamaan fisik di `amba_files/`.
4. **(Poin d)** Saat `cat mnt/tujuan.txt` dijalankan, isinya dibangkitkan **on-the-fly** (dibuat saat dibutuhkan, bukan disiapkan dulu/disimpan di disk) dengan menggabungkan fragmen `KOORD: <...>` dari `1.txt` sampai `7.txt` secara berurutan.

Format output `tujuan.txt`:

```
Tujuan Mas Amba: <fragmen1>,<fragmen2>,...,<fragmen7>
```

contoh:
```
Tujuan Mas Amba: -7.957,382728,443728, ,112.469,8688227961, ,23:,59 WIB
```

---

## Struktur Direktori

```
soal1/
├── kenz_rescue.c       # Program FUSE utama
├── amba_files/         # Direktori berisi catatan harian (dari zip, zip dihapus)
│   ├── 1.txt
│   ├── 2.txt
│   ├── 3.txt
│   ├── 4.txt
│   ├── 5.txt
│   ├── 6.txt
│   └── 7.txt
└── mnt/                # Mount point (dibuat saat runtime)
```

---

## Penjelasan Solusi (Block by Block)

### Header dan Definisi

```c
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
```

**Penjelasan:**

- `FUSE_USE_VERSION 28` — Menentukan versi API FUSE yang digunakan. Versi 28 adalah versi yang kompatibel dengan FUSE 2.x, sesuai contoh pada materi modul.
- `_GNU_SOURCE` — Feature test macro yang memberitahu glibc untuk mengekspos semua simbol POSIX termasuk `S_IFREG` yang digunakan untuk menandai file regular pada `st_mode`.
- `<fuse.h>` — Header utama FUSE yang menyediakan `fuse_main()`, `fuse_operations`, dan callback signatures.
- `<sys/stat.h>` — Menyediakan struktur `struct stat` dan makro `S_IFREG`.
- File header lainnya (`stdio.h`, `string.h`, `unistd.h`, `fcntl.h`, `dirent.h`, `errno.h`) adalah library standar C yang juga digunakan pada contoh program FUSE di materi.

---

### 1. Fungsi `extract_koord()` — Ekstrak Fragmen KOORD

```c
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
```

**Penjelasan:**

- Fungsi ini membuka satu file (misalnya `amba_files/3.txt`) dan mencari baris yang diawali dengan `KOORD: `.
- `strncmp(line, "KOORD: ", 7)` — Membandingkan 7 karakter pertama baris dengan string "KOORD: ". Menggunakan `7` (bukan `8`) karena "KOORD: " memang terdiri dari 7 karakter.
- `char *fragment = line + 7` — Pointer ke karakter setelah prefix "KOORD: ", yaitu fragmen koordinat itu sendiri.
- `fragment[len - 1] = '\0'` — Menghapus newline (`\n`) di akhir fragmen agar tidak ikut digabung.
- `strncpy(out, fragment, outlen - 1)` — Menyalin fragmen ke buffer output dengan aman.
- Return value adalah panjang fragmen (dalam byte), atau `-1` jika file tidak bisa dibuka.

---

### 2. Fungsi `build_tujuan_content()` — Gabungkan Semua Fragmen

```c
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
```

**Penjelasan:**

- Fungsi ini membangun seluruh konten `tujuan.txt` di dalam buffer.
- Prefix `"Tujuan Mas Amba: "` ditulis ke buffer menggunakan `memcpy`.
- Loop `for (i = 1; i <= 7; i++)` membaca fragmen dari `1.txt` sampai `7.txt` secara berurutan.
- `sprintf(fpath, "amba_files/%d.txt", i)` — Membangun path file secara dinamis. Menggunakan `sprintf` (bukan `snprintf`) agar sesuai dengan gaya coding pada contoh materi.
- Setiap fragmen dipisahkan dengan karakter `,` (koma). Koma tidak ditambahkan setelah fragmen terakhir.
- Baris diakhiri dengan satu karakter newline (`\n`), sesuai spesifikasi soal "diakhiri tepat satu newline."
- Return value `pos` adalah total byte yang ditulis — nilai ini menjadi `st_size` pada `getattr`.

---

### 3. Fungsi `compute_tujuan_size()` — Hitung Ukuran Virtual File

```c
static size_t compute_tujuan_size(void)
{
    char tmp[4096];
    return build_tujuan_content(tmp, sizeof(tmp));
}
```

**Penjelasan:**

- Fungsi ini memanggil `build_tujuan_content()` dengan buffer sementara untuk mendapatkan total ukuran konten.
- Ukuran ini **harus sama persis** dengan jumlah byte yang dikembalikan oleh `kenz_read()` saat membaca `tujuan.txt`.
- Jika `st_size` tidak cocok, command seperti `cat` akan mengalami truncation atau menampilkan garbage.

---

### 4. Callback `kenz_getattr()` — Metadata File

```c
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
```

**Penjelasan:**

- `getattr` adalah callback FUSE yang dipanggil saat sistem meminta atribut file (misalnya saat `ls`, `stat`, atau `cat` membuka file).
- **Virtual file `/tujuan.txt`:**
  - `st_mode = S_IFREG | 0444` — Menandai sebagai file regular (`S_IFREG`) dengan permission read-only untuk semua user (`0444`).
  - `st_size = compute_tujuan_size()` — Ukuran harus akurat agar reader tahu kapan berhenti membaca.
- **Passthrough files:** Untuk path selain `/tujuan.txt`, path diprefix dengan `amba_files/` lalu dilakukan `lstat()` pada file fisik yang sesungguhnya.
- `lstat()` mengembalikan metadata dari file di disk — size, permission, inode, dll.

---

### 5. Callback `kenz_readdir()` — List Direktori

```c
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
```

**Penjelasan:**

- `readdir` adalah callback FUSE yang dipanggil saat user menjalankan `ls` pada sebuah direktori.
- `(void) offset; (void) fi;` — Menyatakan bahwa parameter ini sengaja tidak digunakan, untuk menghindari compiler warning.
- `filler(buf, ".", NULL, 0)` dan `filler(buf, "..", NULL, 0)` — Selalu tambahkan entri `.` (current dir) dan `..` (parent dir). Tanpa ini, `ls` akan berperilaku aneh.
- Loop `while ((de = readdir(dp)) != NULL)` membaca semua entry dari direktori `amba_files/` dan menambahkannya ke daftar mount.
- Setelah loop selesai, **file virtual `tujuan.txt` ditambahkan manual** menggunakan `filler()`. Ini membuat file muncul di `ls mnt/` meskipun tidak ada file fisik dengan nama tersebut di `amba_files/`.
- `st_mode = S_IFREG | 0444` pada `tujuan.txt` memberitahu `ls` bahwa ini adalah file regular, bukan direktori atau symlink.

---

### 6. Callback `kenz_read()` — Baca Isi File

```c
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
```

**Penjelasan:**

- `read` adalah callback FUSE yang dipanggil saat sistem membaca data dari file (misalnya saat `cat`, `head`, atau `less`).
- **Virtual file `/tujuan.txt`:**
  - `static char content[4096]` — Buffer static untuk menyimpan konten yang dibangkitkan. `static` memastikan buffer dan flag `built` tetap ada antar pemanggilan fungsi.
  - `if (!built)` — Konten hanya dibangun sekali pada pemanggilan pertama. Setelah itu, buffer langsung digunakan.
  - `offset >= content_len` — Jika offset melebihi ukuran file, tidak ada data yang dikembalikan (EOF).
  - `offset + size > content_len` — Menyesuaikan size agar tidak membaca melebihi batas konten.
  - `memcpy(buf, content + offset, size)` — Menyalin data dari buffer ke buffer FUSE (`buf`), dimulai dari posisi `offset`.
  - **Ini adalah on-the-fly generation** — konten tidak pernah ditulis ke disk. Saat `cat mnt/tujuan.txt` dijalankan, `read` dipanggil dan konten dibangun di memori saat itu juga.
- **Passthrough files:** Untuk path selain `/tujuan.txt`, file fisik dibuka dengan `open()`, dibaca dengan `pread()`, lalu ditutup. `pread()` membaca data pada posisi tertentu tanpa mengubah file offset, sehingga thread-safe.

---

### 7. `fuse_operations` dan `main()`

```c
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
```

**Penjelasan:**

- `fuse_operations` adalah struct yang menghubungkan callback FUSE dengan fungsi implementasi.
- Hanya tiga callback yang diimplementasikan: `getattr`, `readdir`, dan `read`. Callback lainnya (seperti `write`, `mkdir`, `unlink`) tidak diperlukan untuk soal ini.
- `umask(0)` — Mengatur umask ke 0 agar permission file tidak dipengaruhi oleh umask default shell.
- `fuse_main()` — Fungsi utama FUSE yang menangani argument parsing, mount filesystem, dan menjalankan event loop. Fungsi ini tidak akan kembali (return) sampai filesystem di-unmount.

---

## Perintah untuk Berinteraksi dengan Program

### Prasyarat

Pastikan FUSE sudah terinstall:

```bash
sudo apt update
sudo apt install libfuse-dev
```

### Kompilasi

```bash
cd soal1/
gcc -Wall `pkg-config fuse --cflags` kenz_rescue.c -o kenz_rescue `pkg-config fuse --libs`
```

### Mount Filesystem

```bash
mkdir -p mnt
./kenz_rescue -f mnt
```

Flag `-f` (foreground) menjalankan FUSE di foreground sehingga `printf` debug bisa terlihat. Terminal akan "hang" — ini normal, artinya FUSE sedang menunggu request.

### Verifikasi Passthrough (Terminal 2)

Buka terminal kedua di direktori yang sama:

```bash
# List isi mount — harus menampilkan 8 entry
ls mnt/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt  tujuan.txt

# Verifikasi source folder hanya punya 7 entry
ls amba_files/
# Output: 1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt

# Byte-identical check untuk semua 7 file passthrough
for i in 1 2 3 4 5 6 7; do
    diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"
done
```

**Output yang diharapkan:**
```
1.txt OK
2.txt OK
3.txt OK
4.txt OK
5.txt OK
6.txt OK
7.txt OK
```

### Verifikasi Virtual File `tujuan.txt`

```bash
# Cek metadata
stat mnt/tujuan.txt
```

**Output yang diharapkan:**
```
  File: mnt/tujuan.txt
  Size: 72         Blocks: 0          IO Block: 4096   regular file
  Access: (0444/-r--r--r--)
```

```bash
# Baca konten yang dibangkitkan on-the-fly
cat mnt/tujuan.txt
```

**Output yang diharapkan:**
```
Tujuan Mas Amba: -7.957,382728,443728, ,112.469,8688227961, ,23:,59 WIB
```

### Verifikasi On-the-Fly Generation

```bash
# tujuan.txt TIDAK BOLEH ada di amba_files/
ls amba_files/tujuan.txt
# Output: ls: cannot access 'amba_files/tujuan.txt': No such file or directory
```

### Unmount

Di terminal yang menjalankan FUSE, tekan `Ctrl+C` untuk menghentikan proses. Kemudian:

```bash
fusermount -u mnt
```

Verifikasi unmount berhasil:

```bash
ls mnt/
# Harus kosong (tidak ada output)
```

---

## Cara Menjalankan (Step-by-Step)

### 1. Persiapan Direktori

```bash
cd soal1/
```

Pastikan struktur direktori sudah benar:
```
soal1/
├── kenz_rescue.c
├── amba_files/
│   ├── 1.txt ... 7.txt
└── mnt/
```

### 2. Kompilasi

```bash
gcc -Wall `pkg-config fuse --cflags` kenz_rescue.c -o kenz_rescue `pkg-config fuse --libs`
```

### 3. Mount

```bash
./kenz_rescue -f mnt
```

### 4. Verifikasi (di terminal kedua)

```bash
ls mnt/
ls amba_files/
for i in 1 2 3 4 5 6 7; do diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"; done
stat mnt/tujuan.txt
cat mnt/tujuan.txt
ls amba_files/tujuan.txt 2>&1
```

### 5. Unmount

```bash
# Di terminal FUSE
Ctrl+C

# Kemudian
fusermount -u mnt
```

---

## Referensi Materi

Berdasarkan **Modul 4 - File System - FUSE**:

- **FUSE (Filesystem in Userspace)** — Interface untuk membuat filesystem di userspace tanpa harus mengubah kernel.
- **Cara kerja FUSE** — `fuse_main()` memanggil `fuse_mount()` yang membuat UNIX domain socket, kemudian `fusermount()` memuat modul FUSE kernel dan membuka `/dev/fuse`.
- **Callback penting** — `getattr` (metadata file), `readdir` (list direktori), `read` (baca data).
- **Kompilasi** — `gcc -Wall \`pkg-config fuse --cflags\` file.c -o output \`pkg-config fuse --libs\``
- **Unmount** — `fusermount -u [mountpoint]` atau `sudo umount [mountpoint]`.

---
---

# Soal 2 - Poke MOO

## Penjelasan Singkat Masalah

MOO adalah sebuah **mini database service** dengan struktur sederhana: **folder = database** dan **csv file = table**. Workdir program MOO di dalam container adalah `/app/db`. Saat program dijalankan, MOO membuka **TCP Connection** pada port `9000`. Client yang tersambung dapat melakukan operasi database melalui perintah teks line-by-line.

Available Commands yang didukung server MOO:

```
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

Yang disediakan dalam release: binary `server` (sudah dikompilasi) dan file checker `notes.csv.enc`. Tugasmu terbagi menjadi tiga bagian:

### Fuse (Poin a–d)

**a.** Buat `fuse.c` yang mengimplementasikan fungsi-fungsi operasi:

- `getattr`, `readdir`
- `mkdir`, `rmdir`
- `create`, `open`
- `read`, `write`
- `truncate`, `unlink`
- `access`, `utimens`

Fuse ini menghubungkan **2 direktori**: direktori asli `encrypted_storage` dan mounting point `fuse_mount`.

**b.** Direktori `fuse_mount` berfungsi seperti filesystem secara umum ketika dimount. Pada direktori tersebut harus bisa **create file/folder, access folder, read file, remove file/folder, melihat atribut (metadata)**. Apapun yang dibuat, diupdate, dan didelete akan otomatis sama dengan direktori asli `encrypted_storage`.

**c.** Program fuse ini **tidak pure passthrough**. `fuse_mount` menjadi tempat translator dari file/folder yang terenkripsi pada folder `encrypted_storage` ke siapapun yang akan mengaksesnya melalui `fuse_mount`. Ketika file dibuat pada direktori `fuse_mount`, file akan **terenkripsi menggunakan algoritma XOR (key `0x76`)** dan tersimpan pada `encrypted_storage` dengan format nama yang ditambahkan `.enc`. Berlaku sebaliknya — jika ada file backup yang di-write di `encrypted_storage`, maka isinya (terdekripsi) dapat terlihat melalui `fuse_mount`.

**d.** Sebagai **checker** bahwa fuse berhasil, buat direktori `tests` di dalam `encrypted_storage`. File `notes.csv.enc` (provided) diletakkan ke dalam direktori `tests`. Pastikan isi file terbaca pada mount point `fuse_mount`.

### Containerization

Isolate minidatabase service untuk dikontainerisasi menggunakan Docker. Buat image aplikasi dengan **base image `ubuntu:latest`** kemudian copy program ke **workdir `/app`** dan **expose PORT 9000**. Build image dengan tag **`soal-2-modul-4-sisop`**.

### Integration

Jalankan container pada background dengan image yang telah dibikin sebelumnya, **nama `db_app`** dan **bind mount direktori `fuse_mount` ke direktori `db` yang ada di `/app`** (yaitu `/app/db`). Lalu buat program `client.c` untuk berinteraksi dengan server melalui socket TCP connection.

---

## Struktur Direktori

```
soal2/
├── Dockerfile              # Definisi image Docker (base ubuntu:latest)
├── Makefile                # Convenience wrapper (compile / clean / mount / run)
├── client.c                # Source TCP client
├── fuse.c                  # Source FUSE encrypted storage
├── server                  # Binary MOO mini-DB (provided)
├── encrypted_storage/      # Direktori asli, berisi *.enc terenkripsi
│   └── tests/
│       └── notes.csv.enc   # Checker file (disalin saat `make init`)
└── fuse_mount/             # Mount point FUSE (kosong sampai ./fuse dijalankan)
```

> **Catatan:** Direktori `encrypted_storage/` dan `fuse_mount/` ada di repo (dijaga via `.gitkeep`) tetapi awalnya kosong. File `notes.csv.enc` tersedia di `SoalShiftPraktikum/Soal 2/notes.csv.enc` dan disalin ke `encrypted_storage/tests/` saat setup (manual atau via `make init`).

---

## Penjelasan Solusi (Block by Block)

### 1. fuse.c — FUSE Encrypted Storage

Sesuai materi modul, FUSE diimplementasikan dengan mengisi `struct fuse_operations` dengan pointer ke fungsi-fungsi callback. Fuse program ini menggunakan **FUSE_USE_VERSION 28** (sama seperti contoh di materi).

#### Header dan Konstanta

```c
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
```

- `FUSE_USE_VERSION 28` — Versi API FUSE yang digunakan, sesuai materi modul.
- `_GNU_SOURCE` — Mengaktifkan ekstensi GNU (misalnya untuk `utimensat`).
- `XOR_KEY 0x76` — Key untuk algoritma XOR enkripsi/dekripsi (1 byte, simetris).
- `backend[1024]` — Buffer untuk path absolut direktori `encrypted_storage`. Diisi di `main()` dengan `realpath()`.

#### Helper: XOR encrypt/decrypt

```c
static void xor_buf(char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] ^= XOR_KEY;
}
```

XOR adalah operasi simetris — operasi yang sama mengenkripsi dan mendekripsi. Fungsi ini dipanggil sekali di `read` (untuk dekripsi) dan sekali di `write` (untuk enkripsi).

**Contoh perhitungan:**

| Karakter | Hex | XOR 0x76 |
|----------|-----|----------|
| `h` | 0x68 | 0x1e |
| `a` | 0x61 | 0x17 |
| `l` | 0x6c | 0x1a |
| `o` | 0x6f | 0x19 |

Jadi `"halo"` di-encode menjadi 4 byte `1e 17 1a 19`.

#### Helper: Resolve real path

```c
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
```

Aturan path translation:
- Jika path adalah **direktori yang sudah ada**, gunakan apa adanya (tanpa `.enc`).
- Jika tidak, ini file — tambahkan suffix `.enc`.

Contoh:

| User mengakses | Real path |
|----------------|-----------|
| `/` | `encrypted_storage/` |
| `/file1.txt` | `encrypted_storage/file1.txt.enc` |
| `/halo` (dir) | `encrypted_storage/halo` |
| `/halo/file2.txt` | `encrypted_storage/halo/file2.txt.enc` |
| `/tests/notes.csv` | `encrypted_storage/tests/notes.csv.enc` |

#### Callback `getattr` — Mendapatkan metadata

```c
static int xmp_getattr(const char *path, struct stat *stbuf)
{
    char fpath[2048];
    build_real_path(path, fpath);

    if (lstat(fpath, stbuf) == -1)
        return -errno;

    return 0;
}
```

Dipanggil oleh `ls`, `stat`, dan operasi lain yang butuh metadata. Memanggil `lstat()` pada real path (yang sudah ditranslate ke `.enc` jika file).

#### Callback `readdir` — Listing direktori

```c
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
```

Membuka direktori asli dengan `opendir()`, lalu untuk setiap entry **menghapus suffix `.enc`** sebelum diserahkan ke `filler()`. Sehingga user melihat `notes.csv` (bukan `notes.csv.enc`) saat melakukan `ls fuse_mount/tests/`.

#### Callback `mkdir` dan `rmdir` — Passthrough

```c
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
```

Direktori **tidak diberi suffix `.enc`** — direktori passthrough penuh tanpa transformasi nama.

#### Callback `create` dan `open` — Membuka file

```c
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
```

Untuk file selalu **append `.enc`** sebelum `open()`. File descriptor disimpan di `fi->fh` agar tidak perlu open ulang di `read`/`write`.

#### Callback `read` — Dekripsi saat baca

```c
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
        xor_buf(buf, res);   // dekripsi in-place

    if (opened_here)
        close(fd);

    return res;
}
```

Alur:
1. Buka file `.enc` (ciphertext).
2. `pread()` byte ciphertext sesuai `offset` dan `size`.
3. **XOR in-place** untuk dekripsi ke buffer caller.
4. Return jumlah byte yang dibaca.

#### Callback `write` — Enkripsi saat tulis

```c
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
```

Alur:
1. Salin plaintext ke buffer sementara `tmp` (**jangan modifikasi buffer caller**).
2. XOR `tmp` → ciphertext.
3. `pwrite()` ciphertext ke file `.enc`.

#### Callback `truncate` dan `unlink`

```c
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
```

Keduanya passthrough ke file `.enc` di backend.

#### Callback `access` dan `utimens`

```c
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
```

Keduanya menggunakan `build_real_path()` karena bisa kena ke file maupun direktori (`access` dipanggil untuk pengecekan permission, `utimens` untuk update timestamp).

#### Wiring: `fuse_operations` struct dan `main`

```c
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
```

**Catatan penting `realpath()`:** Saat FUSE dijalankan tanpa flag `-f` (daemon mode), `fuse_main()` melakukan `chdir("/")`. Jika `backend` masih relatif (`"encrypted_storage"`), semua callback akan gagal karena path mengarah ke `/encrypted_storage` (yang tidak ada). Dengan `realpath()` di awal `main()`, backend disimpan sebagai **absolute path** sehingga program tetap bekerja baik di foreground (`-f`) maupun daemon mode.

#### Compile

```bash
gcc -Wall `pkg-config fuse --cflags` fuse.c -o fuse `pkg-config fuse --libs`
```

Sama persis dengan command di materi modul.

---

### 2. Dockerfile — Containerization

```dockerfile
FROM ubuntu:latest

WORKDIR /app

COPY server .
RUN chmod +x server

EXPOSE 9000

CMD ["./server"]
```

**Penjelasan per directive:**

- `FROM ubuntu:latest` — Base image sesuai instruksi soal.
- `WORKDIR /app` — Workdir program MOO, sesuai instruksi soal.
- `COPY server .` — Menyalin binary `server` (yang sudah dikompilasi, provided) ke `/app/server`.
- `RUN chmod +x server` — Memastikan binary executable (kadang executable bit hilang saat copy via WSL).
- `EXPOSE 9000` — Port TCP server MOO, sesuai instruksi soal.
- `CMD ["./server"]` — Command yang dijalankan saat container start.

**Build:**

```bash
docker build -t soal-2-modul-4-sisop .
```

---

### 3. client.c — TCP Client

Sesuai problem text *"...dapat diakses melalui TCP Connection (**seperti modul sebelumnya**) port 9000"*, client menggunakan **socket programming dari modul sebelumnya** (Modul 3 IPC).

#### Header

```c
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 9000
#define BUFSZ       4096
```

Library standar BSD socket: `<sys/socket.h>` untuk `socket/connect/send/recv`, `<arpa/inet.h>` untuk `inet_pton/htons`.

#### Setup koneksi

```c
int sock = socket(AF_INET, SOCK_STREAM, 0);
if (sock < 0) {
    perror("socket");
    return 1;
}

struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
addr.sin_port   = htons(SERVER_PORT);
inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(sock);
    return 1;
}
```

- `socket(AF_INET, SOCK_STREAM, 0)` — Membuat TCP socket IPv4.
- `htons()` — Convert port number ke network byte order.
- `inet_pton()` — Convert IP address string ke struct.
- `connect()` — Membuka koneksi ke server.

#### Greeting

```c
printf("Connected to DB Server on port %d\n", SERVER_PORT);
printf("Type HELP for available commands\n");
printf("Type EXIT to quit\n");
```

Cetak instruksi awal sesuai format yang diharapkan.

#### REPL Loop

```c
for (;;) {
    printf("\ndb > ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin))
        break;

    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\n')
        line[n - 1] = '\0';

    if (strcmp(line, "EXIT") == 0)
        break;

    if (line[0] == '\0')
        continue;

    strncat(line, "\n", sizeof(line) - strlen(line) - 1);
    if (send(sock, line, strlen(line), 0) < 0) {
        perror("send");
        break;
    }

    int r = recv(sock, reply, sizeof(reply) - 1, 0);
    if (r <= 0)
        break;
    reply[r] = '\0';

    size_t rlen = strlen(reply);
    if (rlen > 0 && reply[rlen - 1] == '\n')
        reply[rlen - 1] = '\0';

    printf("\n%s\n", reply);
}

close(sock);
```

**Format output (sesuai screenshot soal):**

- `printf("\ndb > ")` — Baris kosong sebelum prompt agar ada jarak antar interaksi.
- `printf("\n%s\n", reply)` — Mem-frame reply server dengan baris kosong di atas dan bawah.
- Trailing newline pada reply server di-strip dulu agar tidak terjadi double newline.

Format akhir di terminal:

```
db > HELP
                          ← blank line
AVAILABLE COMMANDS:
HELP
...
DROP DATABASE <db>
                          ← blank line
db > CREATE DATABASE tests
                          ← blank line
DATABASE CREATED
                          ← blank line
db > 
```

**Compile:**

```bash
gcc -Wall client.c -o client
```

---

### 4. Makefile — Workflow Wrapper

Makefile adalah convenience layer yang membungkus semua command standar (`gcc`, `docker`, `fusermount`) ke dalam target yang lebih singkat. Tidak menambahkan method baru — setiap recipe di dalamnya hanyalah command yang sudah ada di materi modul.

#### Target utama

```makefile
all: fuse client

fuse: fuse.c
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) $< -o $@ $(FUSE_LIBS)

client: client.c
	$(CC) $(CFLAGS) $< -o $@

init: all
	@mkdir -p $(STORAGE)/tests $(MOUNT)
	@if [ ! -f "$(STORAGE)/tests/notes.csv.enc" ]; then \
		cp "$(RELEASE)/notes.csv.enc" "$(STORAGE)/tests/notes.csv.enc"; \
	fi
```

- `make` (default = `make all`) — Kompilasi `fuse.c` → `fuse` dan `client.c` → `client`.
- `make init` — `make all` + salin file checker `notes.csv.enc` ke `encrypted_storage/tests/`.

#### Target `mount` dengan `-o allow_other`

```makefile
mount: fuse
	@if mount | grep -q " on $(CURDIR)/$(MOUNT) "; then \
		echo "Already mounted at $(MOUNT)/"; \
	else \
		./fuse $(MOUNT) -o allow_other && echo "Mounted FUSE at $(MOUNT)/ (with allow_other)"; \
	fi
```

Flag `-o allow_other` diperlukan agar **Docker daemon (yang berjalan sebagai root)** bisa traverse FUSE mount. Tanpa flag ini, `docker run -v fuse_mount:/app/db ...` akan gagal dengan error `mkdir … : file exists`. Flag ini membutuhkan **`user_allow_other` di-uncomment di `/etc/fuse.conf`** (setup one-time per machine).

#### Target `clean`

```makefile
clean: unmount
	@docker rm -f $(CONTAINER) 2>/dev/null || true
	@rm -f fuse client
	@rm -rf $(STORAGE) $(MOUNT)
	@mkdir -p $(STORAGE) $(MOUNT)
```

Reset ke struktur start-point:
1. Unmount FUSE (via prereq `unmount` target).
2. Force-remove container `db_app`.
3. Hapus binary `fuse` dan `client`.
4. Wipe direktori `encrypted_storage/` dan `fuse_mount/`.
5. Recreate keduanya sebagai direktori kosong.

#### Target Docker

```makefile
docker:
	docker build -t $(IMAGE) .

run: docker
	@docker rm -f $(CONTAINER) 2>/dev/null || true
	docker run -d --name $(CONTAINER) -p 9000:9000 -v "$(CURDIR)/$(MOUNT)":/app/db $(IMAGE)

stop:
	@docker stop $(CONTAINER) 2>/dev/null || true
	@docker rm $(CONTAINER) 2>/dev/null || true
```

- `make docker` — Build image dengan tag `soal-2-modul-4-sisop`.
- `make run` — Build (jika belum) + run container `db_app` dengan **bind mount** `fuse_mount/` ke `/app/db`. Bind mount adalah salah satu jenis Docker Mount yang dibahas di materi.
- `make stop` — Stop dan remove container.

---

## Perintah untuk Berinteraksi dengan Program

### Prerequisite One-Time per Mesin

```bash
# Install FUSE development library
sudo apt update
sudo apt install libfuse-dev

# Enable user_allow_other di /etc/fuse.conf (untuk integrasi dengan Docker)
sudo sed -i 's/^#user_allow_other/user_allow_other/' /etc/fuse.conf
grep user_allow_other /etc/fuse.conf
# Output yang diharapkan: baris user_allow_other tanpa # di depan
```

### Setup dan Build

```bash
cd soal2/

# Compile + restore checker file
make init

# Atau manual:
gcc -Wall `pkg-config fuse --cflags` fuse.c -o fuse `pkg-config fuse --libs`
gcc -Wall client.c -o client
mkdir -p encrypted_storage/tests
cp "../../SoalShiftPraktikum/Soal 2/notes.csv.enc" encrypted_storage/tests/
```

### Mount FUSE

```bash
# Daemon mode (idiomatic, terminal kembali ke prompt)
./fuse fuse_mount -o allow_other
# Atau:
make mount

# Foreground mode (untuk debugging dengan printf)
./fuse -f fuse_mount -o allow_other
```

### Verifikasi FUSE bekerja

```bash
# 1. Plaintext checker terbaca melalui mount
cat fuse_mount/tests/notes.csv
# Expected: author,notes\nadmin,TEST_SUCCESS

# 2. Ciphertext masih ada di backend
xxd encrypted_storage/tests/notes.csv.enc | head
# Expected: 1703 021e 1904 5a18 1902 1305 7c17 121b ...

# 3. Round-trip write/read
echo "halo dunia" > fuse_mount/sample.txt
cat fuse_mount/sample.txt
# Expected: halo dunia

xxd encrypted_storage/sample.txt.enc
# Expected: 1e17 1a19 5612 0318 1f17 7c

# 4. mkdir tanpa suffix .enc
mkdir fuse_mount/halo
ls encrypted_storage/
# Expected: halo/ (tanpa .enc) + tests/ + sample.txt.enc
```

### Unmount FUSE

```bash
fusermount -u fuse_mount
# Atau:
make unmount
```

### Build dan Run Container

```bash
# Build image
docker build -t soal-2-modul-4-sisop .
# Atau:
make docker

# Verifikasi image terbuild
docker images
# Output yang diharapkan: soal-2-modul-4-sisop:latest

# Jalankan container db_app dengan bind mount fuse_mount → /app/db
docker run -d \
    --name db_app \
    -p 9000:9000 \
    -v "$PWD/fuse_mount":/app/db \
    soal-2-modul-4-sisop
# Atau:
make run

# Verifikasi container running
docker ps -a
# Expected: db_app dengan STATUS "Up X seconds"
```

> **Penting:** FUSE harus sudah dimount **dengan `-o allow_other`** sebelum container dijalankan, jika tidak Docker daemon (sebagai root) tidak bisa traverse mount-nya dan container gagal start dengan error `mkdir … : file exists`.

### Interaksi via Client

```bash
./client
```

**Contoh sesi:**

```
Connected to DB Server on port 9000
Type HELP for available commands
Type EXIT to quit

db > HELP

AVAILABLE COMMANDS:
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

db > CREATE DATABASE tests

DATABASE CREATED

db > CREATE TABLE tests users email password

TABLE CREATED

db > LIST DATABASE

tests

db > LIST TABLE tests

users.csv

db > EXIT
```

### Verifikasi Pipeline Lengkap

```bash
# Setelah CREATE DATABASE + CREATE TABLE via client, cek backend
tree encrypted_storage/
# Expected:
# encrypted_storage/
# └── tests/
#     ├── history.log.enc       ← dibuat MOO server
#     ├── notes.csv.enc         ← checker
#     └── users.csv.enc         ← dibuat MOO server

# Dan view-nya yang terdekripsi di fuse_mount
sudo tree fuse_mount/
# Expected:
# fuse_mount/
# └── tests/
#     ├── history.log
#     ├── notes.csv
#     └── users.csv
```

### Stop dan Cleanup

```bash
# Stop dan remove container
docker stop db_app && docker rm db_app
# Atau:
make stop

# Unmount FUSE
fusermount -u fuse_mount
# Atau:
make unmount

# Reset penuh ke start-point (hapus binary, wipe dirs, drop container)
make clean

# Atau full reset termasuk hapus image
make distclean
```

### Quick Reference Makefile

```
make            # compile fuse + client
make init       # compile + restore checker file
make mount      # mount FUSE dengan -o allow_other
make unmount    # unmount FUSE
make docker     # build Docker image
make run        # docker build + run container db_app
make stop       # stop + remove container
make clean      # reset ke start-point (unmount, hapus binary, wipe dirs)
make distclean  # clean + hapus image juga
make re         # clean + init
make help       # tampilkan daftar target
```

---

## Cara Menjalankan (Step-by-Step)

### 1. Setup One-Time per Mesin

```bash
sudo apt update
sudo apt install libfuse-dev
sudo sed -i 's/^#user_allow_other/user_allow_other/' /etc/fuse.conf
```

### 2. Persiapan Direktori dan Compile

```bash
cd soal2/
make init
```

`make init` akan compile `fuse` dan `client`, lalu menyalin `notes.csv.enc` ke `encrypted_storage/tests/`.

### 3. Mount FUSE

```bash
make mount
# atau manual: ./fuse fuse_mount -o allow_other
```

### 4. Verifikasi FUSE

```bash
cat fuse_mount/tests/notes.csv
# Expected: author,notes\nadmin,TEST_SUCCESS

xxd encrypted_storage/tests/notes.csv.enc | head
# Expected: ciphertext starting with 1703 021e ...
```

### 5. Build dan Run Container

```bash
make run
# atau manual:
# docker build -t soal-2-modul-4-sisop .
# docker run -d --name db_app -p 9000:9000 -v "$PWD/fuse_mount":/app/db soal-2-modul-4-sisop

docker ps -a
# Verifikasi db_app status "Up X seconds"
```

### 6. Interaksi via Client

```bash
./client
```

Coba perintah `HELP`, `CREATE DATABASE tests`, `LIST DATABASE`, `LIST TABLE tests`, `EXIT`.

### 7. Verifikasi End-to-End

```bash
tree encrypted_storage/    # ciphertext di backend
sudo tree fuse_mount/      # plaintext view melalui FUSE
```

### 8. Cleanup

```bash
make stop          # hentikan container
make unmount       # unmount FUSE
make clean         # reset ke start-point
```

---
---

# Soal 3 - LibraryIT

## Penjelasan Singkat Masalah

IT Library Nusantara adalah perpustakaan digital khusus bidang Information Technology yang menyimpan koleksi e-book, paper riset, source code, dan dokumentasi teknis. Karena koleksi semakin banyak dan anggota semakin beragam, perpustakaan membutuhkan sistem pengelolaan file yang terstruktur, aman, dan bisa diakses bersama dalam satu jaringan.

Kamu ditunjuk sebagai System Administrator baru di IT Library Nusantara. Tugasmu adalah membangun infrastruktur LibraryIT dari nol menggunakan **Docker** dan **Samba**.

### Spesifikasi Server (Poin a)

Perpustakaan membutuhkan sebuah server yang berjalan di dalam container dengan nama `libraryit-server`. Di dalam server harus tersedia empat ruang penyimpanan koleksi yaitu `ebooks`, `papers`, `sourcecode`, dan `docs` yang semuanya terletak di dalam direktori `/libraryit/`.

Server ini harus sudah memiliki tiga anggota perpustakaan yaitu:

| Username | Password | Kelompok |
|----------|----------|----------|
| `member` | `member123` | `readonly` |
| `contributor` | `contrib456` | `staff` |
| `librarian` | `lib789` | `staff` |

Setiap anggota tergabung dalam kelompok:
- **readonly** — hanya `member`
- **staff** — `contributor` dan `librarian`

Seluruh user dan kelompok ini harus terbentuk otomatis sejak pertama kali container dijalankan.

### Aturan Akses (Poin b)

Setiap koleksi memiliki aturan akses yang berbeda:

- **ebooks & papers**: Kelompok `staff` bisa membaca dan menulis, sedangkan kelompok `readonly` hanya bisa membaca.
- **sourcecode**: Kelompok `readonly` tidak boleh mengaksesnya sama sekali bahkan tidak boleh melihatnya saat membuka daftar share yang tersedia.
- **docs**: Bisa dibaca oleh semua kelompok, namun hanya `librarian` secara spesifik yang boleh menulis di sana — meskipun `contributor` masuk kelompok `staff`, ia tidak punya hak tulis di koleksi ini.

Seluruh konfigurasi akses harus berbasis kelompok dan tidak boleh ada akses tanpa identitas (guest/anonymous) di semua koleksi.

### Persistensi dan Keamanan Host (Poin c)

Server harus menyediakan mekanisme agar data yang tersimpan tetap ada walaupun container dihapus. Oleh karena itu, data harus tersimpan secara persisten.

Selain itu, user di **host tidak boleh mengubah direktori secara langsung** (host user tidak boleh langsung menulis di direktori host). Kelola akses agar koleksi `sourcecode` dan `docs` bisa dibaca oleh semua kelompok, namun penulisan harus dibatasi.

### Logging (Poin d)

Kepala IT Library Nusantara ingin mengetahui siapa saja yang mengakses koleksi apa dan apa yang mereka lakukan. Jika terjadi aksi yang mencurigakan, semua logs bisa diakses di file log dengan format:

```
[YYYY-MM-DD HH:MM:SS] [LEVEL] [USERNAME] [AKSI] [NAMA FILE/SHARE]
```

Gunakan level **INFO** untuk aktivitas normal dan **WARNING** untuk aksi yang ditolak. File log harus bisa dipantau langsung dari luar container. Bisa menggunakan service tambahan seperti `libraryit-logger` yang terpisah. File log yang sama bisa dipantau melalui `docker logs`.

---

## Struktur Direktori

```
soal3/
├── Dockerfile          # Definisi image Docker
├── docker-compose.yml  # Konfigurasi multi-service
├── smb.conf            # Konfigurasi Samba
├── entrypoint.sh       # Setup otomatis user, grup, permission
├── data/
│   ├── ebooks/
│   ├── papers/
│   ├── sourcecode/
│   └── docs/
└── logs/
    └── libraryit.log   # Dibuat otomatis saat container berjalan
```

> **Catatan:** Direktori `data/` dan `logs/` dibuat secara manual sebelum `docker compose up`.

---

## Penjelasan Solusi (Block by Block)

### 1. Dockerfile

Dockerfile mendefinisikan image container berbasis **Ubuntu 22.04** yang berisi semua tools yang dibutuhkan.

```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        samba \
        samba-vfs-modules \
        socat \
        ca-certificates \
        acl && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

COPY smb.conf      /etc/samba/smb.conf
COPY entrypoint.sh /usr/local/bin/entrypoint.sh

RUN chmod 0755 /usr/local/bin/entrypoint.sh

EXPOSE 445

CMD ["/usr/local/bin/entrypoint.sh"]
```

**Penjelasan per baris:**

- `FROM ubuntu:22.04` — Menggunakan Ubuntu 22.04 sebagai base image.
- `ENV DEBIAN_FRONTEND=noninteractive` — Mencegah prompt interaktif saat instalasi package.
- `samba` — Package server SMB (Server Message Block) untuk berbagi file antar jaringan, sesuai materi modul.
- `samba-vfs-modules` — Modul VFS tambahan, termasuk `vfs_full_audit` yang digunakan untuk audit logging.
- `socat` — Utility untuk membuat socket Unix domain (SOCK_DGRAM) di `/dev/log` agar bisa menerima syslog dari `vfs_full_audit`. Shell biasa tidak bisa membuat Unix socket, sehingga `socat` diperlukan sebagai penghubung.
- `acl` — Package untuk Access Control List (ACL), digunakan oleh `setfacl` agar user `librarian` bisa menulis ke direktori `docs` meskipun permission dasarnya `555`.
- `ca-certificates` — Certificate authority untuk koneksi HTTPS jika diperlukan.
- `COPY smb.conf /etc/samba/smb.conf` — Menyalin konfigurasi Samba ke dalam container.
- `COPY entrypoint.sh /usr/local/bin/entrypoint.sh` — Menyalin script entrypoint yang akan dieksekusi saat container start.
- `EXPOSE 445` — Port default SMB yang akan di-mapping ke host melalui Docker Compose.
- `CMD ["/usr/local/bin/entrypoint.sh"]` — Script yang dijalankan otomatis saat container start.

---

### 2. docker-compose.yml

Docker Compose mengatur dua service: server utama dan logger sidecar.

```yaml
services:
  libraryit-server:
    build: .
    container_name: libraryit-server
    hostname: libraryit-server
    ports:
      - "1445:445"
    volumes:
      - ./data/ebooks:/libraryit/ebooks
      - ./data/papers:/libraryit/papers
      - ./data/sourcecode:/libraryit/sourcecode
      - ./data/docs:/libraryit/docs
      - ./logs:/libraryit/logs
    restart: unless-stopped

  libraryit-logger:
    image: alpine:3
    container_name: libraryit-logger
    depends_on:
      - libraryit-server
    volumes:
      - ./logs:/libraryit/logs:ro
    command:
      - sh
      - -c
      - |
        until [ -f /libraryit/logs/libraryit.log ]; do sleep 1; done
        exec tail -F /libraryit/logs/libraryit.log
    restart: unless-stopped
```

**Penjelasan:**

- **`libraryit-server`**
  - `build: .` — Build image dari Dockerfile di direktori saat ini.
  - `ports: "1445:445"` — Mapping port SMB dari container (445) ke host (1445) agar tidak bentrok dengan SMB host.
  - `volumes` — **Bind mount** digunakan untuk persistensi data. Semua file yang ditulis ke `/libraryit/ebooks`, `/libraryit/papers`, dll. akan tersimpan di host di `./data/`. Ini adalah salah satu jenis Docker Mount yang disebutkan di materi: **Bind Mount**.

- **`libraryit-logger`**
  - Menggunakan image `alpine:3` yang sangat ringan.
  - `depends_on` — Menunggu `libraryit-server` start terlebih dahulu.
  - `volumes: ./logs:/libraryit/logs:ro` — Hanya membaca log (read-only).
  - `command` — Menunggu file `libraryit.log` muncul, lalu menjalankan `tail -F` untuk streaming log secara real-time.
  - Output logger bisa dilihat dengan `docker logs -f libraryit-logger`.

---

### 3. smb.conf

File konfigurasi Samba yang mengatur akses berbasis grup.

#### Global Settings

```conf
[global]
   workgroup = WORKGROUP
   server string = LibraryIT Server
   server role = standalone server
   security = user
   map to guest = never
   restrict anonymous = 2
   server min protocol = SMB2
   access based share enum = yes

   load printers = no
   printing = bsd
   printcap name = /dev/null
   disable spoolss = yes

   log file = /libraryit/logs/samba.log
   max log size = 1000
   log level = 1

   vfs objects = full_audit
   full_audit:prefix = %U|%S
   full_audit:success = connect disconnect mkdir rmdir read write pwrite pread open opendir unlink rename
   full_audit:failure = connect mkdir read write open opendir
   full_audit:facility = LOCAL5
   full_audit:priority = NOTICE
```

**Penjelasan directive penting:**

| Directive | Fungsi |
|-----------|--------|
| `security = user` | Autentikasi berbasis username/password |
| `map to guest = never` | Menolak koneksi tanpa autentikasi |
| `restrict anonymous = 2` | Memblokir akses anonymous sepenuhnya |
| `access based share enum = yes` | Menyembunyikan share yang tidak bisa diakses user (menyembunyikan `sourcecode` dari `member`) |
| `vfs objects = full_audit` | Mengaktifkan modul audit VFS |
| `full_audit:prefix = %U\|%S` | Format log: username\|sharename\|operation\|status |
| `full_audit:facility = LOCAL5` | Mengirim log ke syslog facility LOCAL5 |
| `full_audit:priority = NOTICE` | Priority level syslog |

> **Catatan `%u` vs `%U`:** `%u` mengembalikan user efektif setelah `force user` diterapkan (selalu `root` di `sourcecode`). `%U` mengembalikan username session asli (misalnya `contributor`). Selalu gunakan `%U` untuk audit log agar log menunjukkan siapa yang sebenarnya terhubung.

#### Share Definitions

**[ebooks]** dan **[papers]** — staff bisa menulis, readonly hanya baca:

```conf
[ebooks]
   comment = E-Books Collection
   path = /libraryit/ebooks
   valid users = @readonly, @staff
   read only = yes
   write list = @staff
   browseable = yes
   guest ok = no
   create mask = 0664
   directory mask = 0775
```

- `valid users = @readonly, @staff` — Hanya anggota grup `readonly` dan `staff` yang bisa mengakses.
- `read only = yes` — Defaultnya semua user hanya bisa membaca.
- `write list = @staff` — Pengecualian: anggota `staff` boleh menulis.
- `create mask = 0664` — File baru: owner `rw-`, group `rw-`, others `r--`.
- `directory mask = 0775` — Direktori baru: owner `rwx`, group `rwx`, others `r-x`.

> **Catatan pola `read only`:** Kombinasi `read only = yes` + `write list = ...` adalah pola yang benar. Jika menggunakan `read only = no`, semua user bisa menulis meskipun `write list` sudah di-set.

**[sourcecode]** — hanya staff, tersembunyi dari readonly:

```conf
[sourcecode]
   comment = Source Code Repository
   path = /libraryit/sourcecode
   valid users = @staff
   read only = yes
   write list = @staff
   browseable = yes
   guest ok = no
   create mask = 0640
   directory mask = 0750
   force user = root
   force group = staff
```

- `valid users = @staff` — Hanya staff yang bisa melihat share ini.
- `force user = root` — Diperlukan karena direktori host di-set ke `750` (group hanya `r-x`, tanpa write). Dengan `force user = root`, operasi filesystem dilakukan sebagai root yang punya `rwx` penuh.
- `force group = staff` — Group yang dipakai untuk operasi filesystem adalah `staff`.

> **Catatan `force user`:** `force user = root` hanya digunakan untuk `sourcecode`. Jika digunakan di share lain (seperti `docs`), `write list` akan di-bypass karena root selalu bisa menulis di level filesystem.

**[docs]** — semua bisa baca, hanya `librarian` yang boleh menulis:

```conf
[docs]
   comment = Technical Documentation
   path = /libraryit/docs
   valid users = @readonly, @staff
   read only = yes
   write list = librarian
   browseable = yes
   guest ok = no
   create mask = 0664
   directory mask = 0775
```

- `write list = librarian` — Hanya user `librarian` (bukan seluruh grup staff) yang boleh menulis.
- Tidak ada `force user` — Samba menjalankan operasi sebagai user yang sebenarnya login, sehingga `write list` bisa dicek dengan tepat di level protokol.

> **Catatan host permission vs Samba permission:** `valid users`, `read only`, dan `write list` mengontrol akses di level protokol Samba. Permission filesystem (`chmod`, `chown`) juga penting karena Samba menjalankan operasi sebagai user yang login (kecuali jika `force user` digunakan).

---

### 4. entrypoint.sh

Script bash yang berjalan otomatis setiap kali container start.

#### Bagian 1: Membuat Grup

```bash
getent group staff    >/dev/null 2>&1 || groupadd -g 50   staff
getent group readonly >/dev/null 2>&1 || groupadd -g 1000 readonly
```

- `getent group` — Mengecek apakah grup sudah ada.
- `groupadd -g 50 staff` — Membuat grup `staff` dengan GID 50. Grup ini sudah ada secara default di Ubuntu, jika belum baru dibuat.
- `groupadd -g 1000 readonly` — Membuat grup `readonly` dengan GID 1000.

#### Bagian 2: Membuat User

```bash
USERS=(
    "member:1000:readonly:member123"
    "contributor:1001:staff:contrib456"
    "librarian:1002:staff:lib789"
)

for entry in "${USERS[@]}"; do
    IFS=':' read -r username uid group password <<< "$entry"

    if ! id -u "$username" >/dev/null 2>&1; then
        useradd -M -u "$uid" -s /usr/sbin/nologin -G "$group" "$username"
        echo "${username}:${password}" | chpasswd
    fi

    if ! pdbedit -L 2>/dev/null | grep -q "^${username}:"; then
        (echo "$password"; echo "$password") | smbpasswd -a -s "$username" >/dev/null
    fi
done
```

- `useradd -M` — Membuat user tanpa home directory.
- `useradd -G "$group"` — Menambahkan ke grup sekunder. Grup primer otomatis dibuat dengan nama yang sama.
- `chpasswd` — Set password Linux user.
- `smbpasswd -a -s` — Menambahkan user ke database Samba secara non-interaktif.
- `pdbedit -L` — Memeriksa apakah user sudah terdaftar di Samba.

#### Bagian 3: Setup Permission Direktori

```bash
mkdir -p \
    "${LIBRARYIT_ROOT}/ebooks" \
    "${LIBRARYIT_ROOT}/papers" \
    "${LIBRARYIT_ROOT}/sourcecode" \
    "${LIBRARYIT_ROOT}/docs" \
    "${LOG_DIR}"

# ebooks, papers: 775 root:staff
chown root:staff "${LIBRARYIT_ROOT}/ebooks" "${LIBRARYIT_ROOT}/papers"
chmod 0775       "${LIBRARYIT_ROOT}/ebooks" "${LIBRARYIT_ROOT}/papers"

# sourcecode: 750 root:staff
chown root:staff "${LIBRARYIT_ROOT}/sourcecode"
chmod 0750       "${LIBRARYIT_ROOT}/sourcecode"

# docs: 555 root:staff + ACL
chown root:staff "${LIBRARYIT_ROOT}/docs"
chmod 0555       "${LIBRARYIT_ROOT}/docs"
setfacl -m u:librarian:rwx "${LIBRARYIT_ROOT}/docs"

# logs: 755 root:root
chown root:root "${LOG_DIR}"
chmod 0755      "${LOG_DIR}"
touch "${LOG_FILE}"
chmod 0644 "${LOG_FILE}"
```

**Penjelasan permission:**

| Direktori | Permission | Owner:Group | Arti |
|-----------|------------|-------------|------|
| `ebooks`, `papers` | `775` | `root:staff` | Owner & group: rwx; others: r-x |
| `sourcecode` | `750` | `root:staff` | Owner: rwx; group: r-x; others: --- |
| `docs` | `555` | `root:staff` | Semua: r-x (tidak ada write) |
| `logs` | `755` | `root:root` | Owner: rwx; group & others: r-x |

- `setfacl -m u:librarian:rwx` — Menggunakan **ACL** untuk memberikan permission write kepada user `librarian` pada direktori `docs` yang permission dasarnya `555`.

> **Catatan `setfacl` pada bind mount:** ACL disimpan di inode filesystem, yang dibagi antara host dan container melalui bind mount. Setting ACL di dalam container akan berlaku di host juga. Ini memungkinkan host directory `docs` tetap read-only (555) untuk user biasa, namun `librarian` bisa menulis melalui Samba berkat ACL.

#### Bagian 4: Validasi Konfigurasi

```bash
testparm -s /etc/samba/smb.conf >/dev/null 2>&1
```

- `testparm -s` — Memvalidasi sintaks `smb.conf` sebelum server dijalankan. Jika ada kesalahan konfigurasi, script akan berhenti karena `set -e` di awal file.

#### Bagian 5: Logger (Shell-based)

Logger terdiri dari dua shell script yang di-embed via heredoc:

**`format-audit.sh`** — Menerima dan memformat pesan syslog dari `vfs_full_audit`:

```bash
#!/bin/bash
LOG="/libraryit/logs/libraryit.log"
IFS= read -r line
line=$(printf '%s' "$line" | sed 's/^<[0-9]*>//')
if ! printf '%s' "$line" | grep -q "smbd_audit:"; then exit 0; fi
payload=$(printf '%s' "$line" | sed 's/.*smbd_audit: *//')
# ... parsing field user|share|op|status|args ...
# ... mapping operasi ke nama human-readable ...
echo "[$ts] [$level] [$user] [$action] [$target]" >> "$LOG"
```

Script ini:
1. Membaca satu baris syslog dari `socat` via stdin.
2. Menghapus prefix priority (`<13>`).
3. Mengekstrak payload setelah `smbd_audit:`.
4. Memisahkan field dengan delimiter `|`.
5. Mapping operasi Samba ke nama yang lebih mudah dibaca (`pwrite_send` → `WRITE`).
6. Menulis ke `/libraryit/logs/libraryit.log` dengan format yang diminta.

**`tail-denied.sh`** — Memantau Samba log untuk aksi yang ditolak:

```bash
#!/bin/bash
LOG="/libraryit/logs/libraryit.log"
SAMBA_LOG="/libraryit/logs/samba.log"
while [ ! -f "$SAMBA_LOG" ]; do sleep 0.5; done
tail -f "$SAMBA_LOG" | while IFS= read -r line; do
    if printf '%s' "$line" | grep -q "not permitted to access this share"; then
        # ekstrak user dan share, format sebagai WARNING DENIED
        echo "[$ts] [WARNING] [$user] [DENIED] [$share]" >> "$LOG"
    fi
done
```

Script ini menunggu file `samba.log` muncul, lalu menggunakan `tail -f` untuk memantau baris-baris baru. Jika ada pesan "not permitted to access this share", script mengekstrak user dan share, lalu menulis log dengan level `WARNING`.

**Menjalankan logger:**

```bash
rm -f /dev/log
socat UNIX-RECV:/dev/log,fork EXEC:/usr/local/bin/format-audit.sh >/dev/null 2>&1 &
/usr/local/bin/tail-denied.sh >/dev/null 2>&1 &
```

- `socat UNIX-RECV:/dev/log` — Membuat socket Unix domain SOCK_DGRAM di `/dev/log`. Socket ini menerima syslog messages dari `vfs_full_audit`.
- `fork` — `socat` membuat child process baru untuk setiap datagram yang diterima.
- `EXEC:/usr/local/bin/format-audit.sh` — Setiap datagram di-pass ke script `format-audit.sh` melalui stdin.

> **Catatan urutan startup:** Logger harus bind `/dev/log` sebelum smbd start. Jika smbd start lebih dulu, pemanggilan `openlog()`-nya bisa gagal karena `/dev/log` belum ada. `entrypoint.sh` menjalankan `socat` dulu, menunggu socket muncul (`for _ in 1 2 3 4 5; do [ -S /dev/log ] && break; sleep 0.2; done`), baru menjalankan `smbd`.

#### Bagian 6: Menjalankan smbd

```bash
exec smbd --foreground --no-process-group
```

- `exec` — Menggantikan proses shell dengan `smbd`, sehingga PID 1 container adalah `smbd`.
- `--foreground` — Menjaga `smbd` berjalan di foreground agar Docker bisa melacak lifecycle container dengan benar.
- `--no-process-group` — Mencegah `smbd` membuat process group baru.

---

## Perintah untuk Berinteraksi dengan Program

### Build dan Jalankan

```bash
cd soal3/
mkdir -p data/ebooks data/papers data/sourcecode data/docs logs
docker compose up --build -d
```

Tunggu ~5 detik agar smbd dan logger selesai diinisialisasi.

### Melihat Status Container

```bash
# Melihat container yang berjalan
docker ps -a --filter "name=libraryit"

# Atau
docker compose ps
```

### Verifikasi User dan Grup

```bash
# Melihat user Samba yang terdaftar
docker exec libraryit-server pdbedit -L

# Melihat grup Linux
docker exec libraryit-server getent group staff readonly
```

**Output yang diharapkan:**
```
member:1000:
librarian:1002:
contributor:1001:

staff:x:50:contributor,librarian
readonly:x:1000:member
```

### Verifikasi Share Visibility

```bash
# Member hanya melihat 3 share (sourcecode tersembunyi)
smbclient -L //localhost -p 1445 -U member%member123
```

**Output yang diharapkan:** `ebooks`, `papers`, `docs` terlihat. `sourcecode` tidak terlihat.

### Verifikasi Akses yang Ditolak

```bash
# member tidak boleh akses sourcecode
smbclient //localhost/sourcecode -p 1445 -U member%member123 -c "ls"
# → tree connect failed: NT_STATUS_ACCESS_DENIED

# member tidak boleh menulis ke ebooks
smbclient //localhost/ebooks -p 1445 -U member%member123 -c "put /etc/hostname test.txt"
# → NT_STATUS_ACCESS_DENIED opening remote file \test.txt

# contributor tidak boleh menulis ke docs
smbclient //localhost/docs -p 1445 -U contributor%contrib456 -c "put /etc/hostname test.txt"
# → NT_STATUS_ACCESS_DENIED opening remote file \test.txt
```

### Verifikasi Akses yang Diizinkan

```bash
# contributor (staff) boleh menulis ke ebooks
smbclient //localhost/ebooks -p 1445 -U contributor%contrib456 -c "put /etc/hostname hello.txt"

# contributor (staff) boleh menulis ke sourcecode
smbclient //localhost/sourcecode -p 1445 -U contributor%contrib456 -c "put /etc/hostname hello_world.py"

# librarian boleh menulis ke docs
smbclient //localhost/docs -p 1445 -U librarian%lib789 -c "put /etc/hostname test.txt"
```

### Verifikasi Permission di Host

```bash
# Melihat permission direktori
ls -la data/
```

**Output yang diharapkan:**
```
drwxrwxr-x  root staff  ebooks
drwxrwxr-x  root staff  papers
drwxr-x---  root staff  sourcecode
drwxrwxr-x  root staff  docs
```

### Verifikasi Host Tidak Bisa Modifikasi Langsung

```bash
# Host user tidak boleh menulis ke docs
touch data/docs/test_dari_host.txt
# → touch: cannot touch 'data/docs/test_dari_host.txt': Permission denied
```

### Melihat Log

```bash
# Melihat log dari dalam container
docker exec libraryit-server cat /libraryit/logs/libraryit.log

# Atau melalui sidecar
docker logs -f libraryit-logger
```

**Format log yang diharapkan:**
```
[2026-05-08 10:00:01] [INFO] [contributor] [CONNECT] [SourceCode]
[2026-05-08 10:00:05] [INFO] [contributor] [WRITE] [hello_world.py]
[2026-05-08 10:01:22] [WARNING] [member] [DENIED] [SourceCode]
[2026-05-08 10:02:45] [INFO] [librarian] [WRITE] [test.txt]
```

### Mengakses Container

```bash
# Shell interaktif ke server
docker exec -it libraryit-server bash
```

### Menghentikan dan Membersihkan

```bash
# Menghentikan container
docker compose down

# Membersihkan: container, image, dan file yang di-generate
docker compose down --rmi all
find data/ -type f -not -name ".gitkeep" -delete
find logs/ -type f -not -name ".gitkeep" -delete
```

---

## Cara Menjalankan (Step-by-Step)

### 1. Persiapan Direktori

```bash
cd soal3/
mkdir -p data/ebooks data/papers data/sourcecode data/docs logs
```

### 2. Build dan Start

```bash
docker compose up --build -d
```

Tunggu ~5 detik agar smbd dan logger selesai diinisialisasi.

### 3. Verifikasi

```bash
# Verifikasi user dan grup
docker exec libraryit-server pdbedit -L
docker exec libraryit-server getent group staff readonly

# Verifikasi share visibility
smbclient -L //localhost -p 1445 -U member%member123

# Verifikasi akses
docker exec libraryit-server cat /libraryit/logs/libraryit.log
```

### 4. Streaming Log

```bash
docker logs -f libraryit-logger
```

### 5. Menghentikan

```bash
docker compose down
```

### 6. Membersihkan

```bash
docker compose down --rmi all
find data/ -type f -not -name ".gitkeep" -delete
find logs/ -type f -not -name ".gitkeep" -delete
```
