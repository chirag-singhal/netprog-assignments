#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_NUM_CLI 64
#define MAX_CMD_LEN 1024
#define MAX_BUF_SIZE 4096
#define CLIENT_PORT 5100
#define SERVER_PORT 5200
#define CONFIG_FILE "clustershell.cfg"


typedef struct _CONFIG_ENTRY {
    char * name;
    char * ip;
} CONFIG_ENTRY;

typedef struct _CMD_STRUCT {
    int node;
    char * cmd;
} CMD_STRUCT;


void print_cmd_struct(CMD_STRUCT * cmd) {
    printf("*************\n");
    printf("Node : %d\n", cmd->node);
    printf("Cmd : %s\n", cmd->cmd);
    printf("*************\n");
}

void err_exit(const char * err_msg, int sock_fd) {
    perror(err_msg);
    if (sock_fd > 0) {
        close(sock_fd);
        printf("Closed socket %d...\n", sock_fd);
    }
    exit(EXIT_FAILURE);
}

CONFIG_ENTRY ** read_config(const char * filename) {
    CONFIG_ENTRY ** config = malloc((MAX_NUM_CLI + 1) * sizeof(CONFIG_ENTRY *));
    FILE * config_fp = fopen(filename, "r");
    if (config_fp == NULL)
        err_exit("Error opening config. Exiting...\n", -1);

    char name[12], ip[20];
    size_t i;
    while(fscanf(config_fp, " %s", name) != EOF) {
        fscanf(config_fp, " %s", ip);
        config[i] = malloc(sizeof(CONFIG_ENTRY));
        config[i]->name = strdup(name);
        config[i]->ip = strdup(ip);
        ++i;
    }
    config[i] = NULL;

    return config;
}

void free_config(CONFIG_ENTRY ** config) {
    size_t i = 0;
    while (config[i] != NULL) {
        free(config[i]->name);
        free(config[i]->ip);
        free(config[i]);
        ++i;
    }
    free(config);
}

int server_init(int port) {
    struct sockaddr_in serv_addr = {0};

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0)
        err_exit("Error in socket. Exiting...\n", -1);

    int sockopt_optval = 1;
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &sockopt_optval, sizeof(sockopt_optval)) < 0)
        err_exit("Error in setsockopt. Exiting...\n", serv_sock);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(serv_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        err_exit("Error in bind. Exiting...\n", serv_sock);

    if (listen(serv_sock, 5) < 0)
        err_exit("Error in listen. Exiting...\n", serv_sock);

    return serv_sock;
}

int client_init(char * ip, int port) {
    struct sockaddr_in serv_addr = {0};

    serv_addr.sin_family = AF_INET;
    if (ip == NULL)
        serv_addr.sin_addr.s_addr = INADDR_ANY;
    else
        inet_aton(ip, &(serv_addr.sin_addr));
    serv_addr.sin_port = htons(port);

    int client_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (connect(client_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        err_exit("Error in connect. Exiting...\n", client_sock);

    return client_sock;
}

CMD_STRUCT * parse_single_cmd(const char * cmd) {
    char * tmp_cmd = strdup(cmd);

    CMD_STRUCT * single_cmd = malloc(sizeof(CMD_STRUCT));

    char * dot_token = strstr(tmp_cmd, ".");
    if (dot_token != NULL) {
        char * space_token = strdup(cmd);
        strncpy(space_token, tmp_cmd, (dot_token-tmp_cmd));
        space_token[dot_token-tmp_cmd] = '\0';

        // trim leading spaces
        while (space_token[0] == ' ')
            space_token = space_token + 1;

        // trim trailing spaces
        size_t token_len = strlen(space_token);
        while (token_len-1 >= 0 && space_token[token_len-1] == ' ')
            space_token[--token_len] = '\0';

        if (strstr(space_token, " ") != NULL) {
            // there is a space between start and the first '.'
            // this means the '.' belongs to a file name and not a node identifier
            single_cmd->node = -1; //self

            // trim leading spaces
            while (tmp_cmd[0] == ' ')
                tmp_cmd = tmp_cmd + 1;

            // trim trailing spaces
            token_len = strlen(tmp_cmd);
            while (token_len-1 >= 0 && tmp_cmd[token_len-1] == ' ')
                tmp_cmd[--token_len] = '\0';
            
            single_cmd->cmd = strdup(tmp_cmd);
        }
        else {
            // it is a node identifier
            if (*(space_token+1) == '*')
                single_cmd->node = 0; //all
            else
                single_cmd->node = atoi(space_token+1);
            
            ++dot_token;
            
            // trim leading spaces
            while (dot_token[0] == ' ')
                dot_token = dot_token + 1;

            // trim trailing spaces
            token_len = strlen(dot_token);
            while (token_len-1 >= 0 && dot_token[token_len-1] == ' ')
                dot_token[--token_len] = '\0';

            single_cmd->cmd = strdup(dot_token);
        }
        free(space_token);
    }
    else {
        single_cmd->node = -1; //self

        // trim leading spaces
        while (tmp_cmd[0] == ' ')
            tmp_cmd = tmp_cmd + 1;

        // trim trailing spaces
        size_t token_len = strlen(tmp_cmd);
        while (token_len-1 >= 0 && tmp_cmd[token_len-1] == ' ')
            tmp_cmd[--token_len] = '\0';
        
        single_cmd->cmd = strdup(tmp_cmd);
    }

    free(tmp_cmd);

    return single_cmd;
}

CMD_STRUCT ** parse_multiple_pipe_cmd(char * cmd, size_t * n_pipe_cmds) {
    char * tmp_cmd = strdup(cmd);

    size_t _n_pipe_cmds; int i;
    for(i = 0, _n_pipe_cmds = 1; tmp_cmd[i] != '\0'; (tmp_cmd[i] == '|')? ++_n_pipe_cmds: 0, ++i);

    CMD_STRUCT ** pipe_cmds = malloc(_n_pipe_cmds * sizeof(CMD_STRUCT *));
    *n_pipe_cmds = _n_pipe_cmds;

    int cmd_idx = 0; size_t token_len;
    char * strtok_saveptr;
    char * token = strtok_r(tmp_cmd, "|", &strtok_saveptr);

    while (token != NULL) {
        // trim leading spaces
        while (token[0] == ' ')
            token = token + 1;

        // trim trailing spaces
        token_len = strlen(token);
        while (token_len-1 >= 0 && token[token_len-1] == ' ')
            token[--token_len] = '\0';

        pipe_cmds[cmd_idx++] = parse_single_cmd(token);
        token = strtok_r(NULL, "|", &strtok_saveptr);
    }
    free(tmp_cmd);

    return pipe_cmds;
}

int main() {
    int serv_sock = server_init(SERVER_PORT);

    struct sockaddr_in client_addr;
    int client_sock, client_len = client_len = sizeof(client_addr);

    CONFIG_ENTRY ** config = read_config(CONFIG_FILE);

    printf("Server started at port '%d'\n", SERVER_PORT);

    while (true) {
        client_sock = accept(serv_sock, (struct sockaddr *) &client_addr, &client_len);
        if (client_sock < 0)
            err_exit("Error in accept. Exiting...\n", client_sock);

        printf("Client Connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pid_t client_handler = fork();
        if (client_handler < 0)
            err_exit("Error in fork. Exiting...\n", -1);
        else if (client_handler == 0) {
            // close serv_sock to stop accepting connections
            close(serv_sock);

            while (true) {
                // Handle commands
                char cmd[MAX_CMD_LEN+1];

                int cmd_len = read(client_sock, cmd, MAX_CMD_LEN);
                if (cmd_len < 0)
                    err_exit("Error in read. Exiting...\n", -1);
                cmd[cmd_len] = '\0';

                printf("'%s:%d' sent '%s'\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), cmd);

                if (strcmp(cmd, "nodes") == 0) {
                    FILE * config_fp = fopen(CONFIG_FILE, "r");
                    fseek(config_fp, 0L, SEEK_END);
                    size_t config_size = ftell(config_fp);
                    char * raw_config_txt = malloc((config_size+1) * sizeof(char));
                    rewind(config_fp);

                    config_size = fread(raw_config_txt, sizeof(char), config_size, config_fp);
                    raw_config_txt[config_size] = '\0';
                    ++config_size;

                    write(client_sock, raw_config_txt, config_size);

                    free(raw_config_txt);
                    continue;
                }

                char prev_input[MAX_BUF_SIZE+1];
                memset(prev_input, 0, MAX_BUF_SIZE+1);
                size_t prev_input_len = 0;

                size_t n_cmds;
                CMD_STRUCT ** cmds = parse_multiple_pipe_cmd(cmd, &n_cmds);

                for(size_t cmd_idx = 0; cmd_idx < n_cmds; ++cmd_idx) {
                    // for each command

                    if (cmds[cmd_idx]->node == 0) {
                        // send to all
                        char response_all[MAX_BUF_SIZE];
                        memset(response_all, 0, MAX_BUF_SIZE+1);

                        CONFIG_ENTRY ** tmp_config = config;
                        while(*tmp_config != NULL) {
                            int client_sock2 = client_init((*tmp_config)->ip, CLIENT_PORT);

                            char cmd_prev_response[MAX_CMD_LEN+1 + MAX_BUF_SIZE+1];
                            memset(cmd_prev_response, 0, MAX_CMD_LEN+1 + MAX_BUF_SIZE+1);
                            strcpy(cmd_prev_response, cmds[cmd_idx]->cmd);
                            strcpy(cmd_prev_response+strlen(cmds[cmd_idx]->cmd)+1, prev_input);
                            size_t cmd_prev_response_len = strlen(cmds[cmd_idx]->cmd)+1 + strlen(prev_input)+1;

                            int nbytes = write(client_sock2, cmd_prev_response, cmd_prev_response_len);
                            if (nbytes != cmd_prev_response_len)
                                err_exit("Error in writing. Exiting...\n", client_sock2);

                            char tmp_response[MAX_BUF_SIZE+1];
                            nbytes = read(client_sock2, tmp_response, MAX_BUF_SIZE+1);
                            tmp_response[nbytes] = '\0';

                            strcat(response_all, tmp_response);

                            close(client_sock2);

                            ++tmp_config;
                        }

                        strcpy(prev_input, response_all);
                        prev_input_len = strlen(response_all) + 1;

                    }
                    else {
                        // send to particular node
                        int client_sock2;
                        if (cmds[cmd_idx]->node == -1) {
                            // self
                            client_sock2 = client_init(inet_ntoa(client_addr.sin_addr), CLIENT_PORT);
                        }
                        else {
                            // non-self node on cluster
                            client_sock2 = client_init(config[cmds[cmd_idx]->node - 1]->ip, CLIENT_PORT);
                        }

                        char cmd_prev_response[MAX_CMD_LEN+1 + MAX_BUF_SIZE+1];
                        memset(cmd_prev_response, 0, MAX_CMD_LEN+1 + MAX_BUF_SIZE+1);
                        strcpy(cmd_prev_response, cmds[cmd_idx]->cmd);
                        strcpy(cmd_prev_response+strlen(cmds[cmd_idx]->cmd)+1, prev_input);
                        size_t cmd_prev_response_len = strlen(cmds[cmd_idx]->cmd)+1 + strlen(prev_input)+1;

                        int nbytes = write(client_sock2, cmd_prev_response, cmd_prev_response_len);
                        if (nbytes != cmd_prev_response_len)
                            err_exit("Error in writing. Exiting...\n", client_sock2);

                        prev_input_len = read(client_sock2, prev_input, MAX_BUF_SIZE+1);

                        close(client_sock2);

                    }

                }

                prev_input[prev_input_len++] = '\0';

                // write back to connected client
                write(client_sock, prev_input, prev_input_len);
                // printf("'%s' sent back to '%s:%d'\n", prev_input, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            }

            close(client_sock);
        }

        close(client_sock);
    }

    free_config(config);

    return EXIT_SUCCESS;
}