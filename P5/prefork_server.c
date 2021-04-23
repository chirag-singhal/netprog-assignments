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
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

#define min(_a, _b) (_a < _b) ? _a: _b
#define max(_a, _b) (_a > _b) ? _a: _b


int minSpareServ, maxSpareServ, maxReqPerChild;
int listen_fd;

enum serv_status {
    SS_INIT,
    SS_IDLE,
    SS_BUSY,
    SS_EXIT
};

struct ctrl_msg {
    int status;
};

void err_exit(const char * err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

void handle_conn(int ctrl_fd) {
    // Handle HTTP requests (maxReqPerChild)
    // Notify parent of every change (serv_status)
    struct ctrl_msg msg;

    // Send INIT status message to parent
    // INIT is same as IDLE, other than to notify the fork sucess
    msg.status = SS_INIT;
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

void create_serv() {
    // Create a non-blocking UNIX Domain socket and fork
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
        err_exit("Error in socketpair. Exiting...\n");
    int sock_flags = fcntl(sv[0], F_GETFL, 0);
    fcntl(sv[0], F_SETFL, sock_flags | O_NONBLOCK);
    sock_flags = fcntl(sv[1], F_GETFL, 0);
    fcntl(sv[1], F_SETFL, sock_flags | O_NONBLOCK);

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
    
}

void create_serv_expo(int n_serv) {
    // Exponentially till 32 and constant rate later
    int n_created = 0;
    
    for (int i = 0; n_created < n_serv; ++i) {
        int rate = 1 << min(i, 5);
        for (int ii = 0; ii < rate; ++ii, ++n_created) {
            create_serv();
        }
    }
}

int main(int argc, char * argv[]) {

    if (argc < 4)
        err_exit("One or more of the arguments 'MinSpareServers', 'MaxSpareServers' and 'MaxRequestsPerChild' is/are not specified! Exiting...\n");
    
    minSpareServ = atoi(argv[1]);
    maxSpareServ = atoi(argv[2]);
    maxReqPerChild = atoi(argv[3]);

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
    
    create_serv_expo(minSpareServ);

    return EXIT_SUCCESS;
}
