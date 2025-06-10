#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <tinyexpr.h>

// Error messages
#define ERROR_USAGE                                                            \
    "Usage: ./uqexpr [--loopable string] [--initialise string] "               \
    "[--significantfigs 2..7] [filename]\n"
#define ERROR_FILE "uqexpr: can't open file \"%s\" for reading\n"
#define ERROR_INVALID_VAR "uqexpr: invalid variable(s) were detected\n"
#define ERROR_DUPLICATE_VAR "uqexpr: one or more variables are duplicated\n"
#define INVALID_COMMAND "Invalid command, expression or assignment operation\n"

// Error numbers
#define ERROR_USAGE_NO 16
#define ERROR_FILE_NO 3
#define ERROR_INVALID_VAR_NO 10
#define ERROR_DUPLICATE_VAR_NO 11

// Constants
#define SIG_FIGS_UPPER 7
#define DEFAULT_SIG_FIGS 5
#define SIG_FIGS_LOWER 2
#define MAX_VARIABLES 50
#define MAX_LOOPS 50
#define MAX_CHARS 24
#define MIN_CHARS 1
#define MAX_LINE 1024
#define LOOP_VARS 3
#define RANGE_LEN 6

// Command-line arguments
#define SIG_FIGS "--significantfigs"
#define INITIALISE "--initialise"
#define LOOPABLE "--loopable"

/* Variable
 * Struct to store variables and value pairs
 * name: the variable
 * value: the value to be assigned to the given variable
 */
typedef struct {
    char name[MAX_CHARS];
    double value;
} Variable;

/* Loop
 * Stores the loop variable and range
 * name: the variable assigned to the given range
 * current: the current value being held by the variable
 * start: the starting value
 * inc: the increment value
 * end: the end value
 */
typedef struct {
    char name[MAX_CHARS];
    double current;
    double start;
    double inc;
    double end;
} Loop;

/* -- Command-line processing -- */
void get_sig_figs(int argc, char* argv[], int* i, int* sigFigs);
void get_loop_vars(int argc, char* argv[], Loop loops[], int* loopCount,
        Variable variables[], int* varCount, int* i);
void get_variable(
        int argc, char* argv[], Variable variables[], int* varCount, int* i);
void cmd_line_checker(int argc, char* argv[], int* sigFigs, char** filename,
        Variable variables[], int* varCount, Loop loops[], int* loopCount);

/* -- Parsing and validation -- */
int is_valid_name(const char* varName);
int is_valid_value(const char* varValue);
int parse_loop(
        const char* input, char* name, double* start, double* inc, double* end);
void check_duplicate(Variable variables[], const int* varCount, char* varName);

/* -- Expression and evaluation handling -- */
void process_expressions(FILE* file, Variable variables[], int* varCount,
        Loop loops[], int* loopCount, int significantFigs);
void handle_print_command(Variable variables[], const int* varCount,
        Loop loops[], const int* loopCount, int significantFigs);
void handle_range_command(char* line, Loop loops[], int* loopCount,
        Variable variables[], int *varCount, int significantFigs);
void handle_assignment(char* line, te_variable teVars[], Variable variables[],
        int* varCount, Loop loops[], const int* loopCount, int significantFigs);

/* -- Output and printing -- */
void print_loop(Loop loops[], int loopCount, int significantFigs);
void print_vars(Variable variables[], int varCount, int significantFigs);
void print_single_loop(const Loop* loop, int significantFigs);

/* Loop and variable manipulation -- */
int set_variable(
        Variable variables[], int* varCount, const char* name, double value);
int update_loop(Loop loops[], int* loopCount, Variable variables[],
        int* varCount, const char* name, double start, double inc, double end);
int set_loop_variable(
        Loop loops[], int loopCount, const char* name, double value);
int remove_variable(Variable variables[], int* varCount, const char* name);

/* -- Helpers -- */
char* trim_whitespace(char* str);
FILE* get_file(char* filename);
void set_te_vars(te_variable teVars[], Variable variables[],
        const int* varCount, Loop loops[], const int* loopCount);

int main(int argc, char* argv[])
{
    int significantFigs = DEFAULT_SIG_FIGS;
    char* filename = NULL;

    // Initialise variable storage
    Variable variables[MAX_VARIABLES];
    int varCount = 0;

    // Loop Storage
    Loop loops[MAX_LOOPS]; // Store up to 50 loops
    int loopCount = 0;

    // Validate command-line arguments
    cmd_line_checker(argc, argv, &significantFigs, &filename, variables,
            &varCount, loops, &loopCount);

    // Get the input file
    FILE* file = get_file(filename);

    // Launch uqexpr
    printf("Welcome to uqexpr!\n");
    printf("s4321079 wrote this program.\n");

    // Print any entered variables
    if (varCount) {
        print_vars(variables, varCount, significantFigs);
    } else {
        printf("No variables were specified.\n");
    }
    if (loopCount) {
        print_loop(loops, loopCount, significantFigs);
    } else {
        printf("No loop variables were specified.\n");
    }
    // Prompt for user input if reading from stdin
    if (file == stdin) {
        printf("Submit your expressions and assignment operations below.\n");
    }

    process_expressions(
            file, variables, &varCount, loops, &loopCount, significantFigs);

    if (file != stdin) {
        fclose(file);
    }
    printf("Thank you for using uqexpr.\n");
    return 0;
}

/* cmd_line_checker()
 * -------------------
 * Parses and validates command-line arguments for the uqexpr program.
 * Recognizes optional flags --significantfigs, --initialise, and --loopable,
 * followed optionally by a filename.
 *
 * argc: Number of command-line arguments.
 * argv: Array of command-line argument strings.
 * sigFigs: Pointer to store the specified number of significant figures.
 * filename: Pointer to store the filename if provided, or NULL for stdin.
 * variables: Array to store initialized variables from --initialise.
 * varCount: Pointer to the count of stored variables.
 * loops: Array to store initialized loop variables from --loopable.
 * loopCount: Pointer to the count of stored loop variables.
 *
 * Returns: void
 * Errors:
 *   - Exits with ERROR_USAGE_NO for invalid argument structure or usage.
 *   - Exits with ERROR_INVALID_VAR_NO if invalid variable or loop syntax is
 * detected.
 *   - Exits with ERROR_DUPLICATE_VAR_NO if a duplicate variable is provided.
 *
 */
void cmd_line_checker(int argc, char* argv[], int* sigFigs, char** filename,
        Variable variables[], int* varCount, Loop loops[], int* loopCount)
{
    for (int i = 1; i < argc; i++) {
        // Check for "--significantfigs"
        if (strcmp(argv[i], SIG_FIGS) == 0
                && strlen(argv[i]) == strlen(SIG_FIGS)) {
            get_sig_figs(argc, argv, &i, sigFigs);

            // Check for "--initialise"
        } else if (strcmp(argv[i], INITIALISE) == 0
                && strlen(argv[i]) == strlen(INITIALISE)) {
            get_variable(argc, argv, variables, varCount, &i);

            // Check for "--loopable"
        } else if (strcmp(argv[i], LOOPABLE) == 0
                && strlen(argv[i]) == strlen(LOOPABLE)) {
            get_loop_vars(
                    argc, argv, loops, loopCount, variables, varCount, &i);

            // Anything else beginning with "--" is a usage error
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, ERROR_USAGE);
            exit(ERROR_USAGE_NO);
        } else {
            // If none of above match, potential filename
            // If its not the last arg or empty string, then trigger usage error
            if (i != argc - 1 || strlen(argv[i]) == 0) {
                fprintf(stderr, ERROR_USAGE);
                exit(ERROR_USAGE_NO);
            }
            // Store filename to be opened and validated
            *filename = argv[i];
        }
    }
}

/* get_sig_figs()
 * Parses the command-line arguments to retrieve the significant figures value.
 *
 * argc: Number of command-line arguments.
 * argv: Array of command-line argument strings.
 * i: Pointer to the current index in argv being processed.
 * sigFigs: Pointer to the variable where the significant figures value will be
 * stored.
 *
 * Returns: void
 * Errors: Exits the program with ERROR_USAGE_NO if the significant figures
 * argument is invalid.
 *
 */
void get_sig_figs(int argc, char* argv[], int* i, int* sigFigs)
{
    if (*i + 1 >= argc || strlen(argv[*i + 1]) != 1
            || atoi(argv[*i + 1]) < SIG_FIGS_LOWER
            || atoi(argv[*i + 1]) > SIG_FIGS_UPPER) {
        fprintf(stderr, ERROR_USAGE);
        exit(ERROR_USAGE_NO);
    }
    *sigFigs = atoi(argv[++(*i)]);
}

/* is_valid_name()
 * ----------------
 * Validates whether the given string is a valid variable or loop name.
 * A valid name must be between MIN_CHARS and MAX_CHARS in length,
 * and consist only of alphabetic characters (a–z, A–Z).
 *
 * varName: The string to validate as a variable name (must not be NULL).
 *
 * Returns: 1 if the name is valid, 0 otherwise.
 * Global variables modified: None
 * Errors: None – returns 0 if the input is invalid.
 *
 */
int is_valid_name(const char* varName)
{
    int length = strlen(varName);

    // Check the length is between 1 and 24 chars
    if (length < MIN_CHARS || length > MAX_CHARS) {
        return 0; // Invalid
    }

    // Check that variable chars are all aphabetic
    for (int i = 0; varName[i] != '\0'; i++) {
        if (!isalpha(varName[i])) {
            return 0; // Invalid
        }
    }
    return 1; // Valid
}

/* get_variable()
 * ---------------
 * Parses and stores a variable from the command-line arguments.
 *
 * argc: Number of command-line arguments.
 * argv: Array of command-line argument strings.
 * variables: Array to store the parsed variables.
 * varCount: Pointer to the count of variables stored.
 * i: Pointer to the current index in argv being processed.
 *
 * Returns: void
 * Errors: Exits the program with ERROR_INVALID_VAR_NO if the variable is
 * invalid or duplicated.
 */
void get_variable(
        int argc, char* argv[], Variable variables[], int* varCount, int* i)
{
    // Check the next argument after --initialise exists
    if (++(*i) >= argc) {
        fprintf(stderr, ERROR_USAGE);
        exit(ERROR_USAGE_NO);
    }

    // Check for equal sign
    if (!strchr(argv[*i], '=')) {
        fprintf(stderr, ERROR_INVALID_VAR);
        exit(ERROR_INVALID_VAR_NO);
    }

    char* assignment = argv[*i];
    char* equalSign = strchr(assignment, '=');

    // Split var=val pair into two strings
    *equalSign = '\0';
    char* varName = assignment;
    char* varValue = equalSign + 1;

    // Ensure variable name and value a not empty
    if (strlen(varName) == 0 || strlen(varValue) == 0) {
        fprintf(stderr, ERROR_INVALID_VAR);
        exit(ERROR_INVALID_VAR_NO);
    }

    // Check if variable name and value are valid
    if (!is_valid_name(varName) || !is_valid_value(varValue)) {
        fprintf(stderr, ERROR_INVALID_VAR);
        exit(ERROR_INVALID_VAR_NO);
    }

    // Check if duplicate variable
    check_duplicate(variables, varCount, varName);

    // Store the variable
    strcpy(variables[*varCount].name, varName);
    variables[*varCount].value = atof(varValue);

    (*varCount)++;
}

/* check_duplicate()
 * ------------------
 * Checks for duplicate variable names in the variables array.
 *
 * variables: Array of existing variables.
 * varCount: Pointer to the number of variables stored.
 * varName: Name of the variable to check for duplication.
 *
 * Returns: void
 * Errors: Exits the program with ERROR_DUPLICATE_VAR_NO if a duplicate is
 * found.
 *
 */
void check_duplicate(Variable variables[], const int* varCount, char* varName)
{
    for (int j = 0; j < *varCount; j++) {
        if (strcmp(variables[j].name, varName) == 0) {
            fprintf(stderr, ERROR_DUPLICATE_VAR);
            exit(ERROR_DUPLICATE_VAR_NO);
        }
    }
}

/* is_valid_value()
 * -----------------
 * Checks if a string represents a valid floating-point value.
 *
 * value: The string to validate.
 *
 * Returns: 1 if the value is a valid floating-point number, 0 otherwise.
 */
int is_valid_value(const char* value)
{
    if (value == NULL || *value == '\0') {
        return 0;
    }

    char* end;
    strtod(value, &end);

    return (*end == '\0');
}

/* get_loop_vars()
 * ----------------
 * Parses and stores loop variables from the command-line arguments.
 *
 * argc: Number of command-line arguments.
 * argv: Array of command-line argument strings.
 * loops: Array to store the parsed loops.
 * loopCount: Pointer to the count of loops stored.
 * i: Pointer to the current index in argv being processed.
 *
 * Returns: void
 * Errors: Exits the program with ERROR_INVALID_VAR_NO if the loop variable is
 * invalid.
 *
 */
void get_loop_vars(int argc, char* argv[], Loop loops[], int* loopCount,
        Variable variables[], int* varCount, int* i)
{
    // Check if no loop given
    if (++(*i) >= argc) {
        fprintf(stderr, ERROR_USAGE);
        exit(ERROR_USAGE_NO);
    }

    char name[MAX_CHARS];
    double start;
    double inc;
    double end;

    if (!parse_loop(argv[*i], name, &start, &inc, &end)) {
        fprintf(stderr, ERROR_INVALID_VAR);
        exit(ERROR_INVALID_VAR_NO);
    }

    if (!update_loop(
                loops, loopCount, variables, varCount, name, start, inc, end)) {
        fprintf(stderr, ERROR_INVALID_VAR);
        exit(ERROR_INVALID_VAR_NO);
    }
}

/* parse_loop()
 * -------------
 * Parses a loop definition string into its components.
 *
 * input: The loop definition string (e.g., "x,1,2,10").
 * name: Buffer to store the parsed loop variable name.
 * start: Pointer to store the parsed start value.
 * inc: Pointer to store the parsed increment value.
 * end: Pointer to store the parsed end value.
 *
 * Returns: 1 if parsing is successful, 0 otherwise.
 */
int parse_loop(
        const char* input, char* name, double* start, double* inc, double* end)
{
    char copy[MAX_LINE];
    strncpy(copy, input, MAX_LINE);
    copy[MAX_LINE - 1] = '\0';

    char* token = strtok(copy, ",");
    if (!token || !is_valid_name(token)) {
        return 0;
    }
    strcpy(name, token);

    char* values[LOOP_VARS];
    for (int i = 0; i < LOOP_VARS; i++) {
        values[i] = strtok(NULL, ",");
        if (!values[i] || !is_valid_value(values[i])) {
            return 0;
        }
    }

    *start = atof(values[0]);
    *inc = atof(values[1]);
    *end = atof(values[2]);

    // Validate bounds
    if (*inc == 0 || (*start < *end && *inc < 0)
            || (*start > *end && *inc > 0)) {
        return 0;
    }

    return 1;
}

/* update_loop()
 * --------------
 * Updates an existing loop or adds a new loop to the loops array.
 *
 * loops: Array of existing loops.
 * loopCount: Pointer to the number of loops stored.
 * variable: Array of existing variables
 * varCount: Pointer to number of variables stored.
 * name: Name of the loop variable.
 * start: Start value of the loop.
 * inc: Increment value of the loop.
 * end: End value of the loop.
 *
 * Returns: 1 if the loop is successfully updated or added, 0 otherwise.
 */
int update_loop(Loop loops[], int* loopCount, Variable variables[],
        int*varCount, const char* name, double start, double inc, double end)
{
    // Validate validity of range
    if (inc == 0 || (start < end && inc < 0) || (start > end && inc > 0)) {
        return 0;
    }

    // Remove conflicting regular variable
    remove_variable(variables, varCount, name);

    // Update loop
    for (int i = 0; i < *loopCount; i++) {
        if (strcmp(loops[i].name, name) == 0) {
            loops[i].current = start;
            loops[i].start = start;
            loops[i].inc = inc;
            loops[i].end = end;
            return 1;
        }
    }

    // Add new loop
    strcpy(loops[*loopCount].name, name);
    loops[*loopCount].current = start;
    loops[*loopCount].start = start;
    loops[*loopCount].inc = inc;
    loops[*loopCount].end = end;

    (*loopCount)++;
    return 1;
}

/* print_vars()
 * -------------
 * Prints all non-loop variables and their values.
 *
 * variables: Array of variables to print.
 * varCount: Number of variables in the array.
 * significantFigs: Number of significant figures to display.
 *
 * Returns: void
 */
void print_vars(Variable variables[], int varCount, int significantFigs)
{
    if (varCount) {
        printf("Variables:\n");
        for (int i = 0; i < varCount; i++) {
            printf("%s = %.*g\n", variables[i].name, significantFigs,
                    variables[i].value);
        }
    }
}

/* print_loop()
 * -------------
 * Prints all loop variables and their current, start, increment, and end
 * values.
 *
 * loops: Array of loops to print.
 * loopCount: Number of loops in the array.
 * significantFigs: Number of significant figures to display.
 *
 * Returns: void
 */
void print_loop(Loop loops[], int loopCount, int significantFigs)
{
    if (loopCount != 0) {
        printf("Loop variables:\n");
        for (int i = 0; i < loopCount; i++) {
            printf("%s = %.*g (%.*g, %.*g, %.*g)\n", loops[i].name,
                    significantFigs, loops[i].current, significantFigs,
                    loops[i].start, significantFigs, loops[i].inc,
                    significantFigs, loops[i].end);
        }
    }
}

/* get_file()
 * -----------
 * Opens a file for reading based on the provided filename.
 *
 * filename: Name of the file to open. If NULL, stdin is used.
 *
 * Returns: Pointer to the opened file.
 * Errors: Exits the program with ERROR_FILE_NO if the file cannot be opened.
 *
 */
FILE* get_file(char* filename)
{
    if (!filename) {
        return stdin;
    }

    // Attempt to open the file
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, ERROR_FILE, filename);
        exit(ERROR_FILE_NO);
    }

    return file;
}

/* process_expressions()
 * ----------------------
 * Reads and processes each expression or command from the input stream.
 * Handles expression evaluation, assignments, and commands like @print and
 * @range.
 *
 * file: Input file stream (or stdin).
 * variables: Array of variables to be used and updated.
 * varCount: Pointer to the count of variables.
 * loops: Array of loop variables to be used and updated.
 * loopCount: Pointer to the count of loops.
 * significantFigs: Number of significant figures to use for output.
 *
 * Returns: void
 * Errors: prints INVALID_COMMAND to stdout if there is an invalid expression or
 * command.
 *
 */
void process_expressions(FILE* file, Variable variables[], int* varCount,
        Loop loops[], int* loopCount, int significantFigs)
{
    char line[MAX_LINE];
    te_variable teVars[MAX_VARIABLES];

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        // Ignore empty lines or # (comments)
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char exprCopy[MAX_LINE];
        strcpy(exprCopy, line);

        // Set variables for tinyexpr library
        set_te_vars(teVars, variables, varCount, loops, loopCount);
        int err;
        double result;
        if (strchr(line, '=')) {
            handle_assignment(line, teVars, variables, varCount, loops,
                    loopCount, significantFigs);
        } else if (strcmp(trim_whitespace(line), "@print") == 0) {
            handle_print_command(
                    variables, varCount, loops, loopCount, significantFigs);
        } else if (strncmp(trim_whitespace(line), "@range", RANGE_LEN) == 0) {
            handle_range_command(line, loops, variables, varCount, loopCount,
                    significantFigs);
        } else {
            // Handle non-assignment expressions
            char* exprStr = trim_whitespace(exprCopy);
            te_expr* expr
                    = te_compile(exprStr, teVars, *varCount + *loopCount, &err);
            if (!expr) {
                fprintf(stderr, INVALID_COMMAND);
                continue;
            }

            result = te_eval(expr);
            te_free(expr);
            printf("Result = %.*g\n", significantFigs, result);
        }
    }
}

/* handle_range_command()
 * -----------------------
 * Handles the @range command by updating or adding a loop variable
 * and printing its state.
 *
 * line: The full line containing the @range command.
 * loops: Array of loop variables.
 * loopCount: Pointer to the count of loop variables.
 * significantFigs: Number of significant figures to use for output.
 *
 * Returns: void
 * Errors: prints INVALID_COMMAND to stdout if parse_loop() or update_loop()
 * fail.
 *
 */
void handle_range_command(char* line, Loop loops[], int* loopCount,
        Variable variables[], int* varCount, int significantFigs)
{
    char* args = trim_whitespace(line + RANGE_LEN); // skip "@range"
    char name[MAX_CHARS];
    double start;
    double inc;
    double end;

    if (!parse_loop(args, name, &start, &inc, &end)
            || !update_loop(
                loops, loopCount, variables, varCount, name, start, inc, end)) {
        fprintf(stderr, INVALID_COMMAND);
        return;
    }
    for (int i = 0; i < *loopCount; i++) {
        if (strcmp(loops[i].name, name) == 0) {
            print_single_loop(&loops[i], significantFigs);
            break;
        }
    }
}

/* print_single_loop()
 * --------------------
 * Prints a single loop variable in the format: name = current (start, inc,
 * end).
 *
 * loop: Pointer to the Loop structure to print.
 * significantFigs: Number of significant figures to use for output.
 *
 * Returns: void
 *
 */
void print_single_loop(const Loop* loop, int significantFigs)
{
    printf("%s = %.*g (%.*g, %.*g, %.*g)\n", loop->name, significantFigs,
            loop->current, significantFigs, loop->start, significantFigs,
            loop->inc, significantFigs, loop->end);
}

/* handle_print_command()
 * -----------------------
 * Handles the @print command, displaying all current variables and loops.
 *
 * variables: Array of stored variables.
 * varCount: Pointer to the count of variables.
 * loops: Array of stored loop variables.
 * loopCount: Pointer to the count of loop variables.
 * significantFigs: Number of significant figures to use for output.
 *
 * Returns: void
 *
 */
void handle_print_command(Variable variables[], const int* varCount,
        Loop loops[], const int* loopCount, int significantFigs)
{
    if (*varCount > 0) {
        print_vars(variables, *varCount, significantFigs);
    } else {
        printf("No variables were specified.\n");
    }

    if (*loopCount > 0) {
        print_loop(loops, *loopCount, significantFigs);
    } else {
        printf("No loop variables were specified.\n");
    }
}

/* handle_assignment()
 * ---------------------
 * Handles variable or loop assignment from expressions of the form `name =
 * expr`. Evaluates the expression and assigns the result to the appropriate
 * variable.
 *
 * line: The full input line containing the assignment.
 * teVars: Array of te_variable used for expression evaluation.
 * variables: Array of variable structs.
 * varCount: Pointer to the count of variables.
 * loops: Array of loop structs.
 * loopCount: Pointer to the count of loops.
 * significantFigs: Number of significant figures for printing output.
 *
 * Returns: void
 * Errors: Prints an error message if the assignment is invalid or if
 * expression evaluation fails.
 *
 */
void handle_assignment(char* line, te_variable teVars[], Variable variables[],
        int* varCount, Loop loops[], const int* loopCount, int significantFigs)
{
    char* equalSign = strchr(line, '=');
    *equalSign = '\0';
    char* lhs = trim_whitespace(line);
    char* rhs = trim_whitespace(equalSign + 1);
    int err;

    if (!is_valid_name(lhs)) {
        fprintf(stderr, INVALID_COMMAND);
        return;
    }

    te_expr* expr = te_compile(rhs, teVars, *varCount + *loopCount, &err);
    if (!expr) {
        fprintf(stderr, INVALID_COMMAND);
        return;
    }

    double result = te_eval(expr);
    te_free(expr);

    if (!set_loop_variable(loops, *loopCount, lhs, result)) {
        set_variable(variables, varCount, lhs, result);
    }
    printf("%s = %.*g\n", lhs, significantFigs, result);
}

/* set_loop_variable()
 * --------------------
 * Updates the value of an existing loop variable if the name matches.
 *
 * loops: Array of loop structures.
 * loopCount: Number of loops stored.
 * name: Name of the variable to update.
 * value: New value to assign to the loop variable.
 *
 * Returns: 1 if the variable was found and updated, 0 otherwise.
 */
int set_loop_variable(
        Loop loops[], int loopCount, const char* name, double value)
{
    for (int i = 0; i < loopCount; i++) {
        if (strcmp(loops[i].name, name) == 0) {
            // Update current value and return 1 for success
            loops[i].current = value;
            return 1;
        }
    }
    return 0; // Not found
}

/* trim_whitespace()
 * ------------------
 * Removes leading and trailing whitespace from a string.
 *
 * str: The string to trim. This string is modified in-place.
 *
 * Returns: Pointer to the trimmed string.
 * Errors: None
 *
 */
char* trim_whitespace(char* str)
{
    while (isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == '\0') {
        return str;
    }

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    return str;
}

/* set_variable()
 * ----------------
 * Adds or updates a variable in the variables array.
 *
 * variables: Array of variable structs.
 * varCount: Pointer to the count of variables.
 * name: Name of the variable.
 * value: Value to assign to the variable.
 *
 * Returns: 1 if the variable was added or updated successfully, 0 otherwise.
 * Errors: None
 *
 */
int set_variable(
        Variable variables[], int* varCount, const char* name, double value)
{
    for (int i = 0; i < *varCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            variables[i].value = value;
            return 1;
        }
    }
    if (*varCount < MAX_VARIABLES) {
        strcpy(variables[(*varCount)].name, name);
        variables[(*varCount)++].value = value;
        return 1;
    }
    return 0;
}

/* set_te_vars()
 * ---------------
 * Populates an array of te_variable structures for use with TinyExpr
 * using current variables and loop values.
 *
 * teVars: Array to populate with variable mappings.
 * variables: Array of Variable structs.
 * varCount: Pointer to the count of variables.
 * loops: Array of Loop structs.
 * loopCount: Pointer to the count of loops.
 *
 * Returns: void
 *
 */
void set_te_vars(te_variable teVars[], Variable variables[],
        const int* varCount, Loop loops[], const int* loopCount)
{
    int index = 0;

    for (int i = 0; i < *varCount; i++) {
        teVars[i].name = variables[i].name;
        teVars[i].address = &(variables[i].value);
        teVars[i].type = TE_VARIABLE;
        index++;
    }

    for (int i = 0; i < *loopCount; i++) {
        teVars[index].name = loops[i].name;
        teVars[index].address = &(loops[i].current);
        teVars[index].type = TE_VARIABLE;
        index++;
    }
}

/* remove_variable()
 * ------------------
 * Removes a variable from the variables array by name.
 *
 * variables: array of Variable structs
 * varCount: pointer to the current count of variables
 * name: name of the variable to remove
 *
 * Returns: 1 if variable was found and removed, 0 otherwise
 */
int remove_variable(Variable variables[], int* varCount, const char* name)
{
    for (int i = 0; i < *varCount; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            for (int j = i; j < *varCount - 1; j++) {
                variables[j] = variables[j + 1];
            }
        }
        (*varCount)--;
        return 1;
    }
    return 0;
}