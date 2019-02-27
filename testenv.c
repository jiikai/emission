#define _POSIX_C_SOURCE 200112L

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char const *argv[]) {
    char *ptr;
    time_t old = (time_t) strtol(getenv("TEST_TIME"), &ptr, 10);
    time_t new;
    time(&new);
    if (old < new) {
        printf("%d\n", (int) new );
        return (int) new;
    }
    return (int) old;
}
