#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_COMPONENTS 100
#define MAX_PARTS 20

typedef enum
{
  NODE,
  PIPE,
  CONCATENATE,
  STDERR_COMP,
  FILE_COMP
} ComponentType;

typedef struct
{
  ComponentType type;
  char *name;

  char *command;          // For NODE type:
  char *from;             // For PIPE type:
  char *to;               // For PIPE type:
  int parts_count;        // For CONCATENATE type:
  char *parts[MAX_PARTS]; // For CONCATENATE type:
  char *from_node;        // For STDERR type:
  char *filename;         // For FILE type (extra credit):
} Component;

typedef struct
{
  Component components[MAX_COMPONENTS];
  int count;
} FlowGraph;

FlowGraph parse_flow_file(const char *filename);

Component *find_component(FlowGraph *graph, const char *name);

Component *find_input_pipe_for_node(FlowGraph *graph, const char *node_name);

void execute_action(FlowGraph *graph, const char *action_name);

void execute_pipe(FlowGraph *graph, Component *pipe_comp);

void execute_node(FlowGraph *graph, Component *node, int input_fd, int output_fd);

void execute_concatenate(FlowGraph *graph, Component *concat, int output_fd);

void execute_stderr(FlowGraph *graph, Component *stderr_comp, int output_fd);

void execute_file(FlowGraph *graph, Component *file_comp, int input_fd, int output_fd);

char **parse_command(const char *command);

char *trim_whitespace(char *str);

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    fprintf(stderr, "Usage: %s <flow_file> <action_name>\n", argv[0]);
    fprintf(stderr, "Example: %s filecount.flow doit\n", argv[0]);
    exit(0);
  }

  const char *flow_filename = argv[1];
  const char *action_name = argv[2];

  FlowGraph graph = parse_flow_file(flow_filename);
  execute_action(&graph, action_name);

  // TODO: Free allocated memory (important to prevent memory leaks!)

  return 0;
}

FlowGraph parse_flow_file(const char *filename)
{
  FlowGraph graph;
  graph.count = 0;

  FILE *file = fopen(filename, "r");
  if (file == NULL)
  {
    fprintf(stderr, "Error: Cannot open file %s\n", filename);
    exit(1);
  }

  char line[MAX_LINE];

  Component *current = NULL;
  while (fgets(line, sizeof(line), file) != NULL)
  {
    line[strcspn(line, "\n")] = '\0';
    if (strlen(line) == 0)
      continue;

    char *key = strtok(line, "=");
    char *value = strtok(NULL, "=");

    if (key == NULL || value == NULL)
      continue;

    key = trim_whitespace(key);
    value = trim_whitespace(value);
    if (strcmp(key, "node") == 0)
    {
      current = &graph.components[graph.count++];
      current->type = NODE;
      current->name = strdup(value);
      current->command = NULL;
    }
    else if (strcmp(key, "command") == 0)
    {
      if (current != NULL && current->type == NODE)
      {
        current->command = strdup(value);
      }
    }
    else if (strcmp(key, "pipe") == 0)
    {
      current = &graph.components[graph.count++];
      current->type = PIPE;
      current->name = strdup(value);
      current->from = NULL;
      current->to = NULL;
    }
    else if (strcmp(key, "from") == 0)
    {
      if (current != NULL)
      {
        if (current->type == STDERR_COMP)
        {
          current->from_node = strdup(value);
        }
        else
        {
          current->from = strdup(value);
        }
      }
    }
    else if (strcmp(key, "to") == 0)
    {
      if (current)
      {
        current->to = strdup(value);
      }
    }
    else if (strcmp(key, "concatenate") == 0)
    {
      current = &graph.components[graph.count++];
      current->type = CONCATENATE;
      current->name = strdup(value);
      current->parts_count = 0;
      for (int i = 0; i < MAX_PARTS; i++)
      {
        current->parts[i] = NULL;
      }
    }
    else if (strcmp(key, "stderr") == 0)
    {
      current = &graph.components[graph.count++];
      current->type = STDERR_COMP;
      current->name = strdup(value);
      current->from_node = NULL;
    }
    else if (strcmp(key, "file") == 0)
    {
      current = &graph.components[graph.count++];
      current->type = FILE_COMP;
      current->name = strdup(value);
      current->filename = NULL;
    }
    else if (strcmp(key, "parts") == 0)
    {
      if (current)
        current->parts_count = atoi(value);
    }
    else if (strncmp(key, "part_", 5) == 0)
    {
      if (current != NULL && current->type == CONCATENATE)
      {
        int index = -1;
        if (sscanf(key, "part_%d", &index) == 1)
        {
          if (index >= 0 && index < current->parts_count)
          {
            current->parts[index] = strdup(value);
          }
        }
      }
    }
    else if (strcmp(key, "name") == 0)
    {
      if (current != NULL && current->type == FILE_COMP)
      {
        current->filename = strdup(value);
      }
    }
  }

  fclose(file);
  return graph;
}

void execute_action(FlowGraph *graph, const char *action_name)
{
  Component *action = find_component(graph, action_name);

  if (action == NULL)
  {
    fprintf(stderr, "Error: Action '%s' not found\n", action_name);
    exit(1);
  }
  if (action->type == PIPE)
  {
    execute_pipe(graph, action);
  }
  else if (action->type == NODE)
  {
    execute_node(graph, action, STDIN_FILENO, STDOUT_FILENO);
  }
  else if (action->type == CONCATENATE)
  {
    execute_concatenate(graph, action, STDOUT_FILENO);
  }
  else if (action->type == STDERR_COMP)
  {
    execute_stderr(graph, action, STDOUT_FILENO);
  }
  else if (action->type == FILE_COMP)
  {
    execute_file(graph, action, STDIN_FILENO, STDOUT_FILENO);
  }

  // Wait for all child processes to finish
  // -1 means wait for any child process
  // This prevents zombie processes
  while (wait(NULL) > 0)
    ;
}

void execute_pipe(FlowGraph *graph, Component *pipe_comp)
{
  // pipe_fds[0] is the read end, pipe_fds[1] is the write end
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1)
  {
    perror("pipe");
    exit(1);
  }

  Component *from_comp = find_component(graph, pipe_comp->from);
  Component *to_comp = find_component(graph, pipe_comp->to);

  if (from_comp == NULL || to_comp == NULL)
  {
    fprintf(stderr, "Error: Invalid pipe components\n");
    exit(1);
  }

  pid_t pid = fork();

  if (pid == -1)
  {
    perror("fork");
    exit(1);
  }
  else if (pid == 0)
  {
    // CHILD PROCESS
    close(pipe_fds[0]);
    if (from_comp->type == NODE)
    {
      // Check if this node has an input pipe
      Component *input_pipe = find_input_pipe_for_node(graph, from_comp->name);
      if (input_pipe != NULL)
      {
        // Execute the input pipe first to provide input to this node
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[1]);
        execute_pipe(graph, input_pipe);
      }
      else
      {
        execute_node(graph, from_comp, STDIN_FILENO, pipe_fds[1]);
      }
    }
    else if (from_comp->type == STDERR_COMP)
    {
      execute_stderr(graph, from_comp, pipe_fds[1]);
    }
    else if (from_comp->type == FILE_COMP)
    {
      execute_file(graph, from_comp, -1, pipe_fds[1]);
    }
    else if (from_comp->type == PIPE)
    {
      dup2(pipe_fds[1], STDOUT_FILENO);
      close(pipe_fds[1]);
      execute_pipe(graph, from_comp);
    }
    // TODO: Handle other types

    close(pipe_fds[1]);
    exit(0);
  }

  // PARENT PROCESS
  close(pipe_fds[1]);
  if (to_comp->type == NODE)
  {
    execute_node(graph, to_comp, pipe_fds[0], STDOUT_FILENO);
  }
  else if (to_comp->type == FILE_COMP)
  {
    execute_file(graph, to_comp, pipe_fds[0], STDOUT_FILENO);
  }
  // TODO: Handle other types

  close(pipe_fds[0]);
}

void execute_node(FlowGraph *graph, Component *node, int input_fd, int output_fd)
{
  if (node->command == NULL)
  {
    fprintf(stderr, "Error: Node %s has no command\n", node->name);
    exit(1);
  }

  pid_t pid = fork();

  if (pid == -1)
  {
    perror("fork");
    exit(1);
  }
  else if (pid == 0)
  {
    // CHILD PROCESS
    if (input_fd != STDIN_FILENO)
    {
      dup2(input_fd, STDIN_FILENO);
      close(input_fd);
    }

    if (output_fd != STDOUT_FILENO)
    {
      dup2(output_fd, STDOUT_FILENO);
      close(output_fd);
    }

    // Parse the command into arguments
    char **args = parse_command(node->command);

    execvp(args[0], args);
    perror("execvp");
    exit(1);
  }

  // PARENT PROCESS - just returns (the child does the work)
}

void execute_concatenate(FlowGraph *graph, Component *concat, int output_fd)
{
  for (int i = 0; i < concat->parts_count; i++)
  {
    Component *part_comp = find_component(graph, concat->parts[i]);
    if (part_comp == NULL)
    {
      fprintf(stderr, "Error: Component '%s' in concatenate not found\n", concat->parts[i]);
      continue;
    }

    pid_t pid = fork();

    if (pid == -1)
    {
      perror("fork");
      exit(1);
    }
    else if (pid == 0)
    {
      if (output_fd != STDOUT_FILENO)
      {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
      }
      execute_action(graph, part_comp->name);
      exit(0);
    }
    else
    {
      waitpid(pid, NULL, 0);
    }
  }
}

void execute_stderr(FlowGraph *graph, Component *stderr_comp, int output_fd)
{
  if (stderr_comp->from_node == NULL)
  {
    fprintf(stderr, "Error: stderr component '%s' has no from_node\n", stderr_comp->name);
    exit(1);
  }

  Component *from_node = find_component(graph, stderr_comp->from_node);
  if (from_node == NULL)
  {
    fprintf(stderr, "Error: stderr component '%s' references unknown node '%s'\n",
            stderr_comp->name, stderr_comp->from_node);
    exit(1);
  }

  if (from_node->type != NODE)
  {
    fprintf(stderr, "Error: stderr component '%s' from_node '%s' is not a NODE\n",
            stderr_comp->name, stderr_comp->from_node);
    exit(1);
  }

  // Create a pipe to capture stderr from the node
  int pipe_fds[2];
  if (pipe(pipe_fds) == -1)
  {
    perror("pipe");
    exit(1);
  }

  pid_t pid = fork();
  if (pid == -1)
  {
    perror("fork");
    exit(1);
  }
  else if (pid == 0)
  {
    // CHILD PROCESS - execute the node with stderr redirected to pipe
    close(pipe_fds[0]); // close read end

    // Redirect stderr to the pipe write end
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);

    // Redirect stdout to /dev/null to suppress it
    int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null != -1)
    {
      dup2(dev_null, STDOUT_FILENO);
      close(dev_null);
    }

    // Execute the node
    execute_node(graph, from_node, STDIN_FILENO, STDOUT_FILENO);
    exit(0);
  }
  else
  {
    // PARENT PROCESS - read from pipe and write to output
    close(pipe_fds[1]); // close write end

    // Read from the pipe and write to output
    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer))) > 0)
    {
      if (output_fd != STDOUT_FILENO)
      {
        write(output_fd, buffer, bytes_read);
      }
      else
      {
        write(STDOUT_FILENO, buffer, bytes_read);
      }
    }

    close(pipe_fds[0]);
    waitpid(pid, NULL, 0);
  }
}

void execute_file(FlowGraph *graph, Component *file_comp, int input_fd, int output_fd)
{
  if (file_comp->filename == NULL)
  {
    fprintf(stderr, "Error: file component '%s' has no filename\n", file_comp->name);
    exit(1);
  }

  // File component should act as a source of data by reading from the file
  // and providing it to the next component in the pipe
  // This is different from directly executing file operations - we're just
  // providing the file content as data flow
  
  // Open the file for reading
  int file_fd = open(file_comp->filename, O_RDONLY);
  if (file_fd == -1)
  {
    perror("open");
    fprintf(stderr, "%s: No such file or directory", file_comp->filename);
    exit(1);
  }

  // If we have input from a pipe, we need to handle both input and file
  if (input_fd != STDIN_FILENO && input_fd != -1)
  {
    // We have input from a pipe, so we need to read from both input and file
    // For now, we'll prioritize the file content over pipe input
    // This could be enhanced to concatenate or merge the inputs
    close(input_fd);
  }

  // Read from file and write to output (this is the data flow)
  char buffer[1024];
  ssize_t bytes_read;
  while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
  {
    if (output_fd != STDOUT_FILENO)
    {
      write(output_fd, buffer, bytes_read);
    }
    else
    {
      write(STDOUT_FILENO, buffer, bytes_read);
    }
  }

  close(file_fd);
}

Component *find_component(FlowGraph *graph, const char *name)
{
  for (int i = 0; i < graph->count; i++)
  {
    if (strcmp(graph->components[i].name, name) == 0)
    {
      return &graph->components[i];
    }
  }
  return NULL;
}

Component *find_input_pipe_for_node(FlowGraph *graph, const char *node_name)
{
  for (int i = 0; i < graph->count; i++)
  {
    if (graph->components[i].type == PIPE && 
        graph->components[i].to != NULL &&
        strcmp(graph->components[i].to, node_name) == 0)
    {
      return &graph->components[i];
    }
  }
  return NULL;
}

char **parse_command(const char *command)
{
  char **args = malloc(MAX_ARGS * sizeof(char *));
  char *cmd_copy = strdup(command);

  int i = 0;
  char *start = cmd_copy;
  char *end = cmd_copy;

  while (*start != '\0' && i < MAX_ARGS - 1)
  {
    // Skip leading whitespace
    while (*start == ' ' || *start == '\t')
      start++;

    if (*start == '\0')
      break;

    end = start;

    if (*start == '\'' || *start == '"')
    {
      // Handle quoted strings
      char quote = *start;
      start++; // Skip opening quote
      end = start;

      // Find closing quote
      while (*end != '\0' && *end != quote)
        end++;

      if (*end == quote)
      {
        *end = '\0'; // Null-terminate the argument
        args[i++] = strdup(start);
        start = end + 1;
      }
      else
      {
        // No closing quote found, treat as unquoted
        start = end;
        while (*end != '\0' && *end != ' ' && *end != '\t')
          end++;
        *end = '\0';
        args[i++] = strdup(start);
        start = end + 1;
      }
    }
    else
    {
      // Handle unquoted strings
      while (*end != '\0' && *end != ' ' && *end != '\t')
        end++;
      *end = '\0';
      args[i++] = strdup(start);
      start = end + 1;
    }
  }

  args[i] = NULL;
  free(cmd_copy);
  return args;
}

char *trim_whitespace(char *str)
{
  // Trim leading whitespace
  while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')
    str++;

  // If string is all whitespace
  if (*str == '\0')
    return str;

  // Trim trailing whitespace
  char *end = str + strlen(str) - 1;
  while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
    end--;
  *(end + 1) = '\0';

  return str;
}