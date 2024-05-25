#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARG_SIZE 100
#define MAX_HISTORY_SIZE 30
#define MAX_VAR_SIZE 100
#define MAX_VAR_NAME 50
#define MAX_VAR_VALUE 100

char *history[MAX_HISTORY_SIZE];
int history_count = 0;
char prompt[50] = "hello: ";
int last_status = 0;
int history_index = 0;

void handle_sigint(int sig);
void add_to_history(const char *command);
void get_input(char *input, int size);
char *get_variable(const char *name);
void set_variable(const char *name, const char *value);
void execute_command(char *command, int is_pipe, int is_first_in_pipe, int is_last_in_pipe, int pipe_fd[2]);
void execute_pipeline(char *command);

typedef struct
{
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} ShellVariable;

ShellVariable shell_vars[MAX_VAR_SIZE];
int var_count = 0;

void reset_history_index(){
    history_index = history_count;
}

void handle_sigint(int sig)
{
    printf("\nYou typed Control-C!\n");
    reset_history_index();
    printf("%s", prompt);
    fflush(stdout);
}

void add_to_history(const char *command)
{
    if (strcmp(command, "!!") == 0)
    {
        return;
    }
    if (history_count == MAX_HISTORY_SIZE)
    {
        free(history[0]);
        memmove(history, history + 1, (MAX_HISTORY_SIZE - 1) * sizeof(char *));
        history_count--;
    }
    history[history_count++] = strdup(command);
}

void get_input(char *input, int size)
{
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    char c;
    int pos = 0;
    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == 27)
        {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1)
                continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1)
                continue;
            if (seq[0] == '[')
            {
                if (seq[1] == 'A')
                { // Up arrow
                    if (history_index > 0)
                    {
                        history_index--;
                        strcpy(input, history[history_index]);
                        pos = strlen(input);
                        printf("\33[2K\r%s%s", prompt, input); // Clear the line and print the prompt and input
                        fflush(stdout);
                    }
                }
                else if (seq[1] == 'B')
                { // Down arrow
                    if (history_index < history_count - 1)
                    {
                        history_index++;
                        strcpy(input, history[history_index]);
                        pos = strlen(input);
                        printf("\33[2K\r%s%s", prompt, input); // Clear the line and print the prompt and input
                        fflush(stdout);
                    }
                    else
                    {
                        history_index = history_count;
                        input[0] = '\0';
                        pos = 0;
                        printf("\33[2K\r%s", prompt); // Clear the line and print the prompt only
                        fflush(stdout);
                    }
                }
            }
        }
        else if (c == 127 || c == 8)
        { // Backspace
            if (pos > 0)
            {
                input[--pos] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (c == '\n')
        {
            input[pos] = '\0';
            printf("\n"); // Print a newline before processing the command
            break;
        }
        else
        {
            input[pos++] = c;
            input[pos] = '\0';
            printf("%c", c);
            fflush(stdout);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
}

char *get_variable(const char *name)
{
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(shell_vars[i].name, name) == 0)
        {
            return shell_vars[i].value;
        }
    }
    return NULL;
}

void set_variable(const char *name, const char *value)
{
    for (int i = 0; i < var_count; i++)
    {
        if (strcmp(shell_vars[i].name, name) == 0)
        {
            strcpy(shell_vars[i].value, value);
            return;
        }
    }
    strcpy(shell_vars[var_count].name, name);
    strcpy(shell_vars[var_count].value, value);
    var_count++;
}

int count_pipes(const char *command) {
    int count = 0;
    const char *p = command;

    while (*p != '\0') {
        if (*p == '|') {
            count++;
        }
        p++;
    }

    return count;
}

void execute_pipeline(char *command)
{
    int pipe_count = count_pipes(command);

    if (pipe_count == 0)
    {
        execute_command(command, 0, 0, 0, NULL);
        return;
    }

    int pipe_fds[pipe_count][2];
    for (int i = 0; i < pipe_count; i++)
    {
        if (pipe(pipe_fds[i]) == -1)
        {
            perror("pipe");
            return;
        }
    }


    char *token = strtok(command, "|");
    int i = 0;
    while (token != NULL)
    {

        int is_first = 0, is_last = 0;
        if (i == 0) {
            is_first = 1;
        }
        if (i == pipe_count) {
            is_last = 1;
        }

        char *dup = strdup(token);
        //printf("subcommand: %s\n", dup);
        execute_command(dup, 1, is_first, is_last, pipe_fds[i]);

        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);

        free(dup);
        token = strtok(NULL, "|");
        i++;
    }
}

void execute_command(char *command, int is_pipe, int is_first_in_pipe, int is_last_in_pipe, int pipe_fd[2])
{
    char *args[MAX_ARG_SIZE];
    int append = 0;
    int redirect = 0;
    int input_redirect = 0;
    char *output_filename = NULL;
    char *input_filename = NULL;

    // Tokenize the command
    char *token = strtok(command, " \t\n");
    int arg_count = 0;
    while (token != NULL)
    {
        if (strcmp(token, ">>") == 0)
        {
            append = 1;
            token = strtok(NULL, " \t\n");
            output_filename = token;
            break;
        }
        else if (strcmp(token, "2>") == 0)
        {
            redirect = 1;
            token = strtok(NULL, " \t\n");
            output_filename = token;
            break;
        }
        else if (strcmp(token, "<") == 0)
        {
            input_redirect = 1;
            token = strtok(NULL, " \t\n");
            input_filename = token;
        }
        else
        {
            args[arg_count++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[arg_count] = NULL;

    if (arg_count == 0)
        return;

    if (strcmp(args[0], "prompt") == 0 && arg_count == 3 && strcmp(args[1], "=") == 0)
    {
        strcpy(prompt, args[2]);
        int length = strlen(prompt);

        if (length > 0 && prompt[length - 1] != ' ')
        {
            strcat(prompt, " ");
        }
        return;
    }
    else if (strcmp(args[0], "echo") == 0)
    {
        if (arg_count == 2 && strcmp(args[1], "$?") == 0)
        {
            printf("%d\n", last_status);
        }
        else
        {
            for (int i = 1; i < arg_count; i++)
            {
                if (args[i][0] == '$')
                {
                    char *val = get_variable(args[i]);
                    if (val)
                        printf("%s ", val);
                    else
                        printf("%s ", args[i]);
                }
                else
                {
                    printf("%s ", args[i]);
                }
            }
            printf("\n");
        }
        return;
    } 
    else if (strcmp(args[0], "read") == 0 && arg_count == 2) {
        fflush(stdout);
        char value[MAX_INPUT_SIZE];
        get_input(value, sizeof(value));

        // add '$' as prefix to variable name
        // and store it in variables list
        char name[MAX_VAR_NAME] = "$";
        strcat(name, args[1]);
        set_variable(name, value);
        return;
    } 
    else if (args[0][0] == '$' && arg_count == 3 && strcmp(args[1], "=") == 0)
    {
        set_variable(args[0], args[2]);
        return;
    }
    else if (strcmp(args[0], "cd") == 0)
    {
        if (arg_count > 1)
        {
            if (chdir(args[1]) != 0)
            {
                perror("chdir");
            }
        }
        else
        {
            fprintf(stderr, "cd: missing argument\n");
        }
        return;
    }
    else if (strcmp(args[0], "quit") == 0)
    {
        exit(0);
    }
    else if (strcmp(args[0], "!!") == 0)
    {
        if (history_count > 0)
        {
            strcpy(command, history[history_count - 1]);
            printf("%s\n", command);
            execute_pipeline(command);
        }
        else
        {
            fprintf(stderr, "No commands in history.\n");
        }
        return;
    }
    else if (strcmp(args[0], "if") == 0 && strcmp(args[3], "then") == 0 && strcmp(args[5], "else") == 0 && strcmp(args[6], "fi") == 0)
    {
        char *condition = args[1];
        char *then_cmd = args[4];
        char *else_cmd = args[5];

        if (strcmp(args[2], "==") == 0)
        {
            if (strcmp(get_variable(condition), args[4]) == 0)
            {
                execute_command(then_cmd, 0, 0, 0, NULL);
            }
            else if (else_cmd)
            {
                execute_command(else_cmd, 0, 0, 0, NULL);
            }
        }
        else
        {
            fprintf(stderr, "Unsupported operator: %s\n", args[2]);
        }
        return;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return;
    }
    else if (pid == 0)
    {
        if (is_pipe)
        {
            if (pipe_fd)
            {
                printf("is first: %d, is last %d, command: %s\n", is_first_in_pipe, is_last_in_pipe, args[0]);
                if (!is_first_in_pipe) {
                    dup2(pipe_fd[0], STDIN_FILENO);
                    close(pipe_fd[0]);

                }
                if (!is_last_in_pipe) {
                    dup2(pipe_fd[1], STDOUT_FILENO);
                    close(pipe_fd[1]);
                }

            }
        }

        if (append)
        {
            int fd = open(output_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0)
            {
                perror("open");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        else if (redirect)
        {
            int fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                perror("open");
                exit(1);
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        else if (input_redirect)
        {
            int fd = open(input_filename, O_RDONLY);
            if (fd < 0)
            {
                perror("open");
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        execvp(args[0], args);
        perror("execvp");
        exit(1);
    }
    else
    {
        int status;
        waitpid(pid, &status, 0);
        last_status = WEXITSTATUS(status);
    }
}

int main()
{
    signal(SIGINT, handle_sigint);

    while (1)
    {
        char input[MAX_INPUT_SIZE];
        printf("%s", prompt);
        fflush(stdout);

        get_input(input, sizeof(input));
        if (strlen(input) > 0)
        {
            add_to_history(input);
            execute_pipeline(input);
            reset_history_index();
        }
    }
    return 0;
}
