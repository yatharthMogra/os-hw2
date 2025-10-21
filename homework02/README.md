# Flow System Implementation

## Component Design and Implementation

This document describes how each component type has been designed and implemented in the flow system.

### 1. NODE Component

**Purpose**: Executes shell commands with proper input/output redirection.

**Implementation**:
- Uses `fork()` and `execvp()` to execute commands
- Handles stdin/stdout redirection via file descriptors
- Supports command parsing with quoted arguments
- Maintains proper stderr separation (stderr stays on stderr, not through pipes)

**Key Features**:
- Command parsing with support for quoted strings
- Proper process management with fork/exec
- Input/output redirection handling

### 2. PIPE Component

**Purpose**: Connects output of one component to input of another component.

**Implementation**:
- Creates Unix pipes using `pipe()` system call
- Uses `fork()` to create separate processes for source and destination
- Handles all component types as both source and destination
- Supports recursive pipe execution (pipes calling other pipes)

**Key Features**:
- Bidirectional pipe support (from/to any component type)
- Recursive pipe execution for complex flows
- Proper file descriptor management
- Input pipe detection for nodes

### 3. FILE Component

**Purpose**: Acts as a data source by reading file content and providing it to the pipeline.

**Implementation**:
- Opens files using `open()` system call
- Reads file content and writes to output stream
- Acts as data source only (cannot be pipe destination)
- Handles file not found errors gracefully

**Key Features**:
- File content streaming
- Error handling for missing files
- Data source semantics (cannot receive input)
- Proper file descriptor management

### 4. CONCATENATE Component

**Purpose**: Executes multiple components sequentially and combines their outputs.

**Implementation**:
- Iterates through all parts sequentially
- Uses `fork()` for each part execution
- Waits for each part to complete before next
- Supports input from pipes when used as destination

**Key Features**:
- Sequential execution of parts
- Process management for each part
- Input handling from pipes
- Output redirection support

### 5. STDERR Component

**Purpose**: Captures stderr output from a node and redirects it to stdout.

**Implementation**:
- Creates pipes to capture stderr from source node
- Redirects stderr to pipe, stdout to /dev/null
- Reads from pipe and outputs to destination
- Uses `dup2()` for proper redirection

**Key Features**:
- Stderr capture and redirection
- Process isolation for stderr capture
- Proper file descriptor management

## Safety and Validation Features

### Cycle Detection
- **Purpose**: Prevents infinite loops in flow graphs
- **Implementation**: DFS-based algorithm to detect recursive cycles
- **Scope**: Detects direct self-reference and recursive pipe chains
- **Validation**: Only flags true recursive cycles, allows valid data flow

### Node Count Validation
- **Purpose**: Ensures flow graphs are executable
- **Implementation**: Counts NODE components before execution
- **Validation**: Requires at least one node for execution

### Pipe Semantics Validation
- **Purpose**: Prevents invalid pipe configurations
- **Implementation**: Validates that file components cannot be pipe destinations
- **Validation**: File components are data sources, not data processors

### Component Existence Validation
- **Purpose**: Ensures all referenced components exist
- **Implementation**: Validates component references in pipes and concatenates
- **Validation**: Aborts on missing component references

## Execution Flow

1. **Parse Flow File**: Parse .flow file into component graph
2. **Validate Graph**: Check for cycles, node count, and semantic validity
3. **Execute Action**: Find and execute the specified action component
4. **Process Management**: Handle forks, pipes, and process coordination
5. **Cleanup**: Wait for all child processes and clean up resources

## Key Design Decisions

- **Process Model**: Each component execution uses fork/exec for isolation
- **Pipe Semantics**: Mimics shell pipe behavior for stdout/stderr separation
- **Error Handling**: Comprehensive validation before execution
- **Memory Management**: Proper cleanup of file descriptors and processes
- **Recursive Execution**: Supports complex nested pipe structures

## Supported Flow Patterns

- **Simple Pipes**: `node1 → node2`
- **File Input**: `file → node`
- **Concatenation**: `concat → node`
- **Stderr Capture**: `stderr → node`
- **Complex Chains**: `file → node1 → node2 → concat`
- **Nested Pipes**: `pipe1 → pipe2 → node`

## Error Conditions Handled

- Missing files
- Invalid component references
- Cyclic dependencies
- Empty flow graphs
- Invalid pipe destinations
- Command execution failures
