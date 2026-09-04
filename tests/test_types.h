#ifndef SELFTEST_TEST_TYPES_H
#define SELFTEST_TEST_TYPES_H

/* Different types of concurrency available to use for simulations */
enum concurrency_type { RCU, RCU_COW, SPINLOCK, SEMA, MUTEX, MONITOR };

/* Shared data tests to run using concurrency type */
enum test_type { POINTER, LIST, SYNC_OPS };

/* Different access patterns of reader-writer threads to be spawned */
enum access_pattern { READ_ONLY, READ_MOSTLY, READ_WRITE, WRITE_MOSTLY };

#endif
