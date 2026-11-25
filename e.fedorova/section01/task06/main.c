#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
  off_t begin;
  off_t width;
} Record;

volatile sig_atomic_t expired = 0;

void mark_timeout(int signalNumber) {
  (void)signalNumber;
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

  size_t width = 0;
  int invalid_input = 0;
  int outcome = -1;

  expired = 0;
  if (use_timer)
    alarm(5);

  while (1) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(STDIN_FILENO, &readable);

    int n = pselect(STDIN_FILENO + 1, &readable, NULL,
            NULL, NULL, &waiting_mask);

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
      entry[width] = '\0';

      if (invalid_input)
        outcome = 2;
      else if (got == 0 && width == 0)
        outcome = 0;
      else
        outcome = 1;

      break;
    }

    if (c == '\0')
      invalid_input = 1;

    if (width < space - 1)
      entry[width++] = c;
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

int write_region(int input_fd, off_t begin, off_t width) {
  if (lseek(input_fd, begin, SEEK_SET) == -1) {
    perror("lseek");
    return 1;
  }

  char block[4096];
  char final_byte = '\n';

  while (width != 0) {
    size_t size = sizeof(block);

    if (width > 0 && width < (off_t)size)
      size = (size_t)width;

    ssize_t got = read(input_fd, block, size);

    if (got == -1) {
      if (errno == EINTR)
        continue;

      perror("read");
      return 1;
    }

    if (got == 0) {
      if (width > 0) {
        fprintf(stderr, "unexpected end of file\n");
        return 1;
      }

      break;
    }

    if (fwrite(block, 1, (size_t)got, stdout) != (size_t)got) {
      fprintf(stderr, "output error\n");
      return 1;
    }

    final_byte = block[got - 1];

    if (width > 0)
      width -= got;
  }

  if (final_byte != '\n')
    putchar('\n');

  return 0;
}

static int index_file(int input_fd, Record **rows_out, size_t *count_out) {
  Record *records = NULL;
  size_t record_count = 0;
  off_t cursor = 0, begin = 0;
  char c;
  ssize_t n;
  int exit_code = 0;

  while ((n = read(input_fd, &c, 1)) != 0) {
    if (n == -1) {
      if (errno == EINTR)
        continue;

      perror("read");
      exit_code = 1;
      break;
    }

    cursor++;

    if (c == '\n') {
      Record *expanded = realloc(records, (record_count + 1) * sizeof(Record));

      if (expanded == NULL) {
        perror("realloc");
        exit_code = 1;
        break;
      }

      records = expanded;
      records[record_count].begin = begin;
      records[record_count].width = cursor - begin;
      record_count++;
      begin = cursor;
    }
  }

  if (exit_code == 0 && cursor > begin) {
    Record *expanded = realloc(records, (record_count + 1) * sizeof(Record));

    if (expanded == NULL) {
      perror("realloc");
      exit_code = 1;
    } else {
      records = expanded;
      records[record_count].begin = begin;
      records[record_count].width = cursor - begin;
      record_count++;
    }
  }
  *rows_out = records;
  *count_out = record_count;
  return exit_code;
}

static int read_requests(int input_fd, const Record *records, size_t record_count) {
  int exit_code = 0;
  printf("lines: %zu\n", record_count);

  for (size_t i = 0; i < record_count; i++)
    printf("%zu: offset %lld, length %lld\n",
       i + 1,
       (long long)records[i].begin,
       (long long)records[i].width);

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

    if (requested < 0 || (unsigned long)requested > record_count) {
      printf("line not found\n");
      continue;
    }

    Record line = records[requested - 1];

    printf("line %ld: ", requested);
    exit_code = write_region(input_fd, line.begin, line.width);

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

  struct sigaction action = {0};
  action.sa_handler = mark_timeout;
  sigemptyset(&action.sa_mask);

  if (sigaction(SIGALRM, &action, NULL) == -1) {
    perror("sigaction");
    close(input_fd);
    return 1;
  }

  Record *records = NULL;
  size_t record_count = 0;
  int exit_code = index_file(input_fd, &records, &record_count);
  if (exit_code == 0)
    exit_code = read_requests(input_fd, records, record_count);

  free(records);

  if (close(input_fd) == -1) {
    perror("close");
    exit_code = 1;
  }

  return exit_code;
}

