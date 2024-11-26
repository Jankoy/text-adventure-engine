#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "src/nob.h"

typedef enum { PLATFORM_LINUX, PLATFORM_WINDOWS } build_platform_t;

static bool build_main(Cmd *cmd, build_platform_t platform, bool release, const char **exe_out) {
  const char *platform_string = (platform == PLATFORM_LINUX) ? "linux" : "windows";
  const char *release_string = (release) ? "release" : "debug";

  if (!mkdir_if_not_exists("build"))
    return false;

  const char *platform_build_path =
    temp_sprintf("build/%s", platform_string);
  
  if (!mkdir_if_not_exists(platform_build_path))
    return false;

  const char *release_build_path =
    temp_sprintf("%s/%s", platform_build_path, release_string);

  if (!mkdir_if_not_exists(release_build_path))
    return false;

  const char *exe =
    temp_sprintf("%s/text-adventure-engine", release_build_path);

  if (exe_out)
    *exe_out = exe;

  cmd->count = 0;

  cmd_append(cmd, "cc", "-o", exe);
  cmd_append(cmd, "src/main.c");
  cmd_append(cmd, "-Wall", "-Wextra");

  if (release) cmd_append(cmd, "-O2", "-s");
  else cmd_append(cmd, "-Og", "-ggdb");

  if (platform == PLATFORM_LINUX) cmd_append(cmd, "-lpdcurses");

  if (!cmd_run_sync(*cmd)) return false;

  return true;
}

static void usage(const char *program) {
  printf("%s [--windows | --linux] <-r> [args]\n", program);
  printf("\t--release: Tries to compile with optimizations and without debug "
         "symbols\n");
  printf("\t--linux: Tries to compile for linux with gcc\n");
  printf("\t--windows: Tries to compile for windows with mingw\n");
  printf("\t-r: Tries to run the executable immediately after "
         "building, it passes everything that comes after it to the "
         "executable as arguments\n");
}

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  const char *program = shift_args(&argc, &argv);

  bool release = false;
#ifdef _WIN32
  build_platform_t platform = PLATFORM_WINDOWS;
#else
  build_platform_t platform = PLATFORM_LINUX;
#endif // _WIN32
  bool run_flag = false;

  while (argc > 0) {
    const char *subcmd = shift_args(&argc, &argv);
    if (strcmp(subcmd, "--release") == 0)
      release = true;
    else if (strcmp(subcmd, "--linux") == 0)
      platform = PLATFORM_LINUX;
    else if (strcmp(subcmd, "--windows") == 0)
      platform = PLATFORM_WINDOWS;
    else if (strcmp(subcmd, "-r") == 0) {
      run_flag = true;
      break;
    } else {
      nob_log(ERROR, "Unknown flag %s", subcmd);
      usage(program);
      return 1;
    }
  }  
  
  Cmd cmd = {0};

  const char *exe;

  if (!build_main(&cmd, platform, release, &exe)) return 1;

  if (run_flag) {
    cmd.count = 0;
    cmd_append(&cmd, exe);
    da_append_many(&cmd, argv, argc);
    if (!cmd_run_sync(cmd)) return 1;
  }

  return 0;
}
