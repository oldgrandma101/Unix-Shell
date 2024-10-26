#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>   //for INT_MAX
#include <sys/wait.h> //for waitpid()
#include <unistd.h>   //for fork(), execvp()
#include <fcntl.h>    //for open(), O_RDONLY() etc.
#include "parser.h"
#include "utils.h"
#include <stdbool.h>

void terminate(char *line)
{
    if (line)
        free(line); // release memory allocated to line pointer
    printf("bye\n");
    exit(0);
}

/* Read a line from standard input and put it in a char[] */
char *readline(const char *prompt)
{
    size_t buf_len = 16;
    char *buf = xmalloc(buf_len * sizeof(char));

    printf("%s", prompt);
    if (fgets(buf, buf_len, stdin) == NULL)
    {
        free(buf);
        return NULL;
    }

    do
    {
        size_t l = strlen(buf);
        if ((l > 0) && (buf[l - 1] == '\n'))
        {
            l--;
            buf[l] = 0;
            return buf;
        }
        if (buf_len >= (INT_MAX / 2))
            memory_error();
        buf_len *= 2;
        buf = xrealloc(buf, buf_len * sizeof(char));
        if (fgets(buf + l, buf_len - l, stdin) == NULL)
            return buf;
    } while (1);
}

typedef struct bg_process
{
    pid_t pid;
    char *command;
    struct bg_process *next;

} bg_process;

bg_process *linked_list_of_bg_processes = NULL; // pointer to linked list, initially empty

void add_bg_process(pid_t pid, char *command)
{
    // need to add a new process to linked list
    bg_process *new_process = malloc(sizeof(bg_process)); // create new node for linked list
    new_process->pid = pid;
    new_process->command = strdup(command);
    new_process->next = linked_list_of_bg_processes;
    linked_list_of_bg_processes = new_process;
}

void print_jobs()
{
    // iterate over linked list and print each node's pid and command
    bg_process *current = linked_list_of_bg_processes;

    while (current)
    {
        printf("PID: %d, Command: %s, (running)\n", current->pid, current->command);
        current = current->next;
    }
}

void check_bg_processes() // deletes any finsished processes from linked list
{
    bg_process **current = &linked_list_of_bg_processes; // double pointer to be able to modify linked list
    while (*current)
    {
        int result = waitpid((*current)->pid, NULL, WNOHANG); // WNOHANG means it doesn't wait if there are no terminated processes

        if (result == 0) // means there are no stopped processes
        {
            current = &(*current)->next;
        }
        else
        {
            // there are stopped processes so we should remove them from the linked list
            printf("PID: %d, Command: %s, (finished)\n", (*current)->pid, (*current)->command);
            bg_process *temp = *current;
            (*current) = (*current)->next; // make the next node the current one
            free(temp->command);
            free(temp);
        }
    }
}

void output_redirection(char *out)
{
    // need to declare a file descriptor
    int fd = open(out, O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (fd < 0)
    {
        printf("error with output redirection");
    }

    // need to redirect output to a file
    dup2(fd, STDOUT_FILENO); // output is to the file now instead of stdout
    close(fd);
}

void input_redirection(char *in)
{
    int fd = open(in, O_RDONLY);
    if (fd < 0)
    {
        perror(in);
        exit(1);
    }

    dup2(fd, STDIN_FILENO); // input is from file now instead of stdin
    close(fd);

    // read from the file and output to terminal (stdout)
    char buffer[1024];
    int bytes_read;
    while ((bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[bytes_read] = '\0'; // null-terminate the buffer
        printf("%s", buffer);      // output to terminal
    }
}

void single_command(struct cmdline *l)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        printf("error with fork");
    }
    else if (pid == 0)
    {
        // child
        // check for output redirection
        if (l->out != NULL)
        {
            output_redirection(l->out);
        }

        // check for input redirection
        if (l->in != NULL)
        {
            input_redirection(l->in);
        }

        // use execvp to actually execute the command
        execvp(l->seq[0][0], l->seq[0]);
        perror("execvp failed");
        exit(1);
    }
    else
    {
        // parent process, which is running the shell
        // need to check if the parent process is a background process
        if (l->bg)
        {
            // the parent process is a bckground process
            add_bg_process(pid, l->seq[0][0]);
            printf("pid: %d is running in the background\n", pid);
        }
        else
        {
            // parent process isn't a background process so it needs to wait for the child process
            waitpid(pid, NULL, 0);
        }
    }
}

// handles single and multiple pipes as well as multiple commands
void multiple_commands(struct cmdline *l)
{
    int num_of_commands = 0;
    int in_fd = 0;
    int out_fd = 0;
    pid_t pid;

    // find the number of commands
    while (l->seq[num_of_commands] != NULL)
    {
        num_of_commands++;
    }

    // check for input redirection
    if (l->in != NULL)
    {
        in_fd = open(l->in, O_RDONLY);
        if (in_fd < 0)
        {
            printf("error with input redirection in multiple_commands()");
            exit(1);
        }
    }

    // check for output redirection
    if (l->out != NULL)
    {
        out_fd = open(l->out, O_CREAT | O_WRONLY | O_TRUNC, 0664);
        if (out_fd < 0)
        {
            printf("error with output redirection in multiple_commands()");
            exit(1);
        }
    }

    int fds_for_pipes[2]; // fds_for_pipes[0] is read end
    int prev_fd = in_fd;

    for (int i = 0; i < num_of_commands; i++)
    {
        if (i < num_of_commands - 1)
        {
            if (pipe(fds_for_pipes) < 0)
            {
                printf("error with pipe");
                exit(1);
            }
        }

        pid = fork();

        if (pid < 0)
        {
            printf("error with fork in multiple_commands()");
            exit(1);
        }
        else if (pid == 0)
        {
            // child process
            // if its not the first one take input from prev pipe
            if (prev_fd != 0)
            {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            // if it's not the last command send output to next pipe
            if (i < num_of_commands - 1)
            {
                dup2(fds_for_pipes[1], STDOUT_FILENO);
                close(fds_for_pipes[0]);
                close(fds_for_pipes[1]);
            }
            else if (out_fd != 0)
            {
                // if its the last command and there's output redirection send to a file because there aren't any pipes to send to
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }

            execvp(l->seq[i][0], l->seq[i]);
            printf("error with execvp() in multiple_commands()");
            exit(1);
        }
        else
        {
            // parent process
            if (i < num_of_commands - 1)
            {
                close(fds_for_pipes[1]); // if it's a parent process it already sredirectec its output to the next process when it was a child
            }

            prev_fd = fds_for_pipes[0]; // saves this fd so that it can be used as input for next process

            // checks if its a background process
            if (!l->bg)
            {
                wait(NULL);
            }
            else
            {
                add_bg_process(pid, l->seq[i][0]);
            }
        }
    } // end for

    if (in_fd != 0)
    {
        close(in_fd);
    }
    if (out_fd != 0)
    {
        close(out_fd);
    }
}

int main(void)
{

    while (1)
    {
        struct cmdline *l;
        char *line = 0;
        int i, j;
        char *prompt = "myshell >";

        // check for finished background processes
        check_bg_processes();

        /* Readline use some internal memory structure that
           can not be cleaned at the end of the program. Thus
           one memory leak per command seems unavoidable yet */
        line = readline(prompt); // line is a pointer to char (string)
        if (line == 0 || !strncmp(line, "exit", 4))
        {
            terminate(line);
        }
        else if (!strncmp(line, "jobs", 4))
        {
            print_jobs();
        }
        else
        {
            /* parsecmd, free line, and set it up to 0 */
            l = parsecmd(&line);

            /* If input stream closed, normal termination */
            if (l == 0)
            {

                terminate(0);
            }
            else if (l->err != 0)
            {
                /* Syntax error, read another command */
                printf("error: %s\n", l->err);
                continue;
            }
            else
            {

                // check for multiple commands
                if (l->seq[1] != 0)
                {
                    multiple_commands(l);
                }
                else if (l->seq[1] == 0)
                {
                    single_command(l);
                }
            }
        }
    }
}
