#include "list.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <atomic-ops.h>
#include <debug.h>
#include <kernel/rcu.h>
#include <stdio.h>

#define RELAXED memory_order_relaxed
#define SEQ_CST memory_order_seq_cst
#define RELEASE memory_order_release
#define ACQUIRE memory_order_acquire

static void rcu_grace_period_thread (void *);
static bool rcu_grace_period_elapsed (void);

/* Structure for asynchronous updates to be appended to a CPU-local free list.
 *
 * This is used to defer the free of the data until a GP has been reached. The
 * callback function is called on the data when the GP is reached.
 */
struct rcu_deferred_free
{
  struct list_elem elem;        /* Element of a per-CPU deferred free list */
  void *data;                   /* Pointer to protected data */
  rcu_callback_t callback_func; /* Function called on the data */
};

/* Global Epoch-Based Reclaimation (EBR) timer.
 *
 * Counts the number of grace periods that have elapsed. The GP thread will
 * increment this value once every CPU has seen the latest epoch. Each CPU has
 * a local field to track the last observed epoch on their last QS.
 *
 * All operations on the global epoch are atomic with relaxed ordering. This is
 * to avoid contention on the shared variable, as the frequency the CPUs reach
 * a QS is typically very high.
 */
atomic_int global_epoch;

/* Shutdown flag to tell RCU GP thread to exit its loops */
static atomic_int rcu_shutdown_flag;

// Remove these later (mainly to test if calls are working)
static atomic_int rcu_sync_write_cnt;
static atomic_int rcu_async_write_cnt;

/* Initializes RCU and the relevant data structures */
void
rcu_init (void)
{
  int i;
  global_epoch = 1; // Start at 1 to avoid confusion with the initial value for
                    // the CPUs

  rcu_sync_write_cnt = 0;
  rcu_async_write_cnt = 0;

  // Set the shutdown flag to false, used by the GP thread
  rcu_shutdown_flag = 0;

  ASSERT (__atomic_always_lock_free (sizeof (atomic_int), &global_epoch));
  ASSERT (__atomic_always_lock_free (sizeof (atomic_int), &rcu_shutdown_flag));

  /* Initializes every list up to NCPU_MAX. Doing so provides even more
   * flexibility as to places RCU can be integrated into the Pintos kernel,
   * as the value of ncpu isn't known until the call to acpi_init(..). Since
   * the list initializations are very low on overhead, it won't impact
   * performace, but future-proofs our ability to make use of RCU */
  for (i = 0; i < NCPU_MAX; i++)
    {
      // These lists will be local to each CPU once ACPI initializes cpus arr
      list_init (&global_rcu_data[i].curr_gp.deferred_free_list);
      list_init (&global_rcu_data[i].curr_gp.blocked_writers);
      list_init (&global_rcu_data[i].next_gp.deferred_free_list);
      list_init (&global_rcu_data[i].next_gp.blocked_writers);
    }
}

/* Starts the RCU grace period thread to allow for RCU data reclaimation to
   begin. This is not vital to the immediate usage of RCU, however any deferred
   frees or blocked writers will not be addressed until the GP thread has
   started.

   In between that duration, async writes should be used if they do occur.
 */
void
rcu_spawn_gp_thread (void)
{
  /* Spawn the RCU GP daemon thread */
  thread_spawn_gp_thread (rcu_grace_period_thread);
}

/* Shutdown RCU, killing the GP thread */
void
rcu_shutdown (void)
{
  atomic_store_explicit (&rcu_shutdown_flag, 1, RELEASE);
}

/* Print stats on system shutdown */
void
rcu_print_stats (void)
{
  printf (
      "RCU: %d measured GPs, %d synchronous writes, %d asynchronous writes\n",
      atomic_load (&global_epoch), atomic_load (&rcu_sync_write_cnt),
      atomic_load (&rcu_async_write_cnt));
}

/* RCU ayncronous updates (non-blocking).
 *
 * This function is used to register a callback function to be called on the
 * RCU-protected data once the next grace period has been reached. The writer
 * thread will not be blocked during this call.
 */
void
call_rcu (const void *data, rcu_callback_t callback_func)
{
  ASSERT (data != NULL);
  ASSERT ((void *)callback_func != NULL);

  struct rcu_deferred_free *new_df;
  struct cpu *cpu;
  new_df = malloc (sizeof (struct rcu_deferred_free));

  // Remove this later
  atomic_inci (&rcu_async_write_cnt);

  intr_disable_push ();

  new_df->callback_func = callback_func;
  new_df->data = (void *)data;

  cpu = thread_current ()->cpu;

  list_push_front (&cpu->rcu_next_gp->deferred_free_list, &new_df->elem);

  intr_enable_pop ();
}

/* RCU synchronous updates (blocking).
 *
 * This function is used to block the writer thread until the next grace period
 * has been reached. This should only be called after the GP thread has been
 * started.
 *
 * The thread will be moved into the blocked writer queue for the CPU, and
 * will be unblocked once the next GP is reached (after the current one is
 * hit).
 *
 * The writer spinlock used to serialize updates should be released prior to
 * calling this function. Multiple threads can be batched for a single GP to
 * amortize the cost.
 *
 * Note: This does not free the RCU-protected data. The writer thread must do
 *       that after this call returns, where it will be safe to do so.
 */
void
synchronize_rcu (void)
{
  struct thread *t;
  intr_disable ();
  t = thread_current ();

  // Remove this later
  atomic_inci (&rcu_sync_write_cnt);

  /* Insert the thread into the blocked writer queue for its CPU */
  list_push_front (&t->cpu->rcu_next_gp->blocked_writers, &t->elem);

  thread_block (NULL); // context switch to different thread

  intr_enable ();
}

/* Called by each CPU to mark that it haas traversed through a QS.
 *
 * Sees if the GP thread advanced the epoch counter and frees the elements in
 * the lists for the cpu_curr_gp pointer. It will then swap the two pointers.
 *
 * If the CPU's local epoch is equal to the global epoch, then this will return
 * immediately as the CPU has already seen the current GP. If the GP is
 * observed, the CPU will flip its QS flag to true, indicating it has seen the
 * current GP.
 */
void
rcu_quiescent_state (void)
{
  intr_disable_push ();
  struct cpu *cpu;
  uint32_t curr_epoch;

  cpu = get_cpu ();

  /* Load the global epoch using relaxed memory ordering into a global
   * variable. It can change after this call, but due to the nature of RCU,
   * this is completely fine */
  curr_epoch = (uint32_t)atomic_load_explicit (&global_epoch, RELAXED);

  /* See if the CPU has already seen this GP */
  if (cpu->rcu_last_epoch == curr_epoch)
    {
      intr_enable_pop ();
      return;
    }

  /* The epoch saved for the CPU shouldn't be larger than the current.
   * Some other code must've changed it's epoch so panic the kernel. */
  if (cpu->rcu_last_epoch > curr_epoch)
    PANIC ("CPU %u has an invalid epoch", cpu->id);

  /* Under normal circumstances, the current epoch should only be one greater
   * than the cpu's saved epoch. We allow for any epochs beyond that one, in
   * the event some strange bug ocurs that causes the CPU to miss it. Behavior
   * wouldn't change, but it is not an expected outcome */

  /* Make sure that the CPUs QS flag isn't set to true, otherwise there was a
   * bug with atomic memory ordering */
  if (atomic_load_explicit (&cpu->rcu_qs_reached, ACQUIRE))
    PANIC ("An atomic violation occured with CPU %d", cpu->id);

  /* Notify the GP thread that we've hit a QS */
  atomic_store_explicit (&cpu->rcu_qs_reached, true, RELEASE);

  struct list_elem *e;
  struct rcu_deferred_free *df;
  struct rcu_cpu_lists *curr;
  struct thread *writer;

  /* Save a pointer to the CPU's current GP data struct object for later
   * swapping */
  curr = cpu->rcu_curr_gp;

  /* Swap the next and curr pointers to have new updates added to the
   * emptied lists. If the ready queues are implemented using RCU, this swap
   * coming before the calls to thread_unblock are necessary */
  cpu->rcu_curr_gp = cpu->rcu_next_gp;
  cpu->rcu_next_gp = curr;

  /* Call the callback functions on any RCU-protected data from the deferred
   * free list */
  while (!list_empty (&curr->deferred_free_list))
    {
      e = list_pop_front (&curr->deferred_free_list);
      df = list_entry (e, struct rcu_deferred_free, elem);
      df->callback_func (df->data, NULL);
      free (df); // free the allocated deferred free struct
    }

  /* Loop through the synchronous writer threads, moving them back into the
   * ready queue */
  while (!list_empty (&curr->blocked_writers))
    {
      e = list_pop_front (&curr->blocked_writers);
      writer = list_entry (e, struct thread, elem);
      ASSERT (writer->status == THREAD_BLOCKED);
      thread_unblock (writer);
    }

  /* Update the local epoch */
  cpu->rcu_last_epoch = curr_epoch;
  intr_enable_pop ();
}

/* Grace Period (GP) daemon thread function.
 *
 * Sole modifier of the global epoch. Will check when grace periods have
 * elapsed, flipping all the flags in the cpus who've indicated a QS pass has
 * been made.
 *
 * Any time the GP thread is in execution, interrupts are disabled to ensure it
 * has full control during each run. Instead of thread_yield(), we opt for
 * timer_block_gp_thread() to prevent it from being added back to the CPU's
 * ready queue. This provides more fine grained control over when the GP runs
 * are made.
 */
static void
rcu_grace_period_thread (void *aux UNUSED)
{
  intr_disable_push ();
  unsigned int i;
  int next_epoch;

  for (;;)
    {
      while (!rcu_grace_period_elapsed ())
        {
          if (atomic_load_explicit (&rcu_shutdown_flag, ACQUIRE) == 1)
            thread_kill_gp_thread ();

          intr_enable_pop ();
          thread_block_gp_thread ();
          intr_disable_push ();

          if (atomic_load_explicit (&rcu_shutdown_flag, ACQUIRE) == 1)
            thread_kill_gp_thread ();
        }

      /* Once a GP has elapsed, flip each of the CPUs flags */
      for (i = 0; i < ncpu; i++)
        atomic_store_explicit (&cpus[i].rcu_qs_reached, false, RELEASE);

      /* This is a blantant atomic violation, but the GP thread is the
       * only one who performs updates on the global epoch. To avoid the
       * cost of a sequentially consistent RMW atomic primitive (based on
       * limitations in x86), we can opt for relaxed ordering by breaking
       * the atomicity and performing the R + M seperate from the W. This
       * will minimize contention to save several cycles */
      next_epoch = atomic_load_explicit (&global_epoch, RELAXED) + 1;

      /* If we use RELAXED ordering, why doesn't the compiler reorder the above
       * below the update to the global epoch?
       *   - The compiler won't reorder the dependency (next_epoch) below the
       *     dependent (atomic_store). If we didn't save the loaded global
       *     epoch into a local variable, that guarantee wouldn't exist */
      atomic_store_explicit (&global_epoch, next_epoch, RELAXED);

      /* See if the shutdown flag was set telling the daemon to turn off */
      if (atomic_load_explicit (&rcu_shutdown_flag, ACQUIRE) == 1)
        thread_kill_gp_thread ();
    }

  intr_enable_pop ();
}

/* Checks if each CPU has fliped their QS reached flag to indicate a grace
 * period (GP) has elapsed.
 *
 * Returns true if a GP has elapsed; false otherwise
 */
static bool
rcu_grace_period_elapsed (void)
{
  unsigned int i;
  for (i = 0; i < ncpu; i++)
    if (!atomic_load_explicit (&cpus[i].rcu_qs_reached, ACQUIRE))
      return false;

  return true;
}
