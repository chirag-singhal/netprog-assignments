#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_CMD_LEN 1024


const char * PATH;

typedef struct _CMD_OPTS_REDIRECT {
    // program is first word in opts
    // last word in opts is NULL
    // By default, in_fd=0, out_fd=1 unless modified by piping
    char * program;
    char ** opts;
    size_t n_opts;
    int in_fd;
    int out_fd;
    char * in_redirect_file;
    char * out_redirect_file;
    int is_append;
} CMD_OPTS_REDIRECT;

////////////////////////////////////////

void err_exit(const char *err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

/*
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
*/

char * search_cmd_path(const char * program) {
    char * path_dup = strdup(PATH);
    char * path_token = strtok(path_dup, ":");
    char * tmp_cmd_path = malloc(sizeof(char) * (MAX_CMD_LEN + 1));
    while (path_token != NULL) {
        strcpy(tmp_cmd_path, path_token);
        strcat(tmp_cmd_path, "/");
        strcat(tmp_cmd_path, program);
        if (access(tmp_cmd_path, F_OK) == 0) {
            break;
        }
    }
    free(path_dup);
    if (path_token != NULL) {
        return tmp_cmd_path;
    }
    else {
        return NULL;
    }
}

void execute_single_cmd(CMD_OPTS_REDIRECT * cmd) {
    // Assume the fork for this single cmd happened before this function was called
    
    if (cmd->in_fd != 0)
        dup2(cmd->in_fd, 0);
    if (cmd->out_fd != 0)
        dup2(cmd->out_fd, 1);

    // Redirection is given higher priority.
    // `cmd1 | cmd2 < file` In this case, pipe from cmd1 to cmd2 is broken
    // cmd2 read fd is given the fd of `file`
    if (cmd->in_redirect_file != NULL) {
        int in_redirect_fd = open(cmd->in_redirect_file, O_CREAT | O_RDONLY, 0664);
        if (in_redirect_fd < 0) {
            char err_msg[100];
            sprintf(err_msg, "Cannot open file '%s'. Exiting...\n", cmd->in_redirect_file);
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
            sprintf(err_msg, "Cannot open file '%s'. Exiting...\n", cmd->out_redirect_file);
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
            sprintf(err_msg, "Cannot open file '%s'. Exiting...\n", cmd->out_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(out_redirect_fd, 1);
        }
    }

    // 'cmd_path' is the path of directory slashed with program
    char * cmd_path = search_cmd_path(cmd->program);
    if (cmd_path != NULL) {
        execv(cmd_path, cmd->opts);
    }
    else {
        perror("Command not found in PATH. Exiting...\n");
    }

}

void execute_pipe_cmd(CMD_OPTS_REDIRECT ** cmds, size_t n_cmds) {
    // cmds[0] and cmds[n_cmds-1] already have appropriate in_fd and out_fd
    // set by calling function. By default, it is stdin/stdout.
    // If `||` or `|||` is used, then the out_fd of the last run process
    // is set as in_fd for cmds[0]. Similarly, for cmds[n_cmds-1].
    if (n_cmds < 1) {
        err_exit("Invalid command. Exiting...\n");
    }

    if (n_cmds > 1) {
        int pipe_fd[n_cmds-1][2];
        
        for (size_t i = 1; i < n_cmds; ++i) {
            if (pipe(pipe_fd[i-1]) == -1) {
                err_exit("Error in pipe. Exiting...\n");
            }
            
            cmds[i-1]->out_fd = pipe_fd[i-1][1];
            cmds[i]->in_fd = pipe_fd[i-1][0];

            pid_t child_cmd_pid = fork();
            if(child_cmd_pid < 0) {
                err_exit("Error in forking. Exiting...\n");
            }
            if (child_cmd_pid == 0) {
                // create new process for the single command
                execute_single_cmd(cmds[i-1]);
            }
            else {
                int status;
                close(pipe_fd[i-1][0]);
                close(pipe_fd[i-1][1]);
                waitpid(child_cmd_pid, &status, 0);
            }
        }
    }

    pid_t child_cmd_pid = fork();
    if(child_cmd_pid < 0) {
        err_exit("Error in forking. Exiting...\n");
    }
    if (child_cmd_pid == 0) {
        // create new process for the single command
        execute_single_cmd(cmds[n_cmds-1]);
    }
    else {
        int status;
        waitpid(child_cmd_pid, &status, 0);
    }
}

void execute_double_pipe_cmd(CMD_OPTS_REDIRECT ** in_cmd, size_t n_in_cmd,
    CMD_OPTS_REDIRECT ** out1_cmd, ssize_t n_out1_cmd,
    CMD_OPTS_REDIRECT ** out2_cmd, ssize_t n_out2_cmd) {
    int pipe_fd[2][2];
    if (pipe(pipe_fd[0]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }
    if (pipe(pipe_fd[1]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }
    in_cmd[n_in_cmd-1]->out_fd = pipe_fd[0][1];
    execute_pipe_cmd(in_cmd, n_in_cmd);
    ssize_t n_bytes = tee();
}

void execute(char * cmd) {
    CMD_OPTS_REDIRECT * single_cmd = malloc(sizeof(CMD_OPTS_REDIRECT));
    single_cmd->program = cmd;
    single_cmd->opts = malloc(2*sizeof(char *));
    single_cmd->opts[0] = cmd;
    single_cmd->opts[1] = NULL;
    single_cmd->n_opts = 1;
    single_cmd->in_fd = 0;
    single_cmd->out_fd = 1;
    single_cmd->in_redirect_file = NULL;
    single_cmd->out_redirect_file = NULL;
    single_cmd->is_append = 0;
    execute_pipe_cmd(&single_cmd, 1);
}

void prompt() {
    printf(">> ");
}

int main() {

    const char * PATH = getenv("PATH");
    
    while (1) {
        prompt();

        size_t max_cmd_len = MAX_CMD_LEN + 1;
        char * cmd = malloc(sizeof(char) * max_cmd_len);
        ssize_t cmd_len = getline(&cmd, &max_cmd_len, stdin);

        if (cmd_len == -1 || cmd_len == 0 || (cmd_len >= 1 && cmd[0] == '\n')) {
            continue;
        }

        bool is_bg_proc = false;

        cmd[cmd_len - 1] = '\0';

        if(cmd[cmd_len - 2] == '&') {
            is_bg_proc = true;
            cmd[cmd_len - 2] = '\0';
        } 

        cmd_len = strlen(cmd);

        // Spawn a new process group for the `cmd`
        
        char * tmp_cmd = strdup(cmd);

        int p_sync[2];
		pipe(p_sync);

		pid_t child_exec = fork();

		if(child_exec < 0) {
            
		}
		else if (child_exec == 0) {
			close(p_sync[1]);
			char buff_sync[3];
			int n = read(p_sync[0], buff_sync, 2);
            	//setpgid(0, child_executer);

			int curr_pid = getpid();
			printf("Coordinating Process details:\n");
			printf("\tProcess Id: %d\n", curr_pid);
			printf("\tProcess Group Id: %d\n", getpgid(curr_pid));
			printf("\n");

			execute(tmp_cmd);
		}
        else {
            close(p_sync[0]);
			if(setpgid(child_exec, child_exec) == -1) {
                err_exit("Error in creating new process group. Exiting...\n");
			}

			signal(SIGTTOU, SIG_IGN);
			if(tcsetpgrp(STDIN_FILENO, child_exec) == -1) {
                err_exit("Error in setting foreground process. Exiting...\n");
			}

			write(p_sync[1], "##", 2);

            int status;

            if(!is_bg_proc)
                waitpid(child_exec, &status, WUNTRACED);
        }

        free(tmp_cmd);

        // Wait till `cmd` is completed

        free(cmd);
    }

    return EXIT_SUCCESS;
}
