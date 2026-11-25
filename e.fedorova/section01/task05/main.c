#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

typedef struct {
  off_t begin;
  off_t width;
} Record;

static int index_file(int input_fd, Record **rows_out, size_t *count_out) {
  Record *records = NULL;
  size_t record_count = 0;
  off_t cursor = 0, begin = 0;
  char c;
  ssize_t n;
  int exit_code = 0;

  while ((n = read(input_fd, &c, 1)) == 1) {
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

  if (n == -1) {
    perror("read");
    exit_code = 1;
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
  char block[4096];

  while (1) {
    printf("\nline number: ");
    fflush(stdout);

    if (fgets(entry, sizeof(entry), stdin) == NULL) {
      if (ferror(stdin))
        perror("input");
      break;
    }

    size_t used = 0;
    while (entry[used] != '\0' && entry[used] != '\n')
      used++;

    if (entry[used] != '\n' && !feof(stdin)) {
      int ch = getchar();
      if (ch != '\n' && ch != EOF) {
        while (ch != '\n' && ch != EOF)
          ch = getchar();
        printf("invalid number\n");
        continue;
      }
    }

    char *end;
    errno = 0;
    long requested = strtol(entry, &end, 10);

    while (*end == ' ' || *end == '\t')
      end++;

    if (end == entry || errno == ERANGE ||
      (*end != '\0' && *end != '\n')) {
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

    if (lseek(input_fd, line.begin, SEEK_SET) == -1) {
      perror("lseek");
      exit_code = 1;
      break;
    }

    off_t remaining = line.width;
    char final_byte = '\n';

    printf("line %ld: ", requested);

    while (remaining > 0) {
      size_t size = remaining > (off_t)sizeof(block)
            ? sizeof(block) : (size_t)remaining;

      ssize_t got = read(input_fd, block, size);

      if (got <= 0) {
        if (got < 0)
          perror("read");
        else
          fprintf(stderr, "unexpected end of file\n");

        exit_code = 1;
        break;
      }

      final_byte = block[got - 1];
      fwrite(block, 1, got, stdout);
      remaining -= got;
    }

    if (exit_code != 0)
      break;

    if (final_byte != '\n')
      putchar('\n');
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

  Record *records = NULL;
  size_t record_count = 0;
  int exit_code = index_file(input_fd, &records, &record_count);
  if (exit_code == 0)
    exit_code = read_requests(input_fd, records, record_count);

  free(records);
  close(input_fd);
  return exit_code;
}

