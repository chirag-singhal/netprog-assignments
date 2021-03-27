#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CMD_LEN 1024

const char * PATH;

typedef struct _CMD_OPTS_REDIRECT {
    // program is first word in opts
    // last word in opts is NULL
    char * program;
    char ** opts;
    size_t n_opts;
    char * in_redirect_file;
    char * out_redirect_file;
    int is_append;
} CMD_OPTS_REDIRECT;

////////////////////////////////////////

void err_exit(const char *err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

char **parser_cmd(const char *cmd ) {
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

void execute_cmd(const char **cmd_toks, int n_cmd_toks) {
    // Search in PATH for command
    char cmd_path[MAX_CMD_LEN] = NULL;
    
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

void execute_single_cmd(CMD_OPTS_REDIRECT * cmd) {
    if (cmd->in_redirect_file != NULL) {
        int in_redirect_fd = open(cmd->in_redirect_file, O_CREAT | O_RDONLY, 0664);
        if (in_redirect_fd < 0) {
            char err_msg[100];
            fprintf(err_msg, "Cannot open file '%s'", cmd->in_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(in_redirect_fd, 0);
        }
    }

    if (cmd->is_append && cmd->out_redirect_file != NULL) {
        int out_redirect_fd = open(cmd -> in_redirect_file, O_CREAT | O_WRONLY | O_APPEND, 0664);
        if (out_redirect_fd < 0) {
            char err_msg[100];
            fprintf(err_msg, "Cannot open file '%s'", cmd->out_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(out_redirect_fd, 1);
        }
    }
    else if (!cmd->is_append && cmd->out_redirect_file != NULL) {
        int out_redirect_fd = open(cmd -> in_redirect_file, O_CREAT | O_WRONLY, 0664);
        if (out_redirect_fd < 0) {
            char err_msg[100];
            fprintf(err_msg, "Cannot open file '%s'", cmd->out_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(out_redirect_fd, 1);
        }
    }


}

void execute_pipe(CMD_OPTS single_cmd1, CMD_OPTS single_cmd2) {
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        err_exit("Error in `pipe`. Exiting...");
    }

}

void execute_cmd(char * cmd) {

}

int main() {

    const char * PATH = getenv("PATH");
    
    while (1) {
        printf(">> ");

        size_t max_cmd_len = MAX_CMD_LEN + 1;
        char * cmd = malloc(sizeof(char) * max_cmd_len);
        ssize_t cmd_len = getline(&cmd, &max_cmd_len, stdin);

        if (cmd_len == -1 || cmd_len == 0 || (cmd_len >= 1 && cmd[0] == '\n')) {
            continue;
        }

        // Spawn a new process group for the `cmd`
        
        char * tmp_cmd = strdup(cmd);
        execute_cmd(tmp_cmd);
        free(tmp_cmd);



        // Wait till `cmd` is completed

        free(cmd);
    }

    return EXIT_SUCCESS;
}
