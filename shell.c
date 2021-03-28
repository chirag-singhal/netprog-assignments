#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_CMD_LEN 512

char * PATH;

void err_exit(const char *err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

char **parser_cmd(char *cmd ) {
    const char delim[]= " ";
    int size = 0;
    char temp[MAX_CMD_LEN][MAX_CMD_LEN];
    
    char * token = strtok(cmd, delim);

    while(token != NULL) {
        strcpy(temp[size], token);
        size++;
        token = strtok(NULL, delim);
    }

    char **cmd_toks = malloc(sizeof(char *) * (size + 1));
    for (size_t i = 0; i < size; i++) {
        cmd_toks[i] = malloc(sizeof(char) * (strlen(temp[i]) + 1));
        strcpy(cmd_toks[i], temp[i]);
    }
    cmd_toks[size] = NULL; 
    return cmd_toks;
}

void execute_cmd(char **cmd_toks, int n_cmd_toks) {
    // Search in PATH for command
    char cmd_path[MAX_CMD_LEN];
    
    char * token = strtok(PATH, ":");
    while (token != NULL) {
        char temp_cmd[MAX_CMD_LEN];
        strcpy(temp_cmd, token);
        strcat(temp_cmd, "/");
        strcat(temp_cmd, cmd_toks[0]);
        if (access(temp_cmd, F_OK) == 0) {
            strcpy(cmd_path, temp_cmd);
            break;
        }
        token = strtok(NULL, ":");
    }

    if (cmd_path != NULL) {
        pid_t child = fork();
        if (child == 0) {
            execv(cmd_path, cmd_toks);
        }
        else {
            int status;
            wait(&status);
        }
    }
    else {
        // error
        printf("Command not found in Path!");
    }
}

int main() {

    PATH = getenv("PATH");
    char cmd[MAX_CMD_LEN + 1];

    while (1) {
        printf(">> ");
        char *cmd_read = fgets(cmd, MAX_CMD_LEN + 1, stdin);
        
        // If no char read or error occured 
        if(cmd_read == NULL || cmd[0] == '\n')
            continue;

        int cmd_len = strlen(cmd_read);
        cmd_read[cmd_len - 1] = '\0';
        bool is_bg_proc = false;

        if(cmd_read[cmd_len - 2] == '&') {
            is_bg_proc = true;
            cmd_read[cmd_len - 2] = '\0';
        } 

        cmd_len = strlen(cmd_read);

        // Spawn a new process group for the `cmd`
        //
        // Wait till `cmd` is completed
    }

    return EXIT_SUCCESS;
}
