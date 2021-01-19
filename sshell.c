#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdbool.h>

#define CMDLINE_MAX 512
#define MAX_ARGS 16
#define MAX_TOKEN_LENGTH 32
#define MAX_NUM_PIPES 4

/* Contains parsed command line and necessary information to run commands */
struct Command {
        char *args[MAX_TOKEN_LENGTH];
        char originalCmd[CMDLINE_MAX];
        char *file;
        int argsIndex;
        int append;
        int status;
};

enum cmdType {PIPING = 3, REDIRECTION = 4, NORMAL = 5, PARSEERROR = 6};

int determineParseType(struct Command *data, char cmd[], int *pipeSize);
int parseNormal(struct Command *data, char cmd[]);
int parseRedirection(struct Command *data, char cmd[]);
int parsePiping(struct Command *data, char cmd[], int *pipeSize);
void createCommand(struct Command *data);
void redirect(struct Command *data);
int changeDirectory(struct Command *data);
int printWorkingDirectory();
void destroyCommand(struct Command *data);
void pipeline(struct Command *data, int pipeSize, char cmd[]);
int sls();
off_t filesize(const char *filename);

int main(void) {
        char cmd[CMDLINE_MAX];
        struct Command *data = malloc(sizeof(struct Command) * MAX_NUM_PIPES);

        while (1) {
                char *nl;
                int status = 0;
                createCommand(data);

                /* Status helps determine whether cmd uses piping, redirect,
                or normal command */
                int pipeSize;
                enum cmdType commandType;
                pid_t pid;
                pipeSize = 0;

                /* Print prompt */
                printf("sshell@ucd$ ");
                fflush(stdout);

                /* Get command line */
                fgets(cmd, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmd);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmd, '\n');

                if (nl)
                        *nl = '\0';

                /* Handles parsing for all cases */
                commandType = determineParseType(data, cmd, &pipeSize);

                if(commandType == PARSEERROR) {
                        destroyCommand(data);
                        continue;
                }

                /* State machine for builtin commands or piping, redirection, or external command */
                if (!strcmp(cmd, "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed 'exit' [0]\n");
                        free(data);
                        exit(0);
                } else if (!strcmp(data[0].args[0], "cd") && pipeSize == 0) {
                        status = changeDirectory(&data[0]);
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                } else if (!strcmp(cmd, "pwd")) {
                        printWorkingDirectory();
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                } else if (!strcmp(cmd, "sls")) {
                        status = sls();
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, status);
                } else {
                        pid = fork();
                        if (pid > 0) {
                                waitpid(pid, &status, 0);
                                if (commandType != PIPING)
                                        fprintf(stderr, "+ completed '%s' [%d]\n", cmd, WEXITSTATUS(status));
                        } else if (pid == 0) {
                                if (commandType == PIPING) {
                                        pipeline(data, pipeSize, cmd);
                                } else if (commandType == REDIRECTION) {
                                        redirect(&data[0]);
                                } else {
                                        execvp(data->args[0], data->args);
                                        fprintf(stderr, "Error: command not found\n");
                                        exit(EXIT_FAILURE);
                                }
                        } else {
                                fprintf(stderr, "Error creating child");
                                exit(EXIT_FAILURE);
                        }
                }

                destroyCommand(data);
        }
		
        return EXIT_SUCCESS;
}

/* Determines which parse method to use */
/* Only piping requires 4 data objects to differentiate which command belongs to a certain pipe */
int determineParseType(struct Command *data, char cmd[], int *pipeSize) {
        char *pipe, *redirect, tempCMD[CMDLINE_MAX];

        pipe = strchr(cmd, '|');
        redirect = strchr(cmd, '>');
        strcpy(tempCMD, cmd);
        
        /* State machine for parse type */
        if (pipe != NULL) {
                /* Piping detected */
                if (parsePiping(data, tempCMD, pipeSize))
                        return PARSEERROR;

                return PIPING;
        } else if (redirect != NULL) {
                /* if no piping detected, we will always use 1 data object */
                if (parseRedirection(&data[0], tempCMD) == PARSEERROR)
                        return PARSEERROR;

                return REDIRECTION;
        } else {
                /* If no piping detected, we will always use 1 data object */
                if (parseNormal(&data[0], tempCMD) == PARSEERROR)
                        return PARSEERROR;

                return NORMAL;
        }
}

/* Creates new empty command */
void createCommand(struct Command *data) {
        for (int i = 0; i < MAX_NUM_PIPES; i++) {
                data[i].argsIndex = 0;
                data[i].append = 0;
                data[i].status = 0;
                data[i].file = NULL;

                for (int j = 0; j < MAX_ARGS + 1; j++)
                        data[i].args[j] = malloc(sizeof(char));
        }
}

/* Frees allocated memory */
void destroyCommand(struct Command *data) {
        for (int i = 0; i < MAX_NUM_PIPES; i++)
                for (int j = 0; j < MAX_ARGS; j++)
                        free(data[i].args[j]);
}

/* Parses command line using White Space */
int parseNormal(struct Command *data, char cmd[]) {
        char *token;
        token = strtok(cmd, " ");

        if(!token) {
                fprintf(stderr, "Error: missing command\n");
                return PARSEERROR;
        }

        /* Continues to parse white space until max argument reached 
        or no more arguments left */
        while (token != NULL && data->argsIndex < MAX_ARGS + 1) {
                strcpy(data->args[data->argsIndex], token);
                token = strtok(NULL, " ");
                data->argsIndex++;
        }

        if (data->argsIndex > MAX_ARGS) {
                fprintf(stderr, "Error: too many process arguments\n");
                return PARSEERROR;
        }

        data->args[data->argsIndex] = NULL;
        return 0;
}

/* Parses command line for redirection */
int parseRedirection(struct Command *data, char cmd[]) {
        char *token, command[CMDLINE_MAX], *append;

        append = strstr(cmd, ">>");

        if (append != NULL)
                data->append = 1;

        token = strtok(cmd, ">");

        if (cmd[0] == '>')
                strcpy(command, " ");
        else if (token)
                strcpy(command, token);

        token = strtok(NULL, "> ");
        data->file = token;

        /* Flushes strtok buffer until NULL */
        while (token != NULL)
                token = strtok(NULL, " ");

        if (parseNormal(data, command) == PARSEERROR)
                return PARSEERROR;

        if (!data->file) {
                fprintf(stderr, "Error: no output file\n");
                return PARSEERROR;
        }

        return 0;
}

/* Parses command line for piping */
int parsePiping(struct Command *data, char cmd[], int *pipeSize) {
        char *token, cmdCpy[CMDLINE_MAX];
        int index;

        index = 0;
        token = strtok(cmd, "|");

        /* Continues to pare until no more pipes or max pipes reached */
        while (token != NULL && index < MAX_NUM_PIPES) {
                strcpy(data[index].originalCmd, token);
                token = strtok(NULL, "|");
                index++;
        }

        if (index <= 1) {
                fprintf(stderr, "Error: missing command\n");
                return PARSEERROR;
        }

        *pipeSize = index;

        /* Determines parsing type that is needed depending on command  for each pipe */
        for (int i = 0; i < *pipeSize; i++) {
                strcpy(cmdCpy, data[i].originalCmd);

                if (strstr(cmdCpy, ">>") != NULL || strchr(cmdCpy, '>') != NULL) {
                        parseRedirection(&data[i], cmdCpy);

                        if (i == 0) {
                                fprintf(stderr, "Error: mislocated output redirection\n");
                                return PARSEERROR;
                        }
                }
                else
                        parseNormal(&data[i], cmdCpy);
        }

        return 0;
}

/* Adds redirect functionality by redirecting STDOUT */
void redirect(struct Command *data) {
        int fd;
		
        /* Truncate or append to file */
        if (data->append == 0)
                fd = open(data->file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        else
                fd = open(data->file, O_WRONLY | O_APPEND, 0644);

        if (fd == -1) {
                fprintf(stderr, "Error: cannot open output file\n");
                exit(EXIT_FAILURE);
        }

        /* Redirect STDOUT to file descriptor and print output */
        dup2(fd, STDOUT_FILENO);
        close(fd);
        execvp(data->args[0], data->args);
        fprintf(stderr, "Error: command not found\n");
        exit(EXIT_FAILURE);
}

int changeDirectory(struct Command *data) {
        int retval = chdir(data->args[1]);

        if (retval != 0) {
                fprintf(stderr, "Error: cannot cd into directory\n");
                return EXIT_FAILURE;
        }
		
        return EXIT_SUCCESS;
}

int printWorkingDirectory() {
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));
        printf("%s\n", cwd);
        return EXIT_SUCCESS;
}

/* Handles the piping case by making sure to keep track of the right read FD */
/*Source Inspired by https://gist.github.com/zed/7540510 */
void pipeline(struct Command *data, int pipeSize, char cmd[]) {
        int fd[2], in, pid, status;
        in = STDIN_FILENO;
        fd[0] = 0, fd[1] = 0;

        for (int i = 0; i < pipeSize - 1; i ++) {
                pipe(fd);
                pid = fork();

                /* Parent keeps track of FD[0] to make sure child can read from previous command */
                /* Keeps track of exit status of each child */
                if (pid > 0) {
                        close(fd[1]);
                        close(in);
                        in = fd[0];
                        wait(&data[i].status);
                } else if (pid == 0) {
                        close(fd[0]);
                        dup2(in, STDIN_FILENO);
                        dup2(fd[1], STDOUT_FILENO);
                        close(fd[1]);
                        execvp(data[i].args[0], data[i].args);
                        exit(EXIT_FAILURE);
                }
                else {
                        fprintf(stderr, "Error: Child error\n");
                        exit(EXIT_FAILURE);
                }
        }

        dup2(in, STDIN_FILENO);
        dup2(fd[1], STDOUT_FILENO);

        /* Handles the final command whether its a normal or redirect command */
        pid = fork();

        if (pid > 0)
                wait(&status);
        else if (pid == 0) {
                if (strchr(data[pipeSize - 1].originalCmd, '>')
                        || strstr(data[pipeSize - 1].originalCmd, ">>"))
                        redirect(&data[pipeSize - 1]);
                else
                        execvp(data[pipeSize - 1].args[0], data[pipeSize - 1].args);

                exit(EXIT_FAILURE);
        }
        else {
                fprintf(stderr, "Error creating child\n");
                exit(EXIT_FAILURE);
        }

        /* Handles stderr output instead of main */
        fprintf(stderr, "+ completed '%s' ", cmd);

        for (int i = 0; i < pipeSize - 1; i++)
                fprintf(stderr, "[%d]", WEXITSTATUS(data[i].status));

        fprintf(stderr, "[%d]\n", WEXITSTATUS(status));
        exit(EXIT_SUCCESS);
}

int sls() {
        char cwd[PATH_MAX];
        DIR *dirp;
        struct dirent *dp;
        getcwd(cwd, sizeof(cwd));
        dirp = opendir(cwd);
        if (!dirp) {
                fprintf(stderr, "Error: cannot open directory\n");
                return EXIT_FAILURE;
        }

        while ((dp = readdir(dirp)) != NULL) {
                if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, ".."))
                        printf("%s (%ld bytes)\n", dp->d_name, filesize(dp->d_name));
        }
		
        closedir(dirp);
        return EXIT_SUCCESS;
}

// return the size of a given file
// source reference: stack exchange, https://stackoverflow.com/questions/8236/how-do-you-determine-the-size-of-a-file-in-c
off_t filesize(const char *filename) {
        struct stat filestat;
		
        if (stat(filename, &filestat) == 0)
                return filestat.st_size;

        return -1;
}
