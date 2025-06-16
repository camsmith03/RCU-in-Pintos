#include <thread.h>

/* Initialized to NULL until the GP thread is spawned during initialization */
static struct thread *rcu_gp_thread = NULL;


/* To maintain consistency with how the GP thread is scheduled, we avoid
   yielding the CPU like all other threads to push it back in the RQ.

   Instead, we unblock the thread from a timer interrupt on a constant interval
   that is repeatable. The initial nice value of the GP thread is set to
   NICE_MIN to ensure its immediate scheduling post-timer interrupt. Since the
   GP thread has constant time complexity, this should not lead to a noticible
   impact on performance (assuming the ideal interval is selected).
 */
void
thread_block_gp_thread (void)
{
  ASSERT (intr_context () == INTR_OFF);
  ASSERT (rcu_gp_thread != NULL);
  ASSERT (rcu_gp_thread->status == THREAD_RUNNING);

  if (rcu_gp_thread != thread_current ())
    PANIC ("Thread %s attempting to block is not the GP Thread!",
           running_thread ()->name);

  rcu_gp_thread->status = THREAD_BLOCKED;
  lock_own_ready_queue ();
  schedule ();
  unlock_own_ready_queue ();
}

/* Spawn the RCU GP thread */
void
thread_spawn_gp_thread (thread_func gp_func)
{
  ASSERT (rcu_gp_thread == NULL);

  struct thread *gp;

  gp = do_thread_create ("RCU GP Thread", NICE_MIN, gp_func, NULL);
  rcu_gp_thread = gp;
  gp->cpu = bcpu;
  gp->status = THREAD_BLOCKED;

  /* We do NOT call wake_up_new_thread here, as that will assign the gp thread
   * to some arbitrary CPU. This thread should only ever be running on the bcpu
   *
   * Wait till the next timer interrupt interval to start the GP thread */
}

/* Kill the RCU GP thread */
void
thread_kill_gp_thread (void)
{
  ASSERT (intr_context () == INTR_OFF);
  ASSERT (rcu_gp_thread == thread_current ());

  intr_enable_pop ();

  do_thread_exit ();
  NOT_REACHED ();
}

