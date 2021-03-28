#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_CMD_LEN 1024


typedef struct _CMD_OPTS_REDIRECT {
    // program is first word in opts
    // last word in opts is NULL
    // n_opts is the length of opts excluding the last NULL
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


typedef struct _SHORT_CUT_COMMAND {
    int index;
    char *cmd;
    struct _SHORT_CUT_COMMAND *next;
} SHORT_CUT_COMMAND;

typedef struct _LOOKUP_TABLE {
    SHORT_CUT_COMMAND *head;
} LOOKUP_TABLE;

////////////////////////////////////////

void err_exit(const char *err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}


const char * PATH;
bool sigint_rcvd = false;
LOOKUP_TABLE* sc_lookup_table;

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

void execute_pipe_cmd(CMD_OPTS_REDIRECT * in_cmd, CMD_OPTS_REDIRECT * out_cmd) {
    // 'in_cmd.out_fd' are 'out_cmd.in_fd' are set in this function
    int pipe_fd[2];
    
    in_cmd->out_fd = pipe_fd[1];
    out_cmd->in_fd = pipe_fd[0];

    pid_t child_cmd_pid = fork();
    if(child_cmd_pid < 0) {
        err_exit("Error in forking. Exiting...\n");
    }
    if (child_cmd_pid == 0) {
        // create new process for the single command
        execute_single_cmd(in_cmd);
    }
    else {
        int status;
        waitpid(child_cmd_pid, &status, 0);
    }
}

void execute_multiple_pipe_cmd(CMD_OPTS_REDIRECT ** cmds, size_t n_cmds) {
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

void execute_double_pipe_cmd(CMD_OPTS_REDIRECT * in_cmd,
    CMD_OPTS_REDIRECT * out1_cmd, CMD_OPTS_REDIRECT * out2_cmd) {
    // 'in_cmd.out_fd' are set in this function
    // 'out1_cmd.in_fd' and 'out2_cmd.in_fd' are set in this function
    int pipe_fd[2][2];
    if (pipe(pipe_fd[0]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }
    if (pipe(pipe_fd[1]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }

    in_cmd->out_fd = pipe_fd[0][1];
    out1_cmd->in_fd = pipe_fd[0][0];
    out2_cmd->in_fd = pipe_fd[1][0];
    
    pid_t child_cmd_pid = fork();
    if(child_cmd_pid < 0) {
        err_exit("Error in forking. Exiting...\n");
    }
    if (child_cmd_pid == 0) {
        // create new process for the single command
        execute_single_cmd(in_cmd);
    }
    else {
        int status;
        waitpid(child_cmd_pid, &status, 0);
    }
    
    tee(pipe_fd[0][0], pipe_fd[1][1], INT_MAX, 0);
}

void execute_triple_pipe_cmd(CMD_OPTS_REDIRECT * in_cmd,
    CMD_OPTS_REDIRECT * out1_cmd, CMD_OPTS_REDIRECT * out2_cmd, CMD_OPTS_REDIRECT * out3_cmd) {
    // 'in_cmd.out_fd' are set in this function
    // 'out1_cmd.in_fd', 'out2_cmd.in_fd' and 'out3_cmd.in_fd are set in this function
    int pipe_fd[3][2];
    if (pipe(pipe_fd[0]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }
    if (pipe(pipe_fd[1]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }
    if (pipe(pipe_fd[2]) == -1) {
        err_exit("Error in pipe. Exiting...\n");
    }

    in_cmd->out_fd = pipe_fd[0][1];
    out1_cmd->in_fd = pipe_fd[0][0];
    out2_cmd->in_fd = pipe_fd[1][0];
    out3_cmd->in_fd = pipe_fd[2][0];
    
    pid_t child_cmd_pid = fork();
    if(child_cmd_pid < 0) {
        err_exit("Error in forking. Exiting...\n");
    }
    if (child_cmd_pid == 0) {
        // create new process for the single command
        execute_single_cmd(in_cmd);
    }
    else {
        int status;
        waitpid(child_cmd_pid, &status, 0);
    }
    
    tee(pipe_fd[0][0], pipe_fd[1][1], INT_MAX, 0);
    tee(pipe_fd[1][0], pipe_fd[2][1], INT_MAX, 0);
}

CMD_OPTS_REDIRECT * parse_single_cmd(const char * cmd) {
    // 'cmd' is in the format of <program> <opt> ... <opt>
    if (cmd == NULL)
        return NULL;
    
    CMD_OPTS_REDIRECT * single_cmd = malloc(sizeof(CMD_OPTS_REDIRECT));
    
    char * tmp_cmd = strdup(cmd);
    char * token = strtok(tmp_cmd, " ");
    if (token != NULL) {
        size_t n_opts;
        for(int i = 0, n_opts = 1; tmp_cmd[i] != '\0'; (tmp_cmd[i] == ' ')? n_opts++: 0, i++);
        
        single_cmd->opts = malloc((n_opts + 1) * sizeof(char *));
        // +1 for the ending NULL
        
        int opt_idx = 0;
        single_cmd->program = token;
        single_cmd->opts[opt_idx++] = token;
        
        token = strtok(NULL, " ");
        while (token != NULL) {
            single_cmd->opts[opt_idx++] = token;
            token = strtok(NULL, " ");
        }
        single_cmd->opts[opt_idx] = NULL;

        single_cmd->n_opts = n_opts;
        single_cmd->in_fd = 0;
        single_cmd->out_fd = 1;
        single_cmd->in_redirect_file = NULL;
        single_cmd->out_redirect_file = NULL;
        single_cmd->is_append = 0;

        return single_cmd;
    }
    return NULL;
}

CMD_OPTS_REDIRECT * parse_pipe_cmd(char * cmd, char ** new_cmd) {
    // 'cmd' is in the format of <single_cmd> | <multiple_cmd>
    if (cmd == NULL) {
        *new_cmd = NULL;
        return NULL;
    }

    char * tmp_cmd = strdup(cmd);
    char * token = strtok(tmp_cmd, "|");
    if (token != NULL) {
        // trim trailing space
        size_t token_len = strlen(token);
        if (token[token_len-1] == ' ')
            token[token_len-1] = '\0';

        CMD_OPTS_REDIRECT * in_cmd = parse_single_cmd(token);
        
        token = strtok(NULL, "|");
        if (token != NULL) {
            // trim leading space
            if (token[0] == ' ')
                token = token + 1;
            
            // trim trailing space
            token_len = strlen(token);
            if (token[token_len-1] == ' ')
                token[token_len-1] = '\0';

            *new_cmd = token;
        }

        return in_cmd;
    }
    return NULL;
}

CMD_OPTS_REDIRECT ** parse_multiple_pipe_cmd(char * cmd, size_t * n_pipe_cmds) {
    // 'cmd' is in the format of <single_cmd> | ... | <single_cmd>
    if (cmd == NULL) {
        *n_pipe_cmds = 0;
        return NULL;
    }

    char * tmp_cmd = strdup(cmd);
    
    size_t _n_pipe_cmds;
    for(int i = 0, _n_pipe_cmds = 1; tmp_cmd[i] != '\0'; (tmp_cmd[i] == '|')? _n_pipe_cmds++: 0, i++);

    CMD_OPTS_REDIRECT ** pipe_cmds = malloc(_n_pipe_cmds * sizeof(CMD_OPTS_REDIRECT *));
    *n_pipe_cmds = _n_pipe_cmds;

    int cmd_idx = 0; size_t token_len;
    char * token = strtok(tmp_cmd, "|");
    while (token != NULL) {
        // trim leading space
        if (token[0] == ' ')
            token = token + 1;
        
        // trim trailing space
        token_len = strlen(token);
        if (token[token_len-1] == ' ')
            token[token_len-1] = '\0';
        
        pipe_cmds[cmd_idx++] = parse_single_cmd(token);
        token =  strtok(NULL, "|");
    }

    return pipe_cmds;
}

void parse_double_pipe_cmd(char * cmd) {
    // 'cmd' is in the format of <multiple_cmds> || <multiple_cmds> , <multiple_cmds>
    if (cmd == NULL) {
        return;
    }
    
    char * tmp_cmd = strdup(cmd);
    char * token = strtok(tmp_cmd, "|,");
    while (token != NULL) {
        
    }
}

void parse_triple_pipe_cmd(char * cmd) {
    // cmd1 || cmd2 || cmd3 , cmd4, cmd5, cmd6
    // cmd1 -> cmd2, cmd5, cmd6
    // cmd2 -> cmd3, cmd4
    // c1 || c2 | c3, c4
    return;
}

void execute(char * cmd) {
    CMD_OPTS_REDIRECT * single_cmd = malloc(sizeof(CMD_OPTS_REDIRECT));
    single_cmd->program = cmd;
    single_cmd->opts = malloc(3*sizeof(char *));
    single_cmd->opts[0] = cmd;
    single_cmd -> opts[1] = malloc(sizeof(char) * 3);
    single_cmd -> opts[1] = "3d";
    single_cmd->opts[2] = NULL;
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

void sigint_handler(int sig) {
    sigint_rcvd = true;
}

void insert_cmd(int index, char* cmd) {
    if(sc_lookup_table -> head == NULL) {
        sc_lookup_table -> head = malloc(sizeof(SHORT_CUT_COMMAND));
        sc_lookup_table -> head -> index = index;
        sc_lookup_table -> head -> cmd = malloc(sizeof(char) * (strlen(cmd) + 1));
        strcpy(sc_lookup_table -> head -> cmd, cmd);
        sc_lookup_table -> head -> next = NULL;
        return;
    }
    SHORT_CUT_COMMAND* next_cmd = sc_lookup_table -> head;
    while(next_cmd != NULL) {
        if(next_cmd -> index == index) {
            free(next_cmd -> cmd);
            next_cmd -> cmd = malloc(sizeof(char) * (strlen(cmd) + 1));
            strcpy(next_cmd -> cmd, cmd);
            return;
        }
        next_cmd = next_cmd -> next;
    }
    next_cmd = sc_lookup_table -> head;
    sc_lookup_table -> head = malloc(sizeof(SHORT_CUT_COMMAND));
    sc_lookup_table -> head -> index = index;
    sc_lookup_table -> head -> cmd = malloc(sizeof(char) * (strlen(cmd) + 1));
    strcpy(sc_lookup_table -> head -> cmd, cmd);
    sc_lookup_table -> head -> next = next_cmd;
    return;
}

void delete_cmd(int index, char* cmd) {
    if(sc_lookup_table -> head == NULL) {
        err_exit("Error: No entry to delete in lookup table. Exiting...");
        return;
    }
    SHORT_CUT_COMMAND* next_cmd = sc_lookup_table -> head;
    SHORT_CUT_COMMAND* prev_cmd = NULL;
    while(next_cmd != NULL) {
        if(next_cmd -> index == index) {
           prev_cmd -> next = next_cmd -> next;
           free(next_cmd -> cmd);
           free(next_cmd);
           return;
        }
        prev_cmd = next_cmd;
        next_cmd = next_cmd -> next;
    }
    err_exit("Error: Cannot find matching entry to delete in lookup table. Exiting...");
    return;
}

char * search_cmd(int index) {
    SHORT_CUT_COMMAND* next_cmd = sc_lookup_table -> head;
    while(next_cmd != NULL) {
        if(next_cmd -> index == index) {
            return next_cmd -> cmd;
        }
        next_cmd = next_cmd -> next;
    }
    return NULL;
}

int main() {

    PATH = getenv("PATH");
    sc_lookup_table = malloc(sizeof(LOOKUP_TABLE));
    sc_lookup_table -> head = NULL;

    struct sigaction sigint;
    sigint.sa_handler = sigint_handler;
    sigint.sa_flags = 0;
    sigemptyset(&sigint.sa_mask);


    if (sigaction(SIGINT, &sigint, NULL) == -1)
        err_exit("nError in sigaction SIGUSR1!\n");
    
    while (1) {
        
        prompt();

        ssize_t cmd_len;
        char * cmd;
        bool is_bg_proc = false;

        if(sigint_rcvd) {
            int index;
            scanf("%d", &index);
            cmd = search_cmd(index);
            cmd_len = strlen(cmd);
            if(cmd == NULL) {
                err_exit("Error : No such command in lookup table with the given index. Exiting...\n");
            }
            sigint_rcvd = false;
        }

        else {
            size_t max_cmd_len = MAX_CMD_LEN + 1;
            cmd = malloc(sizeof(char) * max_cmd_len);
            cmd_len = getline(&cmd, &max_cmd_len, stdin);

            if (cmd_len == -1 || cmd_len == 0 || (cmd_len >= 1 && cmd[0] == '\n')) {
                continue;
            }

            cmd[cmd_len - 1] = '\0';
            cmd_len = strlen(cmd);
        }

        if(cmd[cmd_len - 1] == '&') {
            is_bg_proc = true;
            if (cmd[cmd_len - 2] == ' ')
                cmd[cmd_len - 2] = '\0';
            else
                cmd[cmd_len - 1] = '\0';
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
			printf("Process details:\n");
			printf("\tProcess Id: %d\n", curr_pid);
			printf("\tProcess Group Id: %d %d\n", getpgid(curr_pid), tcgetpgrp(STDIN_FILENO));
			printf("\n");

            char *tmp_cmd_sc = strdup(cmd);
            bool sc_error = false;
            char *token = strtok(tmp_cmd_sc, " ");
            if (strcmp(token, "sc") == 0) {
                token = strtok(NULL, " ");
                if(token == NULL) {
                    sc_error = true;
                }
                if(strcmp(token, "-i") == 0) {
                    token = strtok(NULL, " ");
                    if(token == NULL) {
                        sc_error = true;
                    }
                    int index = atoi(token);
                    token = strtok(NULL, " ");
                    if(token == NULL) {
                        sc_error = true;
                    }
                    insert_cmd(index, token);
                }
                else if(strcmp(token, "-d") == 0) {
                    token = strtok(NULL, " ");
                    if(token == NULL) {
                        sc_error = true;
                    }
                    int index = atoi(token);
                    token = strtok(NULL, " ");
                    if(token == NULL) {
                        sc_error = true;
                    }
                    delete_cmd(index, token);
                }
                else {
                     sc_error = true;
                }
                if(sc_error) {
                    err_exit("Error Correct format for shortcut command is sc -i <index> <cmd> or sc -d <index> <cmd>. Exiting...\n");
                }
            } 
            free(tmp_cmd_sc);
			execute(tmp_cmd);
		}
        else {
            close(p_sync[0]);
			if(setpgid(child_exec, child_exec) == -1) {
                err_exit("Error in creating new process group. Exiting...\n");
			}

			signal(SIGTTOU, SIG_IGN);
			if(!is_bg_proc && tcsetpgrp(STDIN_FILENO, child_exec) == -1) {
                err_exit("Error in setting foreground process. Exiting...\n");
			}

			write(p_sync[1], "##", 2);

            int status;

            if(!is_bg_proc)
                waitpid(child_exec, &status, WNOHANG);
            tcsetpgrp(0, getpid());
			signal(SIGTTOU, SIG_DFL);
        }

        free(tmp_cmd);

        free(cmd);
    }

    return EXIT_SUCCESS;
}
