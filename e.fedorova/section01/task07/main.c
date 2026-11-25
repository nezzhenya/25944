#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

typedef struct {
  size_t begin;
  size_t width;
} Record;

volatile sig_atomic_t expired = 0;

void mark_timeout(int sig) {
  (void)sig;
  expired = 1;
}

int read_request(char *entry, size_t space, int use_timer) {
  sigset_t blocked_signals, previous_mask, waiting_mask;
  sigemptyset(&blocked_signals);
  sigaddset(&blocked_signals, SIGALRM);

  if (sigprocmask(SIG_BLOCK, &blocked_signals, &previous_mask) == -1)
    return -1;

  waiting_mask = previous_mask;
  sigdelset(&waiting_mask, SIGALRM);

  size_t used = 0;
  int invalid_input = 0;
  int outcome = -1;

  expired = 0;
  if (use_timer)
    alarm(5);

  while (1) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);

    int n = pselect(STDIN_FILENO + 1, &readable, NULL, NULL, NULL, &waiting_mask);

    if (expired) {
      outcome = -2;
      break;
    }

    if (n == -1) {
      if (errno == EINTR)
        continue;
      break;
    }

    char c;
    ssize_t got = read(STDIN_FILENO, &c, 1);

    if (got == -1) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (got == 0 || c == '\n') {
      entry[used] = '\0';
      if (invalid_input)
        outcome = 2;
      else if (got == 0 && used == 0)
        outcome = 0;
      else
        outcome = 1;
      break;
    }

    if (c == '\0')
      invalid_input = 1;

    if (used < space - 1)
      entry[used++] = c;
    else
      invalid_input = 1;
  }

  int error_code = errno;
  if (use_timer)
    alarm(0);

  if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1)
    return -1;

  errno = error_code;
  return outcome;
}

int write_region(const char *mapped_text, size_t begin, size_t used) {
  if (used == 0)
    return 0;

  if (fwrite(mapped_text + begin, 1, used, stdout) != used) {
    fprintf(stderr, "output error\n");
    return 1;
  }

  if (mapped_text[begin + used - 1] != '\n') {
    if (putchar('\n') == EOF) {
      fprintf(stderr, "output error\n");
      return 1;
    }
  }

  return 0;
}

static int index_mapping(const char *mapped_text, size_t file_size, Record **rows_out, size_t *count_out) {
  Record *records = NULL;
  size_t record_count = 0;
  size_t begin = 0;
  int exit_code = 0;

  for (size_t cursor = 0; cursor < file_size; cursor++) {
    if (mapped_text[cursor] == '\n' || cursor == file_size - 1) {
      if (record_count >= SIZE_MAX / sizeof(Record)) {
        fprintf(stderr, "too many lines\n");
        exit_code = 1;
        break;
      }

      Record *expanded = realloc(records, (record_count + 1) * sizeof(Record));
      if (expanded == NULL) {
        perror("realloc");
        exit_code = 1;
        break;
      }

      records = expanded;
      records[record_count].begin = begin;
      records[record_count].width = cursor - begin + 1;
      record_count++;
      begin = cursor + 1;
    }
  }
  *rows_out = records;
  *count_out = record_count;
  return exit_code;
}

static int read_requests(const char *mapped_text, const Record *records, size_t record_count) {
  int exit_code = 0;
  printf("lines: %zu\n", record_count);

  for (size_t i = 0; i < record_count; i++)
    printf("%zu: offset %zu, length %zu\n", i + 1, records[i].begin, records[i].width);

  char entry[64];
  int first_request = 1;

  while (1) {
    if (first_request)
      printf("\nline number (5 seconds, 0 to exit): ");
    else
      printf("\nline number (0 to exit): ");

    if (fflush(stdout) == EOF) {
      fprintf(stderr, "output error\n");
      exit_code = 1;
      break;
    }

    int outcome = read_request(entry, sizeof(entry), first_request);
    first_request = 0;

    if (outcome == -2) {
      printf("\ntime is up\n");
      break;
    }

    if (outcome == -1) {
      perror("input");
      exit_code = 1;
      break;
    }

    if (outcome == 0)
      break;

    if (outcome == 2) {
      printf("invalid number\n");
      continue;
    }

    char *end;
    errno = 0;
    long requested = strtol(entry, &end, 10);

    if (end == entry || errno == ERANGE) {
      printf("invalid number\n");
      continue;
    }

    while (isspace((unsigned char)*end))
      end++;

    if (*end != '\0') {
      printf("invalid number\n");
      continue;
    }

    if (requested == 0)
      break;

    if (requested < 0 || (uintmax_t)requested > (uintmax_t)record_count) {
      printf("line not found\n");
      continue;
    }

    Record record = records[requested - 1];
    printf("line %ld: ", requested);
    exit_code = write_region(mapped_text, record.begin, record.width);

    if (exit_code != 0)
      break;
  }
  return exit_code;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("usage: %s file\n", argv[0]);
    return 1;
  }

  int input_fd = open(argv[1], O_RDONLY);
  if (input_fd == -1) {
    perror("open");
    return 1;
  }

  struct stat metadata;
  if (fstat(input_fd, &metadata) == -1) {
    perror("fstat");
    close(input_fd);
    return 1;
  }

  if (!S_ISREG(metadata.st_mode)) {
    fprintf(stderr, "not a regular file\n");
    close(input_fd);
    return 1;
  }

  if (metadata.st_size < 0 || (uintmax_t)metadata.st_size > SIZE_MAX) {
    fprintf(stderr, "file too large\n");
    close(input_fd);
    return 1;
  }

  size_t file_size = (size_t)metadata.st_size;
  char *mapped_text = NULL;

  if (file_size > 0) {
    mapped_text = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, input_fd, 0);
    if (mapped_text == MAP_FAILED) {
      perror("mmap");
      close(input_fd);
      return 1;
    }
  }

  if (close(input_fd) == -1) {
    perror("close");
    if (file_size > 0)
      munmap(mapped_text, file_size);
    return 1;
  }

  struct sigaction act = {0};
  act.sa_handler = mark_timeout;
  sigemptyset(&act.sa_mask);

  if (sigaction(SIGALRM, &act, NULL) == -1) {
    perror("sigaction");
    if (file_size > 0)
      munmap(mapped_text, file_size);
    return 1;
  }

  Record *records = NULL;
  size_t record_count = 0;
  int exit_code = index_mapping(mapped_text, file_size, &records, &record_count);
  if (exit_code == 0)
    exit_code = read_requests(mapped_text, records, record_count);

  free(records);

  if (file_size > 0 && mapped_text != NULL && mapped_text != MAP_FAILED) {
    if (munmap(mapped_text, file_size) == -1) {
      perror("munmap");
      exit_code = 1;
    }
  }

  if (fflush(stdout) == EOF) {
    fprintf(stderr, "output error\n");
  }

  return exit_code;
}

