#include "selftest/sema_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "selftest/test_types.h"
#include "threads/malloc.h"

static void sema_read_counter (void);
static void sema_write_pointer (void);
static void sema_read_list (void);
static void sema_write_list (void);
static void sema_read_locking (void);

/* Thread function for a sema reader thread.
 *
 * Test type passed as a parameter.
 */
void
sema_reader (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    {
      if (test == POINTER)
        sema_read_counter ();
      else if (test == LIST)
        sema_read_list ();
      else if (test == SYNC_OPS)
        sema_read_locking ();
      else
        print_fatal_error ("SEMA Reader: Test not supported\n");
    }

  after = time ();

  log_read_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for a sema writer thread.
 *
 * Test type passed as a parameter.
 */
void
sema_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations; i++)
    {
      if (test == POINTER)
        sema_write_pointer ();
      else if (test == LIST)
        sema_write_list ();
      else
        print_fatal_error ("SEMA Writer: Test not supported\n");
    }

  after = time ();

  log_write_cycles (after - before);
  sim_thread_exit ();
}

static void
sema_read_counter (void)
{
  struct sema_data *sd;

  sd = (struct sema_data *)shared_data;

  sema_down (&sd->sema);

  if (sd == NULL)
    print_fatal_error ("SEMA Reader: Shared data is NULL!\n");

  if (sd->data.val < 0)
    print_fatal_error ("SEMA Reader: Invalid value!\n");

  if (sd->data.magic != DATA_MAGIC)
    print_fatal_error ("SEMA Reader: Invalid magic!\n");

  sema_up (&sd->sema);
}

static void
sema_write_pointer (void)
{
  struct sema_data *sd;

  sd = (struct sema_data *)shared_data;

  sema_down (&sd->sema);

  if (sd == NULL)
    print_fatal_error ("SEMA Writer: Shared data is NULL!\n");

  if (sd->data.val < 0)
    print_fatal_error ("SEMA Writer: Invalid value!\n");

  if (sd->data.magic != DATA_MAGIC)
    print_fatal_error ("SEMA Writer: Invalid magic!\n");

  sd->data.val++;

  sema_up (&sd->sema);
}

static void
sema_read_locking (void)
{
  sema_down ((struct semaphore *)shared_data);
  sema_up ((struct semaphore *)shared_data);
}

static void
sema_read_list (void)
{
  struct list_data *ld;
  struct list_elem *e;
  struct sema_list *sl;

  sl = (struct sema_list *)shared_data;

  sema_down (&sl->sema);

  for (e = list_begin (&sl->list); e != list_end (&sl->list);
       e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SEMA Read: magic of list entry is invalid");

      if (ld->val < 0)
        print_fatal_error ("SEMA Read: value in list entry is invalid");
    }

  sema_up (&sl->sema);
}

static void sema_add_list_entry (int n);
static void sema_update_list_entry (int n);
static void sema_remove_list_entry (int n);

static void
sema_write_list (void)
{
  struct sema_list *sl;

  sl = (struct sema_list *)shared_data;

  int n;
  sema_down (&sl->sema);

  n = ++op_sequence_counter;

  if (n <= 0)
    print_fatal_error ("SEMA Write List: n is invalid");

  if (n % 5 == 0)
    sema_remove_list_entry (n);
  else if (n % 5 == 1)
    sema_update_list_entry (n);
  else
    sema_add_list_entry (n);

  sema_up (&sl->sema);
}

/* Pushes an entry to the back of the global list with val of n */
static void
sema_add_list_entry (int n)
{
  struct list_data *new_data = malloc (sizeof (struct list_data));
  if (new_data == NULL)
    print_fatal_error ("SEMA List Add: OOM\n");

  new_data->magic = DATA_MAGIC;
  new_data->val = n;

  list_push_back (&((struct sema_list *)shared_data)->list, &new_data->elem);
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
sema_update_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *sll = &((struct sema_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (sll); e != list_end (sll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SEMA List Update: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("SEMA List Update: val is invalid\n");

      if (ld->val >= goal)
        {
          ld->val++;
          break;
        }
    }
}

/* Removes the first entry found in the global list with val >= n / 2 */
static void
sema_remove_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *sll = &((struct sema_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (sll); e != list_end (sll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("SEMA List Remove: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("SEMA List Remove: val is invalid\n");

      if (ld->val >= goal)
        {
          list_remove (e);
          free (ld);
          break;
        }
    }
}
