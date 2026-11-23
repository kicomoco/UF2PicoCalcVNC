#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PICOCALC_VNC_VERSION "1.0.0"
#include "../src/update.c"

enum { DEST, TEMP, BACKUP, NFILES };
static struct {
    bool exists;
    BYTE attributes;
    size_t size;
    uint8_t bytes[1024];
} files[NFILES];
static size_t read_offset;
static bool invalid_uf2, cancel_verify, fail_publish, fail_restore, fail_backup_cleanup;
static bool reject_download;
static int checksum_requests, firmware_requests;
static uint8_t download_data[1024];

static int file_index(const char *path) {
    if (!strcmp(path, UPDATE_DEST)) return DEST;
    if (!strcmp(path, UPDATE_TMP)) return TEMP;
    assert(!strcmp(path, UPDATE_BAK));
    return BACKUP;
}

FRESULT f_stat(const TCHAR *path, FILINFO *info) {
    memset(info, 0, sizeof(*info));
    if (!strcmp(path, "/BOOT2350.UF2")) {
        info->fsize = 512;
        return FR_OK;
    }
    int i = file_index(path);
    if (!files[i].exists) return FR_NO_FILE;
    info->fsize = (FSIZE_t)files[i].size;
    info->fattrib = files[i].attributes;
    return FR_OK;
}
FRESULT f_mkdir(const TCHAR *path) {
    assert(!strcmp(path, "/pico2-apps"));
    return FR_EXIST;
}
FRESULT f_open(FIL *f, const TCHAR *path, BYTE mode) {
    int i = file_index(path);
    if (mode & FA_CREATE_ALWAYS) {
        files[i].exists = true;
        files[i].size = 0;
        files[i].attributes = 0;
    } else if (!files[i].exists) return FR_NO_FILE;
    f->obj.id = (WORD)i;
    f->obj.objsize = (FSIZE_t)files[i].size;
    read_offset = 0;
    return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; return FR_OK; }
FRESULT f_read(FIL *f, void *data, UINT size, UINT *count) {
    int i = f->obj.id;
    assert(read_offset + size <= files[i].size);
    memcpy(data, files[i].bytes + read_offset, size);
    read_offset += size;
    *count = size;
    return FR_OK;
}
FRESULT f_write(FIL *f, const void *data, UINT size, UINT *count) {
    int i = f->obj.id;
    assert(files[i].size + size <= sizeof(files[i].bytes));
    memcpy(files[i].bytes + files[i].size, data, size);
    files[i].size += size;
    *count = size;
    return FR_OK;
}
FRESULT f_unlink(const TCHAR *path) {
    int i = file_index(path);
    if (i == BACKUP && fail_backup_cleanup) return FR_DISK_ERR;
    if (!files[i].exists) return FR_NO_FILE;
    files[i].exists = false;
    return FR_OK;
}
FRESULT f_rename(const TCHAR *from, const TCHAR *to) {
    int a = file_index(from), b = file_index(to);
    if (a == TEMP && b == DEST && fail_publish) return FR_DISK_ERR;
    if (a == BACKUP && b == DEST && fail_restore) return FR_DISK_ERR;
    if (!files[a].exists) return FR_NO_FILE;
    if (files[b].exists) return FR_EXIST;
    files[b] = files[a];
    files[a].exists = false;
    return FR_OK;
}

bool https_init(https_idle_fn fn, char *error, size_t cap) {
    (void)fn; (void)error; (void)cap;
    return true;
}
bool https_get(const char *request, bool head, size_t limit, https_sink_fn sink,
               void *arg, char *redirect, size_t redirect_cap) {
    (void)head; (void)limit; (void)redirect; (void)redirect_cap;
    if (strstr(request, "SHA256SUMS")) {
        checksum_requests++;
        const char *content = "checksum";
        return sink((const uint8_t *)content, strlen(content), arg);
    }
    firmware_requests++;
    assert(strstr(request, "/v1.2.3/picocalc_vnc-v1.2.3.uf2"));
    return !reject_download && sink(download_data, sizeof(download_data), arg);
}
bool update_checksum(const char *text, const char *asset, uint8_t digest[32]) {
    assert(!strcmp(text, "checksum"));
    assert(!strcmp(asset, "picocalc_vnc-v1.2.3.uf2"));
    memset(digest, 0, 32);
    return true;
}
bool update_uf2_block(const uint8_t data[512], uint32_t index, uint32_t *count) {
    (void)data; (void)index;
    *count = 1;
    return !invalid_uf2;
}
void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { (void)ctx; }
void mbedtls_sha256_free(mbedtls_sha256_context *ctx) { (void)ctx; }
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224) { (void)ctx; (void)is224; return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *data, size_t size) {
    (void)ctx; (void)data; (void)size; return 0;
}
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char output[32]) {
    (void)ctx; memset(output, 0, 32); return 0;
}
uint64_t time_us_64(void) { return 0; }
int ui_getkey(void) { return cancel_verify ? KEY_ESC : 0; }
void ui_progress(const char *title, const char *msg) { (void)title; (void)msg; }

static void reset(void) {
    memset(files, 0, sizeof(files));
    memset(download_data, 0x5a, sizeof(download_data));
    error_text[0] = 0;
    strcpy(latest, "v1.2.3");
    invalid_uf2 = cancel_verify = fail_publish = fail_restore = fail_backup_cleanup = reject_download = false;
    checksum_requests = firmware_requests = 0;
}
static void set_old(int index) {
    files[index].exists = true;
    files[index].size = 3;
    memcpy(files[index].bytes, "old", 3);
}
static bool old_at(int index) {
    return files[index].exists && files[index].size == 3 && !memcmp(files[index].bytes, "old", 3);
}

int main(void) {
    reset();
    assert(download());
    assert(checksum_requests == 1 && firmware_requests == 1);
    assert(files[DEST].exists && files[DEST].size == sizeof(download_data));
    assert(!memcmp(files[DEST].bytes, download_data, sizeof(download_data)));
    assert(!files[TEMP].exists && !files[BACKUP].exists);

    reset();
    set_old(DEST);
    assert(download() && firmware_requests == 1);
    assert(files[DEST].size == sizeof(download_data) && !files[BACKUP].exists);

    reset();
    set_old(DEST);
    invalid_uf2 = true;
    assert(!download() && old_at(DEST) && !files[TEMP].exists);
    reset();
    set_old(DEST);
    cancel_verify = true;
    assert(!download() && old_at(DEST) && !files[TEMP].exists && strstr(error_text, "Cancelled"));
    reset();
    set_old(DEST);
    reject_download = true;
    assert(!download() && old_at(DEST) && !files[TEMP].exists);

    reset();
    set_old(DEST);
    fail_publish = true;
    assert(!download() && old_at(DEST) && !files[TEMP].exists && !files[BACKUP].exists);
    reset();
    set_old(DEST);
    fail_publish = fail_restore = true;
    assert(!download() && old_at(BACKUP) && !files[DEST].exists && strstr(error_text, "restore"));
    reset();
    set_old(BACKUP);
    assert(download() && files[DEST].size == sizeof(download_data) && !files[BACKUP].exists);
    reset();
    set_old(BACKUP);
    reject_download = true;
    assert(!download() && old_at(DEST) && !files[BACKUP].exists);
    reset();
    set_old(DEST);
    fail_backup_cleanup = true;
    assert(!download() && old_at(BACKUP) && files[DEST].size == sizeof(download_data));
    assert(strstr(error_text, "could not remove"));
    reset();
    set_old(DEST);
    files[DEST].attributes = AM_DIR;
    assert(!download() && old_at(DEST) && !files[TEMP].exists);
    puts("Update download and replacement tests passed.");
    return 0;
}
