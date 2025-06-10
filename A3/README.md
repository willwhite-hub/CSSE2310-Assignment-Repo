# CSSE2310 - Assignment 3
The uqparallel program will allow a user to run a series of tasks in parallel – with the task commands and/or 
their arguments coming from the command line, a specified file or from stdin. uqparallel will also be able 
to form a pipeline of those commands (the output of one becomes the input of the next, etc.) and limit the 
number of processes running in parallel. The tasks may involve the same command (specified on the command 
line) with different arguments (coming from the command line, stdin, or a specified file); or the tasks may each 
involve different commands.
