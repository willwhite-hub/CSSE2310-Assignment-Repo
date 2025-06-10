/* CSSE2310 2025 Assignment Three
 *
 * Written by William White
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <csse2310a3.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

/* -------------------------------------------------------------------------- */
// Constants
#define SIG_INT_OFFSET 128

// Exit messages
const char* const usageErrorMessage
        = "Usage: ./uqparallel [--exit-on-error] [--pipeline] [--print] "
          "[--maxjobs n] [--arg-file argument-filename] [cmd [fixed-args ...]] "
          "[::: per-task-args ...]\n";
const char* const emptyCommandError
        = "uqparallel: unable to execute empty command\n";
const char* const commandError = "uqparallel: cannot execute \"%s\"\n";
const char* const executionFailure
        = "uqparallel: aborting due to execution failure\n";
const char* const executionInterrupted
        = "uqparallel: execution interrupted - aborting\n";
const char* const fileError
        = "uqparallel: Unable to open file \"%s\" for reading\n";

// Command line arguements
const char* const exitError = "--exit-on-error";
const char* const pipeline = "--pipeline";
const char* const argFile = "--arg-file";
const char* const maxJobs = "--maxjobs";
const char* const printArg = "--print";
const char* const command = "cmd";
const char* const perTaskArgs = ":::";

/* -------------------------------------------------------------------------- */
// Enum definitions

// Program exit codes
typedef enum {
    EXIT_SIGINT_STATUS = 2,
    EXIT_USAGE_STATUS = 6,
    EXIT_SIGUSR1_STATUS = 84,
    EXIT_EMPTY_STATUS = 87,
    EXIT_FILE_STATUS = 19,
} ExitStatus;

// Min/max jobs
typedef enum {
    MIN_JOBS = 1,
    MAX_JOBS = 140,
    DEFAULT_JOBS = 140,
} MaxJobsRange;

// Struct to hold parameters parsed to command line
typedef struct {
    bool exitOnError;
    bool pipelineBool;
    bool printMode;
    int numMaxJobs;
    bool usingFile;
    char* fileName;
    char** fixedArgs;
    int numFixedArgs;
    bool usingPerTaskArgs;
    char** taskArgs;
    int numTaskArgs;
} CmdLineParams;

// Struct to hold command line tasks
typedef struct {
    char** argv; // Null terminated array of tasks
    int argc; // Number of tasks
} Task;

/* -------------------------------------------------------------------------- */
// Function Prototypes

CmdLineParams cmd_line_parser(int argc, char* argv[]);
bool parse_optional_args(CmdLineParams* params, int* argc, char*** argv);
void usage_error();
void parse_fixed_args(CmdLineParams* params, int* argc, char*** argv);
void parse_per_task_args(CmdLineParams* params, int* argc, char*** argv);
bool needs_quotes(const char* s);
void print_task_line(int jobNum, Task* tasks, bool pipeline, int numTasks);
void print_mode(Task* task, int numTasks, bool pipeline);
FILE* read_file(bool usingFile, const char* filename);
void no_task_check(FILE* input, CmdLineParams* params);
Task* build_task_list(
        const CmdLineParams* params, FILE* input, int* outNumTasks);
Task* build_from_command(const CmdLineParams* params, int* outCount);
Task* build_from_file(const CmdLineParams* params, FILE* input, int* outCount);
int run_tasks(Task* tasks, int numTasks, bool exitOnError, int numMaxJobs);
int run_pipeline(Task* tasks, int numTasks, bool exitOnError);
static pid_t the_reaper(pid_t pid, int* rawStatus);
static bool record_status(int rawStatus, bool exitOnError, int* lastStatus);
void free_params(CmdLineParams* params);
void free_task_list(Task* tasks, int numTasks);
void usage_error(void);
void file_error(const char* filename);

/* -------------------------------------------------------------------------- */
int main(int argc, char* argv[])
{
    // Build up command line parameters
    CmdLineParams params = cmd_line_parser(argc, argv);
    FILE* input = read_file(params.usingFile, params.fileName);

    no_task_check(input, &params); // Check if no tasks given

    // Build the task list
    int numTasks;
    Task* tasks = build_task_list(&params, input, &numTasks);

    // If print mode, dump commands then exit
    if (params.printMode) {
        print_mode(tasks, numTasks, params.pipelineBool);
        free_task_list(tasks, numTasks);
        free_params(&params);
        return 0;
    }

    int status;
    // Run the pipeline
    if (params.pipelineBool) {
        status = run_pipeline(tasks, numTasks, params.exitOnError);
    } else {
        // Run tasks
        status = run_tasks(
                tasks, numTasks, params.exitOnError, params.numMaxJobs);
    }
    // Free memory
    free_task_list(tasks, numTasks);
    free_params(&params);
    return status;
}

/* cmd_line_parser()
 * ------------------
 * Parses optional and positional command line arguments into a CmdLineParams
 * struct.
 * Detects invalid combinations and calls usage_error() on failure.
 *
 * argc: number of arguments (including program name)
 * argv: array of argument strings (including program name)
 *
 * Returns: fully populated CmdLineParams structure
 * Global variables modified: none
 * Errors: calls usage_error() on invalid arguments
 *
 */
CmdLineParams cmd_line_parser(int argc, char* argv[])
{
    CmdLineParams params = {0}; // Init fields to zero
    params.numMaxJobs = DEFAULT_JOBS;
    argv++;
    argc--; // Skip program name

    // Reject empty-string args or cmds
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '\0') {
            usage_error();
        }
    }

    // Look for optional arguments
    while (argv[0] && strncmp(argv[0], "--", 2) == 0) {
        if (!parse_optional_args(&params, &argc, &argv)) {
            free_params(&params);
            usage_error();
        }
    }

    parse_fixed_args(&params, &argc, &argv); // Check for cmd/fixed args
    parse_per_task_args(&params, &argc, &argv); // Check for per-task-args

    if (params.pipelineBool && !params.usingFile && !params.usingPerTaskArgs) {
        usage_error();
    }

    // Check if using a file and per task args
    if (params.usingFile && params.usingPerTaskArgs) {
        free_params(&params);
        usage_error();
    }
    return params;
}

/* parse_optional_args()
 * ----------------------
 * Processes one optional argument from argv, updating CmdLineParams and
 * decrementing argc and advancing argv pointer when recognized.
 *
 * params: pointer to CmdLineParams to update
 * argc: pointer to remaining argument count
 * argv: pointer to argument vector pointer
 *
 * Returns: true if an option was parsed and consumed, false otherwise
 * Global variables modified: none
 * Errors: calls usage_error() on duplicate or invalid options
 *
 */
bool parse_optional_args(CmdLineParams* params, int* argc, char*** argv)
{
    char** args = *argv;
    int count = *argc;
    if (strcmp(args[0], exitError) == 0 && args[1] && args[1][0]) {
        if (params->exitOnError) {
            usage_error();
        }
        params->exitOnError = true;
        args++;
        count--;
    } else if (strcmp(args[0], pipeline) == 0) {
        if (params->pipelineBool) {
            usage_error();
        }
        params->pipelineBool = true;
        args++;
        count--;
    } else if (strcmp(args[0], argFile) == 0 && args[1] && args[1][0]) {
        if (params->usingFile) {
            usage_error();
        }
        params->usingFile = true;
        params->fileName = args[1];
        args += 2;
        count -= 2;
    } else if (strcmp(args[0], maxJobs) == 0 && args[1] && args[1][0]) {
        if (params->numMaxJobs != DEFAULT_JOBS) {
            usage_error();
        }
        params->numMaxJobs = atoi(args[1]);
        if (params->numMaxJobs < MIN_JOBS || params->numMaxJobs > MAX_JOBS) {
            free_params(params);
            usage_error();
        }
        args += 2;
        count -= 2;
    } else if (strcmp(args[0], printArg) == 0) {
        if (params->printMode) {
            usage_error();
        }
        params->printMode = true;
        args++;
        count--;
    } else {
        return false;
    }
    *argv = args;
    *argc = count;
    return true;
}

/* parse_fixed_args()
 * ------------------
 * Collects fixed arguments (command and its static arguments) from argv
 * until per-task delimiter or end of arguments.
 *
 * params: pointer to CmdLineParams to store fixedArgs
 * argc: pointer to remaining argument count
 * argv: pointer to argument vector pointer
 *
 * Returns: none
 * Global variables modified: params->fixedArgs, params->numFixedArgs
 * Errors: none
 *
 */
void parse_fixed_args(CmdLineParams* params, int* argc, char*** argv)
{
    if (*argc > 0 && strcmp((*argv)[0], perTaskArgs) != 0) {
        int i = 0;
        // Look for fixed args
        while (i < *argc && strcmp((*argv)[i], perTaskArgs) != 0) {
            // Allocate memory for fixed args
            params->fixedArgs = (char**)realloc((void*)params->fixedArgs,
                    sizeof *params->fixedArgs * (params->numFixedArgs + 1));
            params->fixedArgs[params->numFixedArgs++] = (*argv)[i++];
        }
        *argv += i;
        *argc -= i;
    }
}

/* parse_per_task_args()
 * ----------------------
 * Collects per-task arguments following the ::: delimiter into
 * params->taskArgs.
 *
 * params: pointer to CmdLineParams to store taskArgs
 * argc: pointer to remaining argument count
 * argv: pointer to argument vector pointer
 *
 * Returns: none
 * Global variables modified: params->taskArgs, params->numTaskArgs,
 * params->usingPerTaskArgs
 * Errors: exits with EXIT_EMPTY_STATUS if no per-task args follow delimiter
 *
 */
// Helper for cmd_line_parser()
void parse_per_task_args(CmdLineParams* params, int* argc, char*** argv)
{
    if (*argc > 0 && strcmp((*argv)[0], perTaskArgs) == 0) {
        (*argv)++;
        (*argc)--;
        for (int j = 0; j < *argc; j++) {
            // Allocate memory for taskArgs
            params->taskArgs = (char**)realloc((void*)params->taskArgs,
                    sizeof *params->taskArgs * (params->numTaskArgs + 1));
            params->taskArgs[params->numTaskArgs++] = (*argv)[j];
        }
        // Set usingPerTaskArgs flag to true
        params->usingPerTaskArgs = true;

        if (!params->numTaskArgs) {
            exit(EXIT_EMPTY_STATUS);
        }
        *argv += *argc;
        *argc = 0;
    }
}

/* needs_quotes()
 * --------------
 * Checks if a string contains spaces and thus needs quoting when printed.
 *
 * s: input string to test
 *
 * Returns: true if s contains a space character, false otherwise
 * Global variables modified: none
 * Errors: none
 *
 */
bool needs_quotes(const char* s)
{
    return strchr(s, ' ') != NULL;
}

/* print_task_line()
 * -----------------
 * Prints a single task line prefixed by its job number, quoting arguments
 * containing spaces, and appending a pipe symbol if part of a pipeline.
 *
 * jobNum: 1-based index of the task
 * task: pointer to Task struct containing argv and argc
 * pipeline: whether to append "|" for pipeline continuation
 * numTasks: total number of tasks in pipeline mode
 *
 * Returns: none
 * Global variables modified: none
 * Errors: none
 *
 */
void print_task_line(int jobNum, Task* task, bool pipeline, int numTasks)
{
    printf("%d:", jobNum);

    for (int i = 0; i < task->argc; i++) {
        const char* arg = task->argv[i];
        putchar(' ');
        if (needs_quotes(arg)) {
            // Add double quotes
            putchar('"');
            fputs(arg, stdout);
            putchar('"');
        } else {
            fputs(arg, stdout);
        }
    }

    if (pipeline && jobNum < numTasks) {
        printf(" |");
    }

    putchar('\n');
}

/* print_mode()
 * ------------
 * Iterates through all tasks and prints each via print_task_line().
 *
 * task: array of Task structs
 * numTasks: number of tasks
 * pipeline: whether pipeline formatting is enabled
 *
 * Returns: none
 * Global variables modified: none
 * Errors: none
 *
 */
void print_mode(Task* task, int numTasks, bool pipeline)
{
    for (int j = 0; j < numTasks; j++) {
        // jobs are 1-indexed in the output
        print_task_line(j + 1, &task[j], pipeline, numTasks);
    }
}

/* read_file()
 * -----------
 * Opens the argument file if usingFile is true, otherwise returns stdin.
 *
 * usingFile: flag indicating --arg-file was specified
 * filename: name of file to open
 *
 * Returns: FILE* handle to input stream
 * Global variables modified: none
 * Errors: calls usage_error() if filename missing, file_error() if fopen fails
 *
 */
FILE* read_file(bool usingFile, const char* filename)
{
    // If no file, read from stdin
    if (!usingFile) {
        return stdin;
    }

    if (!filename || filename[0] == '\0') { // Check for missing filename
        usage_error();
    }

    FILE* file = fopen(filename, "r"); // Attempt to open the file
    if (!file) {
        file_error(filename);
    }

    return file;
}

/* no_task_check()
 * ---------------
 * Verifies that there is at least one task source when neither --arg-file
 * nor ::: per-task args are used. Exits with EXIT_EMPTY_STATUS on EOF.
 *
 * input: FILE* handle for reading commands
 * params: pointer to CmdLineParams to check usage flags
 *
 * Returns: none
 * Global variables modified: none
 * Errors: exits with EXIT_EMPTY_STATUS if no tasks to run
 *
 */
void no_task_check(FILE* input, CmdLineParams* params)
{
    if (!params->usingFile && !params->usingPerTaskArgs) {
        int c = fgetc(input);
        if (c == EOF) {
            free_params(params);
            exit(EXIT_EMPTY_STATUS);
        }
        ungetc(c, input);
    }
}

/* build_task_list()
 * ------------------
 * Constructs the array of Task structs either from per-task args or file lines.
 *
 * params: pointer to CmdLineParams with parsing results
 * input: FILE* handle for reading file lines (if not per-task args)
 * outNumTasks: pointer to store number of tasks built
 *
 * Returns: pointer to dynamically allocated array of Task
 * Global variables modified: none
 * Errors: none
 *
 */
Task* build_task_list(
        const CmdLineParams* params, FILE* input, int* outNumTasks)
{
    // Check for task args in command line
    if (params->usingPerTaskArgs) {
        return build_from_command(params, outNumTasks);
    }

    // Otherwise, use tasks from file/stdin
    return build_from_file(params, input, outNumTasks);
}

/* build_from_command()
 * ---------------------
 * Builds tasks when per-task args are specified: each task combines fixedArgs
 * with one per-task argument.
 *
 * params: pointer to CmdLineParams containing fixed and task args
 * outCount: pointer to store number of tasks created
 *
 * Returns: dynamically allocated Task array
 * Global variables modified: none
 * Errors: none
 *
 */
Task* build_from_command(const CmdLineParams* params, int* outCount)
{
    int total = params->numTaskArgs;
    Task* tasks = (Task*)malloc(sizeof *tasks * total);

    for (int i = 0; i < total; i++) {
        int taskArgc = (params->numFixedArgs) + 1; // + 1 for per-task-arg
        // Allocate enough memory to store the args
        char** taskArgv = (char**)malloc(sizeof *taskArgv * (taskArgc + 1));

        // Copy fixed args
        for (int j = 0; j < (params->numFixedArgs); j++) {
            taskArgv[j] = strdup(params->fixedArgs[j]);
        }

        taskArgv[params->numFixedArgs] = strdup(params->taskArgs[i]);
        taskArgv[taskArgc] = NULL; // NULL-terminate

        // Add to Task struct
        tasks[i].argc = taskArgc;
        tasks[i].argv = taskArgv;
    }
    *outCount = total;
    return tasks;
}

/* build_from_file()
 * -----------------
 * Reads lines from input, tokenizes each via split_space_not_quote(),
 * prepends fixedArgs, and builds Task list.
 *
 * params: pointer to CmdLineParams containing fixedArgs
 * input: FILE* handle for reading task lines
 * outCount: pointer to store number of tasks created
 *
 * Returns: dynamically allocated Task array
 * Global variables modified: none
 * Errors: none
 *
 */
Task* build_from_file(const CmdLineParams* params, FILE* input, int* outCount)
{
    int count = 0;
    Task* tasks = NULL;
    char* line = NULL;
    size_t len = 0;

    while (getline(&line, &len, input) != -1) {
        if (line[0] == '\n') {
            continue; // Skip blank
        }
        line[strcspn(line, "\n")] = '\0'; // Strip newline char

        // Make a writeable copy for split_space_not_quote()
        char* buffer = strdup(line);
        int numTokens;
        char** tokens = split_space_not_quote(buffer, &numTokens);

        // Allocate Task argv[] and allow space for NULL terminator
        int taskArgc = params->numFixedArgs + numTokens;
        char** taskArgv = (char**)malloc(sizeof *taskArgv * (taskArgc + 1));

        // Copy fixed args
        for (int i = 0; i < params->numFixedArgs; i++) {
            taskArgv[i] = strdup(params->fixedArgs[i]);
        }

        // Copy each token
        for (int k = 0; k < numTokens; k++) {
            taskArgv[params->numFixedArgs + k] = strdup(tokens[k]);
            taskArgv[taskArgc] = NULL; // NULL terminate array
        }

        free((void*)buffer);
        free((void*)tokens);

        // Append this task
        tasks = realloc(tasks, sizeof *tasks * (count + 1));
        tasks[count].argc = taskArgc;
        tasks[count].argv = taskArgv;
        count++;
    }
    free((void*)line);
    *outCount = count;
    return tasks;
}

/* run_tasks()
 * ------------
 * Executes tasks in parallel up to maxJobs, reaping in-order, and handling
 * exit-on-error behavior.
 *
 * tasks: array of Task structs to execute
 * numTasks: number of tasks
 * exitOnError: whether to abort further tasks on non-zero status
 * maxJobs: maximum concurrent child processes
 *
 * Returns: lastStatus code to exit uqparallel
 * Global variables modified: none
 * Errors: may call record_status which sets exit flags
 *
 */
int run_tasks(Task* tasks, int numTasks, bool exitOnError, int maxJobs)
{
    pid_t* pids = malloc(numTasks * sizeof(pid_t));
    int lastStatus = 0; // Store the last signal
    int nextTask = 0; // Index of next task to spawn
    int running = 0; // Number of children currently running
    int nextReap = 0;

    // Fork and exec all tasks
    while (nextTask < numTasks || running > 0) {
        // Spawn new tasks if under limit
        while (nextTask < numTasks && running < maxJobs) {
            // Frk
            pid_t pid = fork();
            if (pid == 0) {
                int devnull = open("/dev/null", O_WRONLY);
                // Suppress error-messages we don't want
                if (devnull >= 0) {
                    dup2(devnull, STDERR_FILENO);
                    close(devnull);
                }
                // Child
                execvp(tasks[nextTask].argv[0], tasks[nextTask].argv);
                // Exec failed, raise SIGUSR1
                raise(SIGUSR1);
            }
            // Parent
            pids[nextTask++] = pid;
            running++;
        }
        // Reap oldest outstanding child
        int raw;
        if (the_reaper(pids[nextReap++], &raw) > 0) {
            record_status(raw, exitOnError, &lastStatus);
            running--;
        }
    }
    // All children spawned, reap the rest in order
    for (int i = nextReap; i < numTasks; i++) {
        int raw;
        if (the_reaper(pids[i], &raw) > 0) {
            record_status(raw, exitOnError, &lastStatus);
        }
    }
    free((void*)pids);
    return lastStatus;
}

/* the_reaper()
 * -------------
 * Waits for a specific child PID to terminate, capturing its raw status.
 *
 * pid: process ID to wait for
 * rawStatus: pointer to store status from waitpid()
 *
 * Returns: pid of reaped child or -1 on error
 * Global variables modified: none
 * Errors: prints perror() on waitpid failure
 *
 */
static pid_t the_reaper(pid_t pid, int* rawStatus)
{
    pid_t done = waitpid(pid, rawStatus, 0);
    if (done < 0) {
        perror("waitpid");
        return -1;
    }

    return done;
}

/* record_status()
 * ----------------
 * Processes rawStatus to determine exit code or signal offset, and
 * implements exit-on-error signalling.
 *
 * rawStatus: status code from waitpid()
 * exitOnError: flag to abort on error
 * lastStatus: pointer to update lastStatus code
 *
 * Returns: true if exit-on-error condition met, false otherwise
 * Global variables modified: *lastStatus
 * Errors: none
 *
 */
static bool record_status(int rawStatus, bool exitOnError, int* lastStatus)
{
    // Check if there was a signal
    if (WIFSIGNALED(rawStatus)) {
        // Store the signal number
        *lastStatus = SIG_INT_OFFSET + WTERMSIG(rawStatus);
    } else if (WIFEXITED(rawStatus)) {
        // Store the signal status
        int code = WEXITSTATUS(rawStatus);
        *lastStatus = code;
        if (exitOnError && code != 0) {
            return true;
        }
    }
    return false;
}

/* run_pipeline()
 * ---------------
 * Creates a chain of pipes between task processes, executes sequentially,
 * and reaps all children, handling exit-on-error.
 *
 * tasks: array of Task to run as pipeline
 * numTasks: number of tasks
 * exitOnError: whether to abort pipeline on failure
 *
 * Returns: lastStatus code to exit uqparallel
 * Global variables modified: none
 * Errors: exits on pipe or fork failure
 *
 */
int run_pipeline(Task* tasks, int numTasks, bool exitOnError)
{
    pid_t pids[numTasks];
    int lastStatus = 0;
    int prevFd = -1;

    for (int i = 0; i < numTasks; i++) {
        int pipeFd[2] = {-1, -1};
        if (i < numTasks - 1) {
            if (pipe(pipeFd) < 0) {
                perror("pipe");
                exit(1);
            }
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child: wire up stdin
            if (prevFd != -1) {
                dup2(prevFd, STDIN_FILENO);
                close(prevFd);
            }
            // wire up stdout
            if (pipeFd[1] != -1) {
                dup2(pipeFd[1], STDOUT_FILENO);
                close(pipeFd[0]);
                close(pipeFd[1]);
            }

            execvp(tasks[i].argv[0], tasks[i].argv);
        }
        // Parent: track pid and cleanup
        pids[i] = pid;
        if (prevFd != -1) {
            close(prevFd);
        }
        if (pipeFd[1] != -1) {
            close(pipeFd[1]);
        }
        prevFd = pipeFd[0];
    }
    // Reap all pipeline children
    for (int i = 0; i < numTasks; i++) {
        int raw;
        if (the_reaper(pids[i], &raw) > 0) {
            record_status(raw, exitOnError, &lastStatus);
        }
    }
    return lastStatus;
}

/* free_params()
 * --------------
 * Frees dynamically allocated arrays within CmdLineParams struct.
 *
 * params: pointer to CmdLineParams to clean up
 *
 * Returns: none
 * Global variables modified: frees params->fixedArgs and params->taskArgs
 * Errors: none
 *
 */
void free_params(CmdLineParams* params)
{
    free((void*)params->fixedArgs);
    free((void*)params->taskArgs);
}

/* free_task_list()
 * -----------------
 * Frees all allocated memory for Task array, including argv entries.
 *
 * tasks: array of Task to free
 * numTasks: number of tasks in array
 *
 * Returns: none
 * Global variables modified: frees tasks and nested argv pointers
 * Errors: none
 *
 */
void free_task_list(Task* tasks, int numTasks)
{
    for (int i = 0; i < numTasks; i++) {
        for (int j = 0; j < tasks[i].argc; j++) {
            free(tasks[i].argv[j]);
        }
        free((void*)tasks[i].argv);
    }
    free((void*)tasks);
}

/* usage_error()
 * -------------
 * Prints the program’s usage instructions to stderr, indicating that the
 * user provided invalid or missing command-line options, then terminates
 * the program with EXIT_USAGE_STATUS.
 *
 * Returns: Does not return (exits the process)
 * Global variables modified: none
 * Errors: Always exits the program with EXIT_USAGE_STATUS
 *
 */
void usage_error(void)
{
    fprintf(stderr, usageErrorMessage);
    exit(EXIT_USAGE_STATUS);
}

/* file_error()
 * -------------
 * Prints an error message to stderr indicating failure to open the specified
 * file, then terminates the program with EXIT_FILE_STATUS.
 *
 * filename: name of the file that could not be opened
 *
 * Returns: does not return
 * Global variables modified: none
 * Errors: always exits program
 *
 */
void file_error(const char* filename)
{
    fprintf(stderr, fileError, filename);
    exit(EXIT_FILE_STATUS);
}