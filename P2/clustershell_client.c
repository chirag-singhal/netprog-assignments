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
#define CLIENT_PORT 5000
#define SERVER_PORT 5001
#define CONFIG_FILE "clustershell.cfg"


typedef struct _CONFIG_ENTRY {
    char * name;
    char * ip;
} CONFIG_ENTRY;

typedef struct _CMD_STRUCT {
    int node;
    char * cmd;
} CMD_STRUCT;


void err_exit(const char * err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

CONFIG_ENTRY ** read_config(const char * filename) {
    CONFIG_ENTRY ** config = malloc((MAX_NUM_CLI + 1) * sizeof(CONFIG_ENTRY *));
    FILE * config_fp = fopen(filename, "r");
    if (config_fp == NULL)
        err_exit("Error opening config. Exiting...\n");

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
        err_exit("Error in socket. Exiting...\n");

    int sockopt_optval = 1;
    if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &sockopt_optval, sizeof(sockopt_optval)) < 0)
        err_exit("Error in setsockopt. Exiting...\n");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(serv_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        err_exit("Error in bind. Exiting...\n");

    if (listen(serv_sock, 5) < 0)
        err_exit("Error in listen. Exiting...\n");

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
        err_exit("Error in connect. Exiting...\n");

    return client_sock;
}

char * execute_single_cmd(char * cmd, size_t cmd_size, size_t * out_size) {
    char * tmp_cmd = strdup(cmd);
    char * strtok_saveptr;

    *out_size = 0;

    char * token = strtok_r(tmp_cmd, " ", &strtok_saveptr);
    if (token == NULL)
        return NULL;
    if (strcmp(token, "cd") == 0) {
        // handle 'cd
        token = strtok_r(tmp_cmd, "|", &strtok_saveptr); // remaining token
        if (chdir(token) < 0)
            perror("Error in changing directory...");

        *out_size = 0;
        return "";
    }
    else {
        // run the command through shell
        FILE * cmd_fp = popen(cmd, "r");
        if (cmd_fp == NULL)
            perror("Error in popen. Exiting...\n");
        
        char * cmd_out = malloc((MAX_BUF_SIZE+1) * sizeof(char));
        *out_size = fread(cmd_fp, MAX_BUF_SIZE+1, 1, cmd_fp);
        if (*out_size < 0)
            err_exit("Error in fread. Exiting...\n");

        cmd_out[*out_size] = '\0';

        pclose(cmd_fp);

        return cmd_out;
    }

    free(tmp_cmd);
    
    return "";
}

void prompt() {
    char cwd[200];
    getcwd(cwd, 200);
    printf("\n(%s) >> ", cwd);
}

int main() {
    pid_t conn_handler = fork();
    if (conn_handler < 0)
        err_exit("Error in fork. Exiting...\n");
    else if (conn_handler == 0) {
        // Handle communication with server and run commands requested by server
        int client_sock; // 'client_sock' represents actual server
        int serv_sock = server_init(CLIENT_PORT); // 'ser_sock' is the current node (this client)

        while (true) {
            struct sockaddr_in client_addr;
            int client_len = sizeof(client_addr);

            client_sock = accept(serv_sock, (struct sockaddr *) &client_addr, &client_len);
            if (client_sock < 0)
                err_exit("Error in accept. Exiting...\n");
            
            char cmd[MAX_CMD_LEN+1];
            size_t nbytes = read(client_sock, cmd, MAX_CMD_LEN+1);

            if (nbytes < 0)
                err_exit("Error in read. Exiting...\n");
            
            cmd[nbytes] = '\0';

            size_t out_size;
            char * cmd_out = execute_single_cmd(cmd, nbytes, &out_size);

            // reply back to server with response of the command
            write(client_sock, cmd_out, out_size);
            
            free(cmd_out);
            close(client_sock);
        }

    }
    else {
        // Handle shell
        int client_connect = client_init(NULL, SERVER_PORT);
        while(true) {
            prompt();
            char * cmd = malloc(sizeof(char) * (MAX_CMD_LEN + 1));
            size_t max_cmd_len = MAX_CMD_LEN;
            size_t cmd_len = getline(&cmd, &max_cmd_len, stdin);
            cmd[cmd_len - 1] = '\0';
            cmd_len = strlen(cmd);

            if(write(client_connect, cmd, cmd_len) < 0)
                err_exit("Error in writing to server. Exiting...\n");

            char* server_resp = malloc(sizeof(char) * (MAX_BUF_SIZE + 1));

            size_t resp_size = read(client_connect, server_resp, MAX_BUF_SIZE);

            if(resp_size < 0)
                err_exit("Error in reading from server. Exiting...\n");
            
            server_resp[resp_size] = '\0';
            printf("%s\n", server_resp);
        }
        close(client_connect);
    }

    return EXIT_SUCCESS;
}
