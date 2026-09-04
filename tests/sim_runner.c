#include "selftest/sim_runner.h"
#include "selftest/monitor_test.h"
#include "selftest/mutex_test.h"
#include "selftest/rcu_cow_test.h"
#include "selftest/rcu_test.h"
#include "selftest/sema_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "selftest/spinlock_test.h"
#include "selftest/test_types.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/spinlock.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <atomic-ops.h>

static void initialize_shared_data (enum test_type, enum concurrency_type);
static void spawn_readers_and_writers (enum test_type, enum concurrency_type);
static void spawn_threads (const char *, thread_func, enum test_type,
                           uint32_t);

void *shared_data;
struct spinlock shared_data_lock;

struct spinlock read_cycles_lock;
struct spinlock write_cycles_lock;
uint64_t read_cycles_total;
uint64_t write_cycles_total;
uint64_t write_cycles_sync;
uint64_t write_cycles_async;

uint32_t op_sequence_counter;

struct condition simulation_runner_cond;
struct condition threads_start_cond;
struct lock threads_start_mutex;
struct lock threads_end_mutex;
bool threads_start_flag;
uint32_t threads_spawned;
uint32_t threads_active;

uint32_t reader_iterations;
uint32_t writer_iterations;
uint32_t writer_iterations_async;
uint32_t writer_iterations_sync;

uint32_t reader_threads;
uint32_t writer_threads;
uint32_t writer_threads_sync;
uint32_t writer_threads_async;
uint32_t threads_spawned; // == reader_threads + writer_threads

void
start_simulation (enum test_type test_type,
                  enum concurrency_type concurrency_type,
                  enum access_pattern access_pattern)
{
  /* Initialize the reader and writer counts
   *
   * Here are the defaults for each access pattern:
   *
   *   READ_MOSTLY
   *    -> 8 cores :  6 readers, 2 writers
   *    -> 32 cores: 28 readers, 4 writers
   *
   *   READ_WRITE
   *    -> 8 cores :  4 readers,  4 writers
   *    -> 32 cores: 16 readers, 16 writers
   *
   *   WRITE_MOSTLY
   *    -> 8 cores : 2 readers,  6 writers
   *    -> 32 cores: 4 readers, 28 writers
   *
   * */
  switch (access_pattern)
    {
    case READ_ONLY:
      reader_iterations = 200000000;
      writer_iterations_async = 0;
      writer_iterations_sync = 0;
      writer_iterations = 0;
      reader_threads = ncpu;
      writer_threads = 0;
      writer_threads_sync = 0;
      writer_threads_async = 0;
      break;

    case READ_MOSTLY:
      if (ncpu < 4)
        print_fatal_error ("access pattern work best with 4+ CPUs\n");

      reader_iterations = 100000000;
      writer_iterations_async = 100000000;
      writer_iterations_sync = 0;
      writer_iterations = writer_iterations_async + writer_iterations_sync;

      writer_threads_sync = 0;
      writer_threads_async = ncpu / 4;
      writer_threads = writer_threads_sync + writer_threads_async;
      reader_threads = ncpu - writer_threads;
      break;

    case READ_WRITE:
      if (ncpu < 4)
        print_fatal_error ("access pattern work best with 4+ CPUs\n");

      reader_iterations = 100000000;
      writer_iterations_async = 100000000;
      writer_iterations_sync = 0;
      writer_iterations = writer_iterations_async + writer_iterations_sync;

      writer_threads_sync = 0;
      writer_threads_async = ncpu / 2;
      writer_threads = writer_threads_sync + writer_threads_async;
      reader_threads = ncpu - writer_threads;
      break;

    case WRITE_MOSTLY:
      if (ncpu < 4)
        print_fatal_error ("access pattern work best with 4+ CPUs\n");

      reader_iterations = 100000000;
      writer_iterations_async = 100000000;
      writer_iterations_sync = 0;
      writer_iterations = writer_iterations_async + writer_iterations_sync;

      reader_threads = ncpu / 4;
      writer_threads_sync = 0;
      writer_threads_async = ncpu - reader_threads;
      writer_threads = writer_threads_sync + writer_threads_async;
      break;
    }

  printf ("Intializing shared data.\n");
  initialize_shared_data (test_type, concurrency_type);
  printf ("Spawning reader and writer threads.\n");
  spawn_readers_and_writers (test_type, concurrency_type);

  lock_acquire (&threads_end_mutex);

  /* The simulation runner will block until all spawned readers and writers
   * have finished. This is guaranteed to occur once the threads_start_flag is
   * set to true and the threads_active count reaches zero. The last thread to
   * exit will signal on the simulation_runner_cond */
  while (threads_active > 0 || !threads_start_flag)
    cond_wait (&simulation_runner_cond, &threads_end_mutex);

  lock_release (&threads_end_mutex);

  print_final_results (concurrency_type, test_type, access_pattern);
}

static void
initialize_shared_data (enum test_type test_type,
                        enum concurrency_type concurrency_type)
{

  if (test_type == POINTER)
    {
      struct data *data;
      struct sema_data *sema_data;
      struct mutex_data *mutex_data;
      struct monitor_data *monitor_data;

      switch (concurrency_type)
        {
        case RCU:
        case SPINLOCK:
          data = malloc (sizeof (struct data));
          data->val = 0;
          data->magic = DATA_MAGIC;
          spinlock_init (&shared_data_lock);
          shared_data = (void *)data;
          break;

        case MUTEX:
          mutex_data = malloc (sizeof (struct mutex_data));
          lock_init (&mutex_data->mutex);
          mutex_data->data.val = 0;
          mutex_data->data.magic = DATA_MAGIC;
          shared_data = (void *)mutex_data;
          break;

        case SEMA:
          sema_data = malloc (sizeof (struct sema_data));
          sema_init (&sema_data->sema, 1);
          sema_data->data.val = 0;
          sema_data->data.magic = DATA_MAGIC;
          shared_data = (void *)sema_data;
          break;

        case MONITOR:
          monitor_data = malloc (sizeof (struct monitor_data));
          cond_init (&monitor_data->reader_cond);
          cond_init (&monitor_data->writer_cond);
          lock_init (&monitor_data->mutex);
          monitor_data->read_cnt = 0;
          monitor_data->write_cnt = 0;
          monitor_data->data.val = 0;
          monitor_data->data.magic = DATA_MAGIC;
          shared_data = (void *)monitor_data;
          break;

        case RCU_COW:
          print_fatal_error ("RCU COW only intended for LIST tests!\n");
          break;
        }
    }
  else if (test_type == LIST)
    {
      /* Start with one initial element per list */
      struct list_data *init_list_data;
      struct list *generic_list;
      struct sema_list *sema_list;
      struct mutex_list *mutex_list;
      struct monitor_list *monitor_list;

      /* Set the val for writer threads to use (only used for list tests) */
      op_sequence_counter = 0;

      /* Initial value to store in the protected list */
      init_list_data = malloc (sizeof (struct list_data));
      init_list_data->magic = DATA_MAGIC;
      init_list_data->val = 0;

      switch (concurrency_type)
        {
        case RCU:
        case SPINLOCK:
          spinlock_init (&shared_data_lock);
          generic_list = malloc (sizeof (struct list));
          list_init (generic_list);
          list_push_front (generic_list, &init_list_data->elem);
          shared_data = (void *)generic_list;
          break;

        case RCU_COW:
          spinlock_init (&shared_data_lock);
          init_list_data->elem.next = NULL;
          shared_data = (void *)(&init_list_data->elem); // starting root
          break;

        case SEMA:
          sema_list = malloc (sizeof (struct sema_list));
          sema_init (&sema_list->sema, 1);
          list_init (&sema_list->list);
          list_push_front (&sema_list->list, &init_list_data->elem);
          shared_data = (void *)sema_list;
          break;

        case MUTEX:
          mutex_list = malloc (sizeof (struct mutex_list));
          lock_init (&mutex_list->mutex);
          list_init (&mutex_list->list);
          list_push_front (&mutex_list->list, &init_list_data->elem);
          shared_data = (void *)mutex_list;
          break;

        case MONITOR:
          monitor_list = malloc (sizeof (struct monitor_list));
          cond_init (&monitor_list->reader_cond);
          cond_init (&monitor_list->writer_cond);
          lock_init (&monitor_list->mutex);
          list_init (&monitor_list->list);
          monitor_list->read_cnt = 0;
          monitor_list->write_cnt = 0;
          list_push_front (&monitor_list->list, &init_list_data->elem);
          shared_data = (void *)monitor_list;
          break;
        }
    }
  else if (test_type == SYNC_OPS)
    {
      struct spinlock *spinlock;
      struct semaphore *semaphore;
      struct lock *mutex;

      switch (concurrency_type)
        {
        case RCU:
          break;

        case SPINLOCK:
          spinlock = malloc (sizeof (struct spinlock));
          spinlock_init (spinlock);
          shared_data = (void *)spinlock;
          break;

        case SEMA:
          semaphore = malloc (sizeof (struct semaphore));
          sema_init (semaphore, 1);
          shared_data = (void *)semaphore;
          break;

        case MUTEX:
          mutex = malloc (sizeof (struct lock));
          lock_init (mutex);
          shared_data = (void *)mutex;
          break;

        case MONITOR:
        case RCU_COW:
          print_fatal_error ("Test doesn't support that concurrency type");
        }
    }

  else
    {
      print_fatal_error ("Test type not supported!");
    }

  /* ============================================================ */
  /* ================ Universal shared variables ================ */
  /* ============================================================ */

  /* Number of elapsed cycles between all read-side critical sections. Dividing
   * by the number of spawned readers at the end will yield the average CS
   * duration. */
  read_cycles_total = 0;

  /* Number of elapsed write cycles between all write-side critical sections.
   * Dividing by the number of spawned writers at the end will yield the
   * average CS duration.  */
  write_cycles_total = 0;

  /* For RCU tests, both will be incremented when a writer exits */
  if (concurrency_type == RCU || concurrency_type == RCU_COW)
    {
      write_cycles_sync = 0;
      write_cycles_async = 0;
    }

  /* Spinlock to protect read_cycles_total */
  spinlock_init (&read_cycles_lock);

  /* Spinlock to protect write_cycles_total, write_cycles_sync, and
   * write_cycles_async.  */
  spinlock_init (&write_cycles_lock);

  /* Total number of threads. Does not change after this assignment  */
  threads_spawned = reader_threads + writer_threads;

  /* Active threads starts at zero, increments up to threads_spawned, then
   * decrements back down to zero.
   *
   * We use it for two instances,
   *
   *  1. To control thread initialization:
   *      - At the start, each reader and writer will increment the
   *        threads_active count, waiting on the thread start condition
   *        variable. Once the last reader/writer increments the active count,
   *        they will flip the threads start flag and broadcast on the threads
   *        start cond. The start flag is what determines whether a thread will
   *        wait on the condition, as a check to the threads_active count could
   *        be misleading if any had already exited.
   *
   *  2. To control ending when the test has ended:
   *      - After spawning all the threads, the simulation runner will block
   *        for the duration of the tests, waiting until the active thread
   *        count reaches zero, after the threads start flag has been flipped.
   *        Each time a reader or writer thread exits, they will decrement the
   *        active threads count. The last to exit (or when the decrement is
   *        zero), will signal to the simulation runner to wake up and print
   *        the results for all the tests.
   */
  threads_active = 0;

  /* Flag that readers and writers loop on for their condition variable,
   * threads_spawn_cond. This check is made in the call to thread_synchronize.
   * Once true, all threads have been initialized and started, meaning at one
   * point the active thread count was equal to the spawned thread count. */
  threads_start_flag = false;

  /* Lock to protect threads_spawn_cond used to control testing thread
   * initialization. Initially this is used to prevent increments to the active
   * thread cound, up until the last increment is made, where threads_end_mutex
   * will take over the responsibility. */
  lock_init (&threads_start_mutex);

  /* Lock used to protect simulation_runner_cond and the active threads
   * decrements once all threads have been initialized. Technically, only one
   * mutex is necessary to serve both purposes, but for clarity it was easier
   * to use two. */
  lock_init (&threads_end_mutex);

  /* Condition waited on by readers and writers in thread_synchronize,
   * broadcasted to by the last thread to start, which will flip the
   * threads_start_flag to true, allowing the tests to begin. */
  cond_init (&threads_start_cond);

  /* Condition waited on by the simulation runner thread to control when the
   * tests are complete. Will be signaled to once all testing threads have
   * exited. */
  cond_init (&simulation_runner_cond);
}

/* Spawns the reader and writer threads with the totals intialized at the
 * start. The test type will get passed to each thread individually */
static void
spawn_readers_and_writers (enum test_type test,
                           enum concurrency_type concurrency)
{

  switch (concurrency)
    {
    case RCU:
      spawn_threads ("RCU Reader", rcu_reader, test, reader_threads);
      spawn_threads ("Sync Writer", rcu_sync_writer, test,
                     writer_threads_sync);
      spawn_threads ("Async Writer", rcu_async_writer, test,
                     writer_threads_async);
      break;

    case RCU_COW:
      spawn_threads ("RCU Reader", rcu_cow_reader, test, reader_threads);
      spawn_threads ("Sync Writer", rcu_cow_sync_writer, test,
                     writer_threads_sync);
      spawn_threads ("Async Writer", rcu_cow_async_writer, test,
                     writer_threads_async);
      break;

    case SPINLOCK:
      spawn_threads ("Reader", spinlock_reader, test, reader_threads);
      spawn_threads ("Writer", spinlock_writer, test, writer_threads);
      break;

    case SEMA:
      spawn_threads ("Reader", sema_reader, test, reader_threads);
      spawn_threads ("Writer", sema_writer, test, writer_threads);
      break;

    case MUTEX:
      spawn_threads ("Reader", mutex_reader, test, reader_threads);
      spawn_threads ("Writer", mutex_writer, test, writer_threads);
      break;

    case MONITOR:
      spawn_threads ("Reader", monitor_reader, test, reader_threads);
      spawn_threads ("Writer", monitor_writer, test, writer_threads);
      break;
    }
}

/* Spawns amount threads with thread function, passing the test type into the
 * aux parameter. This is a helper for spawn_readers_and_writers. */
static void
spawn_threads (const char *name, thread_func func, enum test_type test,
               uint32_t amount)
{
  uint32_t i;
  for (i = amount; i > 0; i--)
    thread_create (name, 0, func, (void *)test);
}
