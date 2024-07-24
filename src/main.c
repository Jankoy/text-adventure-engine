#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <curses.h>
#include <time.h>
#include <term.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

void get_term_size(int *cols, int *rows) {
  char const *const term = getenv("TERM");
  if (term == NULL) {
    fprintf(stderr, "TERM environment variable not set\n");
    return;
  }

  char const *const cterm_path = ctermid(NULL);
  if (cterm_path == NULL || cterm_path[0] == '\0') {
    fprintf(stderr, "ctermid() failed\n");
    return;
  }

  int tty_fd = open(cterm_path, O_RDWR);
  if (tty_fd == -1) {
    fprintf(stderr,
      "open(\"%s\") failed (%d): %s\n",
      cterm_path, errno, strerror(errno)
    );
    return;
  }

  int setupterm_err;
  if (setupterm((char*)term, tty_fd, &setupterm_err) == ERR) {
    switch (setupterm_err) {
      case -1:
        fprintf(stderr,
          "setupterm() failed: terminfo database not found\n"
        );
        goto done;
      case 0:
        fprintf(stderr,
          "setupterm() failed: TERM=%s not found in database\n",
          term
        );
        goto done;
      case 1:
        fprintf(stderr,
          "setupterm() failed: terminal is hardcopy\n"
        );
        goto done;
    }
  }

  *cols = tigetnum((char*)"cols");
  if (*cols < 0)
    fprintf(stderr, "tigetnum() failed to get cols (%d)\n", *cols);

  *rows = tigetnum((char*)"lines");
  if (*rows < 0)
    fprintf(stderr, "tigetnum() failed to get rows (%d)\n", *rows);

done:
  if (tty_fd != -1)
    close(tty_fd);
}

#define ESC "\033"

#define CLEAR_SCREEN ESC"[2J"
#define RESET_CURSOR ESC"[H"

#define MOVE_CURSOR(column, line) ESC"[%d;%dH", (line), (column)

// Undefine curses.h color definitions
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_MAGENTA
#undef COLOR_CYAN
#undef COLOR_WHITE

#define COLOR_RESET   ESC"[0m"

#define COLOR_BLACK   ESC"[30m"
#define COLOR_RED     ESC"[31m"
#define COLOR_GREEN   ESC"[32m"
#define COLOR_YELLOW  ESC"[33m"
#define COLOR_BLUE    ESC"[34m"
#define COLOR_MAGENTA ESC"[35m"
#define COLOR_CYAN    ESC"[36m"
#define COLOR_WHITE   ESC"[37m"

#define COLOR_GRAY  ESC"[2;37m"

void sig_handler(int _) {
    (void)_;
    printf(RESET_CURSOR);
    printf(CLEAR_SCREEN);
    exit(0);
}

static inline void put_many_char(char c, size_t count) {
  for (size_t i = 0; i < count; ++i)
    putchar(c);
}

static inline void put_many_str(char *s, size_t count) {
  for (size_t i = 0; i < count; ++i)
    printf(s);
}

#define INPUT_BUF_CAP (256 + 1)
static char input_buf[INPUT_BUF_CAP] = {};

static int cols = -1, rows = -1;

typedef struct {
  time_t time;
  const char *msg;
} message_t;

static struct {
  message_t *items;
  size_t count;
  size_t capacity;
} message_log = {};

#define FORMAT_TIME_BUF_CAP 8
static inline const char *format_time(time_t time) {
  static char buf[FORMAT_TIME_BUF_CAP] = {};
  struct tm *local = localtime(&time);
  snprintf(buf, FORMAT_TIME_BUF_CAP, "%02u:%02u", local->tm_hour % 12, local->tm_min);
  return buf;
}

static inline void log_message(const char *message) {
  if ((int)message_log.count < rows - 3)
    nob_da_append(&message_log, ((message_t){time(NULL), strdup(message)}));
  else {
    free((char *)message_log.items[0].msg);
    for (size_t i = 0; i < message_log.count; ++i)
      message_log.items[i] = message_log.items[i + 1];
    message_log.items[message_log.count - 1] = ((message_t){time(NULL), strdup(message)});
  }
}

static inline void log_clear(void) {
  for (size_t i = 0; i < message_log.count; ++i)
    free((char *)message_log.items[i].msg);
  message_log.count = 0;
}

#define MAX_MAP_SIZE 5
typedef struct {
  char map[MAX_MAP_SIZE][MAX_MAP_SIZE];
  const char *rooms[256];
} adventure_t;

static bool read_adventure_file(const char *filename, adventure_t *dest) {
  size_t temp_save = nob_temp_save();
  bool result = true;
  Nob_String_Builder source = {};

  if (!nob_read_entire_file(filename, &source)) {
    log_message(COLOR_RED"Error: Could not read adventure file!");
    nob_return_defer(false);
  }

  Nob_String_View view = {
    .data = source.items,
    .count = source.count
  };
  view = nob_sv_trim(view);

  Nob_String_View line = nob_sv_chop_by_delim(&view, '\n');
  if (!nob_sv_eq(line, nob_sv_from_cstr("map"))) {
    log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
    nob_return_defer(false);
  }
  line = nob_sv_chop_by_delim(&view, '\n');

  bool pam = false;
  int row = 0;
  while (line.count > 0) {
    if (nob_sv_eq(line, nob_sv_from_cstr("pam"))) {
      pam = true;
end:
      line = nob_sv_chop_by_delim(&view, '\n');
      break;
    }
    
    for (size_t i = 0; i < line.count && i <= MAX_MAP_SIZE; ++i) {
      if (line.data[i] != ' ' && line.data[i] != '\n')
        dest->map[row][i] = line.data[i];
    }
    
    line = nob_sv_chop_by_delim(&view, '\n');
    row++;

    if (row > MAX_MAP_SIZE)
      goto end;
  }
  if (!pam) {
    log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
    nob_return_defer(false);
  }

  if (!nob_sv_eq(line, nob_sv_from_cstr("rooms"))) {
    log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
    nob_return_defer(false);
  }
  line = nob_sv_chop_by_delim(&view, '\n');

  bool smoor = false;
  while (line.count > 0) {
    if (nob_sv_eq(line, nob_sv_from_cstr("smoor"))) {
      smoor = true;
      line = nob_sv_chop_by_delim(&view, '\n');
      break;
    }
    char key = line.data[0];
    if (line.data[1] != '=') {
      log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
      nob_return_defer(false);
    }
    if (line.data[2] != '"') {
      log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
      nob_return_defer(false);
    }
    nob_sv_chop_by_delim(&line, '"');
    Nob_String_View value = nob_sv_chop_by_delim(&line, '"');
    dest->rooms[(size_t)key] = strdup(nob_temp_sv_to_cstr(value));
    line = nob_sv_chop_by_delim(&view, '\n');
  }
  if (!smoor) {
    log_message(COLOR_RED"Error: Invalid or corrupt adventure file!");
    nob_return_defer(false);
  }

  log_message(nob_temp_sprintf(COLOR_YELLOW"Adventure \"%s\" loaded successfully!", filename));

defer:  
  nob_sb_free(source);
  nob_temp_rewind(temp_save);
  return result;
}

static adventure_t adventure = {};

int main(void) {
  signal(SIGQUIT, sig_handler);
  signal(SIGINT, sig_handler);

  bool adventure_loaded = false;

  while (true) {
    get_term_size(&cols, &rows);

    printf(RESET_CURSOR);
    printf(CLEAR_SCREEN);

    size_t temp_save = nob_temp_save();

    put_many_char('=', cols);
    putchar('\n');

    for (size_t i = 0; i < message_log.count; ++i)
      printf(COLOR_GRAY"<%s>"COLOR_RESET" %s"COLOR_RESET"\n", format_time(message_log.items[i].time), message_log.items[i].msg);
    
    printf(MOVE_CURSOR(1, rows - 1));
    put_many_char('=', cols);
    
    putchar('\n');
    fgets(input_buf, INPUT_BUF_CAP, stdin);

    if (input_buf[0] == '\n')
      goto end;
    
    input_buf[strlen(input_buf) - 1] = '\0';
    log_message(input_buf);
    
    if (strcmp(input_buf, "exit") == 0 || !input_buf[0])
      break;
    else if (strcmp(input_buf, "clear") == 0)
      log_clear();
    else if (strcmp(input_buf, "load") == 0) {
      adventure_loaded = read_adventure_file("test.ta", &adventure);
      log_message(nob_temp_sprintf(COLOR_YELLOW"%s", adventure.rooms['S']));
    } else if (strcmp(input_buf, "look") == 0) {
      if (!adventure_loaded)
        log_message(COLOR_RED"Error: No adventure loaded, please use the \"load\" command first!");
      else
        log_message(nob_temp_sprintf(COLOR_YELLOW"%s", adventure.rooms['S']));
    } else
      log_message(COLOR_RED"Error: Unknown command!");
    
end:
    memset(input_buf, '\0', INPUT_BUF_CAP);
    nob_temp_rewind(temp_save);
  }

  printf(RESET_CURSOR);
  printf(CLEAR_SCREEN);
  
  return 0;
}
