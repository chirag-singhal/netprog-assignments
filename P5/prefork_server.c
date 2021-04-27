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

#define SERVER_PORT 8080
#define DEBUG       1       // Change to 1 for verbose status messages

int minSpareServ, maxSpareServ, maxReqPerChild;
int listen_fd;

struct proc_info {
    pid_t pid;
    int status;
    int n_conn;
};

enum serv_status {
    SS_INIT=0,
    SS_IDLE=1,
    SS_BUSY=2,
    SS_EXIT=3
};

struct ctrl_msg {
    int status;
    int n_conn;
};

bool sigint_rcvd = false;

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


void sigint_handler(int sig) {
    sigint_rcvd = true;
}

void handle_conn(int ctrl_fd) {
    // Handle HTTP requests (maxReqPerChild)
    // Notify parent of every change (serv_status)
    struct ctrl_msg msg;

    // Send IDLE status message to parent
    msg.status = SS_IDLE;
    msg.n_conn = 0;
    send(ctrl_fd, &msg, sizeof(msg), 0);

    socklen_t cli_addr_len = sizeof(struct sockaddr_in);
    struct sockaddr_in cli_addr;

    char http_buf[2000];

    int n_handled = 0;
    for (int n_req = maxReqPerChild; n_req > 0; --n_req) {
        // accept connection and handle request
        cli_addr_len = sizeof(struct sockaddr_in);
        int conn_fd = accept(listen_fd, (struct sockaddr*) &cli_addr, &cli_addr_len);
        if (conn_fd == -1) {
            if (errno != EINTR) {
                char err_buf[40];
                sprintf(err_buf, "* PID= %-8d\tError in accept...\n", getpid());
                perror(err_buf);
            }
            ++n_req;
            continue;
        }
        
        msg.status = SS_BUSY;
        msg.n_conn = n_handled;
        send(ctrl_fd, &msg, sizeof(msg), 0);

        char ip_buf[20];
        inet_ntop(AF_INET, &(cli_addr.sin_addr), ip_buf, 40);
        printf("* PID= %-8d\tClient IP= %20s:%04d\n", getpid(), ip_buf, ntohs(cli_addr.sin_port));

        // Receive and send dummy HTTP message
        recv(conn_fd, http_buf, 2000, 0);
        sleep(1);
        send(conn_fd, http_buf, 2000, 0); // TODO: Respond with proper dummy response

        close(conn_fd);
        
        ++n_handled;
        
        msg.status = SS_IDLE;
        msg.n_conn = n_handled;
        send(ctrl_fd, &msg, sizeof(msg), 0);
    }

    // Send EXIT status message to parent
    msg.status = SS_EXIT;
    msg.n_conn = n_handled;
    send(ctrl_fd, &msg, sizeof(msg), 0);
    _exit(EXIT_SUCCESS);
}

int create_serv(int epoll_fd, struct proc_info * child_procs, size_t * n_child_procs) {
    // Create a UNIX Domain socket and fork
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
        err_exit("Error in socketpair. Exiting...\n");

    pid_t pid_serv = fork();
    if (pid_serv == -1)
        err_exit("Error in fork. Exiting...\n");
    else if (pid_serv == 0) {
        // sv[1] is child socket
        close(sv[0]);
        handle_conn(sv[1]);
        _exit(EXIT_SUCCESS);
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
    child_procs[sv[0]].n_conn = 0;
    *n_child_procs += 1;

    return pid_serv;
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
    printf("@@ Server starting with %d processes in pool.\n\n", n_created);
}

int main(int argc, char * argv[]) {

    if (argc < 4)
        err_exit("One or more of the arguments 'MinSpareServers', 'MaxSpareServers' and 'MaxRequestsPerChild' is/are not specified! Exiting...\n");
    
    minSpareServ = atoi(argv[1]);
    maxSpareServ = atoi(argv[2]);
    maxReqPerChild = atoi(argv[3]);

    // +10 is safety buffer incase some file is opened (including stdin and stdout)
    size_t n_child_procs = 0, max_child_procs = maxSpareServ + 32 + 10;
    struct proc_info * child_procs = malloc(max_child_procs * sizeof(struct proc_info));
    memset(child_procs, 0, sizeof(max_child_procs * sizeof(struct proc_info)));

    // Epoll instance
    int epoll_fd = epoll_create1(0);
    int n_events;
    struct epoll_event event, trig_events[50];

    struct ctrl_msg msg_buf;

    // Set up signal handler
    struct sigaction sigint;
    sigint.sa_handler = sigint_handler;
    sigint.sa_flags = 0;
    sigemptyset(&sigint.sa_mask);
    sigaddset(&sigint.sa_mask, SIGINT);

    sigaction(SIGINT, &sigint, NULL);

    // Bind and listen on listen_fd
    struct sockaddr_in cli_addr;
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_port = htons(SERVER_PORT);
    cli_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bind(listen_fd, (struct sockaddr*)&cli_addr, sizeof(cli_addr)) == -1)
        err_exit("Error in bind. Exiting...\n");

    if (listen(listen_fd, 10) == -1)
        err_exit("Error in listen. Exiting...\n");

    printf("@@ Server listening on PORT %d.\n", SERVER_PORT);

    // Initial creation of process pool
    create_serv_expo(minSpareServ, epoll_fd, child_procs, &n_child_procs);


    // Process-pool control logic
    int min_serv_tmp = 1, n_busy = 0, n_idle = 0;
    while (true) {
        // Wait on epoll instance

        if(sigint_rcvd) {
            printf("\n~~ Num Child Procs: %-6ld\tSpare Procs: %-6d\tNum Clients: %-6d\n", n_child_procs, n_idle, n_busy);
            for(int i = 0; i < max_child_procs; i++) {
                if(child_procs[i].pid > 0 && (child_procs[i].status == SS_IDLE || child_procs[i].status == SS_BUSY)) {
                    printf("~ PID: %d\t Number of clients handled: %d\n", child_procs[i].pid, child_procs[i].n_conn);
                }
            }
            sigint_rcvd = false;
        }

        n_events = epoll_wait(epoll_fd, trig_events, 50, -1);
        if (n_events == -1 && errno == EINTR)
            continue;
        
        // Update status or kill exhausted process
        int n_new_conn = 0, n_new_idle = 0;
        for (int i = 0; i < n_events; ++i) {
            if (recv(trig_events[i].data.fd, &msg_buf, sizeof(msg_buf), 0) == -1 && errno == EINTR)
                continue;
            if (msg_buf.status == SS_EXIT) {
                // Kill and recycle
                waitpid(child_procs[trig_events[i].data.fd].pid, NULL, 0);
                int _tmp_pid = child_procs[trig_events[i].data.fd].pid;
                if (child_procs[trig_events[i].data.fd].status == SS_IDLE) {
                    --n_idle;
                }
                else if (child_procs[trig_events[i].data.fd].status == SS_BUSY) {
                    --n_busy;
                }
                --n_child_procs;
                
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, trig_events[i].data.fd, &event);
                
                child_procs[trig_events[i].data.fd].pid = 0;
                child_procs[trig_events[i].data.fd].status = SS_INIT;
                child_procs[trig_events[i].data.fd].n_conn = 0;

                int _tmp_new_pid = create_serv(epoll_fd, child_procs, &n_child_procs);

                printf("\n>>> Num Child Procs: %-6ld\tSpare Procs: %-6d\tNum Clients: %-6d\n>>  Action= %-15s\tStatus= 'Killed PID %d, Created PID %d'\n\n", n_child_procs, n_idle, n_busy, "RECYCLE", _tmp_pid, _tmp_new_pid);
            }
            else if (msg_buf.status == SS_BUSY) {
                if (child_procs[trig_events[i].data.fd].status == SS_IDLE) {
                    --n_idle;
                    ++n_busy;
                    ++n_new_conn;
                }
                child_procs[trig_events[i].data.fd].status = msg_buf.status;
                child_procs[trig_events[i].data.fd].n_conn = msg_buf.n_conn;
            }
            else { // SS_IDLE
                if (child_procs[trig_events[i].data.fd].status == SS_BUSY) {
                    ++n_idle;
                    --n_busy;
                    ++n_new_idle;
                }
                else if (child_procs[trig_events[i].data.fd].status != SS_IDLE) {
                    ++n_idle;
                    ++n_new_idle;
                }
                child_procs[trig_events[i].data.fd].status = msg_buf.status;
                child_procs[trig_events[i].data.fd].n_conn = msg_buf.n_conn;
            }
        }
        if (DEBUG && (n_new_conn || n_new_idle))
            printf("\n@@ Received %d new connections on %d spare procs.\n@  %d procs turned IDLE.\n\n", n_new_conn, n_new_conn, n_new_idle);

        // Take action based on the incoming control message
        
        // Delete excess processes
        int n_del = 0;
        for (int i = 0; n_idle > maxSpareServ && i < max_child_procs; ++i) {
            if (child_procs[i].pid > 0 && child_procs[i].status == SS_IDLE) {
                kill(child_procs[i].pid, SIGKILL);
                waitpid(child_procs[i].pid, NULL, 0);
                --n_idle;
                --n_child_procs;
                ++n_del;
                
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, i, &event);
                
                child_procs[i].pid = 0;
                child_procs[i].status = SS_INIT;
                child_procs[i].n_conn = 0;
            }
        }
        if (n_del)
            printf("\n>>> Num Child Procs: %-6ld\tSpare Procs: %-6d\tNum Clients: %-6d\n>>  Action= %-15s\tStatus= 'Killed %d excess child processes'\n\n", n_child_procs, n_idle, n_busy, "KILL_EXCESS_PROC", n_del);
        
        // Add new processes to pool and sleep
        if (n_idle < minSpareServ) {
            for (int i = 0; i < min_serv_tmp; ++i)
                create_serv(epoll_fd, child_procs, &n_child_procs);
            printf("\n>>> Num Child Procs: %-6ld\tSpare Procs: %-6d\tNum Clients: %-6d\n>>  Action= %-15s\tStatus= 'Added %d processes to pool'\n\n", n_child_procs, n_idle, n_busy, "ADD_NEW_PROC", min_serv_tmp);
            if (min_serv_tmp < 32)
                min_serv_tmp *= 2;
            sleep(1);
        }
        if (n_idle >= minSpareServ) {
            min_serv_tmp = 1;
        }

    }

    close(epoll_fd);
    free(child_procs);

    return EXIT_SUCCESS;
}
