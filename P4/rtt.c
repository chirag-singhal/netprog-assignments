#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netdb.h>
#include <errno.h>

// #define MAX_THREAD_LIMIT  2000
#define HOSTS_FILE      "host.txt"


void err_exit(const char * err_msg) {
    perror(err_msg);
    exit(EXIT_FAILURE);
}

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum=0;
    unsigned short result;
    
    // 16 bit words... sizeof(unsigned short) = 2UL
    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

void send_v4(char ip[41], int epoll_fd) {
    int sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    int sock_flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, sock_flags | O_NONBLOCK);

    struct sockaddr_in cli_addr = {0};
    // ICMP has no ports
    cli_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &cli_addr.sin_addr) == 0)
        err_exit("Invalid IPv4 address. Exiting...\n");

    struct timespec send_time;

    size_t icmp_data_len = sizeof(struct icmphdr) + sizeof(struct timespec);
    void * icmp_data = malloc(icmp_data_len);
    memset(icmp_data, 0, icmp_data_len);

    struct icmphdr * icmp = (struct icmphdr *) icmp_data;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = getpid();
    icmp->un.echo.sequence = 0;

    clock_gettime(CLOCK_REALTIME, &send_time);
    memcpy(icmp + 1, &send_time, sizeof(struct timespec)); // Set send time
    icmp->checksum = checksum((void *) &icmp_data, icmp_data_len);

    sendto(sock_fd, icmp_data, icmp_data_len, 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
}

void send_v6(char ip[41], int epoll_fd) {
    struct sockaddr_in6 cli_addr = {0};
    // ICMP has no ports
    cli_addr.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, ip, &cli_addr.sin6_addr) == 0)
        err_exit("Invalid IPv4 address. Exiting...\n");
    
    struct icmp6_hdr icmp6 = {0};
    icmp6.icmp6_type = ICMP6_ECHO_REQUEST;
    icmp6.icmp6_code = 0;
    icmp6.icmp6_dataun.icmp6_un_data16[0] = getpid();
    icmp6.icmp6_dataun.icmp6_un_data16[1] = 0;
    icmp6.icmp6_cksum = checksum((void *)&icmp6, sizeof(icmp6));
}

int main() {

    FILE * hosts_fp;
    if ((hosts_fp = fopen(HOSTS_FILE, "r")) == NULL)
        err_exit("Error opening HOSTS FILE. Exiting...\n");

    // size_t num_forks = 0;
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        err_exit("Error in epoll. Exiting...\n");
    
    pid_t pid_receiver = fork();

    if (pid_receiver > 0) {
        // Sending ICMP echo requests to all IPs
        char ip[41];
        while (fgets(ip, 41, hosts_fp)) {
            if (ip[strlen(ip) - 1] == '\n')
                ip[strlen(ip) - 1] = '\0';

            // Differentiate between ipv4 and ipv6
            if (strstr(ip, ".")) {
                // ipv4 address
                send_v4(ip, epoll_fd);
            }
            else {
                // ipv6 address
                send_v6(ip, epoll_fd);
            }

        }
    }
    else if (pid_receiver == 0) {
        // Waiting for ICMP echo replies from all IPs using epoll (and threads?)
    }
    else err_exit("Error in fork. Exiting...\n");

    close(epoll_fd);

    return EXIT_SUCCESS;
}
