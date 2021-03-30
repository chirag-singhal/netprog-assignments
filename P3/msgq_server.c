#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

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

typedef struct _OLD_MSG {
    MSG msg[MAX_OLD_MSG];
    int start_ptr;
    size_t n_msg;
} OLD_MSG;

typedef struct _GROUP {
    char name[MAX_NAME_LEN];
    size_t n_members;
    char members[MAX_GROUP_SIZE][MAX_NAME_LEN];
    time_t join_time[MAX_GROUP_SIZE];
    OLD_MSG old_msg;
    time_t delete_time;
} GROUP;

size_t N_GROUPS;
GROUP ALL_GROUPS[MAX_NUM_GROUPS];

int get_old_msg(OLD_MSG * old_msg, size_t pos) {
    if (old_msg->n_msg == 0)
        return -1;
    
    return (old_msg->start_ptr + pos - 1) % MAX_OLD_MSG;
}

int add_old_msg(OLD_MSG * old_msg, MSG * msg) {
    if (old_msg->n_msg == MAX_OLD_MSG) {
        printf("Maximum number of old messages...\n");
        return -1;
    }
    
    if (old_msg->n_msg == 0) {
        old_msg->start_ptr = 0;
        old_msg->n_msg++;
        old_msg->msg[0] = *msg;
        return 0;
    }

    old_msg->msg[(old_msg->start_ptr + old_msg->n_msg)] = *msg;
    old_msg->n_msg++;
    return 0;
}

int get_queue_id(const char * username) {
    key_t id;
    if (strcmp(username, "server") == 0)
        id = 1234;
    else
        id = ftok(username, 'z');
print("QUEUE ID : %d\n", id);
    return msgget(id, IPC_CREAT|0666);
}

int join_group(char * groupname, char * username) {
    GROUP * grp = NULL;
    for (size_t grp_idx = 0; grp_idx < N_GROUPS; ++grp_idx) {
        if (strcmp(ALL_GROUPS[grp_idx].name, groupname) == 0) {
            grp = &ALL_GROUPS[grp_idx];
            break;
        }
    }
    if (grp == NULL) {
        printf("Group '%s' not found...\n", groupname);
        return -1;
    }

    if (grp->n_members < MAX_GROUP_SIZE) {
        bool member_exist = false;
        for (size_t i = 0; i < grp->n_members; ++i) {
            if (strcmp(grp->members[i], username) == 0) {
                member_exist = true;
                break;
            }
        }
        if (!member_exist) {
            // add user
            grp->join_time[grp->n_members] = time(NULL);
            strcpy(grp->members[grp->n_members++], username);

            // send old messages
            int id = get_queue_id(username);
            
            for (size_t i = 0; i < (grp->old_msg).n_msg; ++i) {
                if (grp->join_time[grp->join_time[grp->n_members-1]] <
                    (grp->old_msg).msg[get_old_msg(&(grp->old_msg), i)].time +
                    grp->delete_time) {
                        if (msgsnd(id, &(grp->old_msg).msg[get_old_msg(&(grp->old_msg), i)], sizeof(MSG), 0) < 0)
                            perror("Error in msgsnd...\n");
                    }
            }
            
            return grp->n_members;
        }
        else {
            printf("Member '%s' already present in group...\n", username);
            return -1;
        }
    }
    else {
        printf("Group '%s' full...\n", grp->name);
        return -1;
    }
}

int create_group(char * groupname, char * creator_name) {
    bool group_exist = false;
    for (size_t i = 0; i < N_GROUPS; ++i) {
        if (strcmp(ALL_GROUPS[i].name, groupname) == 0) {
            group_exist = true;
            break;
        }
    }
    if (!group_exist) {
        GROUP grp;
        memset(&grp, 0, sizeof(GROUP));
        grp.join_time[0] = time(NULL);
        strcpy(grp.name, groupname);
        grp.n_members = 1;
        strcpy(grp.members[0], creator_name);
        grp.old_msg.start_ptr = 0;
        grp.old_msg.n_msg = 0;
        ++N_GROUPS;
        ALL_GROUPS[N_GROUPS++] = grp;

        return 0;
    }
    return -1;
}

void list_group(char * username) {
    int id = get_queue_id(username);
    MSG msg = {0};
    
    strcpy(msg.body, "");
    for (size_t i = 0; i < N_GROUPS; ++i) {
        strcat(msg.body, ALL_GROUPS[i].name);
        strcat(msg.body, "\n");
    }
    
    msg.type = LIST_GROUP_MSG;
    
    if(msgsnd(id, &msg, sizeof(msg), 0) < 0)
        perror("Error in msgsnd...\n");
}

void send_private_msg(MSG * msg) {
    int id = get_queue_id(msg->receiver);
    if (msgsnd(id, msg, sizeof(MSG), 0) < 0)
        perror("Error in msgsnd...\n");
}

int send_group_msg(MSG * msg) {
    GROUP * grp = NULL;
    for (size_t grp_idx = 0; grp_idx < N_GROUPS; ++grp_idx) {
        if (strcmp(ALL_GROUPS[grp_idx].name, msg->group) == 0) {
            grp = &ALL_GROUPS[grp_idx];
            break;
        }
    }
    if (grp == NULL) {
        printf("Group '%s' not found...\n", msg->group);
        return -1;
    }

    for (size_t ii = 0; ii < grp->n_members; ++ii) {
        int id = get_queue_id(grp->members[ii]);
        if (msgsnd(id, msg, sizeof(MSG), 0) < 0)
            perror("Error in msgsnd...\n");
    }

    msg->time = time(NULL);
    add_old_msg(&(grp->old_msg), msg);
    return 0;
}

int set_delete_time(MSG * msg) {
    GROUP * grp = NULL;
    for (size_t grp_idx = 0; grp_idx < N_GROUPS; ++grp_idx) {
        if (strcmp(ALL_GROUPS[grp_idx].name, msg->group) == 0) {
            grp = &ALL_GROUPS[grp_idx];
            break;
        }
    }
    if (grp == NULL) {
        printf("Group '%s' not found...\n", msg->group);
        return -1;
    }

    grp->delete_time = msg->delete_time;

    return 0;
}

int main() {
    N_GROUPS = 0;
    
    MSG msg;

    int id = get_queue_id("server");

    while (true) {
        if (msgrcv(id, &msg, sizeof(msg), 0, 0) < 0)
            perror("Error in msgrcv...\n");

        if (fork() == 0) {
printf("body %s\n", msg.body);
            switch (msg.type) {
                case PRIVATE_MSG: {
                    send_private_msg(&msg);
                    break;
                }
                
                case GROUP_MSG: {
                    send_group_msg(&msg);
                    break;
                }

                case LIST_GROUP_MSG: {
                    list_group(msg.sender);
                    break;
                }

                case CREATE_GROUP_MSG: {
                    create_group(msg.group, msg.sender);
                    break;
                }

                case JOIN_GROUP_MSG: {
                    join_group(msg.group, msg.sender);
                    break;
                }

                case AUTO_DELETE_MSG: {
                    set_delete_time(&msg);
                    break;
                }
            }
        }
        else {
            wait(0);
        }
    }

}