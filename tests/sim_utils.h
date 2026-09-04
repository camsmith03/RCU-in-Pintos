#ifndef SELFTEST_SIM_UTILS_H
#define SELFTEST_SIM_UTILS_H

#include <debug.h>
#include <stdio.h>

/* Without NDEBUG, PANICs just stop the kernel w/o printing. This simple
 * wrapper is to just get around that limitation */
#define print_fatal_error(...)                                                 \
  ({                                                                           \
    printf(__VA_ARGS__);                                                       \
    PANIC(__VA_ARGS__);                                                        \
  })

#include "threads/synch.h"
#include <x86.h>

/* Record cycle time to variable "t". Should be of type uint64_t */
#define time()                                                                 \
  ({                                                                           \
    barrier();                                                                 \
    uint64_t _TIME = rdtscp();                                                 \
    barrier();                                                                 \
    (_TIME);                                                                   \
  })

#include "selftest/test_types.h"

void print_final_results(enum concurrency_type, enum test_type,
                         enum access_pattern);

void thread_sychronize(void);
void sim_thread_exit(void);

#include <stdint.h>

void log_write_cycles(uint64_t elapsed_cycles);
void log_read_cycles(uint64_t elapsed_cycles);
void log_sync_write_cycles(uint64_t elapsed_cycles);
void log_async_write_cycles(uint64_t elapsed_cycles);

#endif
