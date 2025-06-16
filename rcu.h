#ifndef __LIB_KERNEL_RCU_H
#define __LIB_KERNEL_RCU_H

#include "threads/interrupt.h"
#include "threads/synch.h"
#include <atomic-ops.h>
#include <kernel/list.h>

/* Since Pintos is a non-preemptable kernel, we define the boundaries of RCU
 * read-side critical sections as the enabling and disabling of preemption.
 *
 * During this section, we guarantee that no context switches will occur, and
 * therefore no quiescent state will be reached.
 */
#define rcu_read_lock() intr_disable_push()
#define rcu_read_unlock() intr_enable_pop()

/* We can avoid compiler optimizations attempting to store the accessed pointer
 * into a register by casting as volatile before the dereference.
 *
 * This will force the data to be read from the CPUs cache (where it may or may
 * not be invalid). This is based on the Linux kernel's RCU implementation.
 */
#define ACCESS_ONCE(P) (*(volatile typeof(P) *)&(P))

/* For any RCU pointer to be dereferenced, this call is vital to ensure the
 * atomicity is guaranteed. Using the ACCESS_ONCE macro, we can ensure that the
 * dereference is atomic and that the compiler does not optimize the read.
 */
#define rcu_dereference(P)                                                     \
  ({                                                                           \
    typeof(P) _P = ACCESS_ONCE(P);                                             \
    (_P);                                                                      \
  })

/* Allow for the writer to assign a new updated pointer VAL, to the RCU-
 * protected address P.
 *
 * We place the barrier before the assignment to ensure that the compiler does
 * not reorder the assignment with any other operations local to this core that
 * came before it.
 */
#define rcu_assign_pointer(P, VAL)                                             \
  ({                                                                           \
    barrier();                                                                 \
    ACCESS_ONCE(P) = (VAL);                                                    \
  })

/* The rcu callback function is used to clean up the shared data structure
 * after no more readers are referencing it */
typedef void (*rcu_callback_t)(const void *data, void *aux);

/* Each CPU will have two GP data structure pointers.
 *
 * These are dynamically allocated, containing the current list of deferred
 * frees with callback functions (async writes) and blocked writer threads
 * (sync writes).
 *
 * The two seperate GP structure pointers are:
 *
 *   1. rcu_curr_gp: The finalized lists of updates scheduled for processing
 *                   after the next GP is observed. Updates cannot be written
 *                   to these lists.
 *
 *   2. rcu_next_gp: The current lists where updates will be appended to until
 *                   the next GP is observed.
 *
 * When a GP is observed during a QS for the CPU (new epoch value found), the
 * rcu_curr_gp lists will be emptied. Then, the rcu_curr_gp and rcu_next_gp
 * pointers will be swapped. The rcu_next_gp will then be empty, allowing for
 * updates to be appended to it.
 */
struct rcu_cpu_lists {
  /* CPU-local RCU deferred free list. This is where asynchronous updates will
   * place their callback functions on data possibly still within reference */
  struct list deferred_free_list;

  /* CPU-local RCU blocked_writers. This is where synchronous update threads
   * will be appended to, moved back into the ready queue once the next GP is
   * observed */
  struct list blocked_writers;
};

void rcu_init(void);
void rcu_spawn_gp_thread(void);
void rcu_shutdown(void);
void call_rcu(const void *data, rcu_callback_t callback_func);
void synchronize_rcu(void);
void rcu_quiescent_state(void);

/* Each loop iteration requires the end check to be protected by a call to 
 * rcu_dereference. It doesn't hurt to call it on the elem->next field, however
 * we are guaranteed enough protection here assuming this is used properly */
#define list_for_each_rcu(LIST, ELEM)                                          \
  for (ELEM = list_begin(LIST); rcu_dereference(ELEM) != list_end(LIST);       \
       ELEM = ELEM->next)


#ifdef TESTING
void rcu_print_stats(void);
#endif 

#endif /* lib/kernel/rcu.h */
