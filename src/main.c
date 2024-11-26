#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#include <sys/types.h>
#undef MOUSE_MOVED
#else
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <curses.h>
// Undefine curses.h color definitions
#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_MAGENTA
#undef COLOR_CYAN
#undef COLOR_WHITE
#include <term.h>
#endif //_WIN32

#include <signal.h>
#include <time.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

// As it stands, these functions are written very hackily.
#ifdef _WIN32
void get_term_size(int *cols, int *rows) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
	    fprintf(stderr, "GetConsoleScreenBufferInfo() failed\n");
        return;
	}
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}
#else
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
#endif //_WIN32

#define ESC "\033"

#define CLEAR_SCREEN ESC"[2J"
#define RESET_CURSOR ESC"[H"

#define MOVE_CURSOR(column, line) ESC"[%d;%dH", (line), (column)

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
    da_append(&message_log, ((message_t){time(NULL), strdup(message)}));
  else {
      free((char *)message_log.items[0].msg);
    for (size_t i = 0; i < message_log.count - 1; ++i)
      message_log.items[i] = message_log.items[i + 1];
    message_log.items[message_log.count - 1] = ((message_t){time(NULL), strdup(message)});
  }
}

static inline void log_help(void) {
  log_message(COLOR_YELLOW"Type \"load <adventure name>\" to load an <adventure name>.ta file.");
  log_message(COLOR_YELLOW"Then type \"look\" to look around the room, or \"look <direction>\" to look into a nearby room.");
}

static inline void log_clear(void) {
  for (size_t i = 0; i < message_log.count; ++i)
    free((char *)message_log.items[i].msg);
  message_log.count = 0;
}

typedef enum {
  NORTH,
  EAST,
  SOUTH,
  WEST,
  INVALID_DIRECTION
} direction_t;

typedef struct {
  const char *description;
  char connections[4];
} room_t;

#define MAX_MAP_SIZE 5
typedef struct {
  char map[MAX_MAP_SIZE][MAX_MAP_SIZE];
  room_t rooms[256];
} adventure_t;

#define SV(cstr) sv_from_cstr((cstr))
#ifdef _WIN32
String_View sv_chop_by_newline(String_View *sv) {
  String_View part = sv_chop_by_delim((sv), '\n');
  return sv_chop_by_delim(&part, '\r');
}
#else
String_View sv_chop_by_newline(String_View *sv) {
  return sv_chop_by_delim(sv, '\n');
}
#endif //_WIN32
typedef int (chop_predicate_t)(int);
String_View sv_chop_by_predicate(String_View *sv, chop_predicate_t predicate) {
    size_t i = 0;
    while (i < sv->count && !predicate(sv->data[i])) {
        i += 1;
    }

    Nob_String_View result = nob_sv_from_parts(sv->data, i);

    if (i < sv->count) {
        sv->count -= i + 1;
        sv->data  += i + 1;
    } else {
        sv->count -= i;
        sv->data  += i;
    }

    return result;
}

direction_t get_direction_index(String_View dir) {
  if (sv_eq(dir, SV("north"))) return NORTH;
  if (sv_eq(dir, SV("east"))) return EAST;
  if (sv_eq(dir, SV("south"))) return SOUTH;
  if (sv_eq(dir, SV("west"))) return WEST;
  return INVALID_DIRECTION;
}

#define error_read(filename) \
  do { \
    log_message(temp_sprintf(COLOR_RED"Error %d: could not read adventure file: %s", __LINE__, (filename))); \
    return_defer(false); \
  } while (0);

#define error_invalid(filename) \
  do { \
  log_message(temp_sprintf(COLOR_RED"Error %d: invalid or corrupt adventure file: %s", __LINE__, (filename))); \
  return_defer(false); \
  } while (0);

static bool read_adventure_file(const char *filename, adventure_t *dest) {
  size_t save = temp_save();
  bool result = true;
  String_Builder source = {};

  if (!read_entire_file(filename, &source)) error_read(filename);

  String_View view = {
    .data = source.items,
    .count = source.count
  };
  view = sv_trim(view);

  String_View line = sv_chop_by_newline(&view);
  if (!sv_eq(line, SV("map"))) error_invalid(filename);
  line = sv_chop_by_newline(&view);

  bool pam = false;
  int row = 0;
  while (line.count > 0) {
    if (sv_eq(line, SV("pam"))) {
      pam = true;
end:
      line = sv_chop_by_newline(&view);
      break;
    }
    
    for (size_t i = 0; i < line.count && i <= MAX_MAP_SIZE; ++i) {
      if (line.data[i] != ' ' && !(line.data[i] == '\n' || line.data[i] == '\r'))
        dest->map[row][i] = line.data[i];
    }
    
    line = sv_chop_by_newline(&view);
    row++;

    if (row > MAX_MAP_SIZE) goto end;
  }
  if (!pam) error_invalid(filename);

  if (!sv_eq(line, SV("rooms"))) error_invalid(filename);
  line = sv_chop_by_newline(&view);

  bool smoor = false;
  while (line.count > 0) {
    if (line.data[0] == '#') goto skip;
    if (sv_eq(line, SV("smoor"))) {
      smoor = true;
      line = sv_chop_by_newline(&view);
      break;
    }
    if (!sv_end_with(line, ";")) error_invalid(filename);
    room_t room = {0};
    char key = line.data[0];
    if (line.data[1] != '=') error_invalid(filename);
    if (line.data[2] != '"') error_invalid(filename);
    sv_chop_by_delim(&line, '"');
    String_View value = sv_chop_by_delim(&line, '"');
    room.description = strdup(temp_sv_to_cstr(value));
    if (line.data[0] == '(') {
      line.count--;
      line.data++;
      if (line.data[0] == ';') error_invalid(filename);
      while (line.data[0] != ';' && line.count > 1) {
        direction_t dir = get_direction_index(sv_chop_by_delim(&line, '='));
        if (dir == INVALID_DIRECTION) error_invalid(filename);
        char r = line.data[0];
        line.count--;
        line.data++;
        if (!(line.data[0] == ',' || line.data[0] == ')')) error_invalid(filename);
        room.connections[dir] = r;
        line.count--;
        line.data++;
      }
    }
    if (line.data[0] != ';') error_invalid(filename);
    dest->rooms[(size_t)key] = room;
skip:
    line = sv_chop_by_newline(&view);
  }
  if (!smoor) error_invalid(filename);

defer:  
  sb_free(source);
  temp_rewind(save);
  return result;
}

static adventure_t adventure = {};

int main(void) {
#ifdef SIGQUIT
  signal(SIGQUIT, sig_handler);
#endif
  signal(SIGINT, sig_handler);

  bool adventure_loaded = false;
  char current_room = 'S';

  while (true) {
    get_term_size(&cols, &rows);

    printf(RESET_CURSOR);
    printf(CLEAR_SCREEN);

    size_t save = temp_save();

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
    String_View input = SV(input_buf);
    for (size_t i = 0; i < input.count; ++i)
      input_buf[i] = tolower(input_buf[i]);
    String_View cmd = sv_chop_by_predicate(&input, isspace);
    
    if (sv_eq(cmd, SV("exit")) || !input_buf[0])
      break;
    if (sv_eq(cmd, SV("help"))) {
      log_help();
    } else if (sv_eq(cmd, SV("clear")))
      log_clear();
    else if (sv_eq(cmd, SV("load"))) {
      if (sv_eq(input, SV(""))) {
        log_message(COLOR_RED"Error: no adventure name provided, please provide a name");
      } else {
        const char *filename = temp_sprintf(SV_Fmt".ta", SV_Arg(input));
        if ((adventure_loaded = read_adventure_file(filename, &adventure))) {
          log_message(temp_sprintf(COLOR_YELLOW"Info: adventure \"%s\" loaded successfully", filename));
          log_message(temp_sprintf(COLOR_YELLOW"%s", adventure.rooms[(unsigned char)current_room].description));
        }
      }
    } else if (sv_eq(cmd, SV("look"))) {
      if (!adventure_loaded)
        log_message(COLOR_RED"Error: no adventure loaded, please use the \"load\" command first");
      else {
        String_View direction = sv_chop_by_predicate(&input, isspace);
        if (sv_eq(direction, SV("")))
          log_message(temp_sprintf(COLOR_YELLOW"%s", adventure.rooms[(unsigned char)current_room].description));
        else {
          direction_t idx = get_direction_index(direction);
          if (idx == INVALID_DIRECTION)
            log_message(temp_sprintf(COLOR_RED"Error: \""SV_Fmt"\" is an invalid direction (north, south, east, west)", SV_Arg(direction)));
          else {
          log_message(temp_sprintf(COLOR_YELLOW"%s", adventure.rooms[(unsigned char)adventure.rooms[(unsigned char)current_room].connections[idx]].description));
          }
        }
      }
    } else
      log_message(COLOR_RED"Error: unknown command");
    
end:
    memset(input_buf, '\0', INPUT_BUF_CAP);
    temp_rewind(save);
  }

  printf(RESET_CURSOR);
  printf(CLEAR_SCREEN);
  
  return 0;
}
