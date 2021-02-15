# C Custom Shell
----

## Instructions to run:
* `make clean` to clean the current directory
* `make` to build source files
* `./myshell` to start the shell program

## Features:
* supports all Bash commands as the normal shell would, as well their respective flags. Eg. `ls -al`, `rm -rf`, etc.
* includes command string parsing so extra tabs, spaces, and newlines won't affect the interpretation of the command
* utilizes process forking, as well as I/O redirection to achieve features such as pipes, and I/O redirection commands such as `1>`, `2>`.
* supports multiple pipe chaining, so `|` commands can be chained together
* includes robust error checking to specific clear issues with commands
* `Ctrl+C` and `Ctrl+D` (`SIGINT` and `EOF`) interrupts are supported in the shell
* backgrounding processes is supported with `&`
* supports multi-command parsing with `;`. Eg. `ls -al; pwd`
