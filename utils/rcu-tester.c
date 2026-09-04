#define _POSIX_C_SOURCE 199309L
#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int get_choice (char *msg, char *choices[]);
static void draw_menu (char *options[], int curr_highlight, int start_row,
                       int start_col);
static void clear_all_screen (void);
static bool get_confirmation (void);
static int msleep (uint64_t msec);
static void spawn_script (int test_type, int concurrency_type,
                          int access_pattern, int smp);

#define MAX_STRING 80  // longest allowed response
#define MAX_ENTRY 1024 // longest allowed entry
#define MAX_ARGS 8
#define MESSAGE_LINE 6 // misc. messages on this line
#define Q_LINE 20      // line for questions

extern char *const *environ;

char *test_menu_title = "Select a test type:";
char *test_menu[] = {
  "1. Pointer test", "2. List test", "3. Read-Side Locking", "Quit", 0,
};

char *concurrency_menu_title = "Select a concurrency type:";
char *concurrency_menu_1[] = {
  "1. RCU", "2. Spinlock", "3. Semaphore", "4. Mutex", "Quit", 0,
};

char *concurrency_menu_2[] = {
  "1. RCU",   "2. Spinlock",          "3. Semaphore",
  "4. Mutex", "5. RCU Copy-On-Write", "Quit",
  0,
};

char *access_pattern_menu_title = "Select an access pattern:";
char *access_pattern_menu[] = {
  "1. Read Only",    "2. Read Mostly", "3. Read Write",
  "4. Write Mostly", "Quit",           0,
};

int
main ()
{
  int choice, test_type, concurrency_type, access_pattern;
  char **c_menu;

  initscr ();
  do
    {
      choice = get_choice (test_menu_title, test_menu);
      if (choice == 'Q' && get_confirmation ())
        {
          endwin ();
          exit (0);
        }
      else if (choice == '1')
        {
          c_menu = concurrency_menu_1;
          test_type = 0;
          choice = 'Q';
        }
      else if (choice == '2')
        {
          c_menu = concurrency_menu_2;
          test_type = 1;
          choice = 'Q';
        }
      else if (choice == '3')
        {
          c_menu = concurrency_menu_1;
          test_type = 2;
          choice = 'Q';
        }
    }
  while (choice != 'Q');

  do
    {
      choice = get_choice (concurrency_menu_title, c_menu);
      if (choice == 'Q' && get_confirmation ())
        {
          endwin ();
          exit (0);
        }
      else if (choice == '1')
        {
          concurrency_type = 0;
          choice = 'Q';
        }
      else if (choice == '2')
        {
          concurrency_type = 1;
          choice = 'Q';
        }
      else if (choice == '3')
        {
          concurrency_type = 2;
          choice = 'Q';
        }
      else if (choice == '4')
        {
          concurrency_type = 3;
          choice = 'Q';
        }
      else if (choice == '5')
        {
          concurrency_type = 4;
          choice = 'Q';
        }
    }
  while (choice != 'Q');

  if (test_type == 2)
    {
      spawn_script (test_type, concurrency_type, 0, 1);
      exit (0);
    }

  do
    {
      choice = get_choice (access_pattern_menu_title, access_pattern_menu);
      if (choice == 'Q' && get_confirmation ())
        {
          endwin ();
          exit (0);
        }
      else if (choice == '1')
        {
          access_pattern = 0;
          choice = 'Q';
        }
      else if (choice == '2')
        {
          access_pattern = 1;
          choice = 'Q';
        }
      else if (choice == '3')
        {
          access_pattern = 2;
          choice = 'Q';
        }
      else if (choice == '4')
        {
          access_pattern = 3;
          choice = 'Q';
        }
    }
  while (choice != 'Q');

  nocbreak ();
  bool valid_input = false;
  int smp = 0;
  do
    {
      clear_all_screen ();
      char smp_msg[] = "Enter the number of CPUs: ";
      char str[MAX_STRING];
      memset (str, 0, sizeof (str));

      mvprintw (MESSAGE_LINE, 10, "%s", smp_msg);
      clrtoeol ();
      refresh ();

      cbreak ();

      getstr (str);
      smp = 0;

      nocbreak ();
      valid_input = false;
      int i = 0;
      while (*(str + i) != '\0')
        {
          if (!isdigit (*(str + i)))
            {
              mvprintw (MESSAGE_LINE, 10, "NOT A NUMBER");
              clrtoeol ();
              refresh ();
              msleep (800);
              valid_input = false;
              break;
            }

          valid_input = true;
          smp *= 10;
          smp += (int)(*(str + i) - '0');
          i++;
        }
    }
  while (!valid_input);

  endwin ();

  spawn_script (test_type, concurrency_type, access_pattern, smp);
}

static int
get_choice (char *msg, char *choices[])
{
  int selected_row = 0;
  int max_row = 0;
  int start_screenrow = MESSAGE_LINE, start_screencol = 10;

  char **option;
  int selected;
  int key = 0;

  option = choices;

  while (*option)
    {
      max_row++;
      option++;
    }

  // prevents against menu shortening
  if (selected_row >= max_row)
    selected_row = 0;

  clear_all_screen ();

  mvprintw (start_screenrow - 2, start_screencol, "%s", msg);
  keypad (stdscr, true);
  cbreak ();
  noecho ();

  key = 0;
  while (key != 'Q' && key != 'q' && key != KEY_ENTER && key != '\n')
    {
      if (key == KEY_UP)
        {
          if (selected_row == 0)
            selected_row = max_row - 1;
          else
            selected_row--;
        }

      if (key == KEY_DOWN)
        {
          if (selected_row == (max_row - 1))
            selected_row = 0;
          else
            selected_row++;
        }

      selected = *choices[selected_row];
      draw_menu (choices, selected_row, start_screenrow, start_screencol);
      key = getch ();
    }

  keypad (stdscr, false);
  nocbreak ();
  echo ();

  if (key == 'Q' || key == 'q')
    selected = 'Q';

  return selected;
}

static void
draw_menu (char *options[], int curr_highlight, int start_row, int start_col)
{
  int curr_row = 0;
  char **option_ptr;
  char *txt_ptr;

  option_ptr = options;
  while (*option_ptr)
    {
      if (curr_row == curr_highlight)
        {
          mvaddch (start_row + curr_row, start_col - 3, '>');
          mvaddch (start_row + curr_row, start_col + 40, '<');
        }
      else
        {
          mvaddch (start_row + curr_row, start_col - 3, ' ');
          mvaddch (start_row + curr_row, start_col + 40, ' ');
        }

      txt_ptr = options[curr_row];
      mvprintw (start_row + curr_row, start_col, "%s", txt_ptr);
      curr_row++;
      option_ptr++;
    }
  mvprintw (start_row + curr_row + 3, start_col,
            "Move arrow keys, then press Return");
  refresh ();
}

static void
clear_all_screen (void)
{
  clear ();
  mvprintw (2, 13, "== RCU Benchmark Runner ==");
  refresh ();
}

static bool
get_confirmation (void)
{
  bool confirmed = false;
  char first_char;

  mvprintw (Q_LINE, 5, "Are you sure? ");
  clrtoeol ();
  refresh ();

  cbreak ();
  first_char = getch ();
  if (first_char == 'Y' || first_char == 'y')
    confirmed = true;

  nocbreak ();

  if (first_char == 'N' || first_char == 'n')
    {
      mvprintw (Q_LINE, 1, "     Cancelled");
      clrtoeol ();
      refresh ();
      msleep (800);
    }
  else if (!confirmed)
    {
      mvprintw (Q_LINE, 1, "     Unrecognized input (enter y/n)");
      clrtoeol ();
      refresh ();
      msleep (800);
      return get_confirmation ();
    }

  return confirmed;
}

/* Sleeps for the requested number of milliseconds */
static int
msleep (uint64_t msec)
{
  struct timespec ts;
  int res;

  ts.tv_sec = (long)(msec / 1000);
  ts.tv_nsec = (long)((msec % 1000) * 1000000);

  do
    {
      res = nanosleep (&ts, &ts);
    }
  while (res && errno == EINTR);

  return res;
}

char *test_types[] = { "POINTER", "LIST", "SYNC_OPS" };
char *concurrency_types[] = { "RCU", "SPINLOCK", "SEMA", "MUTEX", "RCU_COW" };
char *access_patterns[]
    = { "READ_ONLY", "READ_MOSTLY", "READ_WRITE", "WRITE_MOSTLY" };

static void
spawn_script (int test_type, int concurrency_type, int access_pattern, int smp)
{
  endwin ();
  int fd;
  pid_t pid1, pid2;

  fd = open ("../tests/sim_params.h", O_RDWR | O_TRUNC | O_CREAT, 0666);

  if (fd == -1)
    {
      fprintf (stderr, "File open error (../tests/sim_params.h)\n");
      exit (1);
    }

  char buffer[100];
  memset (buffer, 0, sizeof (buffer));

  int n = snprintf (buffer, 100, "#define TEST_TYPE %s\n",
                    test_types[test_type]);

  if (n <= 0)
    {
      fprintf (stderr, "Error with snprinf\n");
      exit (1);
    }

  write (fd, buffer, n);
  memset (buffer, 0, sizeof (buffer));

  n = snprintf (buffer, 100, "#define CONCURRENCY_TYPE %s\n",
                concurrency_types[concurrency_type]);

  if (n <= 0)
    {
      fprintf (stderr, "Error with snprinf\n");
      exit (1);
    }

  write (fd, buffer, n);

  memset (buffer, 0, sizeof (buffer));

  n = snprintf (buffer, 100, "#define ACCESS_PATTERN %s\n",
                access_patterns[access_pattern]);

  if (n <= 0)
    {
      fprintf (stderr, "Error with snprinf\n");
      exit (1);
    }

  write (fd, buffer, n);
  close (fd);

  if ((pid1 = fork ()) == 0)
    {
      chdir ("../filesys");
      system ("make");
      exit (0);
    }

  waitpid (pid1, NULL, 0);

  if ((pid2 = fork ()) == 0)
    {
      char *args[MAX_ARGS];
      char smp_buf[10];
      snprintf (smp_buf, 10, "%d", smp);

      args[0] = "./ptest.sh";
      args[1] = "args-none";
      args[2] = "--mem";
      args[3] = "1000";
      args[4] = "--kvm";
      args[5] = "--smp";
      args[6] = smp_buf;
      args[7] = NULL;

      execve ("./ptest.sh", args, environ);
      fprintf (stderr, "Child failed execve!\n");
      exit (1);
    }

  int status;

  waitpid (pid2, &status, 0);
  exit (status);
}
