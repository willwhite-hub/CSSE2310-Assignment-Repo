/* CSSE2310 2025 Assignment Four
 * uqfaceclient.c
 *
 * Written by William White
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "protocol.h"
/* -------------------------------------------------------------------------- */
// Constants

#define STDIN_BUFFER 4096

// Exit Messages
const char* const usageErrorMessage
        = "Usage: ./uqfaceclient port [--replaceimage filename] [--outputimage "
          "filename] [--detect filename]\n";
const char* const fileReadErrorMessage
        = "uqfaceclient: unable to open the input file \"%s\" for reading\n";
const char* const fileWriteErrorMessage
        = "uqfaceclient: unable to open the output file \"%s\" for writing\n";
const char* const portErrorMessage
        = "uqfaceclient: cannot connect to the server on port \"%s\"\n";
const char* const serverErrorMessage
        = "uqfaceclient: received the following error message: \"%s\"\n";
const char* const communicationErrorMessage
        = "uqfaceclient: a communication error occured\n";

// Command Line Arguments
const char* const replaceImage = "--replaceimage";
const char* const outputImage = "--outputimage";
const char* const detectImage = "--detect";

/* -------------------------------------------------------------------------- */
// Enum Definitions

// Program exit codes
typedef enum {
    EXIT_USAGE_STATUS = 10,
    EXIT_FILEREAD_STATUS = 7,
    EXIT_FILEWRITE_STATUS = 19,
    EXIT_SERVERPORT_STATUS = 3,
    EXIT_SUCCESS_STATUS = 0,
    EXIT_ERRMESSAGE_STATUS = 9,
    EXIT_COMMERR_STATUS = 13,
} ExitStatus;

// Struct to hold parameters parsed to command line
typedef struct {
    char* port;
    char* detectFilename;
    char* replaceFilename;
    char* outputFilename;
} CmdLineParams;

typedef struct {
    FILE* to; // for writing to server
    FILE* from; // for reading from server
} SocketStreams;

/* -------------------------------------------------------------------------- */
// Function Prototypes
unsigned char* detect_image(const CmdLineParams* params, size_t* detectSize);
unsigned char* replace_image(const CmdLineParams* params, size_t* replaceSize);
FILE* open_output_file(const CmdLineParams* params);
SocketStreams create_socket_streams(int sockfd);
void close_socket_streams(SocketStreams streams);
CmdLineParams cmd_line_parser(int argc, char* argv[]);
bool parse_optional_args(CmdLineParams* params, int* argc, char*** argv);
int connect_to_server(const char* port);
unsigned char* read_file(const char* filename, size_t* outSize);
unsigned char* read_stdin(size_t* outSize);
void usage_error(void);
void file_error(const char* filename, bool writing);
void port_error(const char* port);

int main(int argc, char* argv[])
{
    // Parse command line parameters
    CmdLineParams params = cmd_line_parser(argc, argv);

    // Read image data
    size_t detectSize = 0;
    unsigned char* detectData = detect_image(&params, &detectSize);

    size_t replaceSize = 0;
    unsigned char* replaceData = replace_image(&params, &replaceSize);

    // Open output file
    FILE* outputFile = open_output_file(&params);

    // Connect to server and create streams
    int sockfd = connect_to_server(params.port);
    SocketStreams streams = create_socket_streams(sockfd);

    // Communication Protocol
    send_request(streams.to, detectData, detectSize, replaceData, replaceSize);
    receive_request(streams.from, outputFile);

    // Cleanup
    free(detectData);
    free(replaceData);
    if (outputFile != stdout) {
        fclose(outputFile);
    }
    close_socket_streams(streams);

    return 0;
}

/* detect_image()
 * --------------
 * Reads image data for face detection. If a detect filename is specified,
 * reads from that file, otherwise reads from stdin.
 *
 * params: pointer to command line parameters structure containing filenames
 * detectSize: pointer to store the size of the detected image data
 *
 * Returns: pointer to allocated buffer containing image data, or NULL on
 * failure Errors: exits with code 7 if file cannot be opened for reading, or
 * code 13 on communication error (memory allocation failure)
 */
unsigned char* detect_image(const CmdLineParams* params, size_t* detectSize)
{
    if (params->detectFilename) {
        // Read from specified file
        return read_file(params->detectFilename, detectSize);
    }

    // No file specified, read from stdin
    return read_stdin(detectSize);
}

/* replace_image()
 * ---------------
 * Reads replacement image data if a replace filename is specified in the
 * command line parameters.
 *
 * params: pointer to command line parameters structure
 * replaceSize: pointer to store the size of the replacement image data
 *
 * Returns: pointer to allocated buffer containing replacement image data,
 *          or NULL if no replacement image specified
 * Errors: exits with code 7 if file cannot be opened for reading, or code 13
 *         on communication error (memory allocation failure)
 */
unsigned char* replace_image(const CmdLineParams* params, size_t* replaceSize)
{
    *replaceSize = 0;
    if (params->replaceFilename) {
        return read_file(params->replaceFilename, replaceSize);
    }
    return NULL;
}

/* open_output_file()
 * ------------------
 * Opens the output file for writing if specified in command line parameters,
 * otherwise returns stdout for output.
 *
 * params: pointer to command line parameters structure containing output
 * filename
 *
 * Returns: FILE pointer for output (either opened file or stdout)
 * Errors: exits with code 19 if output file cannot be opened for writing
 */
FILE* open_output_file(const CmdLineParams* params)
{
    if (params->outputFilename) {
        FILE* outputFile = fopen(params->outputFilename, "wb");
        if (!outputFile) {
            file_error(params->outputFilename, true); // exits with code 19
        }
        return outputFile;
    }
    return stdout;
}

/* create_socket_streams()
 * -----------------------
 * Creates separate FILE streams for reading and writing from a socket file
 * descriptor. Duplicates the socket to allow separate buffered streams.
 *
 * sockfd: socket file descriptor to create streams from
 *
 * Returns: SocketStreams structure containing read and write FILE pointers
 * Errors: exits with code 13 if dup() or fdopen() fails
 */
SocketStreams create_socket_streams(int sockfd)
{
    SocketStreams streams = {NULL, NULL};

    // Duplicate the socket for separate read/write streams
    int sockfdDup = dup(sockfd);
    if (sockfdDup < 0) {
        perror("dup");
        close(sockfd);
        exit(EXIT_COMMERR_STATUS);
    }

    // Create FILE* streams
    streams.to = fdopen(sockfd, "w"); // for writing to server
    streams.from = fdopen(sockfdDup, "r"); // for reading from server

    if (!streams.to || !streams.from) {
        perror("fdopen");
        if (streams.to) {
            fclose(streams.to);
        }
        if (streams.from) {
            fclose(streams.from);
        }
        exit(EXIT_COMMERR_STATUS);
    }

    return streams;
}

/* close_socket_streams()
 * ----------------------
 * Closes both read and write FILE streams in a SocketStreams structure.
 * Safe to call with NULL stream pointers.
 *
 * streams: SocketStreams structure containing streams to close
 *
 * Returns: void
 */
void close_socket_streams(SocketStreams streams)
{
    if (streams.to) {
        fclose(streams.to);
    }
    if (streams.from) {
        fclose(streams.from);
    }
}

/* cmd_line_parser()
 * -----------------
 * Parses command line arguments to extract port number and optional
 * arguments for detect, replace, and output image filenames.
 *
 * argc: number of command line arguments
 * argv: array of command line argument strings
 *
 * Returns: CmdLineParams structure containing parsed parameters
 * Errors: exits with code 10 if invalid arguments provided
 */
CmdLineParams cmd_line_parser(int argc, char* argv[])
{
    CmdLineParams params = {0};
    // Skip program name
    argv++;
    argc--;

    if (argc < 1) {
        usage_error();
    }

    // Reject empty-string args or cmds
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '\0') {
            usage_error();
        }
    }

    // Get the port
    params.port = argv[0];
    argv++;
    argc--;

    // Get optional args
    while (argc > 0) {
        if (!parse_optional_args(&params, &argc, &argv)) {
            // unexpected argument
            usage_error();
        }
    }

    return params;
}

/* parse_optional_args()
 * ---------------------
 * Parses optional command line arguments (--detect, --replaceimage,
 * --outputimage) and updates the parameters structure accordingly.
 *
 * params: pointer to parameters structure to update
 * argc: pointer to remaining argument count (modified)
 * argv: pointer to remaining argument array (modified)
 *
 * Returns: true if a valid optional argument was parsed, false otherwise
 * Errors: exits with code 10 if duplicate arguments or invalid format
 */
bool parse_optional_args(CmdLineParams* params, int* argc, char*** argv)
{
    char** args = *argv;
    int count = *argc;

    if (strcmp(args[0], detectImage) == 0) { // Check for --detect
        if (params->detectFilename) { // Check for duplicate
            usage_error();
        }
        // Must have a filename next
        if (count < 2 || args[1][0] == '\0') {
            usage_error();
        }
        params->detectFilename = args[1];
        args += 2;
        count -= 2;
    } else if (strcmp(args[0], replaceImage) == 0) { // Check for --replaceimage
        if (params->replaceFilename) {
            usage_error();
        }
        if (count < 2 || args[1][0] == '\0') {
            usage_error();
        }
        params->replaceFilename = args[1];
        args += 2;
        count -= 2;
    } else if (strcmp(args[0], outputImage) == 0) { // Check for --outputimage
        if (params->outputFilename) {
            usage_error();
        }
        if (count < 2 || args[1][0] == '\0') {
            usage_error();
        }
        params->outputFilename = args[1];
        args += 2;
        count -= 2;
    } else {
        // Not one of our options
        return false;
    }

    *argv = args;
    *argc = count;
    return true;
}

/* connect_to_server()
 * -------------------
 * Establishes a TCP connection to localhost on the specified port using
 * getaddrinfo() and socket system calls.
 *
 * port: string representation of port number to connect to
 *
 * Returns: connected socket file descriptor
 * Errors: exits with code 3 if connection cannot be established
 */
int connect_to_server(const char* port)
{
    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4, for generic could use AF_UNSPEC
    hints.ai_socktype = SOCK_STREAM;

    int err;
    if ((err = getaddrinfo("localhost", port, &hints, &ai))) {
        freeaddrinfo(ai);
        port_error(port);
    }

    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(ai);
        port_error(port);
    }

    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(ai);
        port_error(port);
    }

    freeaddrinfo(ai);
    return fd;
}

/* read_file()
 * -----------
 * Reads the entire contents of a file into a dynamically allocated buffer.
 * Uses fseek() and ftell() to determine file size before reading.
 *
 * filename: path to file to read
 * outSize: pointer to store the size of the file data read
 *
 * Returns: pointer to allocated buffer containing file contents
 * Errors: exits with code 7 if file cannot be opened or read, code 13 on
 *         memory allocation failure or read error
 */
unsigned char* read_file(const char* filename, size_t* outSize)
{
    FILE* file = fopen(filename, "rb"); // Attempt to open the file
    if (!file) {
        file_error(filename, false);
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        file_error(filename, false);
    }

    long fileSize = ftell(file);
    if (fileSize < 0) {
        fclose(file);
        file_error(filename, false);
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        file_error(filename, false);
    }

    // Read into buffer
    unsigned char* buffer = malloc(fileSize);
    if (!buffer) {
        fclose(file);
        exit(EXIT_COMMERR_STATUS);
    }

    size_t readBytes = fread(buffer, 1, fileSize, file);
    fclose(file);

    if (readBytes != (size_t)fileSize) {
        free(buffer);
        exit(EXIT_COMMERR_STATUS);
    }

    *outSize = readBytes;
    return buffer;
}

/* read_stdin()
 * ------------
 * Reads all available data from stdin into a dynamically allocated buffer.
 * Buffer grows as needed using realloc() to accommodate input size.
 *
 * outSize: pointer to store the size of data read from stdin
 *
 * Returns: pointer to allocated buffer containing stdin data
 * Errors: exits with code 13 on memory allocation failure
 */
unsigned char* read_stdin(size_t* outSize)
{
    size_t capacity = STDIN_BUFFER;
    size_t size = 0;
    unsigned char* buffer = malloc(capacity);
    if (!buffer) {
        exit(EXIT_COMMERR_STATUS);
    }

    int chr;
    while ((chr = fgetc(stdin)) != EOF) {
        if (size >= capacity) {
            capacity *= 2;
            unsigned char* newBuffer = realloc(buffer, capacity);
            if (!newBuffer) {
                free(buffer);
                exit(EXIT_COMMERR_STATUS);
            }
            buffer = newBuffer;
        }
        buffer[size++] = (unsigned char)chr;
    }

    *outSize = size;
    return buffer;
}

/* usage_error()
 * -------------
 * Prints the correct usage message to stderr and exits the program
 * with usage error status code.
 *
 * Returns: does not return (exits program)
 */
void usage_error(void)
{
    fprintf(stderr, usageErrorMessage);
    exit(EXIT_USAGE_STATUS);
}

/* file_error()
 * ------------
 * Prints an appropriate file error message to stderr based on whether
 * the error occurred during reading or writing, then exits.
 *
 * filename: name of file that caused the error
 * writing: true if error occurred during writing, false for reading
 *
 * Returns: does not return (exits program)
 */
void file_error(const char* filename, bool writing)
{
    if (!writing) {
        fprintf(stderr, fileReadErrorMessage, filename);
        exit(EXIT_FILEREAD_STATUS);
    } else {
        fprintf(stderr, fileWriteErrorMessage, filename);
        exit(EXIT_FILEWRITE_STATUS);
    }
}

/* port_error()
 * ------------
 * Prints a port connection error message to stderr and exits the program
 * with server port error status code.
 *
 * port: port number that failed to connect
 *
 * Returns: does not return (exits program)
 */
void port_error(const char* port)
{
    fprintf(stderr, portErrorMessage, port);
    exit(EXIT_SERVERPORT_STATUS);
}