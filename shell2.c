// <Adel Alkhamisy>
// <Adel.Alkhamisy@bison.howard.edu>

/**
 * Enhanced Shell Implementation
 * 
 * This program implements a basic Unix shell with built-in commands,
 * process execution, background processes, piping, I/O redirection,
 * signal handling, and timeouts for long-running processes.
 *
 * Built-in commands:
 * - cd: changes the current working directory
 * - pwd: prints the current working directory
 * - echo: prints a message and environment variables
 * - exit: terminates the shell
 * - env: prints current values of environment variables
 * - setenv: sets an environment variable
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>

#define MAX_INPUT_LENGTH 1024
#define MAX_ARGUMENTS 128
#define WORKING_DIR_BUFFER_SIZE 400
#define TIMEOUT_SECONDS 10

char SHELL_PROMPT[] = "> ";
char TOKEN_DELIMITERS[] = " \t\r\n";
extern char **environ;
int foreground_process_id = -1;

void execute_command_with_pipes_and_redirection(char* command_arguments[]);
void process_token_quotes(char* token);
void terminate_after_timeout(int seconds, int process_id);
void handle_interrupt_signal(int signal_number);
void execute_single_command(char* args[]);

/**
 * Main function - Shell entry point
 * Processes user input and executes commands
 */
int main() {
  char user_input_buffer[MAX_INPUT_LENGTH];
  
  /* Buffer for working directory path */
  char working_directory_buffer[WORKING_DIR_BUFFER_SIZE];
  char *working_directory_path;
  
  /* Stores the tokenized command line input */
  char *command_arguments[MAX_ARGUMENTS];
  
  /* Set up signal handler for Ctrl+C */
  signal(SIGINT, handle_interrupt_signal);

  int argument_index;
  int part_index;
  int echo_index;
  char* env_var_parts[2];
  char* final_argument;
  bool is_background_process;
  int child_pid;
  int timeout_process_id;
  int exit_status;
  char* env_value;
  char* home_directory;
  size_t input_length;

  while (true) {
    /* Print the shell prompt with current working directory */
    fflush(stdout);
    working_directory_path = getcwd(working_directory_buffer, WORKING_DIR_BUFFER_SIZE);
    if (working_directory_path == NULL) {
        perror("getcwd");
        exit(1);
    }
    printf("%s %s", working_directory_path, SHELL_PROMPT);
    fflush(stdout);

    /* Read one line of input from stdin */
    if ((fgets(user_input_buffer, MAX_INPUT_LENGTH, stdin) == NULL)) {
        if (ferror(stdin)) {
            fprintf(stderr, "Error reading input\n");
            exit(1);
        }
        if (feof(stdin)) {
            printf("exit\n");
            exit(0);
        }
    }

    /* Remove trailing newline character */
    input_length = strlen(user_input_buffer);
    if (input_length > 0 && user_input_buffer[input_length-1] == '\n') {
        user_input_buffer[input_length-1] = '\0';
    }
    
    /* Skip processing if line is empty */
    if (strlen(user_input_buffer) == 0) {
        continue;
    }

    /* Tokenize the input */
    command_arguments[0] = strtok(user_input_buffer, TOKEN_DELIMITERS);
    argument_index = 0;
    while(command_arguments[argument_index] != NULL) {
      process_token_quotes(command_arguments[argument_index]);
      argument_index++;
      command_arguments[argument_index] = strtok(NULL, TOKEN_DELIMITERS);
    }
    
    /* Skip processing if no command */
    if (command_arguments[0] == NULL) {
      continue;
    }
    
    /* Assert we have a valid command */
    assert(command_arguments[0] != NULL);
    
    /* Check for background process request */
    final_argument = command_arguments[argument_index-1];
    is_background_process = false;
    if (strcmp(final_argument, "&") == 0) {
       is_background_process = true;
       command_arguments[argument_index-1] = NULL;
    }
    
    /* Handle built-in commands */
    if (strcmp(command_arguments[0], "cd") == 0) {
      if (command_arguments[1] == NULL) {
        /* Change to HOME directory if no argument */
        home_directory = getenv("HOME");
        if (home_directory != NULL) {
          if (chdir(home_directory) != 0) {
            perror("cd");
          }
        } else {
          fprintf(stderr, "cd: HOME not set\n");
        }
      } else if (chdir(command_arguments[1]) != 0) {
        perror("cd");
      }
    }
    else if (strcmp(command_arguments[0], "pwd") == 0) {
      working_directory_path = getcwd(working_directory_buffer, WORKING_DIR_BUFFER_SIZE);
      if (working_directory_path == NULL) {
        perror("pwd");
      } else {
        printf("%s\n", working_directory_path);
      }
    }
    else if (strcmp(command_arguments[0], "echo") == 0) {
      echo_index = 1;
      while (command_arguments[echo_index] != NULL) {
        if (command_arguments[echo_index][0] == '$') {
          env_value = getenv(command_arguments[echo_index] + 1);
          if (env_value != NULL) {
            printf("%s ", env_value);
          } else {
            printf(" "); /* Empty string if variable not found */
          }
        }
        else {
          printf("%s ", command_arguments[echo_index]);
        }
        echo_index++;
      }
      printf("\n");
    }
    else if (strcmp(command_arguments[0], "exit") == 0) {
      exit(0);
    }
    else if (strcmp(command_arguments[0], "env") == 0) {
      if (command_arguments[1] != NULL) {
        env_value = getenv(command_arguments[1]);
        if (env_value != NULL) {
          printf("%s\n", env_value);
        } else {
          printf("\n"); /* Print empty line if variable not found */
        }
      }
      else {
        char** environment_variables = environ;
        for (; *environment_variables; environment_variables++)
          printf("%s\n", *environment_variables);
      }
    }
    else if (strcmp(command_arguments[0], "setenv") == 0) {
      if (command_arguments[1] == NULL) {
        fprintf(stderr, "setenv: missing argument\n");
        continue;
      }
      env_var_parts[0] = strtok(command_arguments[1], "=");
      part_index = 0;
      while (env_var_parts[part_index] != NULL && part_index < 2) {
        part_index++;
        env_var_parts[part_index] = strtok(NULL, "=");
      }
      if (env_var_parts[0] == NULL || env_var_parts[1] == NULL) {
        fprintf(stderr, "setenv: invalid format. Use NAME=VALUE\n");
        continue;
      }
      if (setenv(env_var_parts[0], env_var_parts[1], 1) != 0) {
        perror("setenv");
      }
    }
    else {
      /* External command execution */
      child_pid = fork();
      if (child_pid < 0) {
        perror("fork");
        continue;
      }
      
      if (child_pid == 0) {
        /* Child process */
        signal(SIGINT, SIG_DFL);
        execute_command_with_pipes_and_redirection(command_arguments);
        /* If execute_command returns, an error occurred */
        exit(1);
      }
      else {
        /* Parent process */
        if (is_background_process) {
          foreground_process_id = -1;
          printf("[%d] Background process started\n", child_pid);
        }
        else {
          foreground_process_id = child_pid;
          
          /* Create timeout process */
          timeout_process_id = fork();
          if (timeout_process_id < 0) {
            perror("fork for timeout process");
            /* Continue with main process even if timeout fork fails */
          } else if (timeout_process_id == 0) {
            /* Timeout process */
            signal(SIGINT, SIG_DFL);
            terminate_after_timeout(TIMEOUT_SECONDS, child_pid);
            exit(0);
          }
          else {
            /* Wait for child to complete */
            waitpid(child_pid, &exit_status, 0);
            
            /* Kill the timeout process */
            kill(timeout_process_id, SIGINT);
            waitpid(timeout_process_id, NULL, 0);
            
            /* Report if process terminated with error */
            if (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) != 0) {
              fprintf(stderr, "Process exited with status %d\n", WEXITSTATUS(exit_status));
            }
          }
        }
      }
    }
  }
  /* This should never be reached */
  return -1;
}

/**
 * Wait for specified time, then kill the process if still running
 * @param seconds Time in seconds to wait
 * @param process_id Process ID to kill
 */
void terminate_after_timeout(int seconds, int process_id) {
  sleep(seconds);
  printf("Foreground process timed out after %d seconds.\n", seconds);
  kill(process_id, SIGINT);
}

/**
 * Process command tokens, handling quoted strings
 * Removes surrounding quotes from tokens
 * @param token Token to process
 */
void process_token_quotes(char* token) {
  bool has_quotes = false;
  char opening_quote;
  char closing_quote;
  int char_index;
  
  if (token[0] == '\"' || token[0] == '\'') {
    opening_quote = token[0];
    has_quotes = true;
  }
  
  if (has_quotes) {
    char_index = 0;
    while (token[char_index] != '\0') {
      closing_quote = token[char_index];
      char_index++;
    }
    
    if (opening_quote == closing_quote) { 
      /* Remove the quotes */
      char_index = 1;
      while (token[char_index] != '\0') {
        token[char_index-1] = token[char_index];
        char_index++;
      }
      token[char_index-1] = '\0';
      token[char_index-2] = '\0';
    }
  }
}

/**
 * Execute a single command with I/O redirection
 * @param args Command and arguments array
 */
void execute_single_command(char* args[]) {
  int i;
  int input_fd, output_fd;
  
  /* Process I/O redirection for this command */
  for (i = 0; args[i] != NULL; i++) {
    /* Output redirection */
    if (strcmp(args[i], ">") == 0 && args[i+1] != NULL) {
      output_fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (output_fd < 0) {
        perror("open");
        exit(1);
      }
      if (dup2(output_fd, STDOUT_FILENO) < 0) {
        perror("dup2");
        close(output_fd);
        exit(1);
      }
      close(output_fd);
      args[i] = NULL; /* Terminate args at redirection symbol */
      break;
    }
    
    /* Input redirection */
    if (strcmp(args[i], "<") == 0 && args[i+1] != NULL) {
      input_fd = open(args[i+1], O_RDONLY);
      if (input_fd < 0) {
        perror("open");
        exit(1);
      }
      if (dup2(input_fd, STDIN_FILENO) < 0) {
        perror("dup2");
        close(input_fd);
        exit(1);
      }
      close(input_fd);
      args[i] = NULL; /* Terminate args at redirection symbol */
      break;
    }
  }
  
  /* Execute the command */
  if (args[0] != NULL) {
    execvp(args[0], args);
    perror("execvp");
    exit(1);
  }
}

/**
 * Execute command with arguments
 * Handles piping and I/O redirection
 * @param command_arguments Command and arguments array
 */
void execute_command_with_pipes_and_redirection(char* command_arguments[]) {
  char* commands_by_pipe[MAX_ARGUMENTS][MAX_ARGUMENTS];
  int pipe_file_descriptors[MAX_ARGUMENTS][2];
  int process_ids[MAX_ARGUMENTS];
  int arg_index, pipe_index, cmd_index, j, proc_index;
  int pipe_command_count, command_token_count, num_pipes;
  
  /* Split commands by pipe symbol */
  arg_index = 0;
  pipe_command_count = 0;
  command_token_count = 0;
  
  while (command_arguments[arg_index] != NULL) {
    if (strcmp(command_arguments[arg_index], "|") == 0) {
      if (command_token_count == 0) {
        printf("Invalid pipe command\n");
        return;
      }
      commands_by_pipe[pipe_command_count][command_token_count] = NULL;
      
      pipe_command_count++;
      command_token_count = 0;
    }
    else {
      commands_by_pipe[pipe_command_count][command_token_count] = command_arguments[arg_index];
      command_token_count++;
    }
    arg_index++;
  }
  
  /* Ensure the last command is properly terminated */
  commands_by_pipe[pipe_command_count][command_token_count] = NULL;
  
  num_pipes = pipe_command_count;
  pipe_command_count++; /* Total commands = number of pipes + 1 */
  
  if (num_pipes == 0) {
    /* Simple command with no pipes - handle redirection and execute */
    execute_single_command(commands_by_pipe[0]);
    /* If we get here, execution failed */
    return;
  }
  
  /* Create pipes */
  for (pipe_index = 0; pipe_index < num_pipes; pipe_index++) {
    if (pipe(pipe_file_descriptors[pipe_index]) < 0) {
      perror("pipe");
      return;
    }
  }
  
  /* Create processes for each command in the pipeline */
  for (cmd_index = 0; cmd_index < pipe_command_count; cmd_index++) {
    process_ids[cmd_index] = fork();
    
    if (process_ids[cmd_index] == 0) {
      /* Child process */
      if (cmd_index == 0) {
        /* First command: output to pipe */
        if (dup2(pipe_file_descriptors[0][1], STDOUT_FILENO) < 0) {
          perror("dup2");
          exit(1);
        }
        close(pipe_file_descriptors[0][0]);
        for (j = 1; j < num_pipes; j++) {
          close(pipe_file_descriptors[j][0]);
          close(pipe_file_descriptors[j][1]);
        }
      }
      else if (cmd_index == num_pipes) {
        /* Last command: input from pipe */
        if (dup2(pipe_file_descriptors[num_pipes-1][0], STDIN_FILENO) < 0) {
          perror("dup2");
          exit(1);
        }
        close(pipe_file_descriptors[num_pipes-1][1]);
        for (j = 0; j < num_pipes-1; j++) {
          close(pipe_file_descriptors[j][0]);
          close(pipe_file_descriptors[j][1]);
        }
      }
      else {
        /* Middle command: input from previous pipe, output to next pipe */
        if (dup2(pipe_file_descriptors[cmd_index-1][0], STDIN_FILENO) < 0 || 
            dup2(pipe_file_descriptors[cmd_index][1], STDOUT_FILENO) < 0) {
          perror("dup2");
          exit(1);
        }
        for (j = 0; j < num_pipes; j++) {
          if (j == cmd_index-1) {
            close(pipe_file_descriptors[j][1]);
          }
          else if (j == cmd_index) {
            close(pipe_file_descriptors[j][0]);
          }
          else {
            close(pipe_file_descriptors[j][0]);
            close(pipe_file_descriptors[j][1]);
          }
        }
      }
      
      /* Execute the command with its own I/O redirection */
      execute_single_command(commands_by_pipe[cmd_index]);
      /* If we get here, execution failed */
      exit(1);
    }
    else if (process_ids[cmd_index] < 0) {
      perror("fork");
      return;
    }
  }
  
  /* Parent process: close all pipe ends */
  for (pipe_index = 0; pipe_index < num_pipes; pipe_index++) {
    close(pipe_file_descriptors[pipe_index][0]);
    close(pipe_file_descriptors[pipe_index][1]);
  }
  
  /* Wait for all child processes */
  for (proc_index = 0; proc_index < pipe_command_count; proc_index++) {
    waitpid(process_ids[proc_index], NULL, 0);
  }
}

/**
 * Signal handler for SIGINT (Ctrl+C)
 * Kills the foreground process but keeps the shell running
 * @param signal_number Signal number (SIGINT)
 */
void handle_interrupt_signal(int signal_number) { 
  if (foreground_process_id != -1) {
    kill(foreground_process_id, SIGINT);
  }
}