#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  char **args;
  char *infile;
  char **outfile;
  int append;
  int background;
} Command;

typedef struct {
  Command *commands;
  int num_commands;
} Pipeline;

char *lsh_read_line(void) {
  char *line = NULL;
  size_t bufsize = 0;

  if (getline(&line, &bufsize, stdin) == -1) {
    if (feof(stdin)) {
      exit(EXIT_SUCCESS);
    } else {
      perror("readline");
      exit(EXIT_FAILURE);
    }
  }

  return line;
}

#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"
char **lsh_split_line(char *line) {
  int bufsize = LSH_TOK_BUFSIZE, position = 0;
  char **tokens = (char **)malloc(bufsize * sizeof(char *));
  char *token;

  if (!tokens) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  token = strtok(line, LSH_TOK_DELIM);
  while (token != NULL) {
    tokens[position++] = token;

    if (position >= bufsize) {
      bufsize += LSH_TOK_BUFSIZE;
      tokens = (char **)realloc(tokens, bufsize * sizeof(char *));
    }
    if (!tokens) {
      fprintf(stderr, "lsh: allocation error\n");
      exit(EXIT_FAILURE);
    }

    token = strtok(NULL, LSH_TOK_DELIM);
  }

  tokens[position] = NULL;
  return tokens;
}

Command *lsh_parse_redirect(char *line) {
  int command_bufsize = LSH_TOK_BUFSIZE, outfile_bufsize = LSH_TOK_BUFSIZE,
      command_position = 0, outfile_position = 0;
  char **tokens = lsh_split_line(line);
  char *token;
  Command *command = (Command *)malloc(sizeof(Command));
  command->args = (char **)malloc(sizeof(char *) * command_bufsize);
  command->outfile = (char **)malloc(sizeof(char *) * outfile_bufsize);

  if (!command || !command->args) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  int i;
  for (i = 0; tokens[i] != NULL; i++) {
    token = strtok(tokens[i], ">");
    while (token != NULL) {
      if (token - tokens[i] == 0 &&
          (i == 0 || strcmp(tokens[i - 1], ">") != 0)) {
        command->args[command_position++] = token;

        if (command_position >= command_bufsize) {
          command_bufsize += LSH_TOK_BUFSIZE;
          command->args =
              (char **)realloc(command->args, sizeof(char *) * command_bufsize);
        }

        if (!command->args) {
          fprintf(stderr, "lsh: allocation error\n");
          exit(EXIT_FAILURE);
        }
      } else {
        command->outfile[outfile_position++] = token;
        if (outfile_position >= outfile_bufsize) {
          outfile_bufsize += LSH_TOK_BUFSIZE;
          command->outfile = (char **)realloc(command->outfile,
                                              sizeof(char *) * outfile_bufsize);
        }
        if (!command->outfile) {
          fprintf(stderr, "lsh: allocation error\n");
          exit(EXIT_FAILURE);
        }
      }
      token = strtok(NULL, ">");
    }
  }

  free(tokens);

  return command;
}

void copy_file(char* src_file, char* dest_file){
  int src_fd = open(src_file, O_RDONLY);
    if (src_fd == -1) {
        perror("Error opening source file");
        exit(EXIT_FAILURE);
    }

    int dest_fd = open(dest_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (dest_fd == -1) {
        perror("Error opening/creating destination file");
        close(src_fd);
        exit(EXIT_FAILURE);
    }

    char* buffer = (char*)malloc(sizeof(char)*1024);
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Error writing to destination file");
            close(src_fd);
            close(dest_fd);
            exit(EXIT_FAILURE);
        }
    }

    if (bytes_read == -1) {
        perror("Error reading from source file");
        exit(EXIT_FAILURE);
    }

    free(buffer);
    close(src_fd);
    close(dest_fd);
}

int lsh_launch(Command *command) {
  pid_t pid, wpid;
  int status;

  pid = fork();
  if (pid == 0) {

    if(command->outfile[0] != NULL){
      close(STDOUT_FILENO);
      int fd = open(command->outfile[0], O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
      if(fd == -1){
        fprintf(stderr, "lsh: open file fail");
        exit(EXIT_FAILURE);
      }
    }
    
    if (execvp(command->args[0], command->args) == -1) {
      perror("lsh");
    }
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    perror("lsh");
  } else {
    do {
      wpid = waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    for(int i = 1; command->outfile[i] != NULL; i++) copy_file(command->outfile[0], command->outfile[i]);
    
  }

  return 1;
}

int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);

char *builtin_str[] = {"cd", "help", "exit"};

int (*builtin_func[])(char **) = {&lsh_cd, &lsh_help, &lsh_exit};

int lsh_num_builtins() { return sizeof(builtin_str) / sizeof(char *); }

/*
  Builtin function implementations.
*/
int lsh_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"cd\"\n");
  } else {
    if (chdir(args[1]) != 0) {
      perror("lsh");
    }
  }
  return 1;
}

int lsh_help(char **args) {
  int i;
  printf("make my own shell (by Stephen Brennan)\n");
  printf("Type program names and arguments, and hit enter.\n");
  printf("The following are built in:\n");

  for (i = 0; i < lsh_num_builtins(); i++) {
    printf("  %s\n", builtin_str[i]);
  }

  printf("Use the man command for information on other programs.\n");
  return 1;
}

int lsh_exit(char **args) { return 0; }

int lsh_execute(Command *command) {
  int i;
  if (command->args[0] == NULL) {
    return 1;
  }

  for (int i = 0; i < lsh_num_builtins(); i++) {
    if (strcmp(command->args[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(command->args);
    }
  }

  return lsh_launch(command);
}

void lsh_loop(void) {
  char *line;
  char **args;
  Command *command;
  int status = 1;

  do {
    printf("> ");
    line = lsh_read_line();
    command = lsh_parse_redirect(line);
    status = lsh_execute(command);

    free(line);
    // free(args);
  } while (status);
}

int main(int argc, char **argv) {
  lsh_loop();

  return EXIT_SUCCESS;
}
