#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h> 
#include <sys/stat.h>

#define PORT            5000
#define TEST_PORT       6000
#define TEST_LOOPBACK   0
#define MAX_GROUPS      20

// Multicast IP range : 224.0.0.0 - 239.255.255.255

// STRUCTURES
struct group_info {
    char            name[30];
    struct in_addr  addr;
    in_port_t       port;
    int             fd;
};

enum msg_type {
    MT_TEXT,
    MT_FINDGRP_REQ,
    MT_FINDGRP_REP,
    MT_FILELIST_REQ, // not used, req is just blank
    MT_FILELIST_REP,
    MT_FILE_REQ,
    MT_FILE_REP,
    MT_POLL_REQ,
    MT_POLL_REP
};

struct text_data {
    char                    text[500];
};

struct findgrp_req_data {
    char                    group_name[30];
};

struct findgrp_rep_data {
    char                    group_name[30];
    struct in_addr          group_addr;
    in_port_t               group_port;
};

struct filelist_rep_data {
    int                     n_files;
    char                    files[100][30];
    char                    visited_grps[MAX_GROUPS][30];
};

struct file_req_data {
    char                    file_name[30];
};

// The actual file data is sent by appending it with the header via TCP
struct file_rep_data {
    char                    file_name[30];    
};

struct poll_req_data {
    char                    group_name[30];
    char                    question[100];
    int                     n_options;
    char                    options[10][100];
};

struct poll_rep_data {
    int                     option;
    char                    option_str[100];
};

struct multicast_msg {
    int                         type;
    struct in_addr              src_addr;
    in_port_t                   src_port;
    union {
        struct text_data            text;
        struct findgrp_req_data     findgrp_req;
        struct findgrp_rep_data     findgrp_rep;
        struct filelist_rep_data    filelist_rep;
        struct file_req_data        file_req;
        struct poll_req_data        poll_req;
        struct poll_rep_data        poll_rep;
    }                           data;
};

// GLOBALS
int sock_fd, epoll_fd;
struct group_info groups[MAX_GROUPS]; size_t n_groups;
struct in_addr host_addr;
char local_files[100][30]; size_t n_local_files;
char remote_files[MAX_GROUPS * 100][30]; size_t n_remote_files;


void err_exit(const char * err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

void handle_sigalarm(int sig) {
    return;
}

int max(int a, int b) {
    return (a > b) ? a : b;
}

int min(int a, int b) {
    return (a < b) ? a : b;
}

void share_filenames(bool is_infinite, char group_name[30]) {

    while(true) {
        DIR *files;
        struct multicast_msg msg = {0};
        struct dirent *dir;
        files = opendir("./data");
        int n_files = 0;

        if (files != NULL) {
            while ((dir = readdir(files)) != NULL) {
                struct stat eStat;
                stat(dir -> d_name, &eStat);
                if(!S_ISDIR(eStat.st_mode)) {
                    strcpy(local_files[n_files], dir -> d_name);
                    strcpy(msg.data.filelist_rep.files[n_files++], dir -> d_name); 
                }
            }
            closedir(files);
        }
        msg.data.filelist_rep.n_files = n_files;

        n_local_files = n_files;

        for(int i = 0; i < n_groups; i++) 
            strcpy(msg.data.filelist_rep.visited_grps[i], groups[i].name);
            
        for (size_t i = 0; i < n_groups; ++i) {
            if(is_infinite || strcmp(group_name, groups[i].name) == 0) {
                msg.type = MT_FILELIST_REP;
                msg.src_addr = host_addr;
                msg.src_port = PORT;
                msg.data.findgrp_rep.group_port = groups[i].port;
                msg.data.findgrp_rep.group_addr = groups[i].addr;

                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = htons(groups[i].port);
                addr.sin_addr = groups[i].addr;
                sendto(sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, sizeof(addr));
            }
        }
        if(!is_infinite) 
            return;
        sleep(60);
    }
}

void create_group(char group_name[30], char group_ip[40], in_port_t group_port, int joinflag) {
    // joinflag = 0 -> "Create"
    // joinflag = 1 -> "Join"

        if (n_groups >= MAX_GROUPS) {
        printf("! Cannot %s group. Maximum group limit already reached...\n", (joinflag)?"Join":"Create");
        return;
    }

    struct sockaddr_in addr = {0};
    int fd;
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        perror("! Error in setsockopt. Try again...\n\n");
        close(fd);
        return;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(group_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1 && errno == EADDRINUSE) {
        printf("! Port %d is already in use. Try again with different port...\n\n", group_port);
        close(fd);
        return;
    }
    int bcast = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) == -1) {
        close(fd);
        err_exit("! Error in setsockopt. Try again...\n\n");
    }

    if (inet_pton(AF_INET, group_ip, &(addr.sin_addr)) == -1) {
        printf("! Group IP '%s' is invalid. Try again...\n\n", group_ip);
        close(fd);
        return;
    }
    struct ip_mreq mreq = {0};
    mreq.imr_multiaddr = addr.sin_addr;

    // // BIND TO ALL INTERFACES
    // struct ifaddrs *ifap, *ifa;
    // struct sockaddr_in *sa;
    // getifaddrs (&ifap);
    // for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
    //     if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET) {
    //         sa = (struct sockaddr_in *) ifa->ifa_addr;
    //         mreq.imr_interface = sa->sin_addr;
    //         if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
    //             perror("! Error in setsockopt. Try again...\n\n");
    //             close(fd);
    //             return;
    //         }
    //     }
    // }
    // freeifaddrs(ifap);

    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
        perror("Error in setsockopt. Try again...\n\n");
        close(fd);
        return;
    }
    int loop = TEST_LOOPBACK;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == -1) {
        perror("! Error in setsockopt. Try again...\n\n");
        close(fd);
        return;
    }

    strcpy(groups[n_groups].name, group_name);
    groups[n_groups].addr = addr.sin_addr;
    groups[n_groups].port = group_port;
    groups[n_groups].fd = fd;
    ++n_groups;

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);

    printf(">> %s group '%s' on %s:%d\n\n", (joinflag)?"Joined":"Created", group_name, group_ip, group_port);
}

void find_group_rep(struct multicast_msg * msg) {
    
    for (size_t i = 0; i < n_groups; ++i) {
        if (strcmp((msg->data).findgrp_req.group_name, groups[i].name) == 0) {
            struct multicast_msg rep_msg = {0};
            rep_msg.type = MT_FINDGRP_REP;
            rep_msg.src_addr = host_addr;
            rep_msg.src_port = PORT;
            strcpy(rep_msg.data.findgrp_rep.group_name, groups[i].name);
            rep_msg.data.findgrp_rep.group_port = groups[i].port;
            rep_msg.data.findgrp_rep.group_addr = groups[i].addr;

            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = msg->src_port;
            addr.sin_addr = msg->src_addr;
            sendto(sock_fd, &rep_msg, sizeof(rep_msg), 0, (struct sockaddr *) &addr, sizeof(addr));
            return;
        }    
    }

}

void find_join_group(char group_name[30], int joinflag) {
    // joinflag = 0 -> FINDGRP_REQ
    // joinflag = 1 -> JOIN_GRP
    
    for (size_t i = 0; i < n_groups; ++i) {
        if (strcmp(group_name, groups[i].name) == 0) {
            char ip[40];
            inet_ntop(AF_INET, &(groups[i].addr), ip, 40);
            printf(">> Group '%s' found on %s:%d and you are a member\n\n", group_name, ip, groups[i].port);
            return;
        }    
    }
    
    struct multicast_msg msg = {0};
    msg.type = MT_FINDGRP_REQ;
    msg.src_addr = host_addr;
    strcpy(msg.data.findgrp_req.group_name, group_name);
    
    
    struct sockaddr_in addr; socklen_t addr_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int tmp_sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (bind(tmp_sock_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf("! Error in bind. Try again...\n\n");
        close(tmp_sock_fd);
        return;
    }
    getsockname(tmp_sock_fd, (struct sockaddr *) &addr, &addr_len);
    
    // Network order
    msg.src_port = addr.sin_port;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sendto(sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, sizeof(addr));

    int tmp_epoll_fd = epoll_create1(0);
    struct epoll_event tmp_event;
    tmp_event.events = EPOLLIN;
    tmp_event.data.fd = tmp_sock_fd;
    epoll_ctl(tmp_epoll_fd, EPOLL_CTL_ADD, tmp_sock_fd, &tmp_event);

    // Wait for 3 seconds MAX
    int tmp_n_events = epoll_wait(tmp_epoll_fd, &tmp_event, 1, 3000);
    if (tmp_n_events == 0) {
        printf(">> Group '%s' not found\n\n", group_name);
    }
    else if (tmp_n_events > 0) {
        recvfrom(tmp_sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, &addr_len);
        char ip[40];
        inet_ntop(AF_INET, &(msg.data.findgrp_rep.group_addr), ip, 40);
        printf(">> Group '%s' found on %s:%d and you are not a member\n\n", msg.data.findgrp_rep.group_name, ip, msg.data.findgrp_rep.group_port);
        if (joinflag)
            create_group(msg.data.findgrp_rep.group_name, ip, msg.data.findgrp_rep.group_port, 1);
    }
    else {
        printf("! Error in epoll_wait. Try again...\n\n");
    }

    close(tmp_epoll_fd);
    close(tmp_sock_fd);
}

void send_group_mssg(char group_name[30], char mssg[500]) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    for (int i = 0; i < n_groups; ++i) {
        if (strcmp(group_name, groups[i].name) == 0) {
            struct multicast_msg rep_msg = {0};
            rep_msg.type = MT_TEXT;
            rep_msg.src_addr = host_addr;
            rep_msg.src_port = PORT;
            strcpy(rep_msg.data.text.text, mssg);

            addr.sin_port = htons(groups[i].port);
            addr.sin_addr = groups[i].addr;
            if (sendto(sock_fd, &rep_msg, sizeof(rep_msg), 0, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                perror("! Error in sendto. Try again...\n\n");
            return;
        }
    }
}

void receive_text(int fd, struct multicast_msg * msg) {
    for (int i = 0; i < n_groups; ++i) {
        if (groups[i].fd == fd) {
            char ip[40];
            inet_ntop(AF_INET, &(groups[i].addr), ip, 40);
            printf(">> Received from %s:%d in group '%s'\n>  %s\n", ip, groups[i].port, groups[i].name, msg->data.text.text);
            return;
        }
    }
}

void request_file(char filename[30]) {
    for (int i = 0; i < n_local_files; ++i) {
        if (strcmp(local_files[i], filename) == 0) {
            printf("! File '%s' already exists. Try again with different file name...\n\n", filename);
            return;
        }
    }
    
    struct sockaddr_in addr = {0}; socklen_t addr_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);
    
    int tmp_sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bind(tmp_sock_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        printf("! Error in bind. Try again...\n\n");
        close(tmp_sock_fd);
        return;
    }
    getsockname(tmp_sock_fd, (struct sockaddr *) &addr, &addr_len);
    
    if (listen(tmp_sock_fd, 1) == -1) {
        printf("! Error in listen. Try again...\n\n");
        close(tmp_sock_fd);
        return;
    }

    struct multicast_msg msg = {0};
    for (int i = 0; i < n_groups; ++i) {
        msg.type = MT_FILE_REQ;
        msg.src_addr = host_addr;
        msg.src_port = ntohs(addr.sin_port);
        strcpy(msg.data.file_req.file_name, filename);

        addr.sin_port = htons(groups[i].port);
        addr.sin_addr = groups[i].addr;
        sendto(sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, sizeof(addr));
    }

    sigaction(SIGALRM, &(struct sigaction){handle_sigalarm}, NULL);
    alarm(60);
    int conn_fd = accept(tmp_sock_fd, &addr, &addr_len);
    if (conn_fd == -1 && errno == EINTR) {
        printf(">> Requested file '%s' not found in any group\n\n", filename);
        close(tmp_sock_fd);
        return;
    }
    sigaction(SIGALRM, &(struct sigaction){SIG_DFL}, NULL);

    char filename_path[50] = "";
    strcat(filename_path, "data/");
    strcat(filename_path, filename);

    FILE * fp = fopen(filename_path, "w");

    char buf[500];
    size_t n_bytes = 0;
    while (n_bytes = recv(conn_fd, buf, 500, 0)) {
        fwrite(buf, 1, n_bytes, fp);
    }

    char ip[40];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, 40);

    strcpy(local_files[n_local_files++], filename);
    printf(">> Received file '%s' from %s:%d\n\n", filename, ip, ntohs(addr.sin_port));

    fclose(fp);
    close(conn_fd);
    close(tmp_sock_fd);
}

void reply_file(int fd, struct multicast_msg * msg) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr = msg->src_addr;
    addr.sin_port = htons(msg->src_port);
    
    for (int i = 0; i < n_local_files; ++i) {
        if (strcmp(local_files[i], (msg->data).file_req.file_name) == 0) {
            int tmp_sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect(tmp_sock_fd, &addr, sizeof(addr)) == -1) {
                close(tmp_sock_fd);
                return;
            }

            char filename_path[50] = "";
            strcat(filename_path, "data/");
            strcat(filename_path, (msg->data).file_req.file_name);

            FILE * fp = fopen((msg->data).file_req.file_name, "r");
            
            char buf[500];
            size_t n_bytes = 0;
            while (n_bytes = fread(buf, 1, 500, fp)) {
                send(tmp_sock_fd, buf, n_bytes, 0);
            }
            
            fclose(fp);
            close(tmp_sock_fd);
            return;
        }
    }
}

void create_poll(char group_name[30], char question[100], int n_options, char options[10][100]) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    for (int i = 0; i < n_groups; ++i) {
        if (strcmp(group_name, groups[i].name) == 0) {
            struct multicast_msg msg = {0};
            msg.type = MT_POLL_REQ;
            msg.src_addr = host_addr;
            msg.src_port = PORT;
            strcpy(msg.data.poll_req.group_name, group_name);
            strcpy(msg.data.poll_req.question, question);
            msg.data.poll_req.n_options = n_options;

            for(int j = 0; j < n_options; j++) {
                strcpy(msg.data.poll_req.options[j], options[j]);
            }

            addr.sin_port = htons(groups[i].port);
            addr.sin_addr = groups[i].addr;
            if (sendto(sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, sizeof(addr)) == -1)
                perror("! Error in sendto. Try again...\n\n");
            return;
        }
    }

    // Receive replies
}

void get_list_files(char group_name[30]) {
    share_filenames(false, group_name);
}

void list_files() {
    printf(">> Local files (%ld) :\n", n_local_files);
    for (size_t i = 0; i < n_local_files; ++i) {
        printf(">  \t\t%s\n", local_files[i]);
    }
    printf(">> Remote files (%ld) :\n", n_remote_files);
    for (size_t i = 0; i < n_remote_files; ++i) {
        printf(">  \t\t%s\n", remote_files[i]);
    }
    printf("\n");
}

void reply_poll(struct multicast_msg * msg) {

    int sel_option = 0;
    printf(">> Poll received on group '%s'\n", (msg->data).poll_req.group_name);
    printf(">  Question:- %s\n", (msg->data).poll_req.question);
    for (int i = 0; i < (msg->data).poll_req.n_options; ++i) {
        printf(">  Option %d: %s\n", i+1, (msg->data).poll_req.options[i]);
    }
    printf("> Enter your choice (option number): ");
    fflush(stdout);
    char * opt_buf = malloc(5 * sizeof(char)); size_t opt_buf_len;
    getline(&opt_buf, &opt_buf_len, stdin);
    sel_option = atoi(opt_buf);
    printf("\n");
    free(opt_buf);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    for (int i = 0; i < n_groups; ++i) {
        if (strcmp(msg -> data.poll_req.group_name, groups[i].name) == 0) {
            struct multicast_msg rep_msg = {0};
            rep_msg.type = MT_POLL_REP;
            rep_msg.src_addr = host_addr;
            rep_msg.src_port = PORT;
            rep_msg.data.poll_rep.option = sel_option;
            strcpy(rep_msg.data.poll_rep.option_str, (msg->data).poll_req.options[sel_option-1]);

            addr.sin_port = htons(groups[i].port);
            addr.sin_addr = groups[i].addr;
            sendto(sock_fd, &rep_msg, sizeof(rep_msg), 0, (struct sockaddr *) &addr, sizeof(addr));
            return;
        }
    }
}

void receive_poll_reply(struct multicast_msg * msg) {
    char ip[40];
    inet_ntop(AF_INET, &(msg->src_addr), ip, 40);
    printf(">  (%d) \'%s\' option chosen by %s:%d\n", (msg->data).poll_rep.option, (msg->data).poll_rep.option_str, ip, msg->src_port);
}

void update_filelists(int fd, struct multicast_msg * msg) {
    for(int i = 0; i < msg -> data.filelist_rep.n_files; i++) {
        bool found = false;
        for(int j = 0; j < n_remote_files; j++) {
            if(remote_files[j] == msg  -> data.filelist_rep.files[i]) {
                found = true;
                break;
            }
        }
        if(!found)
            strcpy(remote_files[n_remote_files++], msg -> data.filelist_rep.files[i]);
    }
}


void handle_multicast_msg(int fd) {
    // fd is multicast group socket fd
    /*
     Receive and respond
        + Receive text
        + Request file rep
        + Poll rep
        - List files rep
    */
    struct sockaddr_in addr = {0}; socklen_t addr_len;
    struct multicast_msg msg = {0};
    
    recvfrom(fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, &addr_len);
    
    if (msg.type == MT_TEXT) {
        receive_text(fd, &msg);
    }
    else if (msg.type == MT_FILE_REQ) {
        reply_file(fd, &msg);
    }
    else if (msg.type == MT_POLL_REQ) {
        reply_poll(&msg);
    }
    else if (msg.type == MT_FILELIST_REP) {
        // forward to all other non-visited groups
        update_filelists(fd, &msg);
    }
    else if (msg.type == MT_POLL_REP) {
        receive_poll_reply(&msg);
    }
}

void handle_broadcast_msg() {
    /*
     Receive and respond
        + Find group rep
    */
    struct sockaddr_in addr = {0}; socklen_t addr_len;
    struct multicast_msg msg = {0};
    
    recvfrom(sock_fd, &msg, sizeof(msg), 0, (struct sockaddr *) &addr, &addr_len);
    
    if (msg.type == MT_FINDGRP_REQ) {
        find_group_rep(&msg);
    }
}

bool handle_cmd(char cmd_buf[100], size_t cmd_len) {
    /*
     Parse and Call corresponding command functions
        + Send text
        + Create group
        + Find group req
        + List group
        + List file req
        + Request file req
        + Poll req
    */
        bool is_error = false;
        char * tmp_cmd = strdup(cmd_buf);
        char* saved_ptr;
        char* cmd_token = strtok_r(tmp_cmd, " ", &saved_ptr);
        
        if(strcmp(cmd_token, "create-group") == 0) {
            //create-group [group_name] [group_ip] [group_port]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            char * group_ip = strtok_r(NULL, " ", &saved_ptr);
            if(group_ip == NULL) 
                return false;
            char * group_port_str = strtok_r(NULL, " ", &saved_ptr);
            if(group_port_str == NULL) 
                return false;
            in_port_t group_port = atoi(group_port_str);
            create_group(group_name, group_ip, group_port, 0);
        }
        else if(strcmp(cmd_token, "join-group") == 0) {
            //join-group [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            find_join_group(group_name, 1);
        }
        else if(strcmp(cmd_token, "find-group") == 0) {
            //find-group [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            find_join_group(group_name, 0);
        }
        else if(strcmp(cmd_token, "request") == 0) {
            //request [file_name]
            char * file_name = strtok_r(NULL, " ", &saved_ptr);
            if(file_name == NULL) 
                return false;
            request_file(file_name);
        }
        else if(strcmp(cmd_token, "list-files") == 0) {
            //list-files [group_name]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                list_files();
            else
                list_files(group_name);
        }
        else if(strcmp(cmd_token, "send") == 0) {
            //send [group_name] [mssg]
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;

            char *mssg = group_name + strlen(group_name) + 1;
            if(*mssg == '\0') 
                return false;
            send_group_mssg(group_name, mssg);
        }
        else if(strcmp(cmd_token, "create-poll") == 0) {
            //create-poll "[question]" n_options "[option1]" "[option2]" "[option3]"
            char * group_name = strtok_r(NULL, " ", &saved_ptr);
            if(group_name == NULL) 
                return false;
            char * question = strtok_r(NULL, "\"", &saved_ptr);
            // question = strtok_r(NULL, "\"", &saved_ptr);
            if(question == NULL) 
                return false;

            char * n_options_str = strtok_r(NULL, "\"", &saved_ptr);
            if(n_options_str == NULL) 
                return false;
            int n_options = atoi(n_options_str);
            char options[10][100];
            for(int i = 0; i < n_options; i++) {
                char * option = strtok_r(NULL, "\"", &saved_ptr);
                if(option == NULL) 
                    return false;
                strcpy(options[i], option);
                strtok_r(NULL, "\"", &saved_ptr);
            }
            create_poll(group_name, question, n_options, options);
        }
        return true;
}

int main() {
    n_groups = 0;
    n_local_files = 0;
    n_remote_files = 0;
    
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    
    if (bind(sock_fd, &addr, addr_len) == -1)
        err_exit("Error in bind. Exiting...\n");

    int loop = TEST_LOOPBACK;
    if (setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == -1) {
        close(sock_fd);
        err_exit("Error in setsockopt. Try again...\n\n");
    }

    int bcast = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) == -1) {
        close(sock_fd);
        err_exit("Error in setsockopt. Try again...\n\n");
    }

    char hostname[100];
    struct hostent * host_entry;

    gethostname(hostname, 100);
    host_entry = gethostbyname(hostname);
    host_addr = *((struct in_addr*) host_entry->h_addr_list[0]);

    pid_t child_pid = fork();

    if(child_pid == 0) {
        share_filenames(true, NULL);
        _exit(EXIT_SUCCESS);
    }

    struct epoll_event event, trig_events[50];
    epoll_fd = epoll_create1(0);
    
    event.events = EPOLLIN;
    event.data.fd = STDIN_FILENO;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);

    event.events = EPOLLIN;
    event.data.fd = sock_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event);

    printf("Application running on port %d...\n\n\n", PORT);
    
    char * cmd_buf = malloc(sizeof(char) * 100); size_t cmd_len;
    while (true) {

        int n_events = epoll_wait(epoll_fd, trig_events, 50, -1);
        if (n_events == -1 && errno == EINTR)
            continue;

        for (int i = 0; i < n_events; ++i) {
            if (trig_events[i].data.fd == STDIN_FILENO) {
                int i = 0;
                while(read(STDIN_FILENO, cmd_buf+i, 1) > 0) {
                    if (cmd_buf[i] == '\n') {
                        cmd_buf[i] = '\0';
                        break;
                    }
                    ++i;
                }
                if (i == 0)
                    continue;
                // getline(&cmd_buf, &cmd_len, stdin);
                // if (cmd_buf[cmd_len - 1] == '\n')
                //     cmd_buf[cmd_len - 1] = '\0';

                handle_cmd(cmd_buf, cmd_len);
            }
            else if (trig_events[i].data.fd == sock_fd) {
                handle_broadcast_msg();
            }
            else {
                handle_multicast_msg(trig_events[i].data.fd);
            }
        }
    }

    //Clean up
    for (int i = 0; i < n_groups; ++i) {
        close(groups[i].fd);
    }

    free(cmd_buf);
    close(epoll_fd);
    close(sock_fd);

    exit(EXIT_SUCCESS);
}
