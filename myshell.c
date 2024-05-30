#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include <string.h>
#include <signal.h>
#include <termios.h>

#define MAX_CMND_L 1024
#define CD_CMND "cd"
#define EXIT_CMND "quit"
#define REDO_CMND "!!"
#define PROMPT_CMND "prompt"
#define INI_PROMPT "hello: "
#define MAX_HISTORY_SIZE 30

char *history[MAX_HISTORY_SIZE];
int history_count = 0;
int history_index = 0;

char *prompt = INI_PROMPT;
int status;
enum redirect_symbol
{
    REDIRECT_NONE = 0,
    REDIRECT_OUT = 1,     // >
    REDIRECT_APP = 2,     // >>
    REDIRECT_ERR = 3,     // 2>
    REDIRECT_ERR_APP = 4, // 2>>
    REDIRECT_IN = 5       // <
};

void reset_history_index()
{
    history_index = history_count;
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
void signal_handler(int signal)
{
    printf("\nYou typed Control-C!\n");
    reset_history_index();
    printf("%s", prompt);
    fflush(stdout);
}

int command_parser(char **parsed_cmnd, char *cmnd, const char *delimeter)
{
    // defaut delimeter is space,but we want it as argument so we can use it for pipe
    char *token;
    token = strtok(cmnd, delimeter);
    int str_index = -1;
    while (token)
    {
        parsed_cmnd[++str_index] = malloc(strlen(token) + 1);
        strcpy(parsed_cmnd[str_index], token);

        // if the delimiter is PIPE we need to ensure there is no spaces:
        if (strcmp(delimeter, "|") == 0)
        {
            if (parsed_cmnd[str_index][0] == ' ')
            {
                memmove(parsed_cmnd[str_index], parsed_cmnd[str_index] + 1, strlen(parsed_cmnd[str_index]));
            }
            if (parsed_cmnd[str_index][strlen(parsed_cmnd[str_index]) - 1] == ' ')
            {
                parsed_cmnd[str_index][strlen(parsed_cmnd[str_index]) - 1] = '\0';
            }
        }
        parsed_cmnd[str_index][strlen(token)] = '\0';
        token = strtok(NULL, delimeter);
    }
    parsed_cmnd[++str_index] = NULL;
    return str_index;
}

void prompt_changer(const char *new_prompt)
{
    int len = strlen(new_prompt);
    if(len == 0)
    {
        perror("prompt can't be empty");
        return;
    }
    prompt = strdup(new_prompt);
    if (len > 0 && prompt[len -1] != ' ')
    {
        strcat(prompt, " ");
    }
    // prompt[strlen(new_prompt)] = '\0';
}
void set_variable(char *var, char *val)
{
    int new_var = setenv(var, val, 1);
    if (new_var != 0)
    {
        perror("setenv");
    }
}
char *get_variable(char *var)
{
    char *val = getenv(var);
    if (val == NULL)
    {
        perror("getenv");
    }
    return val;
}

void command_with_ampersand(char *cmnd)
{
    pid_t pid = fork();
    if (pid == 0)
    { // we are in child process. no need to wait() as we want it to be & behavior
        // setting signal handler as default behavior
        signal(SIGINT, SIG_DFL);
        char *commands[MAX_CMND_L];
        int commands_with_ampersand = command_parser(commands, cmnd, " ");
        // eliminating the & from the command
        commands[commands_with_ampersand - 1] = NULL;
        execvp(commands[0], commands);
    }
}
/* in this implementation the first and last command wont get a pipe*/
void command_with_pipe(char *cmnd)
{
    char *commands[MAX_CMND_L];
    int initial_parsing = command_parser(commands, cmnd, "|");
    char *comnds_without_pipe[MAX_CMND_L];
    int fd[initial_parsing][2];
    for (int i = 0; i < initial_parsing; ++i)
    {
        // parse the commands with space
        command_parser(comnds_without_pipe, commands[i], " ");
        // creating pipe for pairs of commands
        if (i != initial_parsing - 1)
        {
            pipe(fd[i]);
        }
        pid_t pid = fork();
        // setting the signal handler as default behavior (because child)
        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);
            if (i != initial_parsing - 1)
            {
                dup2(fd[i][1], 1);
                close(fd[i][0]);
                close(fd[i][1]);
            }
            if (i != 0)
            {
                dup2(fd[i - 1][0], 0);
                close(fd[i - 1][0]);
                close(fd[i - 1][1]);
            }
            execvp(comnds_without_pipe[0], comnds_without_pipe);
        }
        if (i != 0)
        {
            close(fd[i - 1][0]);
            close(fd[i - 1][1]);
        }
        wait(NULL); // wait for the child process to end
    }
}
enum redirect_symbol get_symbol(const char *cmnd)
{
    if (strstr(cmnd, "2>>") != NULL)
    {
        return REDIRECT_ERR_APP;
    }
    else if (strstr(cmnd, "2>") != NULL)
    {
        return REDIRECT_ERR;
    }
    else if (strstr(cmnd, ">>") != NULL)
    {
        return REDIRECT_APP;
    }
    else if (strstr(cmnd, ">") != NULL)
    {
        return REDIRECT_OUT;
    }
    else if (strstr(cmnd, "<") != NULL)
    {
        return REDIRECT_IN;
    }
    return REDIRECT_NONE;
}
void commands_with_redirection(char *cmnd)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        signal(SIGINT, SIG_DFL);
        enum redirect_symbol symbol = get_symbol(cmnd);
        char *parsed_cmnd[MAX_CMND_L];
        int cmnds = command_parser(parsed_cmnd, cmnd, " ");
        int fd;
        switch (symbol)
        {
        case REDIRECT_NONE:
            break; // not suppuse to happen
        case REDIRECT_OUT:
            fd = creat(parsed_cmnd[cmnds - 1], 0660);
            dup2(fd, 1);
            break;
        case REDIRECT_APP:
            fd = open(parsed_cmnd[cmnds - 1], O_CREAT | O_APPEND | O_RDWR, 0660);
            dup2(fd, 1);
            break;
        case REDIRECT_ERR:
            fd = open(parsed_cmnd[cmnds - 1], O_CREAT | O_WRONLY | O_TRUNC, 0660);
            dup2(fd, 2);
            break;
        case REDIRECT_ERR_APP:
            fd = open(parsed_cmnd[cmnds - 1], O_CREAT | O_WRONLY | O_APPEND, 0660);
            dup2(fd, 2);
            break;
        case REDIRECT_IN:
            fd = open(parsed_cmnd[cmnds - 1], O_RDONLY, 0660);
            dup2(fd, 0);
            break;
        }
        parsed_cmnd[cmnds - 2] = parsed_cmnd[cmnds - 1] = NULL;
        execvp(parsed_cmnd[0], parsed_cmnd);
    }
    else
    {
        wait(&status);
    }
}
void commands_miscellaneous(char *cmnd)
{
    /*this function will handle any other cmnds that are not pipe or redirect excplict*/
    char *parsed_cmnd[MAX_CMND_L];
    command_parser(parsed_cmnd, cmnd, " ");
    // now check what cmnd is it:
    if (!strcmp(parsed_cmnd[0], PROMPT_CMND)) // prompt changing
    {
        prompt_changer(parsed_cmnd[2]);
    }
    else if (!strcmp(parsed_cmnd[0], CD_CMND)) // for CD
    {
        chdir(parsed_cmnd[1]);
    }
    else if (!strcmp(parsed_cmnd[0], "echo") && !strcmp(parsed_cmnd[1], "$?"))
    { // echo #? print the status of last command
        printf("%d\n", WEXITSTATUS(status));
    }
    else if (!strcmp(parsed_cmnd[0], "echo") && parsed_cmnd[1][0] == '$')
    { // echo $ will print var
        char *var = parsed_cmnd[1] + 1;
        var[strlen(var)] = '\0';
        char *var_value = get_variable(var);
        printf("%s\n", var_value);
    }
    else if (!strcmp(parsed_cmnd[0], "read"))
    {
        char buff[MAX_CMND_L];
        fgets(buff, MAX_CMND_L, stdin);
        buff[strlen(buff) - 1] = '\0';
        set_variable(parsed_cmnd[1], buff);
    }
    else if (parsed_cmnd[0][0] == '$' && parsed_cmnd[1][0] == '=')
    {
        // var assignment
        char *var = parsed_cmnd[0] + 1;
        var[strlen(var)] = '\0';
        set_variable(var, parsed_cmnd[2]);
    } // else for different general cmnds
    else if (fork() == 0)
    {
        signal(SIGINT, SIG_DFL);
        execvp(parsed_cmnd[0], parsed_cmnd);
    }
    else
    {
        wait(&status);
    }
}

void execute_if_then_else(const char *if_command, const char *then_command, const char *else_command)
{
    if (if_command && then_command)
    {
        int result = system(if_command); // Execute the if condition
        if (result == 0)
        {
            // If the if condition is true, execute the then command
            system(then_command);
        }
        else if (else_command)
        {
            // If the if condition is false and else part is provided, execute the else command
            system(else_command);
        }
    }
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
        if (c == '\n')
        {
            input[pos] = '\0';
            printf("\n"); // Print a newline before processing the command
            break;
        }

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


int main()
{
    // Setting the signal_handler
    signal(SIGINT, signal_handler);
    char command[MAX_CMND_L];
    char command_copy[MAX_CMND_L]; // For redo !!
    char *if_command = NULL;
    char *then_command = NULL;
    char *else_command = NULL;
    int in_if_block = 0;
    int then_skip = 0; // Flag to skip the next line after "then"
    int else_skip = 0; // Flag to skip the next line after "else"

    while (1)
    {
        // do not print promt if in the middle of an if block
        if (! in_if_block) {
            printf("%s", prompt);
            fflush(stdout);
        }

        // get user command
        get_input(command, MAX_CMND_L);

        // ignore empty commands
        if (strlen(command) == 0) {
            continue;
        }

        // check commands


        // if exit
        if (!strcmp(command, EXIT_CMND))
        {
            break;
        }
        // if last command
        if (!strcmp(command, "!!"))
        {
            strcpy(command, command_copy);
        }
        else
        {
            strcpy(command_copy, command);
        }

        // add command to history list
        add_to_history(command);

        // If we're inside an if block, accumulate lines until 'fi' is encountered
        if (in_if_block)
        {
            // Check for 'fi' keyword
            if (strstr(command, "fi") != NULL)
            {
                in_if_block = 0; // End of if block

                execute_if_then_else(if_command, then_command, else_command);
                // Reset buffers
                free(if_command);
                free(then_command);
                free(else_command);
                if_command = NULL;
                then_command = NULL;
                else_command = NULL;
                then_skip = 0;
                else_skip = 0;
            }
            else if (then_skip)
            {
                then_command = strdup(command);
                then_skip = 0;
            }
            else if (else_skip)
            {
                else_command = strdup(command);
                else_skip = 0;
            }
            else if (strstr(command, "then") == command)
            {
                then_skip = 1;
            }
            else if (strstr(command, "else") == command)
            {

                else_skip = 1;
            }
            else
            {

                if (!if_command)
                {
                    if_command = strdup(command);
                }
            }
        }
        else
        {

            if (strstr(command, "if ") == command)
            {
                in_if_block = 1;
                if_command = strdup(command + 3);
            }
            else if (strchr(command, '|'))
            {
                command_with_pipe(command);
            }
            else if (strchr(command, '&'))
            {
                command_with_ampersand(command);
            }
            else if (strstr(command, "<") || strstr(command, ">"))
            {
                commands_with_redirection(command);
            }
            else
            {
                commands_miscellaneous(command);
            }
        }
        // reset history position
        reset_history_index();
    }
}