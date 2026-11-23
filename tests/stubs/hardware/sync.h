#pragma once
#define __dmb() atomic_thread_fence(memory_order_seq_cst)
