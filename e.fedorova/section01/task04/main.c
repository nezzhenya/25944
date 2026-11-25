#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_SIZE 1024

typedef struct Entry {
  char *text;
  struct Entry *following;
} Entry;

static Entry *new_entry(const char *text) {
  Entry *item = malloc(sizeof(*item));
  if (item == NULL)
    return NULL;

  size_t bytes = strlen(text) + 1;
  item->text = malloc(bytes);
  if (item->text == NULL) {
    free(item);
    return NULL;
  }

  memcpy(item->text, text, bytes);
  item->following = NULL;
  return item;
}

static int read_text(char *text, size_t capacity) {
  printf("enter string: ");
  fflush(stdout);
  if (fgets(text, (int)capacity, stdin) == NULL) {
    if (ferror(stdin)) {
      fprintf(stderr, "input error\n");
      return -1;
    }
    return 0;
  }
  if (text[0] == '.')
    return 0;

  size_t length = strlen(text);
  if (length > 0 && text[length - 1] == '\n') {
    text[length - 1] = '\0';
  } else {
    int next = getchar();
    if (next != '\n' && next != EOF) {
      while (next != '\n' && next != EOF)
        next = getchar();
      if (ferror(stdin)) {
        fprintf(stderr, "input error\n");
        return -1;
      }
      fprintf(stderr, "string too long\n");
      return 2;
    }
    if (ferror(stdin)) {
      fprintf(stderr, "input error\n");
      return -1;
    }
  }
  if (strchr(text, '\033') != NULL) {
    fprintf(stderr, "bad input\n");
    return 2;
  }
  return 1;
}

int main(void) {
  Entry *first = NULL, *last = NULL;
  char text[INPUT_SIZE];
  int status = 0;

  for (;;) {
    int result = read_text(text, sizeof(text));
    if (result == 0)
      break;
    if (result < 0) {
      status = 1;
      break;
    }
    if (result == 2)
      continue;

    Entry *item = new_entry(text);
    if (item == NULL) {
      fprintf(stderr, "memory allocation error\n");
      status = 1;
      break;
    }
    if (last != NULL)
      last->following = item;
    else
      first = item;
    last = item;
  }

  if (status == 0) {
    printf("\nlist:\n");
    for (Entry *item = first; item != NULL; item = item->following)
      puts(item->text);
  }

  while (first != NULL) {
    Entry *following = first->following;
    free(first->text);
    free(first);
    first = following;
  }
  return status;
}

