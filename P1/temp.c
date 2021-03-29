#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <errno.h>

int main() {

    char * temp[3] = {"ls", "-zz", NULL};
    execv("/bin/ls", temp);

    return EXIT_SUCCESS;
}
