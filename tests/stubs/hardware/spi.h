#pragma once
#include <stddef.h>
#include <stdint.h>
#define spi1 1
#define SPI_CPOL_0 0
#define SPI_CPHA_0 0
#define SPI_MSB_FIRST 0
void spi_init(int spi, unsigned baud);
void spi_set_format(int spi, unsigned bits, int cpol, int cpha, int order);
int spi_write_blocking(int spi, const uint8_t *data, size_t count);
int spi_write16_blocking(int spi, const uint16_t *data, size_t count);
