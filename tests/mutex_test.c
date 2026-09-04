#include "selftest/mutex_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "selftest/test_types.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static void mutex_read_pointer (void);
static void mutex_write_pointer (void);
static void mutex_read_list (void);
static void mutex_write_list (void);
static void mutex_read_locking (void);

/* Thread function for a mutex reader thread.
 *
 * Test type passed as a parameter.
 */
void
mutex_reader (void *args)
{
  uint32_t i;
  uint64_t before, after;

  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    {
      if (test == POINTER)
        mutex_read_pointer ();
      else if (test == LIST)
        mutex_read_list ();
      else if (test == SYNC_OPS)
        mutex_read_locking ();
      else
        print_fatal_error ("MUTEX Reader: Test type not supported\n");
    }

  after = time ();

  log_read_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for a mutex writer thread.
 *
 * Test type passed as a parameter.
 */
void
mutex_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations; i++)
    {
      if (test == POINTER)
        mutex_write_pointer ();
      else if (test == LIST)
        mutex_write_list ();
      else
        print_fatal_error ("MUTEX Writer: Test type not supported\n");
    }

  after = time ();

  log_write_cycles (after - before);
  sim_thread_exit ();
}

static void
mutex_read_pointer (void)
{
  uint64_t before, after;
  struct mutex_data *md;

  md = (struct mutex_data *)shared_data;

  lock_acquire (&md->mutex);

  if (md == NULL)
    print_fatal_error ("MUTEX Reader: Shared data is NULL!\n");

  if (md->data.val < 0)
    print_fatal_error ("MUTEX Reader: Invalid value!\n");

  if (md->data.magic != DATA_MAGIC)
    print_fatal_error ("MUTEX Reader: Invalid magic!\n");

  lock_release (&md->mutex);
}

static void
mutex_write_pointer (void)
{
  struct mutex_data *md;

  md = (struct mutex_data *)shared_data;

  lock_acquire (&md->mutex);

  if (md == NULL)
    print_fatal_error ("MUTEX Writer: Shared data is NULL!\n");

  if (md->data.val < 0)
    print_fatal_error ("MUTEX Writer: Invalid value!\n");

  if (md->data.magic != DATA_MAGIC)
    print_fatal_error ("MUTEX Writer: Invalid magic!\n");

  md->data.val++;

  lock_release (&md->mutex);
}

static void
mutex_read_locking (void)
{
  lock_acquire ((struct lock *)shared_data);
  lock_release ((struct lock *)shared_data);
}

static void
mutex_read_list (void)
{
  struct list_data *ld;
  struct list_elem *e;
  struct mutex_list *ml;

  ml = (struct mutex_list *)shared_data;

  lock_acquire (&ml->mutex);

  for (e = list_begin (&ml->list); e != list_end (&ml->list);
       e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MUTEX Read: magic of list entry is invalid");

      if (ld->val < 0)
        print_fatal_error ("MUTEX Read: value in list entry is invalid");
    }

  lock_release (&ml->mutex);
}

static void mutex_add_list_entry (int n);
static void mutex_update_list_entry (int n);
static void mutex_remove_list_entry (int n);

static void
mutex_write_list (void)
{
  int n;
  struct mutex_list *ml;

  ml = (struct mutex_list *)shared_data;

  lock_acquire (&ml->mutex);

  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("MUTEX Write List: n is invalid");

  if (n % 5 == 0)
    mutex_remove_list_entry (n);
  else if (n % 5 == 1)
    mutex_update_list_entry (n);
  else
    mutex_add_list_entry (n);

  lock_release (&ml->mutex);
}

/* Pushes an entry to the back of the global list with val of n */
static void
mutex_add_list_entry (int n)
{
  struct list_data *new_data = malloc (sizeof (struct list_data));
  if (new_data == NULL)
    print_fatal_error ("MUTEX List Add: OOM\n");

  new_data->magic = DATA_MAGIC;
  new_data->val = n;

  list_push_back (&((struct mutex_list *)shared_data)->list, &new_data->elem);
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
mutex_update_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *mll = &((struct mutex_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (mll); e != list_end (mll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MUTEX List Update: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("MUTEX List Update: val is invalid\n");

      if (ld->val >= goal)
        {
          ld->val++;
          break;
        }
    }
}

/* Removes the first entry found in the global list with val >= n / 2 */
static void
mutex_remove_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *mll = &((struct mutex_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (mll); e != list_end (mll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MUTEX List Remove: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("MUTEX List Remove: val is invalid\n");

      if (ld->val >= goal)
        {
          list_remove (e);
          free (ld);
          break;
        }
    }
}
