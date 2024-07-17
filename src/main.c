#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <curses.h>
#include <term.h>

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
    } // switch
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

static volatile sig_atomic_t running = true;

static void sig_handler(int _) {
    (void)_;
    running = false;
    printf(RESET_CURSOR);
    printf(CLEAR_SCREEN);
    exit(0);
}

#define INPUT_BUF_CAP (256 + 1)
static char input_buf[INPUT_BUF_CAP] = {};

static const char *const test_message_log[] = {
  "First message!",
  "Second message!",
  "Third message!"
};

int main(void) {
  signal(SIGQUIT, sig_handler);
  signal(SIGINT, sig_handler);
  
  while (running) {
    int cols, rows;
    get_term_size(&cols, &rows);

    printf(RESET_CURSOR);
    printf(CLEAR_SCREEN);

    for (int i = 0; i < cols; ++i)
      putchar('=');
    printf(MOVE_CURSOR(1, rows));
    for (int i = 0; i < cols; ++i)
      putchar('=');
    printf(MOVE_CURSOR(1, 2));
    
    fgets(input_buf, INPUT_BUF_CAP, stdin);

    if (strcmp(input_buf, "exit\n") == 0)
      running = false;
  }
  
  printf(RESET_CURSOR);
  printf(CLEAR_SCREEN);
  return 0;
}
