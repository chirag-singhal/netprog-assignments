#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>

#define SENDER_NAME_MAX 30
#define MSG_SIZE_MAX 256
#define MAX_GROUP_SIZE 32;

typedef struct _MSG {
    long int msg_type;
    char sender[SENDER_NAME_MAX];
    char msg_body[MSG_SIZE_MAX];
    int time;
} MSG;

typedef struct _GROUP {
    char name[SENDER_NAME_MAX];
    char ip[INET_ADDRSTRLEN];
    int send_fd;
	struct sockaddr_in send_addr;
	int recv_fd;
	struct sockaddr_in recv_addr;
    char members[][INET_ADDRSTRLEN];
} GROUP;

int main() {

    

}