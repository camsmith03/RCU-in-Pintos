#include "selftest/rcu_cow_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "threads/malloc.h"
#include "threads/spinlock.h"
#include <kernel/list.h>
#include <kernel/rcu.h>
#include <stdint.h>

static void free_list_from_root (struct list_elem *root);
static void rcu_cow_add_list_entry (int n, bool is_sync);
static void rcu_cow_update_list_entry (int n, bool is_sync);
static void rcu_cow_remove_list_entry (int n, bool is_sync);
static void rcu_cow_reader_internal (void);
static void rcu_cow_sync_writer_internal (void);
static void rcu_cow_async_writer_internal (void);

/* Copy-On-Write (COW) is a form of synchronization that offers ideal read-side
 * performance with RCU by forcing writers to copy the entire list prior to
 * updates. It is not ideal in most (if not all) situations. */

/* Asynchronous write callback function for the shared list */
static void
async_shared_list_callback (const void *old_root, void *args UNUSED)
{
  free_list_from_root ((struct list_elem *)old_root);
}

void
rcu_cow_reader (void *args UNUSED)
{
  uint32_t i;
  uint64_t before, after;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    rcu_cow_reader_internal ();

  after = time ();

  log_read_cycles (after - before);
  sim_thread_exit ();
}

void
rcu_cow_sync_writer (void *args UNUSED)
{
  uint32_t i;
  uint64_t before, after;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations_sync; i++)
    rcu_cow_sync_writer_internal ();

  after = time ();

  log_sync_write_cycles (after - before);
  sim_thread_exit ();
}

void
rcu_cow_async_writer (void *args UNUSED)
{
  uint32_t i;
  uint64_t before, after;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations_async; i++)
    rcu_cow_async_writer_internal ();

  after = time ();

  log_async_write_cycles (after - before);
  sim_thread_exit ();
}

static void
rcu_cow_reader_internal (void)
{
  struct list_elem *root = (struct list_elem *)shared_data;
  struct list_data *ld;
  struct list_elem *e;

  // Enter the RCU read-side critical section (readers CANNOT BLOCK)
  rcu_read_lock ();

  /* We acquire the root (value of next for the head of the list) using RCU
   * protection, but assume the rest of the list falls under the copy-on-write
   * protection */
  e = rcu_dereference (root);

  for (; e != NULL; e = e->next)
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("RCU COW Read: magic not found!\n");

      if (ld->val < 0)
        print_fatal_error ("RCU COW Read: val of list entry is invalid\n");
    }

  // Exit the RCU read side critical section
  rcu_read_unlock ();
}

static void
rcu_cow_sync_writer_internal (void)
{
  int n;

  /* With RCU, writes are still serialized, so we must hold the spinlock for
   * the duration of the list modifications */
  spinlock_acquire (&shared_data_lock);
  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("RCU COW SYNC Write: n is invalid!\n");

  /* Synchronous element updates drops the spinlock before blocking the thread
   * to amortize the writer cost */
  if (n % 5 == 0)
    rcu_cow_remove_list_entry (n, true);
  else if (n % 5 == 1)
    rcu_cow_update_list_entry (n, true);
  else
    rcu_cow_add_list_entry (n, true);
}

static void
rcu_cow_async_writer_internal (void)
{
  int n;

  /* With RCU, writes are still serialized, so we must hold the spinlock for
   * the duration of the list modifications */
  spinlock_acquire (&shared_data_lock);
  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("RCU COW ASYNC Write: n is invalid!\n");

  if (n % 5 == 0)
    rcu_cow_remove_list_entry (n, false);
  else if (n % 5 == 1)
    rcu_cow_update_list_entry (n, false);
  else
    rcu_cow_add_list_entry (n, false);

  spinlock_release (&shared_data_lock);
}

/* Pushes an entry to the back of the global list with val of n */
static void
rcu_cow_add_list_entry (int n, bool is_sync)
{
  struct list_elem *old_root = (struct list_elem *)shared_data;
  struct list_elem *new_root, *e_old, *e_new;
  struct list_data *new_root_ld, *ld, *ld_cpy, *ld_new;

  /* Allocate the new root pointer */
  new_root_ld = malloc (sizeof (struct list_data));

  if (new_root_ld == NULL)
    print_fatal_error ("RCU COW Add: OOM\n");

  new_root = &new_root_ld->elem;
  new_root_ld->magic = DATA_MAGIC;
  new_root_ld->val = list_entry (old_root, struct list_data, elem)->val;

  /* Allocate the new data element to insert into the list */
  ld_new = malloc (sizeof (struct list_data));

  if (ld_new == NULL)
    print_fatal_error ("RCU COW Add: OOM\n");

  ld_new->val = n;
  ld_new->magic = DATA_MAGIC;

  e_new = new_root; // use for old list traversal
  e_old = old_root; // use for new list traversal

  for (; e_old != NULL; e_old = e_old->next)
    {
      ld = list_entry (e_old, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("RCU COW Add: magic not found!\n");

      ld_cpy = malloc (sizeof (struct list_data));
      if (ld_cpy == NULL)
        print_fatal_error ("RCU COW Add: OOM\n");

      /* Copy the current struct into the new struct */
      *ld_cpy = *ld;

      /* Add ld_cpy to the copied list */
      e_new->next = &ld_cpy->elem;
      e_new = e_new->next;
    }

  // Insert the new element to the back of the list we copied

  e_new->next = &ld_new->elem;
  e_new->next->next = NULL; // Set back to NULL for reader consistency

  rcu_assign_pointer (shared_data, new_root);
  if (is_sync)
    {
      spinlock_release (&shared_data_lock);
      synchronize_rcu ();
      free_list_from_root (old_root); // free the old list
    }
  else
    {
      /* Invoke the callback for the old list */
      call_rcu (old_root, async_shared_list_callback);
    }
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
rcu_cow_update_list_entry (int n, bool is_sync)
{
  struct list_elem *old_root = (struct list_elem *)shared_data;
  struct list_elem *new_root, *e_old, *e_new, *e_reclaim;
  struct list_data *new_root_ld, *ld, *ld_cpy;
  bool made_update = false;

  int goal = n / 2;

  /* Allocate the new root pointer */
  new_root_ld = malloc (sizeof (struct list_data));

  if (new_root_ld == NULL)
    print_fatal_error ("RCU COW Update: OOM\n");

  new_root = &new_root_ld->elem;
  new_root_ld->magic = DATA_MAGIC;
  new_root_ld->val = list_entry (old_root, struct list_data, elem)->val;

  e_new = new_root; // use for old list traversal
  e_old = old_root; // use for new list traversal

  for (; e_old != NULL; e_old = e_old->next)
    {
      ld = list_entry (e_old, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("RCU COW Update: magic not found!\n");

      ld_cpy = malloc (sizeof (struct list_data));
      if (ld_cpy == NULL)
        print_fatal_error ("RCU COW Update: OOM\n");

      /* Copy the current into the new */
      *ld_cpy = *ld;

      if (ld->val >= goal && !made_update)
        {
          /* Modify the first value that meets the goal spec */
          made_update = true;
          ld_cpy->val++;
        }
      /* Add ld_cpy to the copied list */
      e_new->next = &ld_cpy->elem;
      e_new = e_new->next;
    }

  /* If we made the update, great, wait for the next GP and clear the old
   * list. If not, we wasted that traversal, and must free everything
   * previously allocated before exit. */
  if (made_update)
    {
      /* Update was a success */

      /* Set the final value to NULL for readers */
      e_new->next = NULL;

      /* Swap the root pointer to our new root */
      rcu_assign_pointer (shared_data, new_root);

      if (!is_sync)
        {
          call_rcu (old_root, async_shared_list_callback);
          return;
        }

      spinlock_release (&shared_data_lock);
      synchronize_rcu ();
      e_reclaim = old_root;
    }
  else
    {
      /* Update did not get applied. Remove the list copy */
      e_reclaim = new_root;

      /* Drop lock for consistency */
      if (is_sync)
        spinlock_release (&shared_data_lock);
    }

  /* Free either the old list or the copy if the update didn't apply */
  free_list_from_root (e_reclaim);
}

/* Removes the first entry found in the global list with val >= n / 2.
 *
 * Drops the writer spinlock before blocking the thread to amortize the update
 * cost.
 */
static void
rcu_cow_remove_list_entry (int n, bool is_sync)
{
  struct list_elem *old_root = (struct list_elem *)shared_data;
  struct list_elem *new_root, *e_old, *e_new, *e_reclaim;
  struct list_data *new_root_ld, *ld, *ld_cpy;
  bool made_removal = false;

  int goal = n / 2;

  /* Allocate the new root pointer */
  new_root_ld = malloc (sizeof (struct list_data));

  if (new_root_ld == NULL)
    print_fatal_error ("RCU COW Remove: OOM\n");

  new_root = &new_root_ld->elem;
  new_root_ld->magic = DATA_MAGIC;
  new_root_ld->val = list_entry (old_root, struct list_data, elem)->val;

  e_new = new_root; // use for old list traversal
  e_old = old_root; // use for new list traversal

  for (; e_old != NULL; e_old = e_old->next)
    {
      ld = list_entry (e_old, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("RCU COW Remove: magic not found!\n");

      ld_cpy = malloc (sizeof (struct list_data));
      if (ld_cpy == NULL)
        print_fatal_error ("RCU COW Remove: OOM\n");

      /* Copy the current into the new */
      *ld_cpy = *ld;

      if (ld->val >= goal && !made_removal)
        {
          /* Skip adding this value into the list (once the first element that
           * qualifies is removed) */
          made_removal = true;
        }
      else
        {
          /* Add ld_cpy to the copied list */
          e_new->next = &ld_cpy->elem;
          e_new = e_new->next;
        }
    }

  /* If we made the removal, great, wait for the next GP and clear the old
   * list. If not, we wasted that traversal, and must free everything
   * previously allocated before exit. */
  if (made_removal)
    {
      /* Remove was a success */

      /* Set back of the list to NULL for readers */
      e_new->next = NULL;

      /* Swap the root pointer to our new root */
      rcu_assign_pointer (shared_data, new_root);
      if (!is_sync)
        {
          call_rcu (old_root, async_shared_list_callback);
          return;
        }

      spinlock_release (&shared_data_lock);
      synchronize_rcu ();
      e_reclaim = old_root;
    }
  else
    {
      /* Remove was a waste of time */
      e_reclaim = new_root;

      /* Drop the writer spinlock if no data was found for consistency */
      if (is_sync)
        spinlock_release (&shared_data_lock);
    }

  /* Free the old or new list */
  free_list_from_root (e_reclaim);
}

/* Frees the list at the inactive root pointer. It is assumed the only
 * reference to this list is the thread actively freeing invoking this. */
static void
free_list_from_root (struct list_elem *root)
{
  struct list_elem *e, *e_prev;
  struct list_data *ld;

  if (root == NULL)
    printf ("RCU COW Free List: root == NULL\n");

  e = root->next;
  e_prev = root;

  while (e_prev != NULL)
    {
      ld = list_entry (e_prev, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("RCU COW Free List: magic != DATA_MAGIC (1)\n");

      if (ld->val < 0)
        print_fatal_error ("RCU COW Free List: val < 0 (1)\n");

      free (ld);
      e_prev = e;

      if (e != NULL)
        e = e->next;
    }
}
