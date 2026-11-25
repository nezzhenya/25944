#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

void print_ids(void)
{
    printf("real uid: %ld\n", (long)getuid());
    printf("euid: %ld\n", (long)geteuid());
}

void check_file(void)
{
    FILE *file = fopen("test.txt", "r");

    if (file == NULL)
    {
        perror("fopen");
        return;
    }

    printf("file opened\n");

    if (fclose(file) != 0)
        perror("fclose");
}

int main(void)
{
    printf("before setuid:\n");
    print_ids();
    check_file();

    if (setuid(getuid()) == -1)
    {
        perror("setuid");
        return 1;
    }

    printf("\nafter setuid:\n");
    print_ids();
    check_file();

    return 0;
}

