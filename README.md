# README: Task 1 
## Introduction
This task is about implementation of a shell program named "myshell".

to compile and use the programs you can compile: myshell
- can also use "make all" ,"make clean".
- this program was made in unix based(debian)operation system.

### Basic Shell:
This part have the implementation of a shell program named "myshell". It has the following features:

Ability to run CMD tools that exist on the system (by fork + exec + wait)
Ability to stop running tool by pressing Ctrl+c, but not killing the shell itself (by signal handler)
Ability to redirect output with ">" and ">>" and allow piping with "|", at least for 2 following pipes.
Ability to stop itself by "exit" command.

### Usage:

1. make myshell
2. ./myshell
