#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// store jump mark
sigjmp_buf mark;

// command struct to store
// individual cmds and their
// arguments
struct command
{
    char **cmd;  // list of arguments
    int numArgs; // number of total arguments in line
};

// on sigint kill stdin buffer and show prompt
void sigintHandler(int sig_num)
{
    // Reset handler to catch SIGINT next time.
    signal(SIGINT, sigintHandler);
    // jump back to main to avoid fgets
    siglongjmp(mark, 1);
}

// wait for and read from stdin
char *get_args()
{
    char *args = calloc(sizeof(char), 2048);
    char *result = fgets(args, 2047, stdin);
    // terminate on end of file (ctrl-d)
    if (!result)
        exit(EXIT_FAILURE);
    char *pter = strrchr(args, '\n');
    if (pter)
        *pter = 0;
    return args;
}

// parse whiteSpace in command string, return array of command structs
struct command *parseW(char *cmd)
{
    // duplicate input command to keep it unchanged
    char *tempCmd = strndup(cmd, strlen(cmd));

    // savePter for strtok_r
    char *savePter;

    // auxilliary ints
    int i = 0, size = 0, numCmds = 0, wasAmp = 0;

    // get the first whitespace token from cmd
    char *token = strtok_r(tempCmd, " \t", &savePter);

    // loop through and COUNT number of arguments (to malloc return obj)
    while (token)
    {
        // if not operator, add strlen
        if (strcmp(token, "|") && strcmp(token, ">") && strcmp(token, "<") && strcmp(token, "1>") &&
            strcmp(token, "2>") && strcmp(token, "&>") && strcmp(token, "&"))
        {
        }
        else // is special operator
        {
            numCmds += 2;
        }
        i++;
        token = strtok_r(NULL, " \t", &savePter);
    }

    numCmds++; // increment bc of initial cmd

    // initializing commands list (return object)
    struct command *commands = calloc(numCmds + 1, sizeof(struct command));
    strcpy(tempCmd, cmd); // copy over the cmd again
    savePter = NULL;      // reset savePter for strtok_r
    i = 0;

    // loop through and ALLOCATE space for the commands
    token = strtok_r(tempCmd, " \t", &savePter);
    while (token)
    {
        // error check invalid pipes
        if (!strcmp(token, "|") && i == 0 && size == 0)
        {
            printf("parseW: invalid pipe parsed\n");
            return NULL;
        }

        // if not operator, add to size of cmd
        if (strcmp(token, "|") && strcmp(token, ">") && strcmp(token, "<") && strcmp(token, "1>") &&
            strcmp(token, "2>") && strcmp(token, "&>") && strcmp(token, "&"))
        {
            size += strlen(token);
        }
        else // is special operator
        {
            if (size == 0)
            {
                printf("parseW: invalid syntax, unexpected operator or token '%s'\n", token);
                return NULL;
            }
            else
            {
                if (!strcmp(token, "&"))
                    wasAmp = 1;

                // add all prev commands size
                commands[i].numArgs = numCmds;
                commands[i++].cmd = calloc(size + 1, sizeof(char *));

                // add special operator
                commands[i].numArgs = numCmds;
                commands[i++].cmd = calloc(strlen(token) + 1, sizeof(char *));
                size = 0;
            }
        }

        token = strtok_r(NULL, " \t", &savePter);
    }

    // if last token parsed was a special character other than &, invalid
    if (!size && !wasAmp)
    {
        printf("parseW: invalid pipe parsed\n");
        return NULL;
    }

    commands[i].numArgs = numCmds;                      // set the number of arguments
    commands[i].cmd = calloc(size + 1, sizeof(char *)); // add final command allocation

    strcpy(tempCmd, cmd); // recopy the input
    savePter = NULL;      // reset savePter for strtok_r
    i = 0;                // cmd index
    int z = 0;            // cmd argument index

    // this while loop SETS each struct command's cmd property to their
    // command and their arguments so we can use execvp
    token = strtok_r(tempCmd, " \t", &savePter);
    while (token)
    {
        // if not operator, add token
        if (strcmp(token, "|") && strcmp(token, ">") && strcmp(token, "<") && strcmp(token, "1>") &&
            strcmp(token, "2>") && strcmp(token, "&>") && strcmp(token, "&"))
        {
            commands[i].cmd[z++] = token;
        }
        else // is special operator, reset arg index, inc cmd index
        {
            z = 0;
            commands[++i].cmd[0] = token;
            i++;
        }
        token = strtok_r(NULL, " \t", &savePter);
    }

    return commands;
}

// runs non pipe/redirection commands
pid_t runSimple(struct command *args)
{
    pid_t pid;
    int i = 0;
    int fd;            // store file descriptors
    int pipefds[2];    // store pipe fds
    if (pipe(pipefds)) // pipe
    {
        perror("runSimple error: failed to create pipe for IPC");
        exit(EXIT_FAILURE);
    }

    pid = fork(); // store child pid
    if (pid < 0)  // error
    {
        fprintf(stderr, "An error occured while forking child for runSimple.\n");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) // child
    {
        // child doesn't need read end of pipe
        close(pipefds[0]);

        i = 0;
        // loop and deal with special operators
        while (args[i].cmd)
        {
            // deal with &
            if (!strcmp(args[i].cmd[0], "&"))
            {
                // write to pipe to let parent know
                // there was &
                write(pipefds[1], "amp", 4);
                break;
            }

            // deal with &>
            if (!strcmp(args[i].cmd[0], "&>"))
            {
                // create the output file
                fd = creat(args[i + 1].cmd[0], 0644);
                if (fd < 0)
                {
                    perror("open");
                    exit(EXIT_FAILURE);
                }

                // redirect STDOUT to that file
                if (dup2(fd, STDOUT_FILENO) < 0)
                {
                    perror("runSimple error: failed dup2 for stdout on &>");
                    exit(EXIT_FAILURE);
                }

                // redirect STDERR to STDOUT, which is redirected to that file
                if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
                {
                    perror("runSimple: failed dup2 for stderr on &>");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            }

            // deal with 2>
            if (!strcmp(args[i].cmd[0], "2>"))
            {
                // create the output file
                fd = creat(args[i + 1].cmd[0], 0644);
                if (fd < 0)
                {
                    perror("runSimple: error opening file for 2>");
                    exit(EXIT_FAILURE);
                }

                // redirect STDERR to that file
                if (dup2(fd, STDERR_FILENO) < 0)
                {
                    perror("runSimple: failed dup2 on stderr redirecting on 2>");
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }

            // deal with > and 1>
            if ((!strcmp(args[i].cmd[0], ">") || !strcmp(args[i].cmd[0], "1>")))
            {
                fd = creat(args[i + 1].cmd[0], 0644);
                if (fd < 0)
                {
                    perror("runSimple: error opening argument for >");
                    exit(EXIT_FAILURE);
                }

                // make child's stdout into file
                if (dup2(fd, STDOUT_FILENO) < 0)
                {
                    perror("runSimple: failure dup2 on redirecting STDOUT to fd on > or 1>");
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }

            // deal with <, making sure to check if args[i] exists first
            if (!strcmp(args[i].cmd[0], "<"))
            {
                // open input file
                fd = open(args[i + 1].cmd[0], O_RDONLY);
                if (fd < 0)
                {
                    perror("runSimple: error opening argument for <");
                    exit(EXIT_FAILURE);
                }

                // redirect STDIN to that file
                if (dup2(fd, STDIN_FILENO) < 0)
                {
                    perror("runSimple: error dup2 on redirecting stdin to fd on <");
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }

            i++;
        }

        // exec input with appropriate fd
        execvp(args[0].cmd[0], args[0].cmd);

        // at this pt, we error check
        fprintf(stderr, "runSimple error %s: ", args[0].cmd[0]);
        perror("");
        exit(EXIT_FAILURE);
    }

    // parent closes write end of pipe
    close(pipefds[1]);
    char buffer[4];
    // read what child sent
    if (read(pipefds[0], buffer, 4) < 0)
    {
        perror("runSimple error: failed to read from pipe for IPC");
        exit(EXIT_FAILURE);
    }

    // if there was &, then return 0
    // so we won't waitpid it
    if (!strcmp(buffer, "amp"))
        return 0;

    // return pid if no & so we can waitpid it
    return pid;
}

// runPipedCommands runs commands with at least one pipe in it
// INPUT: a string args, which is already semicolon separated
// OUTPUT: void, but runs all commands and closes pipes
void runPipedCommands(char *args)
{
    int status;      // status for waitpid
    pid_t pid;       // pid for fork
    int cmdsRun = 0; // checks how many commands ran
    int newfds[2];   // stores the new or current fds
    int oldfds[2];   // stores the old fd's read end

    char *nextCmd;        // string that stores next command to run
    char *prevCmd = NULL; // string that stores prev command
    char *cmdToken;       // stores current command token
    char *cmdSavePter;    // required for strtok_r

    // split tokens of command by |
    cmdToken = strtok_r(args, "|", &cmdSavePter);

    // loop through command chunks
    while (cmdToken)
    {
        nextCmd = strtok_r(NULL, "|", &cmdSavePter);
        struct command *commands = parseW(cmdToken);

        // if next command make new pipes
        if (nextCmd)
            pipe(newfds);

        // fork
        pid = fork();
        if (pid == 0) // child
        {
            // if there was prev cmd
            if (prevCmd)
            {
                dup2(oldfds[0], STDIN_FILENO);
                close(oldfds[0]);
                close(oldfds[1]);
            }

            // if next cmd
            if (nextCmd)
            {
                close(newfds[0]);
                dup2(newfds[1], STDOUT_FILENO);
                close(newfds[1]);
            }

            // code to deal with operators
            int i = 0;
            int fd;
            while (commands[i].cmd)
            {
                // deal with &>
                if (!strcmp(commands[i].cmd[0], "&>"))
                {
                    // grab the output file
                    fd = creat(commands[i + 1].cmd[0], 0644);
                    if (fd < 0)
                    {
                        perror("open");
                        exit(EXIT_FAILURE);
                    }

                    // redirect stdout to the file
                    if (dup2(fd, STDOUT_FILENO) < 0)
                    {
                        perror("runPipedCommands: failed dup2 for stdout on &>");
                        exit(EXIT_FAILURE);
                    }

                    // redirect stderr to stdout (which is redirected to fd)
                    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
                    {
                        perror("runPipedCommands: failed dup2 for stderr on &>");
                        exit(EXIT_FAILURE);
                    }

                    close(fd);
                }

                // deal with 2>
                if (!strcmp(commands[i].cmd[0], "2>"))
                {
                    // open output file
                    fd = creat(commands[i + 1].cmd[0], 0644);
                    if (fd < 0)
                    {
                        perror("runPipedCommands: error opening file for 2>");
                        exit(EXIT_FAILURE);
                    }

                    // redirect stderr to that file
                    if (dup2(fd, STDERR_FILENO) < 0)
                    {
                        perror("runPipedCommands: failed dup2 on stderr redirecting on 2>");
                        exit(EXIT_FAILURE);
                    }

                    close(fd);
                }

                // deal with > and 1>
                if (!strcmp(commands[i].cmd[0], ">") || !strcmp(commands[i].cmd[0], "1>"))
                {
                    // create output file
                    fd = creat(commands[i + 1].cmd[0], 0644);
                    if (fd < 0)
                    {
                        perror("runPipedCommands: error opening argument for >");
                        exit(EXIT_FAILURE);
                    }

                    // redirect stdout to that file
                    if (dup2(fd, STDOUT_FILENO) < 0)
                    {
                        perror("runPipedCommands: failure dup2 on redirecting STDOUT to fd on > or 1>");
                        exit(EXIT_FAILURE);
                    }

                    close(fd);
                }

                // deal with <
                if (!strcmp(commands[i].cmd[0], "<"))
                {
                    // open input file
                    fd = open(commands[i + 1].cmd[0], O_RDONLY);
                    if (fd < 0)
                    {
                        perror("runPipedCommands: error opening argument for <");
                        exit(EXIT_FAILURE);
                    }

                    // redirect stdin to that file
                    if (dup2(fd, STDIN_FILENO) < 0)
                    {
                        perror("runPipedCommands: error dup2 on redirecting stdin to fd on <");
                        exit(EXIT_FAILURE);
                    }

                    close(fd);
                }

                i++;
            }

            // execvp the command, all fds should be set
            execvp(commands[0].cmd[0], commands[0].cmd);

            // at this pt, there has been an error
            fprintf(stderr, "runPipedCommands error %s: ", commands[0].cmd[0]);
            perror("");
            exit(EXIT_FAILURE);
        }
        else if (pid < 0) // FORK ERROR
        {
            perror("error");
            exit(EXIT_FAILURE);
        }

        // PARENT

        // if there was a prev command
        if (prevCmd)
        {
            close(oldfds[0]);
            close(oldfds[1]);
        }

        // if there is next command,
        // copy over the current fds
        if (nextCmd)
        {
            oldfds[0] = newfds[0];
            oldfds[1] = newfds[1];
        }

        // set next command to run, and
        // set current command as previous
        prevCmd = cmdToken;
        cmdToken = nextCmd;
        cmdsRun++;

        // do all the stuff above and wait for child to finish
        waitpid(pid, &status, 0);
    }

    // parent: if multiple cmds, close old fds
    if (cmdsRun > 1)
    {
        close(oldfds[0]);
        close(oldfds[1]);
    }
}

/*
Main method has an infinite while loop that does:
    1) check if input is from terminal, if so go to 2. If input is from file, go to 1.5
    1.5) open ./myshell argument, and run it
    2) print myshell prompt
    3) wait for user stdin by using get_args() function
    4) while loop to loop through individual semicolon separated commands.
       if there is a pipe, runPipedCommands(), otherwise, runSimple()
    5) for each individual command, get each argument token and parse
       whitespace by calling parseW, which returns array of strings
       of every single command and their arguments
Main method also has a signal handler for SIGINT (ctrl+c), in which event
another signal handler for SIGINT is setup, and a jump is made to the before
the while loop so that the shell can print prompt and listen for commands again
*/
int main(int argc, char *argv[])
{
    int i = 0, size = 0;
    char *semi;
    char *semiSavePter;
    signal(SIGINT, sigintHandler);
    siginterrupt(SIGINT, 1);
    sigsetjmp(mark, 1);
    printf("\n");

    while (1)
    {
        if (argc > 1) // if arguments are input, dont print prompt
        {
            i = 1;
            int fp;

            // open the file
            fp = open(argv[1], O_RDONLY);
            if (!fp)
            {
                perror("open");
                break;
            }

            // redirect stdin to the file
            dup2(fp, STDIN_FILENO);

            // loop through and get each line of file
            char *line;
            while (line = get_args())
            {
                // put STDIN back to STDIN
                dup2(STDIN_FILENO, STDIN_FILENO);

                // for every semicolon, parse each cmd
                semi = strtok_r(line, ";", &semiSavePter);
                while (semi)
                {
                    // get array of struct commands
                    struct command *args = parseW(semi);

                    // if error in parsing, break and reprint prompt
                    if (!args)
                        break;

                    // if pipe, run piped function
                    if (strchr(semi, '|'))
                    {
                        runPipedCommands(semi);
                    }
                    else // no pipe
                    {
                        // get result from runSimple
                        int result = runSimple(args);

                        // if result is not 0,
                        // then waitpid
                        if (result)
                        {
                            int status = 0;
                            waitpid(result, &status, 0);
                        }

                        // result was 0, then program hangs (bc of &)
                    }

                    semi = strtok_r(NULL, ";", &semiSavePter);
                }
            }

            close(fp);

            exit(EXIT_SUCCESS);
        }
        else // if terminal is the input
        {
            if (isatty(STDIN_FILENO))
            {
                // print prompt
                printf("\033[1;32mmyshell\033[0m>");
                char *line = get_args();

                // parse semis
                semi = strtok_r(line, ";", &semiSavePter);
                // for every semicolon, parse each cmd
                while (semi)
                {
                    // get array of args
                    struct command *args = parseW(semi);
                    // if error in parsing, break and reprint prompt
                    if (!args)
                        break;

                    // if pipe runPipedCommands
                    if (strchr(semi, '|'))
                    {
                        runPipedCommands(semi);
                    }
                    else
                    {
                        // if result, then wait on child (no &)
                        int result = runSimple(args);
                        if (result)
                        {
                            int status = 0;
                            waitpid(result, &status, 0);
                        }

                        // result was 0, then program hangs (bc of &)
                    }

                    semi = strtok_r(NULL, ";", &semiSavePter);
                }
            }
        }
    }

    return 0;
};