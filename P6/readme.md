# P3 Group Communication among System

This exercise develops a P2P group communication among system which allows users to create groups, join groups, send group messages and receive messages, request files and create polls in groups. The group communication among system uses IP multicasting and broadcasting for the features. There is n central server to store data. 


# Usage

# Creating and Joining group

`create` command is used to create a group and `join-group` command is used to join a group. The user who creates the group is already a member of the group. `find-group` command can be used to search a group-name exists or not.

    create <group_name> <group_ip> <group_port>
    join-group <group_name> 
    find-group <group_name> 



# Sending messages

`send` command can be used to send group messages to all members of the group.

    send <group_name> <msg>

# Requesting a File

`request` is a command which allows user to search for a file if its available with the user himself and if its not available a request is made to all the users of all the groups user had joined and responses received within one minute are consolidated. The files are searched in a folder named `data` in the current working directory and should be made before running the code.

    request <file_name>

# List Files

`list-files` command can be used to print the local and remote files and can also be used to update the available list of files of the users of a particular group when group name is also provided in the command.

    list-files
    list-files <group_name>

# Creating Polls

`create-poll` can be used to create a poll in a group which can have a maximum of 10 options to choose from. Question and options must be enclosed in `""`.

    create-poll "<question>" <n_options> "<option1>" "<option2>" "<option3>"


# How to Run:

The following command is used to run on the machine. It compiles and runs the executable.
    
    make run

To exit the process you can press Ctrl + C. 