#pragma once
#include <stdint.h>

// Single-block DES encryption (ECB), as used by VNC authentication.
void des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);

// VNC authentication: encrypt 16-byte challenge with password-derived key.
void vnc_des_response(const char *password, const uint8_t challenge[16], uint8_t response[16]);
