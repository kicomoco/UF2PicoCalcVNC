#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*https_sink_fn)(const uint8_t *data, size_t len, void *arg);
typedef bool (*https_idle_fn)(void);

// All requests verify the certificate chain, host name and validity dates.
bool https_init(https_idle_fn idle, char *error, size_t cap);
void https_free(void);
// HEAD returns the Location header without following it. GET follows HTTPS redirects.
bool https_get(const char *url, bool head, size_t limit, https_sink_fn sink,
               void *arg, char *location, size_t location_cap);
