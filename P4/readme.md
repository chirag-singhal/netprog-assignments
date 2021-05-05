# P4 RTT

This exercise develops a program which reads a text file with one IP(v4/v6) address per line and calculates RTT values of these IPs and prints them in a line. This exercise made use of raw sockets and ICMP ECHO messages to calculate these values. This exercise uses epoll for I/O Multiplexing which is one of the fastest ways for I/O multiplexing. It sends three ICMP ECHO messages to each of the hosts and prints their RTT values only if all the messages are received back.

# Design

For each address in the input hosts file, the program automatically detects if it is IPv4 or IPv6 and calls the appropriate function which sends the ECHO requests. Firstly, for each address, a RAW socket with ICMP protocol is created and it is connected to the IP address using connect call. This pseudo-connection is done on the kernel level so that only packets from that particular IP are received on this socket file descriptor. If the number of open files exceeds the system limit (commonly, 1024), then the program sleeps for a second to release some file descriptors which successfully received all 3 replies. These newly freed file descriptors are used for pending IPs.

The program keeps waiting for replies which are yet to be received. So, to exit the program in case it is waiting for lost packets, press Ctrl+C, which will print the number of IPs pinged and exits the program.



# How to Run:

The following command compiles the executable.

    make compile

Superuser permissions are required because RAW sockets are used in the program.

    sudo ./rtt.o <file_name>

Alternatively, you can provide CAP_NEW_RAW capability to the program and run without sudo.

To exit the process you can press Ctrl + C. 
