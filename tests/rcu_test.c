#include "selftest/rcu_test.h"
#include "list.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "selftest/test_types.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <kernel/rcu.h>
#include <stdint.h>

static void rcu_read_counter (void);
static void rcu_sync_write_pointer (void);
static void rcu_async_write_pointer (void);

static void rcu_read_list (void);
static void rcu_sync_write_list (void);
static void rcu_async_write_list (void);

static void async_shared_counter_callback (const void *, void *);
static void async_shared_list_callback (const void *, void *) UNUSED;

static void rcu_read_locking_test (void);

/* Thread function for an RCU reader thread.
 *
 * Test type passed as a parameter.
 */
void
rcu_reader (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    {
      if (test == POINTER)
        rcu_read_counter ();
      else if (test == LIST)
        rcu_read_list ();
      else if (test == SYNC_OPS)
        rcu_read_locking_test ();
      else
        print_fatal_error ("RCU Reader: Test not supported\n");
    }

  after = time ();

  // Log the elapsed cycles from this thread
  log_read_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for an RCU synchronous writer thread.
 *
 * Test type passed as a parameter.
 */
void
rcu_sync_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations_sync; i++)
    {
      if (test == POINTER)
        rcu_sync_write_pointer ();
      else if (test == LIST)
        rcu_sync_write_list ();
      else
        print_fatal_error ("RCU SYNC Writer: Test not supported\n");
    }

  after = time ();

  // Log the elapsed cycles for this thread
  log_sync_write_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for an RCU asynchronous writer thread.
 *
 * Test type passed as a parameter.
 */
void
rcu_async_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  // Block untill all testing threads have been initialized
  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations_async; i++)
    {
      if (test == POINTER)
        rcu_async_write_pointer ();
      else if (test == LIST)
        rcu_async_write_list ();
      else
        print_fatal_error ("RCU ASYNC Writer: Test not supported\n");
    }

  after = time ();

  // Log the elapsed cycles for this thread
  log_async_write_cycles (after - before);
  sim_thread_exit ();
}

/* RCU reader for the shared counter */
static void
rcu_read_counter (void)
{
  struct data *sd;

  // Enter the RCU read-side critical section (readers CANNOT BLOCK)
  rcu_read_lock ();

  // Safely dereference the shared data
  sd = (struct data *)rcu_dereference (shared_data);

  // The data dereferenced MUST be valid while in the RCU read-side critical
  // section
  if (sd == NULL || sd->magic != DATA_MAGIC || sd->val < 0)
    print_fatal_error ("RCU Reader: reader errors detected!\n");

  // Exit the RCU read side critical section
  rcu_read_unlock ();
}

/* Synchronous RCU writer for the shared counter */
static void
rcu_sync_write_pointer (void)
{
  struct data *old_ptr, *new_ptr;

  new_ptr = malloc (sizeof (struct data));
  if (new_ptr == NULL)
    print_fatal_error ("RCU SYNC Writer: OOM\n");

  spinlock_acquire (&shared_data_lock);

  old_ptr = (struct data *)rcu_dereference (shared_data);
  if (old_ptr == NULL)
    print_fatal_error ("RCU SYNC Writer: null pointer found\n");

  if (old_ptr->magic != DATA_MAGIC)
    print_fatal_error ("RCU SYNC Writer: DATA_MAGIC not detected\n");

  ASSERT (old_ptr->val >= 0);

  // Copy the old obj into the new one
  *new_ptr = *old_ptr;

  // Increment the data parameter in the new data
  new_ptr->val++;

  /* Set the rcu_counter to the newly created data */
  rcu_assign_pointer (shared_data, (void *)new_ptr);

  /* It is now safe to release the spinlock */
  spinlock_release (&shared_data_lock);

  /* Block the writer until the next grace period */
  synchronize_rcu ();

  /* Now safe to free the old data after GP has passed */
  free (old_ptr);
}

/* Asynchronous RCU writer for the shared counter */
static void
rcu_async_write_pointer (void)
{
  struct data *old_ptr, *new_ptr;

  new_ptr = malloc (sizeof (struct data));
  if (new_ptr == NULL)
    print_fatal_error ("RCU ASYNC Writer: OOM\n");

  spinlock_acquire (&shared_data_lock);

  old_ptr = (struct data *)rcu_dereference (shared_data);
  if (old_ptr == NULL)
    print_fatal_error ("RCU ASYNC Writer: null pointer found\n");

  if (old_ptr->magic != DATA_MAGIC)
    print_fatal_error ("RCU ASYNC Writer: DATA_MAGIC not detected\n");

  ASSERT (old_ptr->val >= 0);

  // Copy the old obj into the new one
  *new_ptr = *old_ptr;

  // Increment the data parameter in the new data
  new_ptr->val++;

  /* Set the rcu_counter to the newly created data */
  rcu_assign_pointer (shared_data, (void *)new_ptr);

  /* It is now safe to release the spinlock */
  spinlock_release (&shared_data_lock);

  /* Call the callback function on the shared data */
  call_rcu ((const void *)old_ptr, async_shared_counter_callback);
}

/* Asynchronous write callback function for the shared counter */
static void
async_shared_counter_callback (const void *data, void *args UNUSED)
{
  struct data *sd = (struct data *)data;
  if (sd == NULL || sd->val < 0)
    print_fatal_error ("RCU ASYNC Callback: detected invalid state!\n");

  free (sd);
}

/* Asynchronous write callback function for the shared list */
static void
async_shared_list_callback (const void *list_data, void *args UNUSED)
{
  struct list_data *ld = (struct list_data *)list_data;
  if (ld == NULL || ld->val < 0)
    print_fatal_error ("RCU ASYNC Callback: detected invalid state!\n");

  free (ld);
}

static void
rcu_read_locking_test (void)
{
  rcu_read_lock ();
  rcu_read_unlock ();
}

static void
rcu_read_list (void)
{
  struct list *sl = (struct list *)shared_data;
  struct list_data *ld;
  struct list_elem *e;

  // Enter the RCU read-side critical section (readers CANNOT BLOCK)
  rcu_read_lock ();

  /* Loops through the elements of the list SL, starting from head, and ending
   * once the tail is reached. This handles the necessary memory barriers to
   * ensure reader-writer consistency */
  list_for_each_rcu (sl, e)
  {
    ld = list_entry (e, struct list_data, elem);
    if (ld->magic != DATA_MAGIC)
      print_fatal_error ("RCU List Read: magic not found!\n");

    if (ld->val < 0)
      print_fatal_error ("RCU List Read: val of list entry is invalid\n");
  }

  // Exit the RCU read side critical section
  rcu_read_unlock ();
}

static void rcu_add_list_entry (int n);
static void rcu_update_list_entry (int n);
static void rcu_sync_remove_list_entry (int n);
static void rcu_async_remove_list_entry (int n);

static void
rcu_sync_write_list (void)
{
  int n;

  /* With RCU, writes are still serialized, so we must hold the spinlock for
   * the duration of the list modifications */
  spinlock_acquire (&shared_data_lock);
  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("RCU SYNC List Write: n = %d is invalid!\n", n);

  if (n % 5 == 0)
    {
      /* Synchronous element removal drops the spinlock before blocking the
       * thread to amortize the cost of updates */
      rcu_sync_remove_list_entry (n);
      return;
    }
  else if (n % 5 == 1)
    rcu_update_list_entry (n);
  else
    rcu_add_list_entry (n);

  spinlock_release (&shared_data_lock);
}

static void
rcu_async_write_list (void)
{
  int n;

  /* With RCU, writes are still serialized, so we must hold the spinlock for
   * the duration of the list modifications */
  spinlock_acquire (&shared_data_lock);
  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("RCU ASYNC List Write: n = %d is invalid!\n", n);

  if (n % 5 == 0)
    rcu_async_remove_list_entry (n);
  else if (n % 5 == 1)
    rcu_update_list_entry (n);
  else
    rcu_add_list_entry (n);

  spinlock_release (&shared_data_lock);
}

/* Pushes an entry to the back of the global list with val of n */
static void
rcu_add_list_entry (int n)
{
  struct list_data *new_data = malloc (sizeof (struct list_data));
  if (new_data == NULL)
    print_fatal_error ("RCU List Add: OOM\n");

  new_data->magic = DATA_MAGIC;
  new_data->val = n;

  list_push_back_rcu ((struct list *)shared_data, &new_data->elem);
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
rcu_update_list_entry (int n)
{
  struct list *sl = (struct list *)shared_data;
  struct list_elem *e;
  struct list_data *ld;
  int goal = n / 2;

  list_for_each_rcu (sl, e)
  {
    ld = list_entry (e, struct list_data, elem);

    if (ld->magic != DATA_MAGIC)
      print_fatal_error ("RCU List Update: magic not found!\n");

    if (ld->val >= goal)
      {
        ld->val++;
        return;
      }
  }
}

/* Removes the first entry found in the global list with val >= n / 2.
 *
 * Drops the writer spinlock before blocking the thread to amortize the update
 * cost.
 */
static void
rcu_sync_remove_list_entry (int n)
{
  struct list *sl = (struct list *)shared_data;
  struct list_elem *e;
  struct list_data *ld;
  int goal = n / 2;

  list_for_each_rcu (sl, e)
  {
    ld = list_entry (e, struct list_data, elem);

    if (ld->magic != DATA_MAGIC)
      print_fatal_error ("RCU SYNC List Remove: magic not found!\n");

    if (ld->val >= goal)
      {
        list_remove_rcu (e); // safe removal of shared list entry
        spinlock_release (&shared_data_lock);
        synchronize_rcu (); // block the writer
        free (ld);          // free data after elapsed GP
        return;
      }
  }

  /* Drop the writer spinlock if no data was found to maintain consistency */
  spinlock_release (&shared_data_lock);
}

/* Removes the first entry found in the global list with val >= n / 2.
 *
 * Invokes the callback function "async_shared_list_callback" on the removed
 * data element, does not drop the writer spinlock.
 */
static void
rcu_async_remove_list_entry (int n)
{
  struct list *sl = (struct list *)shared_data;
  struct list_elem *e;
  struct list_data *ld;
  int goal = n / 2;

  list_for_each_rcu (sl, e)
  {
    ld = list_entry (e, struct list_data, elem);

    if (ld->magic != DATA_MAGIC)
      print_fatal_error ("RCU ASYNC List Remove: magic not found!\n");

    if (ld->val >= goal)
      {
        list_remove_rcu (e);
        call_rcu ((const void *)ld, async_shared_list_callback);
        return;
      }
  }
}
