# Read Copy Update (RCU) in Pintos

## What is RCU?

Read-Copy-Update (RCU) is a method of synchronization that allows for wait-free reading of shared data. It is ideally used in read-mostly situations where no more than 10% of acesses made to some shared data structure are writes.

> Side note: It is more in line to say _mostly_ wait-free, as any reads made in contention to concurrent updates can invalidate their cache forcing a refetch to the global storage. Assuming usage in the correct setting, these points of conention should remain rare enough to have little impact.

The 80x86 architecture guarantees that updates to aligned, pointer sized values (sizeof (void\*)) are atomic at the hardware level. That is to say, any update to an aligned shared pointer is guaranteed to be performed atomically. This means the reader will see either the old value or the new value, but not any partial changes to the value. This does not imply the update to a shared pointer will be visible to all cores immediately after but rather that the operation itself will not lead to undefined behavior.

The question now becomes when can we ensure that value is no longer valid in the cache of any core, whereby we can free the memory it is using?

## How does it work?

RCU solves that problem, with a technique that dictactes when old memory can be reclaimed, using the natural atomicity of aligned shared pointers on the 80x86 architecture to its advantage. That is a _massive_ over-simplification, however generally speaking, the core idea is to combine the advantage the architecture provides with techniques to track grace geriods, or the completion of any reads on stale data before reclaimation takes place.

For instance, consider the trivial example below (to keep it simple, ignore freeing memory for now):

```c
 int *shared_data = malloc (sizeof(int));
 *shared_data = 0;

 int
 reader (void)
 {
    return *shared_data;
 }

 void
 writer (void)
 {
    int *copy = malloc (sizeof (int));
    *copy = 42; // A: initialization

    // Think of this as wmb (). A & B will not be reordered across it.
    // x86 already carries the guarantee that the update will propogate
    // to other cores at some point.
    barrier ();

    shared_data = copy; // B: update
 }
```

In this example, assume we have multiple readers and a single writer running concurrently. The 80x86 architecture guarantees that any reader will either see the value of 0 or 42, but not any intermediate representation of 42.

The reason is partly due to Total Store Ordering (TSO) where, simply put, a store buffer is used to control write-backs and changes are propogated via the bus signals to update the cache lines on the reader cores. Real TSO is far more involved, but for the sake of simplicity this serves as a valid abstract explaination.

Recall, x86 uses the MESI protocol (Modified, Exclusive, Shared, or Invalid) for the CPU cache coherence. Note that changes are not reflected immediately on the L1 caches of each individual CPU, so reads may occur after the update that see the old value. Assume before the update each core has the shared data in the Shared (S) state:

1. The writer changes its cache line for the shared value from Shared (S) to Modified (M).

2. The store buffer eventually reflects the change on the globally accessible cache (L2 or L3 depending on hardware) and places the BusRdX signal on the shared bus. This signal will set the cache value from Shared (S) to Invalid (I) for the cache lines of all reader cores. Think of this as (metaphorically) flushing the store buffer.

3. Upon any subsequent read, the Invalid (I) bit is read from the local cache, forcing an invalidation (or L1 miss). The new value gets fetched from the global cache and the local is set back to Shared (S).

Without RCU, one approach to tracking whether the stale data is no longer present in the caches of any CPU is to use atomic reference counting.

Here is one possible (_partial_) solution to that approach:

```c
struct my_struct
{
    int a;       // shared data
    int ref_cnt; // number of active references
};

struct my_struct A = malloc (sizeof (struct my_struct));
A->a = 0;
A->ref_cnt = 0;

/* Marking this as volatile prevents optimizations of GCC from becoming a
 * concern.
 *
 * Note: atomicity and memory ordering across CPUs isn't guaranteed by doing
 *       this, only optimizations on operations with the variable is prevented.
 */
volatile struct my_struct *shared_data = &A;

// Spinlock used to synchronize writes
struct spinlock write_spinlock;

int
reader (void)
{
    int read_data;

    // Other architectures will insist this be an atomic_load (...). This is
    // where 80x86 guarantees our read will be atomic given it is an aligned
    // shared pointer.
    struct my_struct *local_ref = shared_data;

    atomic_inci (&local_ref->ref_cnt, 1);
    read_data = local_ref->a;
    atomic_deci (&local_ref->ref_cnt, 1);

    return read_data;
}

void
writer (void)
{
    spinlock_acquire (&write_spinlock);
    struct my_struct *old_data = shared_data;
    struct my_struct *copy = malloc (sizeof (struct my_struct));
    copy->a = 42;
    copy->ref_cnt = 0;

    barrier (); // memory ordering from above modifications
    shared_data = copy;

    spinlock_release (&write_spinlock);

    while (atomic_load (old_data->ref_cnt) != 0)
        thread_yield (); // schedule a different thread

    free (old_data); // free once no references remain
}
```

_Note_: there is a pretty big UAF in the above code. This is to simpify the design a bit at the cost of validity, however the general layout remains the same. We'll leave it as an exercise to the reader to figure out how to properly implement it

This is a valid approach that would be taken to solve a problem where changes are made to some shared data infrequently. If the concern is reader efficiently, this is a lock-free reader design meant to get around that problem. Of course, RCU wouldn't exist if this were the perfect solution, as a fairly massive bottleneck exists in its design.

The issue lies within the atomic increment and decrement, which force cache invalidations on every single read. Atomic operations are expensive, especially as core count scales upward (around 10-100 cycles per operation). RCU aims to reduce this cost by only having readers re-validate their caches on concurrent updates.

Here we can show how RCU is integrated into the same example:

```c
int *shared_data = malloc (sizeof(int));
*shared_data = 0; // initialize to zero

struct spinlock write_spinlock;

int
reader (void)
{
    int read_data;

    // Begin the RCU readside critical section. No blocking can occur during
    // this section that would trigger a quiensent state to be reached. For
    // Pintos, this is achieved by disabling preemption to prevent context
    // switches.
    rcu_read_lock (); // <==> intr_disable ();

    // Dereferences the shared data protected by RCU. This is a lock-free read
    // with no atomic operations. It simply uses a compiler barrier to prevent
    // reordering of the dereference with any other operations.
    read_data = rcu_deference (shared_data);

    // End the RCU readside critical section.
    rcu_read_unlock (); // <==> intr_enable ();
    return read_data;
}

void
writer (void)
{
    int *copy, *old_data;
    spinlock_acquire (&write_spinlock);

    copy = malloc (sizeof (int));
    *copy = 42;

    // Swaps the shared data with the copy atomically
    rcu_assign_pointer (shared_data, copy);

    // Release the spinlock before blocking call.
    spinlock_release (&write_spinlock);

    // Block the writer until a "grace period" has been reached whereby all CPUs
    // have cycled through a quiescent state.
    synchronize_rcu ();

    // Free the old data once we've confirmed no references remain.
    free (old_data);

}
```

As you can see, with RCU we forgo those expensive atomic read-side operations to have a major impact on the collective read overhead. When the number of reads made in a given interval of time massively outweigh the number of writes, this serves as a means to massively improve throughput while maintaining correctness without any race conditions made.
