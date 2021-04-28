# P3 Group Chat Management System

This exercise develops a group chat system which allows users to create groups, list all groups on the server, join groups, send private and group messages and receive messages in online as well as offline mode. The chat system also implements an option which can be set for a group where users who join a group after `<t>` seconds from the time of message creation also receive it.

# Design

# Server

The server starts before all other clients and has its own message queue. The server coordinates between all the clients. The server receives the message from the queue and based on the type of message, it updates its internal state and sends responses to appropriate clients.


# Client

Every client is uniquely identified by their username which is entered as soon as the client program is launched. The client name cannot be `server`. The client has two threads running. One is a prompting thread which prompts and asks for user input. On receiving the command, it parses and sends the appropriate message to the server message queue. Another is a receiving message thread which reads messages from the client message queue sent by the server and then prints the messages on the terminal.


# Usage

# Creating and Joining group

`create` command is used to create a group and join command is used to join a group. The user who creates the group is already a member of the group.

    create <group_name> <group_ip> <group_port>
    join <group_name> 



# Sending messages

`send` command can be used to send group messages to all members of the group.

    send <group_name> <msg>

# Requesting a File

`request` is a command which allows user to search for a file if its available with the user himself and if its not available a request is made to all the users of all the groups user had joined and responses received within one minute are consolidated. The files are searched in a folder named `data` in the current working directory and should be made before running the code.

    request <file_name>

# List Files




# How to Run:

The following command is run on the machine. It compiles and runs the executable.
    
    make run_server

To exit the process you can press Ctrl + C, regardless of client or server. 