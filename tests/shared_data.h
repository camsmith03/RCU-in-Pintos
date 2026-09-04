#ifndef SELFTEST_SHARED_DATA_H
#define SELFTEST_SHARED_DATA_H

#include "threads/spinlock.h"
#include "threads/synch.h"
#include <atomic-ops.h>
#include <kernel/list.h>
#include <stdint.h>

/* Use a magic value to check for consistency */
#define DATA_MAGIC 0x82730984

/* Generic shared data structure */
struct data {
  int val;        /* Shared protected value */
  uint32_t magic; /* Magic number for testing */
};

/* Generic shared data list structure */
struct list_data {
  struct list_elem elem; /* Elem in some shared list */
  int val;               /* Shared protected value */
  uint32_t magic;        /* Magic number for testing */
};

/* Reference count protected shared data */
struct ref_cnt_data {
  atomic_int cnt;   /* Readers referencing the struct */
  struct data data; /* Protected data */
};

/* Semaphore protected shared data */
struct sema_data {
  struct semaphore sema; /* Semaphore to protect access to the struct */
  struct data data;      /* Protected data */
};

/* Mutex protected shared data */
struct mutex_data {
  struct lock mutex; /* Mutex to protect access to the struct */
  struct data data;  /* Protected data */
};

/* Monitor protected shared data */
struct monitor_data {
  struct condition reader_cond; /* Reader condition variable */
  struct condition writer_cond; /* Writer condition variable */
  struct lock mutex;            /* Shared data mutex */
  uint32_t read_cnt;            /* Number of active readers */
  uint32_t write_cnt;           /* Number of active writers */
  struct data data;             /* Protected data */
};

/* Semaphore protected shared list data */
struct sema_list {
  struct semaphore sema; /* Semaphore to protect access to the list */
  struct list list;      /* Shared list */
};

/* Mutex protected shared list data */
struct mutex_list {
  struct lock mutex; /* Mutex to protect access to the list */
  struct list list;  /* Shared list */
};

/* Monitor-protected shared list data */
struct monitor_list {
  struct condition reader_cond; /* Reader condition variable */
  struct condition writer_cond; /* Writer condition variable */
  struct lock mutex;            /* Shared data mutex */
  uint32_t read_cnt;            /* Number of active readers */
  uint32_t write_cnt;           /* Number of active writers */
  struct list list;             /* Shared list */
};

/* Casted struct for shared data accesses. Either a single struct (POINTER
 * TEST) or a malloc'd list struct (LIST TEST) */
extern void *shared_data;

/* Spinlock to serialize writes for the tests (RCU, RCU_COW, SPINLOCK) */
extern struct spinlock shared_data_lock;

/* Reader cycle tracking. Using a spinlock to allow 64-bit precision */
extern struct spinlock read_cycles_lock;
extern uint64_t read_cycles_total;

/* Writer cycle tracking. Using a spinlock to allow 64-bit precision */
extern struct spinlock write_cycles_lock;
extern uint64_t write_cycles_total;
extern uint64_t write_cycles_async;
extern uint64_t write_cycles_sync;

/* Number of spawned readers and writers. These values remain the same */
extern uint32_t reader_threads;
extern uint32_t writer_threads;
extern uint32_t writer_threads_async;
extern uint32_t writer_threads_sync;

/* Total number of threads spawned. This value remains the same. */
extern uint32_t threads_spawned;

/* Keep track of present readers and writers for sim runner thread. This will
 * start at total_threads_spawned, decrementing to zero on test completion.
 *
 * Readers and writers will both decrement this value once finished in the call
 * to sim_thread_exit(). Decrements after after the loop iterations have
 * completed.
 */
extern uint32_t threads_active;

/* We have 3 list operations (add, update, delete), and perform them in a
 * round-robin style using a modulus over this atomic counter to guarantee each
 * operation is performed.
 *
 * The value of the increment "N" [ where N = next_val++ ] noticed by a writer
 * thread determines the operation the update will perform.
 *
 * Updates to this value are protected by the write-side synchronization, which
 * is why it need not be atomic.
 *
 * Either we:
 *  a) ADD:    add a new entry to the back of the list with data->val = N
 *  b) UPDATE: increment the value of the first entry where data->val >= N / 2
 *  c) DELETE: remove the first entry where data->val >= N / 2
 */
extern uint32_t op_sequence_counter;

/* All reader and writer threads will threads will be created, incrementing the
 * `active_threads` count. The function used to perform these actions for the
 * spawned threads is called `thread_synchronize()`.
 *
 * They will then wait on the condition variable `thread_spawn_cond`.
 *
 * The simulation runner thread will wait on the condition variable
 * `simulation_runner_cond`, until the `active_threads` count reaches the
 * `total_threads_spawned` count.
 *
 * The last spawned thread (writer or reader) will increment the
 * `active_threads` count to the total, and signal on the
 * `simulation_runner_cond` variable to wake it.
 *
 * This is to provide fine-grained control, ensuring the thread structs are all
 * initialized prior to the test start.
 */
extern struct condition threads_start_cond;
extern struct condition simulation_runner_cond;
extern struct lock threads_start_mutex;
extern struct lock threads_end_mutex;
extern bool threads_start_flag;

/* Number of reads/writes each thread will make. This is the number of loops
 * for each thread in their respective functions */
extern uint32_t reader_iterations;
extern uint32_t writer_iterations;
extern uint32_t writer_iterations_async;
extern uint32_t writer_iterations_sync;

#endif
