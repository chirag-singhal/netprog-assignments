#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netdb.h>
#include <errno.h>


#define NUM_MAX_EVENTS  10      // maximum triggered events received from epoll_wait
#define EPOLL_TIMEOUT   10000   // maximum timeout for epoll_wait in milliseconds


int n_complete;

struct host_info {
    bool valid;
    char    ip[41];
    bool    isIPv6;
    double   RTT[3]; // in milliseconds
    short int n_rtt_rcvd;
};

struct pthread_args {
    struct  host_info * hosts;
    size_t  num_hosts;
    int     epoll_fd;
};

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

int send_v4(char ip[41], int epoll_fd) {
    int sock_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    int sock_flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, sock_flags | O_NONBLOCK);

    int ttl = 32;
    if (setsockopt(sock_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == -1)
        err_exit("Error in setting TTL option. Exiting...\n");

    struct sockaddr_in cli_addr = {0};
    // ICMP has no ports
    cli_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &cli_addr.sin_addr) == 0)
        err_exit("Invalid IPv4 address. Exiting...\n");

    connect(sock_fd, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));
    
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) == -1) {
        close(epoll_fd);
        err_exit("Error in adding socket to epoll. Exiting...\n");
    }

    struct timespec send_time;

    size_t icmp_payload_len = sizeof(struct icmphdr) + sizeof(struct timespec);
    void * icmp_payload = malloc(icmp_payload_len);
    memset(icmp_payload, 0, icmp_payload_len);

    struct icmphdr * icmp = (struct icmphdr *) icmp_payload;
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = sock_fd;
    icmp->un.echo.sequence = 0;

    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp + 1, &send_time, sizeof(struct timespec)); // Set send time
    icmp->checksum = checksum(icmp_payload, icmp_payload_len);

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));

    icmp->un.echo.sequence = 1;
    icmp->checksum = 0;

    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp + 1, &send_time, sizeof(struct timespec)); // Set send time
    icmp->checksum = checksum(icmp_payload, icmp_payload_len);

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));

    icmp->un.echo.sequence = 2;
    icmp->checksum = 0;

    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp + 1, &send_time, sizeof(struct timespec)); // Set send time
    icmp->checksum = checksum(icmp_payload, icmp_payload_len);

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr *) &cli_addr, sizeof(struct sockaddr));

    free(icmp_payload);

    return sock_fd;
}

int send_v6(char ip[41], int epoll_fd) {
    int sock_fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    int sock_flags = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, sock_flags | O_NONBLOCK);

    struct sockaddr_in6 cli_addr = {0};
    // ICMP has no ports
    cli_addr.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, ip, &cli_addr.sin6_addr) == 0)
        err_exit("Invalid IPv6 address. Exiting...\n");
    
    connect(sock_fd, (struct sockaddr_in6 *) &cli_addr, sizeof(struct sockaddr_in6));
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = sock_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &event) == -1) {
        close(epoll_fd);
        err_exit("Error in adding socket to epoll. Exiting...\n");
    }

    struct timespec send_time;

    size_t icmp_payload_len = sizeof(struct icmp6_hdr) + sizeof(struct timespec);
    void * icmp_payload = malloc(icmp_payload_len);
    memset(icmp_payload, 0, icmp_payload_len);
    
    struct icmp6_hdr * icmp6 = (struct icmp6_hdr *) icmp_payload;
    icmp6->icmp6_type = ICMP6_ECHO_REQUEST;
    icmp6->icmp6_code = 0;
    icmp6->icmp6_id = sock_fd;
    icmp6->icmp6_seq = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp6 + 1, &send_time, sizeof(struct timespec)); // Set send time
    // icmp6->icmp6_cksum = htons(checksum(icmp_payload, icmp_payload_len));

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr_in6 *) &cli_addr, sizeof(struct sockaddr_in6));

    icmp6->icmp6_seq = 1;
    // icmp6->icmp6_cksum = htons(0);

    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp6 + 1, &send_time, sizeof(struct timespec)); // Set send time
    // icmp6->icmp6_cksum = htons(checksum(icmp_payload, icmp_payload_len));

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr_in6 *) &cli_addr, sizeof(struct sockaddr_in6));

    icmp6->icmp6_seq = 2;
    // icmp6->icmp6_cksum = htons(0);

    clock_gettime(CLOCK_MONOTONIC, &send_time);
    memcpy(icmp6 + 1, &send_time, sizeof(struct timespec)); // Set send time
    // icmp6->icmp6_cksum = htons(checksum(icmp_payload, icmp_payload_len));

    sendto(sock_fd, icmp_payload, icmp_payload_len, 0, (struct sockaddr_in6 *) &cli_addr, sizeof(struct sockaddr_in6));

    free(icmp_payload);

    return sock_fd;
}

void * receive_reply_func(void * args) {
    // Waiting for ICMP echo replies from all IPs using epoll

    // Copy pthread args
    struct host_info * hosts = ((struct pthread_args *)args)->hosts;
    size_t num_hosts = ((struct pthread_args *)args)->num_hosts;
    int epoll_fd = ((struct pthread_args *)args)->epoll_fd;

    struct epoll_event trig_events[NUM_MAX_EVENTS];

    struct timespec recv_time, send_time;
    size_t icmp_payload_len = sizeof(struct timespec) +
                                ((sizeof(struct icmphdr) > sizeof(struct icmp6_hdr)) ? sizeof(struct icmphdr) : sizeof(struct icmp6_hdr));
    void * ip_icmp_payload = malloc(sizeof(struct iphdr) + icmp_payload_len);
    void * icmp_payload;

    bool run_flag = true;
    while (run_flag) {
        // Create a new thread to handle searching and updating the hosts data
        int event_count = epoll_wait(epoll_fd, trig_events, NUM_MAX_EVENTS, 10000);
        clock_gettime(CLOCK_MONOTONIC, &recv_time); // get receive time
        
        if (event_count == 0) {
            printf("Maximum epoll wait timeout reached!\n");
            break;
        }
        else if (event_count == -1)
            err_exit("Error in epoll wait. Exiting...\n");
        
        for (int i = 0; i < event_count; ++i) {
            struct epoll_event * event = &(trig_events[i]);
            if (!hosts[event->data.fd].isIPv6) {
                // Receive IPv4 ICMP echo reply
                recvfrom(event->data.fd, ip_icmp_payload, sizeof(struct iphdr) + icmp_payload_len, 0, NULL, NULL);
                
                icmp_payload = ((struct iphdr *) ip_icmp_payload) + 1;
                if (((struct icmphdr *) icmp_payload)->type == ICMP_ECHOREPLY) {
                    short int rtt_idx = ((struct icmphdr *) icmp_payload)->un.echo.sequence;
                    send_time = *((struct timespec *) (((struct icmphdr *) icmp_payload)+1));

                    // Time difference for RTT in milliseconds
                    double rtt = ((double)(recv_time.tv_sec - send_time.tv_sec)*1000.0) + (double)(recv_time.tv_nsec - send_time.tv_nsec)/1000000.0;
                    hosts[event->data.fd].RTT[rtt_idx] = rtt;
                    if (++hosts[event->data.fd].n_rtt_rcvd == 3) {
                        printf("%-41s - %.2f %.2f %.2f\t\t(ms)\n", hosts[event->data.fd].ip,
                            hosts[event->data.fd].RTT[0],
                            hosts[event->data.fd].RTT[1],
                            hosts[event->data.fd].RTT[2]);
                        close(event->data.fd);
                        ++n_complete;
                        if (n_complete == num_hosts)
                            run_flag = false;
                    }
                }
            }
            else {
                // Receive IPv6 ICMP echo reply
                recvfrom(event->data.fd, ip_icmp_payload, icmp_payload_len, 0, NULL, NULL);
                
                icmp_payload = ip_icmp_payload;
                if (((struct icmp6_hdr *) icmp_payload)->icmp6_type == ICMP6_ECHO_REPLY) {
                    short int rtt_idx = ((struct icmp6_hdr *) icmp_payload)->icmp6_seq;
                    send_time = *((struct timespec *) (((struct icmp6_hdr *) icmp_payload)+1));

                    // Time difference for RTT in milliseconds
                    double rtt = ((double)(recv_time.tv_sec - send_time.tv_sec)*1000.0) + (double)(recv_time.tv_nsec - send_time.tv_nsec)/1000000.0;
                    hosts[event->data.fd].RTT[rtt_idx] = rtt;
                    if (++hosts[event->data.fd].n_rtt_rcvd == 3) {
                        printf("%-41s - %.2f %.2f %.2f\t\t(ms)\n", hosts[event->data.fd].ip,
                            hosts[event->data.fd].RTT[0],
                            hosts[event->data.fd].RTT[1],
                            hosts[event->data.fd].RTT[2]);
                        close(event->data.fd);
                        ++n_complete;
                        if (n_complete == num_hosts)
                            run_flag = false;
                    }
                }
            }
        }
    }
    
    free(ip_icmp_payload);

    // Output of thread. In this case, there is nothing to return
    return NULL;
}

int main(int argc, char * argv[]) {

    if (argc < 2)
        err_exit("HOSTS IP Filename argument not specified! Exiting...\n");

    // Assumption: All the hosts in the file are unique
    size_t num_hosts = 0;
    n_complete = 0;
    
    FILE * hosts_fp;
    if ((hosts_fp = fopen(argv[1], "r")) == NULL)
        err_exit("Error opening HOSTS FILE. Exiting...\n");

    int _c;
    while (EOF != (_c=fgetc(hosts_fp)))
        if (_c == '\n')
            ++num_hosts;
    fseek(hosts_fp, 0, SEEK_SET);

    // 'sock_fd' is directly used as index for the hosts array of structures
    // +10 is a safety limit to ensure that every 'sock_fd' is < '(num_hosts + 10)'
    // because 0 and 1 are used up by stdin and stdout
    // So, safety limit has to be atleast +2
    struct host_info * hosts = malloc(sizeof(struct host_info) * (num_hosts + 10));
    memset(hosts, 0, sizeof(struct host_info) * num_hosts);

    // size_t num_forks = 0;
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        err_exit("Error in epoll. Exiting...\n");

    pthread_t thread_id;
    struct pthread_args args = {hosts, num_hosts, epoll_fd};
    pthread_create(&thread_id, NULL, receive_reply_func, &args);
    
    // Sending ICMP echo requests to all IPs
    char ip[41];
    for (size_t idx = 0; idx < num_hosts; ++idx) {
        fgets(ip, 41, hosts_fp);
        if (ip[strlen(ip) - 1] == '\n')
            ip[strlen(ip) - 1] = '\0';

        int sock_fd;
        // Differentiate between ipv4 and ipv6
        if (strstr(ip, ".")) {
            // ipv4 address
            sock_fd = send_v4(ip, epoll_fd);
            hosts[sock_fd].isIPv6 = 0;
        }
        else {
            // ipv6 address
            sock_fd = send_v6(ip, epoll_fd);
            hosts[sock_fd].isIPv6 = 1;
        }
        hosts[sock_fd].valid = true;
        strncpy(hosts[sock_fd].ip, ip, 41);
        hosts[sock_fd].n_rtt_rcvd = 0;
    }

    pthread_join(thread_id, NULL);

    close(epoll_fd);
    free(hosts);

    return EXIT_SUCCESS;
}
