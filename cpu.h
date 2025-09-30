#ifndef THREADS_CPU_H_
#define THREADS_CPU_H_

#include <kernel/rcu.h>

/* To extend the places RCU can be implemented, we stack allocate the pointers
 * used by the CPUs to allow for RCU initialization in the pre-palloc phase
 * of kernel boot. The two structs comprise 32 bytes total, so 32 bytes is
 * padded at the end to meet the cache line size of the host system. This is to
 * avoid false sharing with the other CPUs to prevent it from impacting cache
 * performance */
struct rcu_data {
  struct rcu_cpu_lists curr_gp; // sizeof => 16 bytes
  struct rcu_cpu_lists next_gp;
  char unused[32]; // Pad to the 64 byte cache line to prevent false sharing
                   // with other CPUs
};

struct cpu {
  /* Contains the data structures with updates that were added from the
   * previous grace period, to be freed on this upcoming one */
  struct rcu_cpu_lists *rcu_curr_gp;

  /* Contains the data structures with updates added from the current grace
   * period, to be freed on the next one */
  struct rcu_cpu_lists *rcu_next_gp;

  /* Last observed grace period epoch (not atomic) */
  uint32_t rcu_last_epoch;

  /* Boolean flag to indicate when a QS is reached */
  atomic_int rcu_qs_reached;
};

extern struct rcu_data global_rcu_data[NCPU_MAX];

#endif
