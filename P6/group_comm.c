#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <errno.h>

#define PORT        5000
#define MAX_GROUPS  20

// STRUCTURES
struct group_info {
    char            name[30];
    struct in_addr  addr;
    in_port_t       port;
    int             fd;
};

enum msg_type {
    MT_MSG
    MT_FINDGRP_REQ,
    MT_FINDGRP_REP,
    MT_FILELIST_REQ, // not used, req is just blank
    MT_FILELIST_REP,
    MT_FILE_REQ,
    MT_FILE_REP,
    MT_POLL_REQ,
    MT_POLL_REP
};

struct findgrp_req_data {
    char                    group_name[30];
}

struct findgrp_rep_data {
    char                    group_name[30];
    struct in_addr          group_addr;
    in_port_t               group_port;
}

struct filelist_rep_data {
    int                     n_files;
    char                    files[100][30];
    char                    visited_grps[MAX_GROUPS][30];
};

struct file_req_data {
    char                    file_name[30];
}

// The actual file data is sent by appending it with the header via TCP
struct file_rep_data {
    char                    file_name[30];    
}

struct poll_req_data {
    char                    group_name[30];
    char                    question[100];
    int                     n_options;
    char                    options[10][100];
};

struct poll_rep_data {
    int                     option;
};

struct multicast_msg {
    int                         type;
    struct in_addr              src_addr;
    in_port_t                   src_port;
    union {
        struct findgrp_req_data     findgrp_req;
        struct findgrp_rep_data     findgrp_rep;
        struct filelist_rep_data    filelist_rep;
        struct file_req_data        file_req;
        struct poll_req_data        poll_req;
        struct poll_rep_data        poll_rep;
    }                           data;
};

// GLOBALS
bool sigalrm_rcvd;
struct group_info groups[MAX_GROUPS]; size_t n_groups;


void sigalrm_handler(int sig) {
    sigalrm_rcvd = true;
}

void err_exit(const char * err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

int max(int a, int b) {
    return (a > b) ? a : b;
}

int min(int a, int b) {
    return (a < b) ? a : b;
}

void share_filenames() {

}

void create_group(char group_name[30], char group_ip[40], in_port_t group_port) {
    if (n_groups >= MAX_GROUPS) {
        printf("! Cannot create group. Maximum group limit already reached...\n");
        return;
    }

    struct sockaddr_in addr;
    int fd;
    if (inet_pton(AF_INET, group_ip, &(addr.sin_addr)) == -1) {
        printf("! Group IP '%s' is invalid. Try again...\n\n", group_ip);
        return;
    }
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1 && errno == EADDRINUSE) {
        printf("! Port %d is already in use. Try again with different port...\n\n", group_port);
        close(fd);
        return;
    }

    struct ip_mreq mreq = {0};
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
        perror("Error in setsockopt. Try again...\n\n");
        close(fd);
        return;
    }
    u_char loop = 0;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == -1) {
        perror("Error in setsockopt. Try again...\n\n");
        close(fd);
        return;
    }

    strcpy(groups[n_groups].name, group_name);
    groups[n_groups].addr = addr.sin_addr;
    groups[n_groups].port = group_port;
    groups[n_groups].fd = fd;
    ++n_groups;

    printf(">> Created group '%s' on %s:%d\n\n", group_name, group_ip, group_port);
}

void find_group(char group_name[30]) {
    
    for (size_t i = 0; i < n_groups; ++i) {
        if (strcmp(group_name, groups[i].name) == 0) {
            printf(">> Group '%s' found on %s:%d\n\n", group_name, groups[i].addr, groups[i].port);
            break;
        }    
    }

    char hostname[100];
    struct hostent * host_entry;
    struct in_addr host_addr;

    gethostname(hostname, 100);
    host_entry = gethostbyname(hostname);
    host_addr = *((struct in_addr*) host_entry->h_addr_list[0]);
    
    struct multicast_msg msg;
    msg.type = MT_FINDGRP_REQ;
    msg.src_addr = host_addr;
    msg.src_port = PORT;
    strcpy(msg.findgrp_req.group_name, group_name);

}

void handle_msg() {
    /*
     Receive and respond
        + Find group
        + Request file
        + Poll
    */
}

bool handle_cmd(char cmd_buf[100], size_t cmd_len) {
    /*
     Parse and Call corresponding command functions
        + Create group
        + Find group
        + List group
        + List file 
        + Request file
        + Create Poll
    */
        bool is_error = false;
        char * tmp_cmd = strdup(cmd_buf);
        char* saved_ptr;
        char* token = strtok_r(tmp_cmd, " ", &saved_ptr);
        
        if(strcmp(token, "create-group") == 0) {
            //create-group [group_name] [group_ip] [group_port]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            char * group_ip = strtok_r(NULL, " ", &saved_ptr);
            if(group_ip == NULL) 
                return false;
            char * group_port = strtok_r(NULL, " ", &saved_ptr);
            if(group_port == NULL) 
                return false;
            create_group(group_name, group_ip, group_port);
        }
        else if(strcmp(token, "join-group") == 0) {
            //join-group [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            join_group(group_name);
        }
        else if(strcmp(token, "find-group") == 0) {
            //find-group [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            find_group(group_name);
        }
        else if(strcmp(token, "request") == 0) {
            //request [file_name]
            char * file_name = strtok_r(NULL, " ", &saved_ptr);
            if(file_name == NULL) 
                return false;
            request(file_name);
        }
        else if(strcmp(token, "list-groups") == 0) {
            //list-groups
            list_groups();
        }
        else if(strcmp(token, "list-files") == 0) {
            //list-files [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;

            list_files(group_name);
        }
        else if(strcmp(token, "send") == 0) {
            //send [group_name] [mssg]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;

            char *mssg = token + strlen(group_name) + 1;
            if(*mssg == '\0') 
                return false;
            send_group_mssg(token, mssg);
        }
        else if(strcmp(token, "create-poll") == 0) {
            //create-poll "[question]" n_options "[option1]" "[option2]" "[option3]"
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            token = strtok_r(NULL, "\"", &saved_ptr);
            char * question = strtok_r(NULL, "\"", &saved_ptr);
            if(question == NULL) 
                return false;

            char * n_options_str = strtok_r(NULL, "\"", &saved_ptr);
            if(n_options_str == NULL) 
                return false;
            int n_options = atoi(n_options_str);
            char options[10][100];
            for(int i = 0; i < n_options; i++) {
                token = strtok_r(NULL, "\"", &saved_ptr);
                char * option = strtok_r(NULL, "\"", &saved_ptr);
                if(option == NULL) 
                    return false;
                strcpy(options[i], option);
            }
            create_poll(group_name, question, n_options, options);
        }
        return true;
}

int main() {
    sigalrm_rcvd = false;
    n_groups = 0;
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    int sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    
    if (bind(sock_fd, &addr, addr_len) == -1)
        err_exit("Error in bind. Exiting...\n");

    struct sigaction sigalrm_action;
    sigalrm_action.sa_handler = sigalrm_handler;
    sigalrm_action.sa_flags = 0;
    sigemptyset(&sigalrm_action.sa_mask);
    sigaddset(&sigalrm_action.sa_mask, SIGALRM);
    
    sigaction(SIGALRM, &sigalrm_action, NULL);

    struct epoll_event event, trig_events[50];
    int epoll_fd = epoll_create1(0);
    
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);

    event.events = EPOLLIN;
    event.data.fd = sock_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event);

    char cmd_buf[100]; size_t cmd_len;

    while (true) {

        if(sigalrm_rcvd) {
            share_filenames();
            sigalrm_rcvd = false;
        }

        int n_events = epoll_wait(epoll_fd, trig_events, 50, -1);
        if (n_events == -1 && errno == EINTR)
            continue;

        for (int i = 0; i < n_events; ++i) {
            if (trig_events[i].data.fd == STDIN_FILENO) {
                getline(&cmd_buf, &cmd_len, stdin);
                if (cmd_buf[cmd_len - 1] == '\n')
                    cmd_buf[cmd_len - 1] = '\0';

                handle_cmd(cmd_buf, cmd_len);
            }
            else if (trig_events[i].data.fd == sock_fd) {
                handle_msg();
            }
        }
    }


    exit(EXIT_SUCCESS);
}
