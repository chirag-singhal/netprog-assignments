# P1 Build your own Bash-like Shell

This exercise develops a Bash-like shell with support for chaining process via pipes. Shell also includes input-output redirections, foreground and background prcoesses and inlcudes some new functionalities like double piping(||), triple piping (|||) and shortcut mode(sc).

# Design

## Shell

The main shell process is a prompting process which shows the prompt and asks for user input.

On succesfully receiving the command, it passes the command to a newly created process which then parses it. This child process is made the leader of a newlhy created process group and all the child processes related to this command will be in this process group. The shell process checks if the command has a & at the end, if it is not present the terminal control is given to this process group (foreground process).

The child process then parses the command and handle all input and output redirections and maintains a structure for it.

# Usage

## Chaining commands and Input-Output Redirections

Chaining commands and input-output redirections work

## Double and Triple Piping

## Short-cut mode

# How to run
    make run_shell

# Assumptions


![design_1](../assets/p1_design_1.png)

![design_1](../assets/p1_design_2.png)
