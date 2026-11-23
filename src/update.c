#include "update.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "https.h"
#include "kbd.h"
#include "net.h"
#include "ui.h"
#include "wifi.h"
#include "update_format.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "mbedtls/sha256.h"

#define RELEASES "https://github.com/kicomoco/UF2PicoCalcVNC/releases/"
#define UPDATE_TMP "/pico2-apps/vnc-update.tmp"
#define UPDATE_DEST "/pico2-apps/picocalc_vnc.uf2"
#define UPDATE_BAK "/pico2-apps/vnc-update.bak"
#define UPDATE_MAX (8u * 1024u * 1024u)

static char error_text[160], latest[40], url[256], location[256], filename[80];
static char checksum_text[256], progress_text[96];
static size_t checksum_len, downloaded;
static uint8_t expected[32], actual[32], block[512];
static FIL file;
static mbedtls_sha256_context sha;
static uint64_t last_progress;

static bool idle(void) {
    int key = ui_getkey();
    if (key == UI_KEY_REDRAW) ui_progress("Updates", progress_text);
    return key != KEY_ESC;
}

static void progress(const char *text) {
    snprintf(progress_text, sizeof(progress_text), "%s", text);
    ui_progress("Updates", progress_text);
}

static bool failed(const char *text) {
    snprintf(error_text, sizeof(error_text), "%s", text);
    return false;
}

static bool checksum_sink(const uint8_t *data, size_t n, void *arg) {
    (void)arg;
    if (n >= sizeof(checksum_text) - checksum_len) return failed("Checksum file too large.");
    memcpy(checksum_text + checksum_len, data, n);
    checksum_len += n;
    checksum_text[checksum_len] = 0;
    return true;
}

static bool file_sink(const uint8_t *data, size_t n, void *arg) {
    (void)arg;
    UINT written;
    if (f_write(&file, data, (UINT)n, &written) != FR_OK || written != n)
        return failed("SD write failed (card full or removed).");
    downloaded += n;
    if (time_us_64() - last_progress > 500000) {
        snprintf(progress_text, sizeof(progress_text), "Downloaded %u KB...", (unsigned)(downloaded / 1024));
        ui_progress("Updates", progress_text);
        last_progress = time_us_64();
    }
    return true;
}

static bool check_latest(void) {
    latest[0] = 0;
    progress("Checking the latest GitHub release...");
    if (!https_init(idle, error_text, sizeof(error_text))) return false;
    if (!https_get(RELEASES "latest", true, 0, NULL, NULL, location, sizeof(location))) return false;
    if (!strcmp(location, RELEASES) || !strcmp(location, "https://github.com/kicomoco/UF2PicoCalcVNC/releases"))
        return failed("No public release has been published yet.");
    const char prefix[] = RELEASES "tag/";
    uint32_t parsed[3];
    if (strncmp(location, prefix, sizeof(prefix) - 1) ||
        !update_version(location + sizeof(prefix) - 1, parsed) ||
        strlen(location + sizeof(prefix) - 1) >= sizeof(latest))
        return failed("Latest release is not a supported vMAJOR.MINOR.PATCH version.");
    strcpy(latest, location + sizeof(prefix) - 1);
    return true;
}

static bool verify_file(const char *path) {
    if (f_open(&file, path, FA_READ) != FR_OK) return failed("Cannot reopen downloaded firmware.");
    FSIZE_t size = f_size(&file);
    bool ok = size && size <= UPDATE_MAX && size % 512 == 0;
    uint32_t count = 0, index = 0;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts(&sha, 0)) ok = false;
    while (ok && index < size / 512) {
        UINT read;
        ok = f_read(&file, block, sizeof(block), &read) == FR_OK && read == sizeof(block) &&
             update_uf2_block(block, index, &count) &&
             mbedtls_sha256_update(&sha, block, sizeof(block)) == 0;
        if (ok && !idle()) { failed("Cancelled."); ok = false; }
        ++index;
    }
    if (mbedtls_sha256_finish(&sha, actual)) ok = false;
    mbedtls_sha256_free(&sha);
    if (f_close(&file) != FR_OK) ok = false;
    if (!ok || count + 1 != size / 512 || memcmp(actual, expected, sizeof(actual)))
        return error_text[0] ? false : failed("Downloaded firmware failed UF2/SHA-256 verification.");
    return true;
}

static bool recover_previous_update(void) {
    FILINFO info;
    FRESULT rc = f_stat(UPDATE_BAK, &info);
    if (rc == FR_OK) {
        if (info.fattrib & AM_DIR) return failed("Firmware backup path is a directory.");
        rc = f_stat(UPDATE_DEST, &info);
        if (rc == FR_NO_FILE) {
            if (f_rename(UPDATE_BAK, UPDATE_DEST) != FR_OK)
                return failed("Cannot restore previous firmware from vnc-update.bak.");
        } else if (rc != FR_OK) {
            return failed("Cannot inspect the existing firmware on the SD card.");
        } else if (info.fattrib & AM_DIR) {
            return failed("Firmware destination is a directory.");
        } else if (f_unlink(UPDATE_BAK) != FR_OK) {
            return failed("Cannot remove the previous firmware backup.");
        }
    } else if (rc != FR_NO_FILE) {
        return failed("Cannot inspect the firmware backup on the SD card.");
    }
    return true;
}

static bool publish_update(void) {
    FILINFO info;
    FRESULT rc = f_stat(UPDATE_DEST, &info);
    if (rc == FR_OK && (info.fattrib & AM_DIR)) return failed("Firmware destination is a directory.");
    if (rc != FR_OK && rc != FR_NO_FILE) return failed("Cannot inspect the existing firmware on the SD card.");
    rc = f_rename(UPDATE_DEST, UPDATE_BAK);
    if (rc != FR_OK && rc != FR_NO_FILE)
        return failed("Cannot back up the existing firmware on the SD card.");
    bool backed_up = rc == FR_OK;
    if (f_rename(UPDATE_TMP, UPDATE_DEST) != FR_OK) {
        if (backed_up && f_rename(UPDATE_BAK, UPDATE_DEST) != FR_OK)
            return failed("Could not publish update or restore firmware. Backup: vnc-update.bak.");
        return failed("Could not publish update on SD card.");
    }
    if (backed_up && f_unlink(UPDATE_BAK) != FR_OK)
        return failed("Update saved, but could not remove vnc-update.bak.");
    return true;
}

static bool download(void) {
    static FILINFO info;
    if (f_stat("/BOOT2350.UF2", &info) != FR_OK || !info.fsize || (info.fattrib & AM_DIR))
        return failed("BOOT2350.UF2 is missing from the SD card. Cannot reboot into UF2Loader.");
    FRESULT rc = f_mkdir("/pico2-apps");
    if (rc != FR_OK && rc != FR_EXIST) return failed("Cannot create pico2-apps on the SD card.");
    if (!recover_previous_update()) return false;
    snprintf(filename, sizeof(filename), "picocalc_vnc-%s.uf2", latest);
    progress("Downloading release checksum...");
    if (!https_init(idle, error_text, sizeof(error_text))) return false;
    snprintf(url, sizeof(url), RELEASES "download/%s/SHA256SUMS", latest);
    checksum_len = 0;
    if (!https_get(url, false, sizeof(checksum_text) - 1, checksum_sink, NULL, NULL, 0)) return false;
    if (!update_checksum(checksum_text, filename, expected)) return failed("Missing or invalid firmware checksum.");
    if (f_open(&file, UPDATE_TMP, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return failed("Cannot create update file on the SD card.");
    progress("Downloading firmware...");
    downloaded = 0;
    last_progress = time_us_64();
    snprintf(url, sizeof(url), RELEASES "download/%s/%s", latest, filename);
    bool ok = https_get(url, false, UPDATE_MAX, file_sink, NULL, NULL, 0);
    if (f_close(&file) != FR_OK) { failed("Could not flush firmware to the SD card."); ok = false; }
    if (ok) {
        progress("Verifying firmware from SD card...");
        ok = verify_file(UPDATE_TMP);
    }
    if (ok) ok = publish_update();
    if (!ok) {
        rc = f_unlink(UPDATE_TMP);
        if (rc != FR_OK && rc != FR_NO_FILE)
            failed("Update failed; could not remove vnc-update.tmp from the SD card.");
    }
    return ok;
}

static void reboot_loader(void) {
    ui_progress_note("Updates", "Verified. Rebooting to UF2Loader...");
    sleep_ms(500);
    // UF2Loader stage3 command protocol: magic, BOOT_SD, argument.
    watchdog_hw->scratch[1] = 1;
    watchdog_hw->scratch[2] = 0;
    watchdog_hw->scratch[0] = 0xe98cc638;
    watchdog_reboot(0, 0, 100);
    for (;;) tight_loop_contents();
}

bool update_screen(void) {
    int selection = 2;
    for (;;) {
        ui_help_ctx = HELP_UPDATE;
        static ui_item_t entries[5];
        memset(entries, 0, sizeof(entries));
        snprintf(entries[0].text, sizeof(entries[0].text), "Current: %s", PICOCALC_VNC_VERSION);
        entries[0].flags = UI_ITEM_SEP;
        snprintf(entries[1].text, sizeof(entries[1].text), "Latest: %.35s", latest[0] ? latest : "not checked");
        entries[1].flags = UI_ITEM_SEP;
        snprintf(entries[2].text, sizeof(entries[2].text), "Check GitHub for updates");
        snprintf(entries[3].text, sizeof(entries[3].text), "Download update & reboot");
        snprintf(entries[4].text, sizeof(entries[4].text), "Back");
        uint32_t current[3], remote[3];
        bool newer = latest[0] && update_version(latest, remote) &&
                     update_version(PICOCALC_VNC_VERSION, current) && update_compare(remote, current) > 0;
        bool online = wifi_connected();
        entries[2].flags = online ? 0 : UI_ITEM_DISABLED;
        entries[3].flags = online && newer && config_storage_ok() ? 0 : UI_ITEM_DISABLED;
        const int exits[] = {KEY_F2, 0};
        int key = 0;
        int result = ui_list("Firmware updates", "F1: help  Enter: select  Esc: back", entries, 5, &selection, exits, &key);
        if (result == -2) return true;
        if (result == -1 || result == 4) return false;
        if (!online) { ui_message("Updates", "Connect to WiFi first."); continue; }
        if (result == 3) {
            if (!newer) { ui_message("Updates", "Check for a newer release first."); continue; }
            if (!config_storage_ok()) { ui_message("Updates", "A writable SD card is required."); continue; }
            if (!ui_confirm("Install update",
                "Download, verify and restart into UF2Loader?\n\nSelect picocalc_vnc.uf2 in the loader to install it."))
                continue;
        }
        error_text[0] = 0;
        bool ok = result == 2 ? check_latest() : download();
        https_free();
        ui_progress_end();
        if (!ok) ui_message("Update failed", error_text);
        else if (result == 3) reboot_loader();
        else {
            update_version(latest, remote);
            update_version(PICOCALC_VNC_VERSION, current);
            ui_message("Updates", update_compare(remote, current) > 0 ?
                "A newer release is available. Select Download update & reboot." :
                "You are running the latest version (or a newer build).");
        }
    }
}
