#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#define MAX_CMD_LEN 50
#define MAX_NAME_LEN 30
#define MAX_GROUP_SIZE 32
#define MAX_NUM_GROUPS 64
#define MAX_OLD_MSG 128
#define MAX_MSG_SIZE 2048

typedef enum _MSG_TYPE {
    PRIVATE_MSG,
    GROUP_MSG,
    CREATE_GROUP_MSG,
    LIST_GROUP_MSG,
    JOIN_GROUP_MSG,
    AUTO_DELETE_MSG
} MSG_TYPE;

typedef struct _MSG {
    MSG_TYPE type;
    char sender[MAX_NAME_LEN];
    char receiver[MAX_NAME_LEN];
    char body[MAX_MSG_SIZE];
    char group[MAX_NAME_LEN];
    time_t time;
    time_t delete_time;
} MSG;

int msg_id;
char* user_name;
pthread_mutex_t lock;
int is_rcvd_mssg = 0;


void prompt() {
    printf("[%s] ", user_name);
    fflush(stdout);
}

void err_exit(const char *err_msg) {
    printf("%s\n", err_msg);
    exit(EXIT_FAILURE);
}

void send_mssg(MSG* msg) {
    if(msgsnd(msg_id, msg, sizeof(MSG), 0) < 0)
        err_exit("Error sending message to server. Exiting...");
}

unsigned long hash(unsigned char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

void* rcv_mssg() {
    
    MSG* msg = malloc(sizeof(MSG));

    // key_t key_client = ftok(user_name, 'z');
    key_t key_client = hash(strdup(user_name));
    int msg_id_client = msgget(key_client, 0644 | IPC_CREAT);

    if(msg_id < 0) {
        err_exit("Error while creating message queue. Exiting...");
    }

    while(true) {
        pthread_mutex_lock(&lock);

        if(msgrcv(msg_id_client, msg, sizeof(MSG), 0, IPC_NOWAIT) < 0) {
            //unlock and sleep
            pthread_mutex_unlock(&lock);
            sleep(0.2);
            continue;
        }
        if(msg -> type == GROUP_MSG) {
            printf("\n[group][%s][%s] %s\n", msg -> group, msg -> sender, msg -> body);
        }
        else if(msg -> type == PRIVATE_MSG) {
            printf("\n[pvt][%s] %s\n", msg -> sender, msg -> body);
        }
        else if(msg -> type == LIST_GROUP_MSG) {
            printf("***************\n");
            printf("Available groups to join\n");
            printf("%s\n", msg -> body);
            printf("***************\n");
            is_rcvd_mssg = 1;
        }
        prompt();

        pthread_mutex_unlock(&lock);
    }
    msgctl(msg_id_client, IPC_RMID, 0);
}

void create_group(char* group_name) {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = CREATE_GROUP_MSG;
    strcpy(msg -> sender, user_name);
    strcpy(msg -> group, group_name);
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
}

void join_group(char *group_name) {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = JOIN_GROUP_MSG;
    strcpy(msg -> sender, user_name);
    strcpy(msg -> group, group_name);
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
}

void list_groups() {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = LIST_GROUP_MSG;
    strcpy(msg -> sender, user_name);
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
    while(!is_rcvd_mssg) {
        sleep(0.2);
    }
    is_rcvd_mssg = 0;
}

void send_group_mssg(char* group_name, char* mssg) {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = GROUP_MSG;
    strcpy(msg -> sender, user_name);
    strcpy(msg -> body, mssg);
    strcpy(msg -> group, group_name);
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
}

void send_priv_mssg(char* rcvr_name, char* mssg) {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = PRIVATE_MSG;
    strcpy(msg -> sender, user_name);
    strcpy(msg -> receiver, rcvr_name);
    strcpy(msg -> body, mssg);
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
}

void set_auto_delete(char* group_name, int time_sec) {
    MSG* msg = malloc(sizeof(MSG));
    msg -> type = AUTO_DELETE_MSG;
    strcpy(msg -> sender, user_name);
    strcpy(msg -> group, group_name);
    msg -> delete_time = time_sec;
    send_mssg(msg);
    pthread_mutex_unlock(&lock);
}

int main() {

    // key_t key = ftok("server", 'z');
    key_t key = hash("server");
    msg_id = msgget(key, 0666 | IPC_CREAT);

    if(msg_id < 0) {
        err_exit("Error while creating message queue. Exiting...");
    }

    user_name = malloc(sizeof(char) * MAX_NAME_LEN);
    printf("Enter alias name : ");
    size_t max_name_len = MAX_NAME_LEN;
    size_t alias_len = getline(&user_name, &max_name_len, stdin);
    user_name[alias_len - 1] = '\0';

    if (pthread_mutex_init(&lock, NULL) != 0) {
        err_exit("\n Error Mutex init. Exiting...\n");
    }
    
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, rcv_mssg, NULL);
    
    while(true) {

        bool is_error = false;
        
        prompt();

        char * cmd = malloc(sizeof(char) * (MAX_CMD_LEN + 1));
        size_t max_cmd_len = MAX_CMD_LEN;
        size_t cmd_len = getline(&cmd, &max_cmd_len, stdin);
        if(cmd_len == 0 || cmd[0] == '\n')
            continue;
        pthread_mutex_lock(&lock);
        cmd[cmd_len - 1] = '\0';
        cmd_len = strlen(cmd);

        char * tmp_cmd = strdup(cmd);
        char* saved_ptr;
        char* token = strtok_r(tmp_cmd, " ", &saved_ptr);
        
        if(strcmp(token, "create") == 0) {
            //create group
            token = strtok_r(NULL, " ", &saved_ptr);
            if(token == NULL) 
                is_error = true;
            create_group(token);
        }
        else if(strcmp(token, "join") == 0) {
            //join group
            token = strtok_r(NULL, " ", &saved_ptr);
            if(token == NULL) 
                is_error = true;
            join_group(token);
        }
        else if(strcmp(token, "list") == 0) {
            //list groups
            list_groups();
        }
        else if(strcmp(token, "send") == 0) {
            //send -p (private) -g (group)
            token = strtok_r(NULL, " ", &saved_ptr);
            if(token == NULL) 
                is_error = true;
            if(strcmp(token, "-p") == 0) {
                //private mssg send
                token = strtok_r(NULL, " ", &saved_ptr);
                if(token == NULL) 
                    is_error = true;
                char *mssg = token + strlen(token) + 1;
                if(*mssg == '\0') 
                    is_error = true;
                send_priv_mssg(token, mssg);
            }
            else if(strcmp(token, "-g") == 0) {
                //group mssg send
                token = strtok_r(NULL, " ", &saved_ptr);
                if(token == NULL) 
                    is_error = true;
                char *mssg = token + strlen(token) + 1;
                if(*mssg == '\0') 
                    is_error = true;
                send_group_mssg(token, mssg);
            }
            else {
                //invalid command
                err_exit("Error: invalid command. Exiting...");
            }
        }
        else if(strcmp(token, "auto") == 0) {
            //auto delete
            token = strtok_r(NULL, " ", &saved_ptr);
            if(token == NULL) 
                is_error = true;
            if(strcmp(token, "delete") == 0) {
                //private mssg send
                char* group_name = strtok_r(NULL, " ", &saved_ptr);
                if(group_name == NULL) 
                    is_error = true;
                token = strtok_r(NULL, " ", &saved_ptr);
                if(token == NULL) 
                    is_error = true;
                int time_sec = atoi(token);
                set_auto_delete(group_name, time_sec);
            }
        }
        else {
            //invalid command
            err_exit("Error: invalid command. Exiting...");
        }
        if(is_error) {
            //invalid command
            err_exit("Error: invalid command. Exiting...");
        }

    }
    pthread_join(thread_id, NULL);
    return EXIT_SUCCESS;
}