#ifndef __LIB_KERNEL_RCU_H
#define __LIB_KERNEL_RCU_H

/* Read-Copy-Update (RCU) is a method of synchronization that allows for wait-
 * free reading of shared data. It is ideally used in read-mostly situations
 * where no more than 10% of acesses made to some shared data structure are
 * writes.
 *
 *   Side note: It is more in line to say mostly wait-free, as any reads made
 *              in contention to concurrent updates can invalidate their cache
 *              forcing a refetch to the global cache. Assuming usage in the
 *              correct setting, these points of conention should remain rare
 *              enough to have little impact.
 *
 * The 80x86 architecture guarantees that updates to aligned, pointer sized
 * values (sizeof (void*)) are atomic at the hardware level. That is to say,
 * any update to an aligned shared pointer is guaranteed to be performed
 * atomically. This means the reader will see either the old value or the new
 * value, but not any partial changes to the value. This does not imply the
 * update to a shared pointer will be visible to all cores immediately after,
 * but rather that the operation itself will not lead to undefined behavior.
 *
 * The question now becomes when can we ensure that value is no longer valid in
 * the cache of any core, whereby we can free the memory it is using.
 *
 * RCU solves that problem, which is a method that dictactes when old memory
 * can be reclaimed, using the natural atomicity of aligned shared pointers on
 * the 80x86 architecture to its advantage. That is a massive over-
 * simplification, however generally speaking, the core idea is to combine the
 * advantage the architecture provides with techniques to track grace geriods,
 * or the completion of any reads on stale data before reclaimation takes place.
 *
 * For instance, consider the trivial example below (to keep it simple, ignore
 * freeing memory for now):
 *
 *    int *shared_data = malloc (sizeof(int));
 *    *shared_data = 0;
 *
 *    int reader (void) {
 *        return *shared_data;
 *    }
 *
 *    void writer (void) {
 *        int *copy = malloc (sizeof (int));
 *        *copy = 42; // A: initialization
 *
 *        // Think of this as wmb (). A & B will not be reordered across it.
 *        // x86 already carries the guarantee that the update will propogate
 *        // to other cores at some point.
 *        barrier ();
 *        shared_data = copy; // B: update
 *    }
 *
 * In this example, assume we have multiple readers and a single writer running
 * concurrently. The 80x86 architecture guarantees that any reader will either
 * see the value of 0 or 42, but not any intermediate representation of 42.
 *
 * The reason is partly due to Total Store Ordering (TSO) where, simply put, a
 * store buffer is used to control write-backs and changes are propogated via
 * the bus signals to update the cache lines on the reader cores. Real TSO is
 * far more involved, but for the sake of simplicity this serves as a valid
 * abstract explaination.
 *
 * Recall, x86 uses the MESI protocol (Modified, Exclusive, Shared, or Invalid)
 * for the CPU cache coherence. Note that changes are not reflected immediately
 * on the L1 caches of each individual CPU, so reads may occur after the update
 * that see the old value. Assume before the update each core has the shared
 * data in the Shared (S) state:
 *
 *    1. The writer changes its cache line for the shared value from Shared (S)
 *       to Modified (M).
 *
 *    2. The store buffer eventually reflects the change on the globally
 *       accessible cache (L2 or L3 depending on hardware) and places the
 *       BusRdX signal on the shared bus. This signal will set the cache value
 *       from Shared (S) to Invalid (I) for the cache lines of all reader
 *       cores. Think of this as (metaphorically) flushing the store buffer.
 *
 *    3. Upon any subsequent read, the Invalid (I) bit is read from the local
 *       cache, forcing an invalidation (or L1 miss). The new value gets
 *       fetched from the global cache and the local is set back to Shared (S).
 *
 * Without RCU, one approach to tracking whether the stale data is no longer
 * present in the caches of any CPU is to use atomic reference counting.
 *
 * Here is a possible solution to that approach:
 *
 *    struct my_struct {
 *        int a; // shared data
 *        int ref_cnt; // reference count to the data;
 *    };
 *
 *    struct my_struct *A = malloc (sizeof (struct my_struct));
 *    A->a = 0;
 *    A->ref_cnt = 0;
 *
 *    // Marking this as volatile prevents optimizations of GCC from becoming a
 *    // concern. Note: atomicity and memory ordering across CPUs isn't
 *    // guaranteed by doing this, only optimizations on operations with the
 *    // variable are prevented.
 *    volatile struct my_struct *shared_data = A;
 *
 *    // Spinlock used to synchronize writes
 *    struct spinlock write_spinlock;
 *
 *    int reader (void) {
 *        int read_data;
 *
 *        // Other architectures will insist this be an atomic_load (...). This
 *        // is where 80x86 guarantees our read will be atomic given it is an
 *        // aligned shared pointer.
 *        struct my_struct *local_ref = shared_data;
 *
 *        atomic_inci (&local_ref->ref_cnt, 1);
 *        read_data = local_ref->a;
 *        atomic_deci (&local_ref->ref_cnt, 1);
 *
 *        return read_data;
 *    }
 *
 *    void writer (void) {
 *        spinlock_acquire (&write_spinlock);
 *        struct my_struct *old_data = shared_data;
 *        struct my_struct *copy = malloc (sizeof (struct my_struct));
 *        copy->a = 42;
 *        copy->ref_cnt = 0;
 *
 *        barrier (); // memory ordering from above modifications
 *        shared_data = copy;
 *
 *        spinlock_release (&write_spinlock);
 *        synchronize_ref_cnt (old_data);
 *
 *        free (old_data); // free once no references remain
 *    }
 *
 *    void synchronize_ref_cnt (struct my_struct *old_data) {
 *         while (atomic_load (old_data->ref_cnt) != 0)
 *           thread_yield (); // schedule a different thread
 *    }
 *
 * This is a valid approach that would be taken to solve a problem where
 * changes are made to some shared data infrequently. If the concern is reader
 * efficiently, this is a lock-free reader design meant to get around that
 * problem. Of course, RCU wouldn't exist if this were the perfect solution, as
 * a fairly massive bottleneck exists in its design.
 *
 * The issue lies within the atomic increment and decrement, which force cache
 * invalidations on every single read. Atomic operations are expensive,
 * especially as core count scales upward (around 10-100 cycles per operation).
 * RCU aims to reduce this cost by only having readers re-validate their caches
 * on concurrent updates.
 *
 * Here we can show how RCU is integrated into the same example:
 *
 *    int *shared_data = malloc (sizeof(int));
 *    *shared_data = 0; // initialize to zero
 *
 *    struct spinlock write_spinlock;
 *
 *    int reader (void) {
 *        int read_data;
 *        // Begin the RCU readside critical section. No blocking can occur
 *        // during this section that would trigger a quiensent state to be
 *        // reached. For Pintos, this is achieved by disabling preemption to
 *        // prevent context switches.
 *        rcu_read_lock (); // <==> intr_disable_push ();
 *
 *        // Dereferences the shared data protected by RCU. This is a wait-free
 *        // read with no atomic operations. It simply uses a compiler barrier
 *        // to prevent reordering of the dereference with any other
 *        // operations.
 *        read_data = rcu_deference (shared_data);
 *
 *        // End the RCU readside critical section.
 *        rcu_read_unlock (); // <==> intr_enable_pop ();
 *        return read_data;
 *    }
 *
 *    void writer (void) {
 *        int *copy, *old_data;
 *        spinlock_acquire (&write_spinlock);
 *
 *        copy = malloc (sizeof (int));
 *        *copy = 42;
 *
 *        // Swaps the shared data with the copy atomically
 *        rcu_assign_pointer (shared_data, copy);
 *
 *        // Release the spinlock before blocking call.
 *        spinlock_release (&write_spinlock);
 *
 *        // Block the writer until a "grace period" has been reached whereby
 *        // all CPUs have cycled through a quiescent state.
 *        synchronize_rcu ();
 *
 *        // Free the old data once we've confirmed no references remain.
 *        free (old_data);
 *    }
 *
 */

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
void rcu_print_stats(void);
void call_rcu(const void *data, rcu_callback_t callback_func);
void synchronize_rcu(void);
void rcu_quiescent_state(void);

/* Each loop iteration requires the end check to be protected by
 * rcu_dereference. It doesn't hurt to call it on the elem->next field, however
 * we are guaranteed enough protection here assuming this is used properly */
#define list_for_each_rcu(LIST, ELEM)                                          \
  for (ELEM = list_begin(LIST); rcu_dereference(ELEM) != list_end(LIST);       \
       ELEM = ELEM->next)

#endif /* lib/kernel/rcu.h */
