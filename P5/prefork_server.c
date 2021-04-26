#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>


int minSpareServ, maxSpareServ, maxReqPerChild;
int listen_fd;

struct proc_info {
    pid_t pid;
    int status;
};

enum serv_status {
    SS_INIT=0,
    SS_IDLE=1,
    SS_BUSY=2,
    SS_EXIT=3
};

struct ctrl_msg {
    int status;
};

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

void handle_conn(int ctrl_fd) {
    // Handle HTTP requests (maxReqPerChild)
    // Notify parent of every change (serv_status)
    struct ctrl_msg msg;

    // Send IDLE status message to parent
    msg.status = SS_IDLE;
    send(ctrl_fd, &msg, sizeof(msg), 0);

    size_t cli_addr_len;
    struct sockaddr_in cli_addr;

    char http_buf[2000];
    
    for (int n_req = maxReqPerChild; n_req > 0; --n_req) {
        // accept connection and handle request
        int conn_fd = accept(listen_fd, &cli_addr, &cli_addr_len);
        msg.status = SS_BUSY;
        send(ctrl_fd, &msg, sizeof(msg), 0);

        // Receive and send dummy HTTP message
        recv(conn_fd, http_buf, 2000, 0);
        sleep(1);
        send(conn_fd, http_buf, 2000, 0); // TODO: Respond with proper dummy response

        close(conn_fd);
        msg.status = SS_IDLE;
        send(ctrl_fd, &msg, sizeof(msg), 0);
    }

    // Send EXIT status message to parent
    msg.status = SS_EXIT;
    send(ctrl_fd, &msg, sizeof(msg), 0);
    _exit(EXIT_SUCCESS);
}

void create_serv(int epoll_fd, struct proc_info * child_procs, size_t * n_child_procs) {
    // Create a UNIX Domain socket and fork
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
        err_exit("Error in socketpair. Exiting...\n");
    // int sock_flags = fcntl(sv[0], F_GETFL, 0);
    // fcntl(sv[0], F_SETFL, sock_flags | O_NONBLOCK);
    // sock_flags = fcntl(sv[1], F_GETFL, 0);
    // fcntl(sv[1], F_SETFL, sock_flags | O_NONBLOCK);

    pid_t pid_serv = fork();
    if (pid_serv == -1)
        err_exit("Error in fork. Exiting...\n");
    else if (pid_serv == 0) {
        // sv[1] is child socket
        close(sv[0]);
        handle_conn(sv[1]);
    }
    else {
        close(sv[1]);
    }

    // sv[0] is parent socket
    // Add sv[0] to epoll instance
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sv[0];
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sv[0], &event);

    child_procs[sv[0]].pid = pid_serv;
    *n_child_procs += 1;
}

void create_serv_expo(int n_serv, int epoll_fd, struct proc_info * child_procs, size_t * n_child_procs) {
    // Exponentially till 32 and constant rate later
    int n_created = 0;
    
    for (int i = 0; n_created < n_serv; ++i) {
        int rate = 1 << min(i, 5);
        for (int ii = 0; ii < rate; ++ii, ++n_created) {
            create_serv(epoll_fd, child_procs, n_child_procs);
        }
    }
}

int main(int argc, char * argv[]) {

    if (argc < 4)
        err_exit("One or more of the arguments 'MinSpareServers', 'MaxSpareServers' and 'MaxRequestsPerChild' is/are not specified! Exiting...\n");
    
    minSpareServ = atoi(argv[1]);
    maxSpareServ = atoi(argv[2]);
    maxReqPerChild = atoi(argv[3]);

    // +10 is safety buffer incase some file is opened
    size_t n_child_procs = 0, max_child_procs = maxSpareServ + 32 + 10;
    struct proc_info * child_procs = malloc(max_child_procs * sizeof(struct proc_info));
    memset(child_procs, 0, sizeof(max_child_procs * sizeof(struct proc_info)));

    // Epoll instance
    int epoll_fd = epoll_create1(0);
    int n_events;
    struct epoll_event trig_events[10];

    struct ctrl_msg msg_buf;

    // Set up signal handler


    // Bind and listen on listen_fd
    struct sockaddr_in cli_addr;
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_port = 8080;
    cli_addr.sin_addr.s_addr = INADDR_ANY;
    
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bind(listen_fd, &cli_addr, sizeof(cli_addr)) == -1)
        err_exit("Error in bind. Exiting...\n");

    if (listen(listen_fd, 10) == -1)
        err_exit("Error in listen. Exiting...\n");
    
    // Initial creation of process pool
    create_serv_expo(minSpareServ, child_procs, &n_child_procs);

    // Process-pool control logic
    while (true) {
        // Wait on epoll instance
        n_events = epoll_wait(epoll_fd, trig_events, 10, -1);
        
        // Update status or kill exhausted process
        for (int i = 0; i < n_events; ++i) {
            recv(trig_events[i].data.fd, &msg_buf, sizeof(msg_buf), 0);
            if (msg_buf.status == SS_EXIT) {
                waitpid(child_procs[trig_events[i].data.fd].pid, NULL, 0);
                child_procs[trig_events[i].data.fd].pid = 0;
                child_procs[trig_events[i].data.fd].status = SS_INIT;
                --n_child_procs;
            }
            else
                child_procs[trig_events[i].data.fd].status = msg_buf.status;
        }

        // Take action based on the incoming control message
        if (n_child_procs > maxSpareServ) {
            // delete excess processes
            for (int i = 0; i < max_child_procs; ++i) {
                if (child_procs[i].pid > 0 && child_procs[i].status == SS_IDLE) {
                    kill(child_procs[i].pid, SIGKILL);
                    waitpid(child_procs[i].pid, NULL, 0);
                    child_procs[i].pid = 0;
                    child_procs[i].status = SS_INIT;
                    --n_child_procs;
                }
            }
        }
    }

    close(epoll_fd);
    free(child_procs);

    return EXIT_SUCCESS;
}
