# P1 RTT

This exercise develops a program which reads a text file with one IP(v4/v6) address per line and calulates RTT values of these IPs and print them in a line. This exercise made use of raw sockets and ICMP ECHO messages to calculate these values. This exercise uses epoll for I/O Multiplexing which is one of the fastest ways for I/O multiplexing. It sends three ICMP ECHO messages to each of the hosts and prints their RTT values onlu if all the messages are received back.


# How to Run:

The following command is used to run on the machine. It compiles and runs the executable.
    
    make run <file_name>

To exit the process you can press Ctrl + C. 
