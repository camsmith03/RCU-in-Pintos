#include "selftest/monitor_test.h"
#include "selftest/shared_data.h"
#include "selftest/sim_utils.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static void monitor_read_pointer (void);
static void monitor_write_pointer (void);
static void monitor_read_list (void);
static void monitor_write_list (void);

/* Thread function for a monitor reader thread.
 *
 * Test type passed as a parameter.
 */
void
monitor_reader (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < reader_iterations; i++)
    {
      if (test == POINTER)
        monitor_read_pointer ();
      else if (test == LIST)
        monitor_read_list ();
      else
        print_fatal_error ("MONITOR Reader: Test not supported\n");
    }

  after = time ();

  log_read_cycles (after - before);
  sim_thread_exit ();
}

/* Thread function for a monitor writer thread.
 *
 * Test type passed as a parameter.
 */
void
monitor_writer (void *args)
{
  uint32_t i;
  uint64_t before, after;
  enum test_type test = (enum test_type)args;

  thread_sychronize ();

  before = time ();

  for (i = 0; i < writer_iterations; i++)
    {
      if (test == POINTER)
        monitor_write_pointer ();
      else if (test == LIST)
        monitor_write_list ();
      else
        print_fatal_error ("MONITOR Writer: Test not supported\n");
    }

  after = time ();

  log_write_cycles (after - before);
  sim_thread_exit ();
}

static void
monitor_read_pointer (void)
{
  struct monitor_data *md;

  md = (struct monitor_data *)shared_data;

  lock_acquire (&md->mutex);
  while (md->write_cnt > 0)
    cond_wait (&md->reader_cond, &md->mutex);

  md->read_cnt++;
  lock_release (&md->mutex);

  /* Safe to check data without lock (typically we'd block for this duration,
   * extending the read-side time). To keep it simple we only hold the lock
   * when incrementing the reader count, as this is how a typical monitor would
   * operate */
  if (md->data.val < 0 || md->data.magic != DATA_MAGIC)
    print_fatal_error ("MONITOR Reader: detected invalid data!\n");

  lock_acquire (&md->mutex);

  if ((md->read_cnt -= 1) == 0)
    cond_signal (&md->writer_cond, &md->mutex);

  lock_release (&md->mutex);
}

static void
monitor_write_pointer (void)
{
  struct monitor_data *md;

  md = (struct monitor_data *)shared_data;
  lock_acquire (&md->mutex);
  while (md->write_cnt + md->read_cnt > 0)
    cond_wait (&md->writer_cond, &md->mutex);

  md->write_cnt++;
  lock_release (&md->mutex);

  if (md->data.val < 0 || md->data.magic != DATA_MAGIC)
    print_fatal_error ("MONITOR Writer: detected invalid data!\n");

  lock_acquire (&md->mutex);

  md->write_cnt--;

  cond_signal (&md->writer_cond, &md->mutex);
  cond_broadcast (&md->reader_cond, &md->mutex);
  lock_release (&md->mutex);
}

static void
monitor_read_list (void)
{
  struct list_data *ld;
  struct list_elem *e;
  struct monitor_list *ml;

  ml = (struct monitor_list *)shared_data;

  lock_acquire (&ml->mutex);

  while (ml->write_cnt > 0)
    cond_wait (&ml->reader_cond, &ml->mutex);

  ml->read_cnt++;

  lock_release (&ml->mutex);

  for (e = list_begin (&ml->list); e != list_end (&ml->list);
       e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);

      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MONITOR Read: magic of list entry is invalid");

      if (ld->val < 0)
        print_fatal_error ("MONITOR Read: value in list entry is invalid");
    }

  lock_acquire (&ml->mutex);

  if ((ml->read_cnt -= 1) == 0)
    cond_signal (&ml->writer_cond, &ml->mutex);

  lock_release (&ml->mutex);
}

static void monitor_add_list_entry (int n);
static void monitor_update_list_entry (int n);
static void monitor_remove_list_entry (int n);

static void
monitor_write_list (void)
{
  int n;
  struct monitor_list *ml;

  ml = (struct monitor_list *)shared_data;

  lock_acquire (&ml->mutex);
  while (ml->read_cnt + ml->write_cnt > 0)
    cond_wait (&ml->writer_cond, &ml->mutex);

  ml->write_cnt++;
  n = ++op_sequence_counter;

  lock_release (&ml->mutex);

  if (n <= 0)
    print_fatal_error ("MONITOR Write List: n is invalid!\n");

  if (n % 5 == 0)
    monitor_remove_list_entry (n);
  else if (n % 5 == 1)
    monitor_update_list_entry (n);
  else
    monitor_add_list_entry (n);

  lock_acquire (&ml->mutex);
  ml->write_cnt--;

  cond_signal (&ml->writer_cond, &ml->mutex);
  cond_broadcast (&ml->reader_cond, &ml->mutex);
  lock_release (&ml->mutex);
}

/* Pushes an entry to the back of the global list with val of n */
static void
monitor_add_list_entry (int n)
{
  struct list_data *new_data = malloc (sizeof (struct list_data));
  if (new_data == NULL)
    print_fatal_error ("MONITOR List Add: OOM\n");

  new_data->magic = DATA_MAGIC;
  new_data->val = n;

  list_push_back (&((struct monitor_list *)shared_data)->list,
                  &new_data->elem);
}

/* Updates the first entry found in the global list with val >= n / 2 by
 * incrementing its val. */
static void
monitor_update_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *mll = &((struct monitor_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (mll); e != list_end (mll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MONITOR List Update: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("MONITOR List Update: val is invalid\n");

      if (ld->val >= goal)
        {
          ld->val++;
          break;
        }
    }
}

/* Removes the first entry found in the global list with val >= n / 2 */
static void
monitor_remove_list_entry (int n)
{
  struct list_data *ld;
  struct list_elem *e;
  struct list *mll = &((struct monitor_list *)shared_data)->list;
  int goal = n / 2;

  for (e = list_begin (mll); e != list_end (mll); e = list_next (e))
    {
      ld = list_entry (e, struct list_data, elem);
      if (ld->magic != DATA_MAGIC)
        print_fatal_error ("MONITOR List Remove: magic not detected!\n");

      if (ld->val < 0)
        print_fatal_error ("MONITOR List Remove: val is invalid\n");

      if (ld->val >= goal)
        {
          list_remove (e);
          free (ld);
          break;
        }
    }
}
