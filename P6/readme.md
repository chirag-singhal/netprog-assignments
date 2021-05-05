# P6 Group Communication among System

This exercise develops a P2P group communication system which allows users to create groups, join groups, send group messages and receive messages, request files and create polls in groups. The group communication among systems uses IP multicasting and broadcasting for the features. There is no central server to store data.

# Design

All the clients need to be connected to the same LAN but on different machines. The program upon start will bind its main communication socket to port 5000. This is the socket from which it receives broadcast communication.

When a client creates or joins a multicast group, an entry is made in the program and a socket is created by the program to receive packets specifically from that group.

The main sockets and the group multicast sockets are I/O multiplexed to make the program responsive. A thread is created with the sole purpose of sending its file list to all multicast groups. Epoll timeouts and alarm with sleep are used to wait for a fixed amount of time to accumulate results from various groups. Multicast loopback is turned off to prevent the client receiving its own packets.

Receiving a file is done using TCP connection. When a client wants to receive a file from the groups, it sends a multicast message to all groups containing a temporary socket. When the receiver has the requested file, it opens a TCP connection to that socket and transfers the file in chunks. The requester receives the file in chunks and saves it in the data directory.

# Usage

# Creating and Joining group

`create` command is used to create a group and the `join-group` command is used to join a group. The user who creates the group is already a member of the group. `find-group` command can be used to search if a group-name exists or not.

    create-group <group_name> <group_ip> <group_port>
    join-group <group_name> 
    find-group <group_name> 



# Sending messages

`send` command can be used to send group messages to all members of the group.

    send <group_name> <msg>

# Requesting a File

`request` is a command which allows the user to search for a file if it's available with the user himself and if it's not available a request is made to all the users of all the groups the user has joined and responses received within one minute are consolidated. The files are searched in a folder named `data` in the current working directory and should be made before running the code.

    request <file_name>

# List Files

`list-files` command can be used to print the local and remote files and can also be used to update the available list of files of the users of a particular group when group name is also provided in the command.

    list-files
    list-files <group_name>

# Creating Polls

`create-poll` can be used to create a poll in a group which can have a maximum of 10 options to choose from. Questions and options must be enclosed in `"  "`.

    create-poll <group_name> "<question>" <n_options> "<option1>" "<option2>" "<option3>"


# How to Run:

The following command is used to run on the machine. It compiles and runs the executable.
    
    make run

To exit the process you can press Ctrl + C. 