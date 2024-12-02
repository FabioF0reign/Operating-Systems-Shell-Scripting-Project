#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "minishell.h"

#define DEFAULT_PROMPT_CHAR "$"

// Function Prototypes
void printPrompt();
void readCommand(char *buffer);
int parseCommand(char *commandLine, struct command_t *command);
int parsePath(char *dirs[]);
char *lookupPath(char **argv, char **dirs);
int handleInternalCommand(struct command_t *cmd);
void *executeCommand(void *commandArg);

// Function to construct and display the shell prompt
void printPrompt() {
    char hostname[256];
    char cwd[1024];
    char promptString[512];

    // Get the hostname
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("Error getting hostname");
        strcpy(hostname, "unknown");
    }

    // Get current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("Error getting current working directory");
        strcpy(cwd, "~");
    }

    // Shell prompt is in format "hostname:current_directory$ "
    if (snprintf(promptString, sizeof(promptString), "%s:%s%s ", hostname, cwd, DEFAULT_PROMPT_CHAR) >= sizeof(promptString)) {
        fprintf(stderr, "Error: Prompt string truncated.\n");
    }

    // Print prompt string
    printf("%s", promptString);
}

// Function to read the user command input
void readCommand(char *buffer) {
    printf("Enter your command: ");
    if (fgets(buffer, LINE_LEN, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline character
    } else {
        if (feof(stdin)) {
            printf("\nExiting shell...\n");
            exit(0); // Exit gracefully
        } else {
            perror("Error reading command");
            buffer[0] = '\0';
        }
    }
}

// Function to parse the command line into a structured format
int parseCommand(char *commandLine, struct command_t *command) {
    int argc = 0;
    char *token = strtok(commandLine, WHITESPACE);

    while (token != NULL && argc < MAX_ARGS) {
        command->argv[argc] = token;
        argc++;
        token = strtok(NULL, WHITESPACE);
    }

    if (argc == 0) {
        return -1; // No command found
    }

    command->argv[argc] = NULL; // Set the last argument to NULL for execvp
    command->name = command->argv[0];
    command->argc = argc;

    return 0;
}

// Function to handle internal shell commands like "cd", "exit", and "pwd"
int handleInternalCommand(struct command_t *cmd) {
    // Handle "cd" command (change directory)
    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc < 2) {
            char *home = getenv("HOME");
            if (chdir(home ? home : "/") != 0) {
                perror("cd");
            }
        } else if (chdir(cmd->argv[1]) != 0) {
            perror("cd");
        }
        return 1; // Internal command handled
    }

    // Handle "exit" command (exit the shell)
    if (strcmp(cmd->argv[0], "exit") == 0) {
        printf("Exiting shell...\n");
        exit(0);
    }

    // Handle "pwd" command (print working directory)
    if (strcmp(cmd->argv[0], "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("pwd");
        }
        return 1; // Internal command handled
    }

    return 0; // Not an internal command
}

// Function to parse the system PATH environment variable into directories
int parsePath(char *dirs[]) {
    char *pathEnvVar = getenv("PATH");
    if (!pathEnvVar) {
        fprintf(stderr, "Error: PATH environment variable not found.\n");
        return -1;
    }

    char *pathCopy = strdup(pathEnvVar);
    if (!pathCopy) {
        perror("strdup failed");
        return -1;
    }

    int i = 0;
    char *token = strtok(pathCopy, ":");
    while (token != NULL && i < MAX_PATHS) {
        dirs[i++] = strdup(token);
        token = strtok(NULL, ":");
    }
    dirs[i] = NULL;
    free(pathCopy);
    return 0;
}

// Function to look up a command in the directories listed in PATH
char *lookupPath(char **argv, char **dir) {
    char *result = malloc(MAX_PATH_LEN);

    // Check if the command is an absolute path
    if (*argv[0] == '/') {
        strcpy(result, argv[0]);
        return result;
    }

    // Look in PATH directories
    for (int i = 0; i < MAX_PATHS && dir[i] != NULL; i++) {
        strcpy(result, dir[i]);
        strcat(result, "/");
        strcat(result, argv[0]);

        if (access(result, X_OK) == 0) {
            return result;
        }
    }

    free(result);
    return NULL;
}

// Function to execute commands in a new thread
void *executeCommand(void *commandArg) {
    struct command_t *command = (struct command_t *)commandArg;
    pid_t pid = fork();

    if (pid == 0) { // Child process
        execvp(command->name, command->argv);
        fprintf(stderr, "minishell: error executing %s\n", command->name);
        exit(EXIT_FAILURE); // Exit child process if execvp fails
    } else if (pid > 0) {
        wait(NULL); // Parent process waits for child
    } else {
        fprintf(stderr, "minishell: error forking in thread\n");
    }

    pthread_exit(NULL);
}

int main() {
    char *pathv[MAX_PATHS];
    struct command_t command;
    char commandLine[LINE_LEN];

    // Initialize PATH directories
    if (parsePath(pathv) != 0) {
        fprintf(stderr, "Error initializing PATH directories.\n");
        return 1;
    }

    // Infinite loop for the shell
    while (1) {
        // Display the shell prompt
        printPrompt();

        // Read the user input
        readCommand(commandLine);
        if (parseCommand(commandLine, &command) == -1) continue;

        // Handle internal commands first (cd, pwd, exit)
        if (handleInternalCommand(&command) == 1) {
            continue;
        }

        // Look up the command in the PATH directories
        command.name = lookupPath(command.argv, pathv);
        if (command.name == NULL) {
            fprintf(stderr, "%s: command not found\n", command.argv[0]);
            continue;
        }

        // Execute the command in a separate thread
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, executeCommand, (void *)&command) != 0) {
            fprintf(stderr, "minishell: error creating thread\n");
            continue;
        }

        pthread_join(thread_id, NULL); // Wait for the thread to finish
    }

    return 0;
}
