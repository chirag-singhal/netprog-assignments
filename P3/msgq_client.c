#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_CMD_LEN 32
#define ALIAS_NAME_SIZE 20


void prompt(char* alias_name) {
    printf("[%s] ", alias_name);
}

int main() {

    char* alias_name = malloc(sizeof(char) * ALIAS_NAME_SIZE);
    printf("Enter alias name : ");
    size_t max_alias_len = ALIAS_NAME_SIZE;
    size_t alias_len = getline(&alias_name, &max_alias_len, stdin);
    alias_name[alias_len - 1] = '\0';

    while(true) {
        prompt(alias_name);
        char * cmd = malloc(sizeof(char) * (MAX_CMD_LEN + 1));
        size_t max_cmd_len = MAX_CMD_LEN;
        size_t cmd_len = getline(&cmd, &max_cmd_len, stdin);
        cmd[cmd_len - 1] = '\0';
        cmd_len = strlen(cmd);
    }
    return EXIT_SUCCESS;
}