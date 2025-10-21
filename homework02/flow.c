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

int detect_cycles(FlowGraph *graph);

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
  // Check if the flow graph has any nodes
  int node_count = 0;
  for (int i = 0; i < graph->count; i++)
  {
    if (graph->components[i].type == NODE)
    {
      node_count++;
    }
  }
  
  if (node_count == 0)
  {
    fprintf(stderr, "Error: Flow graph has no nodes. A flow graph must contain at least one node to be executable.\n");
    exit(1);
  }

  // Check for cycles in the flow graph before executing
  if (detect_cycles(graph))
  {
    fprintf(stderr, "Error: Cycle detected in flow graph. Aborting to prevent infinite loop.\n");
    exit(1);
  }

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

  // Validate pipe semantics
  if (to_comp->type == FILE_COMP)
  {
    fprintf(stderr, "Error: Cannot pipe to file component '%s'. File components are data sources, not data processors.\n", to_comp->name);
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
    else if (from_comp->type == CONCATENATE)
    {
      execute_concatenate(graph, from_comp, pipe_fds[1]);
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
  else if (to_comp->type == CONCATENATE)
  {
    // For concatenate as 'to' component, we need to handle input from pipe
    // We'll read from the pipe and pass it to the concatenate parts
    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(pipe_fds[0], buffer, sizeof(buffer))) > 0)
    {
      write(STDOUT_FILENO, buffer, bytes_read);
    }
    close(pipe_fds[0]);
    
    // Then execute the concatenate parts
    execute_concatenate(graph, to_comp, STDOUT_FILENO);
  }
  else if (to_comp->type == STDERR_COMP)
  {
    execute_stderr(graph, to_comp, STDOUT_FILENO);
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

  // File component should read the file and provide its content as data flow
  // This is the correct behavior for file components - they act as data sources
  
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

// Global variables for cycle detection (static to avoid global namespace pollution)
static int visited[MAX_COMPONENTS];
static int in_path[MAX_COMPONENTS];
static FlowGraph *cycle_graph;

// Helper function to perform DFS from a component
static int dfs_visit(int component_index)
{
  if (in_path[component_index])
  {
    // Found a cycle! This component is already in the current path
    return 1; // Cycle detected
  }
  
  if (visited[component_index])
  {
    // Already visited this component, no cycle from here
    return 0;
  }
  
  // Mark as visited and add to current path
  visited[component_index] = 1;
  in_path[component_index] = 1;
  
  Component *comp = &cycle_graph->components[component_index];
  
  // Check dependencies based on component type
  if (comp->type == PIPE)
  {
    // For pipes, check the 'from' component
    if (comp->from != NULL)
    {
      Component *from_comp = find_component(cycle_graph, comp->from);
      if (from_comp != NULL)
      {
        int from_index = from_comp - cycle_graph->components;
        if (dfs_visit(from_index))
        {
          return 1; // Cycle detected
        }
      }
    }
    // Note: We don't check 'to' component for pipes because data flows in one direction
    // A pipe from A to B is not a cycle, even if B also has a pipe to A
  }
  else if (comp->type == NODE)
  {
    // For nodes, check if there's an input pipe
    Component *input_pipe = find_input_pipe_for_node(cycle_graph, comp->name);
    if (input_pipe != NULL)
    {
      int pipe_index = input_pipe - cycle_graph->components;
      if (dfs_visit(pipe_index))
      {
        return 1; // Cycle detected
      }
    }
  }
  else if (comp->type == CONCATENATE)
  {
    // For concatenate, check all parts
    for (int i = 0; i < comp->parts_count; i++)
    {
      if (comp->parts[i] != NULL)
      {
        Component *part_comp = find_component(cycle_graph, comp->parts[i]);
        if (part_comp != NULL)
        {
          int part_index = part_comp - cycle_graph->components;
          if (dfs_visit(part_index))
          {
            return 1; // Cycle detected
          }
        }
      }
    }
  }
  else if (comp->type == STDERR_COMP)
  {
    // For stderr, check the from_node
    if (comp->from_node != NULL)
    {
      Component *from_node = find_component(cycle_graph, comp->from_node);
      if (from_node != NULL)
      {
        int from_index = from_node - cycle_graph->components;
        if (dfs_visit(from_index))
        {
          return 1; // Cycle detected
        }
      }
    }
  }
  // FILE_COMP and other types don't have dependencies to check
  
  // Remove from current path
  in_path[component_index] = 0;
  return 0; // No cycle found from this component
}

int detect_cycles(FlowGraph *graph)
{
  // Only check for true recursive cycles that would cause infinite loops:
  // 1. Pipes that call themselves directly
  // 2. Pipes that create recursive call chains (pipe1 → pipe2 → pipe1)
  
  for (int i = 0; i < graph->count; i++)
  {
    Component *comp = &graph->components[i];
    
    // Check for direct self-reference in pipes
    if (comp->type == PIPE && comp->from != NULL && comp->to != NULL)
    {
      if (strcmp(comp->from, comp->name) == 0 || strcmp(comp->to, comp->name) == 0)
      {
        return 1; // Direct self-reference cycle
      }
    }
  }
  
  // Check for recursive pipe chains (pipe calling other pipes)
  for (int i = 0; i < graph->count; i++)
  {
    Component *comp = &graph->components[i];
    if (comp->type == PIPE && comp->from != NULL)
    {
      Component *from_comp = find_component(graph, comp->from);
      if (from_comp != NULL && from_comp->type == PIPE)
      {
        // This pipe's 'from' is another pipe, check if it creates a cycle
        if (strcmp(from_comp->name, comp->name) == 0)
        {
          return 1; // Direct pipe-to-pipe cycle
        }
        
        // Check if the from pipe eventually leads back to this pipe
        Component *current = from_comp;
        int depth = 0;
        while (current != NULL && current->type == PIPE && depth < MAX_COMPONENTS)
        {
          if (strcmp(current->name, comp->name) == 0)
          {
            return 1; // Recursive pipe cycle detected
          }
          if (current->from != NULL)
          {
            current = find_component(graph, current->from);
          }
          else
          {
            current = NULL;
          }
          depth++;
        }
      }
    }
  }
  
  return 0; // No cycles found
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