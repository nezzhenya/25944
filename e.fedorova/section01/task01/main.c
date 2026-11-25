#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/resource.h>
#include <string.h>

extern char **environ;

typedef struct {
  int flag;
  char *value;
} Request;

void show_usage(const char *name) {
  printf("usage: %s [options]\n", name);
  printf("options are processed from right to left\n");
  printf("-h             show help\n");
  printf("-i             print uid, euid, gid, egid\n");
  printf("-s             become process group leader\n");
  printf("-p             print pid, ppid, pgid\n");
  printf("-u             print file size limit in 512-byte blocks\n");
  printf("-U value       change file size limit in 512-byte blocks\n");
  printf("-c             print core file size limit in bytes\n");
  printf("-C size        change core file size limit in bytes\n");
  printf("-d             print current directory\n");
  printf("-v             print environment variables\n");
  printf("-V name=value  add or change environment variable\n");
}

static void run_option(const Request *request) {
  int key = request->flag;
  char *value_text = request->value;
  switch (key) {
    case 'i':
      printf("uid: %ld\n", (long)getuid());
      printf("euid: %ld\n", (long)geteuid());
      printf("gid: %ld\n", (long)getgid());
      printf("egid: %ld\n", (long)getegid());
      break;

    case 's':
      if (setpgid(0, 0) == -1)
        perror("setpgid");
      else
        printf("process group changed\n");
      break;

    case 'p':
      printf("pid: %ld\n", (long)getpid());
      printf("ppid: %ld\n", (long)getppid());
      printf("pgid: %ld\n", (long)getpgrp());
      break;

    case 'u':
      printf("ulimit: %ld\n", ulimit(UL_GETFSIZE));
      break;

    case 'U': {
      char *end;
      long value = strtol(value_text, &end, 10);

      if (*end != '\0' || value < 0) {
        printf("invalid value for -U\n");
        break;
      }

      if (ulimit(UL_SETFSIZE, value) == -1)
        perror("ulimit");
      else
        printf("ulimit changed to %ld\n", value);

      break;
    }

    case 'c': {
      struct rlimit bounds;

      if (getrlimit(RLIMIT_CORE, &bounds) == -1) {
        perror("getrlimit");
        break;
      }

      if (bounds.rlim_cur == RLIM_INFINITY)
        printf("core file size: unlimited\n");
      else
        printf("core file size: %llu bytes\n",
           (unsigned long long)bounds.rlim_cur);

      break;
    }

    case 'C': {
      char *end;
      unsigned long long value;
      struct rlimit bounds;

      value = strtoull(value_text, &end, 10);

      if (*end != '\0') {
        printf("invalid value for -C\n");
        break;
      }

      if (getrlimit(RLIMIT_CORE, &bounds) == -1) {
        perror("getrlimit");
        break;
      }

      bounds.rlim_cur = (rlim_t)value;

      if (setrlimit(RLIMIT_CORE, &bounds) == -1)
        perror("setrlimit");
      else
        printf("core file size changed to %llu bytes\n", value);

      break;
    }

    case 'd': {
      char directory[1024];

      if (getcwd(directory, sizeof(directory)) == NULL)
        perror("getcwd");
      else
        printf("current directory: %s\n", directory);

      break;
    }

    case 'v': {
      char **variable = environ;

      while (*variable != NULL) {
        printf("%s\n", *variable);
        variable++;
      }

      break;
    }

    case 'V': {
      char *separator = strchr(value_text, '=');

      if (separator == NULL) {
        printf("use -Vname=value\n");
        break;
      }

      *separator = '\0';

      if (setenv(value_text, separator + 1, 1) == -1)
        perror("setenv");
      else
        printf("variable changed: %s=%s\n",
           value_text, separator + 1);

      *separator = '=';
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  int key;
  Request steps[argc];
  int pending = 0;

  if (argc == 1) {
    show_usage(argv[0]);
    return 0;
  }

  while ((key = getopt(argc, argv, "hispuU:cC:dvV:")) != -1) {
    if (key == 'h') {
      show_usage(argv[0]);
      return 0;
    }

    if (key == '?') {
      fprintf(stderr, "use -h for help\n");
      return 1;
    }

    steps[pending].flag = key;
    steps[pending].value = optarg;
    pending++;
  }

  if (optind < argc) {
    fprintf(stderr, "unexpected argument: %s\n", argv[optind]);
    fprintf(stderr, "use -h for help\n");
    return 1;
  }

  while (pending > 0) {
    run_option(&steps[--pending]);
  }

  return 0;
}

