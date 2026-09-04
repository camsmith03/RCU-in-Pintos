#include "selftest/sim_utils.h"
#include "selftest/shared_data.h"
#include "selftest/test_types.h"
#include "threads/cpu.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include <inttypes.h>

static const char *const concurrency_type_to_str[] = {
  "RCU", "RCU Copy-On-Write", "Spinlock", "Semaphore", "Mutex", "Monitor"
};

static const char *const test_type_to_str[]
    = { "Shared Pointer", "Shared List", "Read-Side Locking" };

static const char *const access_pattern_to_str[]
    = { "Read-Only", "Read-Mostly", "Read-Write", "Write-Mostly" };

void
print_final_results (enum concurrency_type concurrency_type,
                     enum test_type test_type,
                     enum access_pattern access_pattern)
{
  uint64_t avg_read_cycles, avg_write_cycles, avg_sync_cycles,
      avg_async_cycles;

  if (threads_active > 0)
    print_fatal_error ("Threads are still running!\n");

  printf ("\n  ======== Results =======\n");
  printf (" Concurrency Type: %s\n",
          concurrency_type_to_str[concurrency_type]);
  printf (" Test:             %s\n", test_type_to_str[test_type]);
  printf (" Access Pattern:   %s\n", access_pattern_to_str[access_pattern]);
  printf (" # of CPUs:        %u\n", ncpu);
  printf (" Ram Size:         %'" PRIu32 " kB \n\n",
          init_ram_pages * PGSIZE / 1024);

  printf (" Readers:          %u\n", reader_threads);
  if (concurrency_type == RCU || concurrency_type == RCU_COW)
    {
      printf (" Async Writers:    %u\n", writer_threads_async);
      printf (" Sync Writers:     %u\n\n", writer_threads_sync);
    }
  else
    {
      printf (" Writers:          %u\n\n", writer_threads);
    }

  printf (" Reader Loops:     %u\n", reader_iterations);
  if (concurrency_type == RCU || concurrency_type == RCU_COW)
    {
      printf (" Async Loops:      %u\n", writer_iterations_async);
      printf (" Sync Loops:       %u\n\n", writer_iterations_sync);
    }
  else
    {
      printf (" Writer Loops:     %u\n\n", writer_iterations);
    }

  avg_read_cycles = 0;
  if (reader_threads * reader_iterations > 0)
    avg_read_cycles = read_cycles_total / (reader_threads * reader_iterations);

  printf (" Read time total:  %" PRIu64 " cycles\n", read_cycles_total);
  printf (" Read time avg:    %" PRIu64 " cycles\n\n", avg_read_cycles);

  if (concurrency_type == RCU || concurrency_type == RCU_COW)
    {
      printf (" Sync Write time:  %" PRIu64 " cycles\n", write_cycles_sync);

      avg_sync_cycles = 0;
      if (writer_threads_sync * writer_iterations_sync != 0)
        avg_sync_cycles = write_cycles_sync
                          / (writer_threads_sync * writer_iterations_sync);

      printf (" Sync Write avg:   %" PRIu64 " cycles\n\n", avg_sync_cycles);

      printf (" Async Write time: %" PRIu64 " cycles\n", write_cycles_async);

      avg_async_cycles = 0;
      if (writer_threads_async * writer_iterations_async != 0)
        avg_async_cycles = write_cycles_async
                           / (writer_threads_async * writer_iterations_async);

      printf (" Async Write avg:  %" PRIu64 " cycles\n\n", avg_async_cycles);
    }

  printf (" Write time total: %" PRIu64 " cycles\n", write_cycles_total);

  avg_write_cycles = 0;
  if (writer_threads > 0)
    avg_write_cycles
        = write_cycles_total / (writer_threads * writer_iterations);

  printf (" Write time avg:   %" PRIu64 " cycles\n\n", avg_write_cycles);

  uint64_t test_duration = read_cycles_total + write_cycles_total;
  printf (" Total test time:  %" PRIu64 " cycles\n\n", test_duration);
}

void
thread_sychronize (void)
{
  lock_acquire (&threads_start_mutex);

  if ((threads_active += 1) == threads_spawned)
    {
      threads_start_flag = true;
      cond_broadcast (&threads_start_cond, &threads_start_mutex);
    }

  while (!threads_start_flag)
    cond_wait (&threads_start_cond, &threads_start_mutex);

  lock_release (&threads_start_mutex);
}

void
sim_thread_exit (void)
{
  lock_acquire (&threads_end_mutex);

  /* Last thread signals the simulation runner to wake */
  if ((threads_active -= 1) == 0)
    cond_signal (&simulation_runner_cond, &threads_end_mutex);

  lock_release (&threads_end_mutex);

  thread_exit ();
}

void
log_read_cycles (uint64_t elapsed_cycles)
{
  spinlock_acquire (&read_cycles_lock);
  read_cycles_total += elapsed_cycles;
  spinlock_release (&read_cycles_lock);
}

void
log_write_cycles (uint64_t elapsed_cycles)
{
  spinlock_acquire (&write_cycles_lock);
  write_cycles_total += elapsed_cycles;
  spinlock_release (&write_cycles_lock);
}

void
log_sync_write_cycles (uint64_t elapsed_cycles)
{
  spinlock_acquire (&write_cycles_lock);
  write_cycles_total += elapsed_cycles;
  write_cycles_sync += elapsed_cycles;
  spinlock_release (&write_cycles_lock);
}

void
log_async_write_cycles (uint64_t elapsed_cycles)
{
  spinlock_acquire (&write_cycles_lock);
  write_cycles_total += elapsed_cycles;
  write_cycles_async += elapsed_cycles;
  spinlock_release (&write_cycles_lock);
}
