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
    printf("%s\n", err_msg);
    exit(EXIT_FAILURE);
}


char* trim(char* token) {
    while(*token == '|' || *token == ' ') {
        token++;
    }
    return token;
}

const char * PATH;
bool sigint_rcvd = false;
LOOKUP_TABLE* sc_lookup_table;


void print_cmd_struct(CMD_OPTS_REDIRECT * cmd) {
    printf("\n*************\n");
    printf("Program : %s\n", cmd->program);
    printf("Num Opts : %lu\n", cmd->n_opts);
    for (size_t i = 0; i < cmd->n_opts; ++i) {
        printf("Opt : %s\n", cmd->opts[i]);
    }
    printf("In/Out fd : %d , %d\n", cmd->in_fd, cmd->out_fd);
    printf("In/Out redirect : %s , %s\n", cmd->in_redirect_file, cmd->out_redirect_file);
    printf("IsAppend : %d\n", cmd->is_append);
    printf("\n*************\n");
}

void free_cmd_struct(CMD_OPTS_REDIRECT ** cmd, size_t n_cmds) {
    for (size_t i = 0; i < n_cmds; ++i) {
        free(cmd[i]);
    }
    free(cmd);
}

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
        path_token = strtok(NULL, ":");
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
    // print_cmd_struct(cmd);
    
    printf("Command: %s\nProcess ID: %d\n", cmd->program, getpid());
    
    if (cmd->in_fd != 0)
        if (dup2(cmd->in_fd, 0) == -1)
            err_exit("Error in dup2. Exiting...\n");
    if (cmd->out_fd != 0)
        if (dup2(cmd->out_fd, 1) == -1)
            err_exit("Error in dup2. Exiting...\n");

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
            printf("Input file '%s' opened in fd %d. Fd %d is remapped to %d\n", cmd->in_redirect_file, in_redirect_fd, 0, in_redirect_fd);
        }
    }
    if (cmd->is_append && cmd->out_redirect_file != NULL) {
        int out_redirect_fd = open(cmd -> out_redirect_file, O_CREAT | O_WRONLY | O_APPEND, 0664);
        if (out_redirect_fd < 0) {
            char err_msg[100];
            sprintf(err_msg, "Cannot open file '%s'. Exiting...\n", cmd->out_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(out_redirect_fd, 1);
            printf("Append file '%s' opened in fd %d. Fd %d is remapped to %d\n", cmd->out_redirect_file, out_redirect_fd, 1, out_redirect_fd);
        }
    }
    else if (!cmd->is_append && cmd->out_redirect_file != NULL) {
        int out_redirect_fd = open(cmd -> out_redirect_file, O_CREAT | O_WRONLY | O_TRUNC, 0664);
        if (out_redirect_fd < 0) {
            char err_msg[100];
            sprintf(err_msg, "Cannot open file '%s'. Exiting...\n", cmd->out_redirect_file);
            err_exit(err_msg);
        }
        else {
            dup2(out_redirect_fd, 1);
            printf("Output file '%s' opened in fd %d. Fd %d is remapped to %d\n", cmd->out_redirect_file, out_redirect_fd, 1, out_redirect_fd);
        }
    }

    // 'cmd_path' is the path of directory slashed with program
    char * cmd_path = search_cmd_path(cmd->program);
    if (cmd_path != NULL) {
        if (cmd->out_fd == 1 && cmd->out_redirect_file == NULL)
            printf("\n************OUTPUT************\n");
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
        close(pipe_fd[0]);
        close(pipe_fd[1]);
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

        for (size_t i = 1; i <= n_cmds; ++i) {
            if (i < n_cmds && pipe(pipe_fd[i-1]) == -1) {
                err_exit("Error in pipe. Exiting...\n");
            }

            if(i < n_cmds) {
                cmds[i-1]->out_fd = pipe_fd[i-1][1];
                cmds[i]->in_fd = pipe_fd[i-1][0];
                printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", cmds[i-1]->program, cmds[i]->program, pipe_fd[i-1][0], pipe_fd[i-1][1]);
            }
            pid_t child_cmd_pid = fork();
            if(child_cmd_pid < 0) {
                err_exit("Error in forking. Exiting...\n");
            }
            if (child_cmd_pid == 0) {
                execute_single_cmd(cmds[i-1]);
                break;
            }
            else {
                int status;
                if(i == 1) {
                    close(pipe_fd[i-1][1]);
                }
                else if(i < n_cmds) {
                    close(pipe_fd[i-2][0]);
                    close(pipe_fd[i-1][1]);
                }
                else {
                    close(pipe_fd[i-2][0]);
                }
                waitpid(child_cmd_pid, &status, 0);
                if (cmds[i-1]->out_fd == 1 && cmds[i-1]->out_redirect_file == NULL)
                    printf("******************************\n");
                printf("\nStatus of PID %d: %d\n", child_cmd_pid, status);
                printf("______________________________\n\n");
            }
        }
        return;
    }

    //single command

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
        if(cmds[n_cmds-1]->in_fd != 0)
            close(cmds[n_cmds-1]->in_fd);
        if(cmds[n_cmds-1]->out_fd != 1)
            close(cmds[n_cmds-1]->out_fd);
        waitpid(child_cmd_pid, &status, 0);
        printf("******************************\n");
        printf("\nStatus of PID %d: %d\n\n", child_cmd_pid, status);
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
        printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", in_cmd->program, out1_cmd->program, pipe_fd[0][0], pipe_fd[0][1]);
        printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", in_cmd->program, out2_cmd->program, pipe_fd[1][0], pipe_fd[1][1]); 
        execute_single_cmd(in_cmd);
    }
    else {
        int status;
        close(pipe_fd[0][1]);
        waitpid(child_cmd_pid, &status, 0);
    }

    tee(pipe_fd[0][0], pipe_fd[1][1], INT_MAX, 0);

    close(pipe_fd[1][1]);
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
        printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", in_cmd->program, out1_cmd->program, pipe_fd[0][0], pipe_fd[0][1]);
        printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", in_cmd->program, out2_cmd->program, pipe_fd[1][0], pipe_fd[1][1]); 
        printf("Pipe between '%s' and '%s': Read end - %d and Write end - %d\n", in_cmd->program, out3_cmd->program, pipe_fd[2][0], pipe_fd[2][1]);
        execute_single_cmd(in_cmd);
    }
    else {
        int status;
        close(pipe_fd[0][1]);
        waitpid(child_cmd_pid, &status, 0);
    }

    tee(pipe_fd[0][0], pipe_fd[1][1], INT_MAX, 0);
    tee(pipe_fd[1][0], pipe_fd[2][1], INT_MAX, 0);
    close(pipe_fd[1][1]);
    close(pipe_fd[2][1]);
}

CMD_OPTS_REDIRECT * parse_single_cmd(const char * cmd) {
    // 'cmd' is in the format of <program> <opt> ... <opt>
    if (cmd == NULL)
        return NULL;

    CMD_OPTS_REDIRECT * single_cmd = malloc(sizeof(CMD_OPTS_REDIRECT));

    char * strtok_saveptr;

    char * tmp_cmd = strdup(cmd);
    char * tmp_cmd2 = strdup(cmd);
    char * token = strtok_r(tmp_cmd, " <>", &strtok_saveptr);
    if (token != NULL) {
        size_t n_opts; int i;

        char * strtok_redirect_in;
        char* redirect_in_token2 = NULL;
        char* redirect_in_token = strstr(tmp_cmd2, "<");
        if(redirect_in_token != NULL) {
            redirect_in_token++;
            redirect_in_token = trim(redirect_in_token);
            redirect_in_token2 = strtok_r(redirect_in_token, " |>", &strtok_redirect_in);
        }
        

        tmp_cmd2 = strdup(cmd);
        char * strtok_redirect_out;
        char* redirect_out_token2 = NULL;
        char* redirect_out_token = strstr(tmp_cmd2, ">");
        if(redirect_out_token != NULL) {
            redirect_out_token++;
            if(*redirect_out_token == '>') {
                single_cmd->is_append = 1;
                redirect_out_token++;
            }
            else {
                single_cmd ->is_append = 0;
            }
            redirect_out_token = trim(redirect_out_token);
            redirect_out_token2 = strtok_r(redirect_out_token, " |<", &strtok_redirect_out);
        }
        
        tmp_cmd2 = strdup(cmd);
        char* rem_token = strtok_r(tmp_cmd2, "<>", &strtok_saveptr);

        for(i = 0, n_opts = 1; rem_token[i] != '\0'; (rem_token[i] == ' ')? ++n_opts: 0, ++i);
        single_cmd->opts = malloc((n_opts + 1) * sizeof(char *));
        // +1 for the ending NULL

        int opt_idx = 0;

        char* rem_token2 = strtok_r(rem_token, " ", &strtok_saveptr);
        while (rem_token2 != NULL) {
            single_cmd->opts[opt_idx++] = rem_token2;
            rem_token2 = strtok_r(NULL, " ", &strtok_saveptr);
        }
        single_cmd->opts[opt_idx] = NULL;

        single_cmd->program = token;
        single_cmd->n_opts = n_opts;
        single_cmd->in_fd = 0;
        single_cmd->out_fd = 1;
        single_cmd->in_redirect_file = redirect_in_token2;
        single_cmd->out_redirect_file = redirect_out_token2;
        // Find and handle redirection
        // for (size_t ii = 0; ii < single_cmd->n_opts; ++ii) {
        //     if (strcmp(single_cmd->opts[ii], "<") == 0) {
        //         single_cmd->in_redirect_file = strdup(single_cmd->opts[ii+1]);
        //         for (size_t iii = ii+2; iii < single_cmd->n_opts; ++iii) {
        //             single_cmd->opts[iii-2] = single_cmd->opts[iii];
        //         }
        //         single_cmd->n_opts -= 2;
        //         --ii;
        //     }
        //     else if (strcmp(single_cmd->opts[ii], ">>") == 0) {
        //         single_cmd->out_redirect_file = strdup(single_cmd->opts[ii+1]);
        //         single_cmd->is_append = 1;
        //         for (size_t iii = ii+2; iii < single_cmd->n_opts; ++iii) {
        //             single_cmd->opts[iii-2] = single_cmd->opts[iii];
        //         }
        //         single_cmd->n_opts -= 2;
        //         --ii;
        //     }
        //     else if (strcmp(single_cmd->opts[ii], ">") == 0) {
        //         single_cmd->out_redirect_file = strdup(single_cmd->opts[ii+1]);
        //         for (size_t iii = ii+2; iii < single_cmd->n_opts; ++iii) {
        //             single_cmd->opts[iii-2] = single_cmd->opts[iii];
        //         }
        //         single_cmd->n_opts -= 2;
        //         --ii;
        //     }
        // }
        single_cmd->opts[single_cmd->n_opts] = NULL;
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

    char * strtok_saveptr;

    char * tmp_cmd = strdup(cmd);
    char * token = strtok_r(tmp_cmd, "|", &strtok_saveptr);
    if (token != NULL) {
        // trim trailing space
        size_t token_len = strlen(token);
        if (token[token_len-1] == ' ')
            token[token_len-1] = '\0';

        CMD_OPTS_REDIRECT * in_cmd = parse_single_cmd(token);

        token = strtok_r(NULL, "|", &strtok_saveptr);
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

    char * strtok_saveptr;

    char * tmp_cmd = strdup(cmd);

    size_t _n_pipe_cmds; int i;
    for(i = 0, _n_pipe_cmds = 1; tmp_cmd[i] != '\0'; (tmp_cmd[i] == '|')? ++_n_pipe_cmds: 0, i++);

    CMD_OPTS_REDIRECT ** pipe_cmds = malloc(_n_pipe_cmds * sizeof(CMD_OPTS_REDIRECT *));
    *n_pipe_cmds = _n_pipe_cmds;

    int cmd_idx = 0; size_t token_len;
    char * token = strtok_r(tmp_cmd, "|", &strtok_saveptr);

    while (token != NULL) {
        // trim leading space
        if (token[0] == ' ')
            token = token + 1;

        // trim trailing space
        token_len = strlen(token);
        if (token[token_len-1] == ' ')
            token[token_len-1] = '\0';

        pipe_cmds[cmd_idx++] = parse_single_cmd(token);
        token = strtok_r(NULL, "|", &strtok_saveptr);
    }
    return pipe_cmds;
}

void parse_cmd(char * cmd) {
    if (cmd == NULL) {
        return;
    }

    char * strtok_saveptr;

    size_t cmd_len = strlen(cmd);
    char * tmp_cmd = strdup(cmd);
    char * token = strstr(tmp_cmd, "||");

    // commands till '||'
    char * first_token = malloc(MAX_CMD_LEN * sizeof(char));
    if (token == NULL)
        first_token = tmp_cmd;
    else
        strncpy(first_token, tmp_cmd, token - tmp_cmd);

    size_t * n_pipe0_cmds = malloc(sizeof(size_t));
    CMD_OPTS_REDIRECT ** pipe0_cmds = parse_multiple_pipe_cmd(first_token, n_pipe0_cmds);

    if (token != NULL) {
        bool is_triple_pipe = false;

        // triple pipe: token -> token+2 inclusive
        if (token[2] == '|')
            is_triple_pipe = true;

        char * comma_token;

        token = trim(token);
        comma_token = strtok_r(token, ",", &strtok_saveptr);
        if (comma_token != NULL) {
            // commands upto first comma
            size_t * n_pipe1_cmds = malloc(sizeof(size_t));
            CMD_OPTS_REDIRECT ** pipe1_cmds = parse_multiple_pipe_cmd(comma_token, n_pipe1_cmds);

            token = trim(token);
            comma_token = strtok_r(NULL, ",", &strtok_saveptr);
            if (comma_token != NULL) {
                // commands upto second comma/end
                size_t * n_pipe2_cmds = malloc(sizeof(size_t));
                CMD_OPTS_REDIRECT ** pipe2_cmds = parse_multiple_pipe_cmd(comma_token, n_pipe2_cmds);

                if (is_triple_pipe) {
                    token = trim(token);
                    comma_token = strtok_r(NULL, ",", &strtok_saveptr);
                    if (comma_token != NULL) {
                        // remaining commands

                        size_t * n_pipe3_cmds = malloc(sizeof(size_t));
                        CMD_OPTS_REDIRECT ** pipe3_cmds = parse_multiple_pipe_cmd(comma_token, n_pipe3_cmds);

                        // triple pipe execute
                        if (*n_pipe0_cmds > 1) {
                            int pipe_fd[2];
                            if (pipe(pipe_fd) == -1) {
                                err_exit("Error in pipe. Exiting...\n");
                            }

                            pipe0_cmds[*n_pipe0_cmds-2]->out_fd = pipe_fd[1];
                            pipe0_cmds[*n_pipe0_cmds-1]->in_fd = pipe_fd[0];
                            execute_multiple_pipe_cmd(pipe0_cmds, *n_pipe0_cmds-1);
                        }
                        execute_triple_pipe_cmd(pipe0_cmds[*n_pipe0_cmds-1], pipe1_cmds[0], pipe2_cmds[0], pipe3_cmds[0]);
                        execute_multiple_pipe_cmd(pipe1_cmds, *n_pipe1_cmds);
                        execute_multiple_pipe_cmd(pipe2_cmds, *n_pipe2_cmds);
                        execute_multiple_pipe_cmd(pipe3_cmds, *n_pipe3_cmds);

                        free(pipe0_cmds);
                        free(pipe1_cmds);
                        free(pipe2_cmds);
                        free(pipe3_cmds);
                        free(n_pipe0_cmds);
                        free(n_pipe1_cmds);
                        free(n_pipe2_cmds);
                        free(n_pipe3_cmds);
                    }
                    else
                        err_exit("Invalid command. Exiting...\n");
                }
                else {
                    // double pipe execute
                    if (*n_pipe0_cmds > 1) {
                        int pipe_fd[2];
                        if (pipe(pipe_fd) == -1) {
                            err_exit("Error in pipe. Exiting...\n");
                        }

                        pipe0_cmds[*n_pipe0_cmds-2]->out_fd = pipe_fd[1];
                        pipe0_cmds[*n_pipe0_cmds-1]->in_fd = pipe_fd[0];
                        execute_multiple_pipe_cmd(pipe0_cmds, *n_pipe0_cmds-1);
                    }
                    execute_double_pipe_cmd(pipe0_cmds[*n_pipe0_cmds-1], pipe1_cmds[0], pipe2_cmds[0]);
                    execute_multiple_pipe_cmd(pipe1_cmds, *n_pipe1_cmds);
                    execute_multiple_pipe_cmd(pipe2_cmds, *n_pipe2_cmds);

                    free(pipe0_cmds);
                    free(pipe1_cmds);
                    free(pipe2_cmds);
                    free(n_pipe0_cmds);
                    free(n_pipe1_cmds);
                    free(n_pipe2_cmds);
                }

            }
            else
                err_exit("Invalid command. Exiting...\n");
        }
        else
            err_exit("Invalid command. Exiting...\n");
    }
    else {
        // no double and triple pipes
        execute_multiple_pipe_cmd(pipe0_cmds, *n_pipe0_cmds);
    }
}

void prompt() {
    char cwd[200];
    getcwd(cwd, 200);
    printf("\n(%s) >> ", cwd);
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

    if(next_cmd -> index == index) {
        sc_lookup_table -> head = sc_lookup_table -> head -> next;
        return;
    }

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

    while (true) {


        ssize_t cmd_len;
        char * cmd;
        bool is_bg_proc = false;

        if(sigint_rcvd) {
            int index;
            char * index_str = malloc(11 * sizeof(char));
            size_t index_str_len = 11;
            clearerr(stdin);
            index_str_len = getline(&index_str, &index_str_len, stdin);
            if (index_str[index_str_len-1] == '\n')
                index_str[index_str_len-1] = '\0';
            index = atoi(index_str);
            free(index_str);
            cmd = search_cmd(index);
            if(cmd == NULL) {
                printf("Error : No such command in lookup table with the given index. Exiting...\n");
                sigint_rcvd = false;
                continue;
            }
            cmd = strdup(cmd);
            cmd_len = strlen(cmd);
            sigint_rcvd = false;
        }
        else {
            prompt();
            size_t max_cmd_len = MAX_CMD_LEN + 1;
            cmd = malloc(sizeof(char) * max_cmd_len);
            clearerr(stdin);
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

        if(strcmp(cmd, "please exit") == 0) 
            _exit(EXIT_SUCCESS);

        cmd_len = strlen(cmd);
        // Spawn a new process group for the `cmd`

        char * tmp_cmd = strdup(cmd);

        char *tmp_cmd_sc = strdup(cmd);
        bool sc_error = false, sc_cmd = false;
        char *token = strtok(tmp_cmd_sc, " ");
        if (strcmp(token, "sc") == 0) {
            sc_cmd = true;
            token = strtok(NULL, " ");
            if(token == NULL) {
                sc_error = true;
            }
            else if(strcmp(token, "-i") == 0) {
                token = strtok(NULL, " ");
                if(token == NULL) {
                    sc_error = true;
                }
                int index = atoi(token);
                token = token + strlen(token) + 1;
                token = trim(token);
                if(*token == '\0') {
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
                token = token + strlen(token) + 1;
                token = trim(token);
                if(*token == '\0') {
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

        if(sc_cmd)
            continue;

        int p_sync[2];
        pipe(p_sync);

        pid_t child_exec = fork();

        if(child_exec < 0) {
            err_exit("Error in creating a child process. Exiting...\n");
        }
        else if (child_exec == 0) {
            // signal(SIGINT, SIG_DFL);
            close(p_sync[1]);
            char buff_sync[3];
            int n = read(p_sync[0], buff_sync, 2);
                //setpgid(0, child_executer);

            int curr_pid = getpid();
            printf("Process details:\n");
            printf("\tProcess Id: %d\n", curr_pid);
            printf("\tProcess Group Id: %d\n", getpgid(curr_pid));
            printf("\tForeground Process Group Id: %d\n", tcgetpgrp(STDIN_FILENO));
            printf("\n");


            parse_cmd(tmp_cmd);
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
                waitpid(child_exec, &status, WUNTRACED);
            tcsetpgrp(0, getpid());
            signal(SIGTTOU, SIG_DFL);
        }

        free(tmp_cmd);

        free(cmd);
    }

    return EXIT_SUCCESS;
}
