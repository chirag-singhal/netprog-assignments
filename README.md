# Network Programming Assignments


Assignments are done as a requirement of the Networking Programming Course (IS F462) offered at BITS Pilani.

# [P1 Build your own Bash-like Shell](./P1/)

This exercise develops a Bash-like shell with support for chaining process via pipes. Shell also includes input-output redirections, foreground and background prcoesses and inlcudes some new functionalities like double piping, triple piping and shortcut mode.

# [P2 Cluster Shell](./P2/)

This exercise develops a shell which extends to a cluster of machines operating over the network. There is a central server which coordinates the operations and there are multiple clients/nodes which perform the bash commands and communicate to other nodes via server.

# [P3 Group Chat Management System](./P3/)

This exercise develops a group chat system which allows users to create groups, list all groups on the server, join groups, send private and group messages and receive messages in online as well as offline mode. The chat system also implements an option which can be set for a group where users who join a group after `<t>` seconds from the time of message creation also receive it.

# [P4 RTT](./P4/)

This exercise develops a program which reads a text file with one IP(v4/v6) address per line and calculates RTT values of these IPs and prints them in a line. This exercise made use of raw sockets and ICMP ECHO messages to calculate these values. This exercise uses epoll for I/O Multiplexing which is one of the fastest ways for I/O multiplexing. It sends three ICMP ECHO messages to each of the hosts and prints their RTT values only if all the messages are received back.

# [P5 Pre-Fork Server](./P5/)

This exercise develops a pre-forking model for a web server which maintains a process pool to handle client requests. Server binds to a port and creates child processes. Each child process of the server acts as a server and accepts a connection request and sleeps for one second then sends a dummy reply. Server maintains a minimum number of idle child processes minSpareServers and checks that at any point its not more than maxSpareServers. Each child handles a fixed number of requests before it is killed and a new child is created, called recycling.

# [P6 Group Communication among System](./P6/)

This exercise develops a P2P group communication system which allows users to create groups, join groups, send group messages and receive messages, request files and create polls in groups. The group communication among systems uses IP multicasting and broadcasting for the features. There is no central server to store data.

# License

This project is MIT licensed, as found in the [LICENSE](./LICENSE) file.
