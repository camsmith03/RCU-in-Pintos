#include "selftest/spinlock_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "selftest/test_types.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"

static void spinlock_read_pointer (void);
static void spinlock_write_pointer (void);
static void spinlock_read_list (void);
static void spinlock_write_list (void);
static void spinlock_read_locking (void);

#include "devices/timer.h"

/* Thread function for a spinlock reader thread.
 *
 * Test type passed as a parameter.
 */
void
spinlock_reader (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    {
      if (test == POINTER)
        spinlock_read_pointer ();
      else if (test == LIST)
        spinlock_read_list ();
      else if (test == SYNC_OPS)
        spinlock_read_locking ();
      else
        print_fatal_error ("SPINLOCK Reader: test not supported\n");
    }

  after = time ();

  // Log the elapsed cycles from this thread
  log_read_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for a spinlock writer thread.
 *
 * Test type passed as a parameter.
 */
void
spinlock_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations; i++)
    {
      if (test == POINTER)
        spinlock_write_pointer ();
      else if (test == LIST)
        spinlock_write_list ();
      else
        print_fatal_error ("SPINLOCK Writer: test not supported\n");
    }

  after = time ();

  // Log the elapsed cycles from this thread
  log_write_cycles (after - before);
  sim_thread_exit ();
}

static void
spinlock_read_pointer (void)
{
  spinlock_acquire (&shared_data_lock);

  // The data dereferenced MUST be valid while in the RCU read-side critical
  // section
  if (shared_data == NULL)
    print_fatal_error ("SPINLOCK Reader: data is null!");

  if (((struct data *)shared_data)->magic != DATA_MAGIC)
    print_fatal_error ("SPINLOCK Reader: magic is invalid!");

  if (((struct data *)shared_data)->val < 0)
    print_fatal_error ("SPINLOCK Reader: value is invalid!");

  spinlock_release (&shared_data_lock);
}

static void
spinlock_read_locking (void)
{
  spinlock_acquire ((struct spinlock *)shared_data);
  spinlock_release ((struct spinlock *)shared_data);
}

static void
spinlock_write_pointer (void)
{
  spinlock_acquire (&shared_data_lock);

  if (shared_data == NULL)
    print_fatal_error ("SPINLOCK Writer: null pointer found");

  if (((struct data *)shared_data)->magic != DATA_MAGIC)
    print_fatal_error ("SPINLOCK Writer: magic is invalid!");

  if (((struct data *)shared_data)->val < 0)
    print_fatal_error ("SPINLOCK Writer: value is invalid!");

  // Increment the data parameter in the new data
  ((struct data *)shared_data)->val++;

  /* It is now safe to release the spinlock */
  spinlock_release (&shared_data_lock);
}

static void
spinlock_read_list (void)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *sd;

  sd = (struct list *)shared_data;

  spinlock_acquire (&shared_data_lock);

  for (e = list_begin (sd); e != list_end (sd); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SPINLOCK Read: magic of list entry is invalid");

      if (ld->val < 0)
        print_fatal_error ("SPINLOCK Read: value in list entry is invalid");
    }

  spinlock_release (&shared_data_lock);
}

static void spinlock_add_list_entry (int n);
static void spinlock_update_list_entry (int n);
static void spinlock_remove_list_entry (int n);

static void
spinlock_write_list (void)
{
  int n;

  spinlock_acquire (&shared_data_lock);

  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("SPINLOCK Write: op_sequence_counter is invalid");

  if (n % 5 == 0)
    spinlock_remove_list_entry (n);
  else if (n % 5 == 1)
    spinlock_update_list_entry (n);
  else
    spinlock_add_list_entry (n);

  spinlock_release (&shared_data_lock);
}

/* Pushes an entry to the back of the global list with val of n */
static void
spinlock_add_list_entry (int n)
{
  struct list_data *new_data = malloc (sizeof (struct list_data));
  if (new_data == NULL)
    print_fatal_error ("SPINLOCK List Add: OOM\n");

  new_data->magic = DATA_MAGIC;
  new_data->val = n;

  list_push_back ((struct list *)shared_data, &new_data->elem);
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
spinlock_update_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *sd = (struct list *)shared_data;
  int goal = n / 2;

  for (e = list_begin (sd); e != list_end (sd); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SPINLOCK List Update: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("SPINLOCK List Update: val is invalid\n");

      if (ld->val >= goal)
        {
          ld->val++;
          break;
        }
    }
}

/* Removes the first entry found in the global list with val >= n / 2 */
static void
spinlock_remove_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *sd = (struct list *)shared_data;
  int goal = n / 2;

  for (e = list_begin (sd); e != list_end (sd); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SPINLOCK List Remove: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("SPINLOCK List Remove: val is invalid\n");

      if (ld->val >= goal)
        {
          list_remove (e);
          free (ld); // safe to free once off the list
          break;
        }
    }
}
