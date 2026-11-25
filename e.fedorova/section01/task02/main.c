#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

extern char *tzname[];

static void print_calendar(time_t stamp) {
  struct tm *calendar = localtime(&stamp);
  printf("%d/%d/%02d %d:%02d %s \n",
     calendar->tm_mon + 1, calendar->tm_mday,
     calendar->tm_year + 1900, calendar->tm_hour,
     calendar->tm_min, tzname[calendar->tm_isdst]);
}

int main(void) {
  setenv("TZ", "PST8", 1);
  tzset();
  time_t stamp;
  (void)time(&stamp);
  printf("%s", ctime(&stamp));
  print_calendar(stamp);
  return 0;
}

