#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
void mutex_init(mutex_t *mutex);
void mutex_enter_blocking(mutex_t *mutex);
bool mutex_try_enter(mutex_t *mutex, uint32_t *owner);
void mutex_exit(mutex_t *mutex);
