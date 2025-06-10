/* CSSE2310 2025 Assignment 4
 * uqfacedetect.c
 *
 * Writtten by William White
 */

#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <opencv2/imgcodecs/imgcodecs_c.h>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/objdetect/objdetect_c.h>
#include <opencv2/core/core_c.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
/* -------------------------------------------------------------------------- */
// Constants
#define FACE_CASCADE                                                           \
    "/local/courses/csse2310/resources/a4/haarcascade_frontalface_alt2.xml"
#define EYE_CASCADE                                                            \
    "/local/courses/csse2310/resources/a4/haarcascade_eye_tree_eyeglasses.xml"
#define HALF 0.5
#define SCALE_FACTOR 1.1
#define EYE_RADIUS_FACTOR 0.25

// Exit Messages
const char* const usageErrorMessage
        = "Usage: ./uqfacedetect maxconnections maxsize [portnum]\n";
const char* const fileWriteErrorMessage
        = "uqfacedetect: cannot open the image file for writing\n";
const char* const cascadeErrorMessage
        = "uqfacedetect: cannot load a cascade classifier\n";
const char* const portErrorMessage
        = "uqfacedetect: cannot listen on given port \"%s\"\n";

// Protocol Errors
const char* const invalidOpType = "invalid operation type";
const char* const invalidMessage = "invalid message";
const char* const imageZeroBytes = "image is 0 bytes";
const char* const imageTooLarge = "image too large";
const char* const invalidImage = "invalid image";
const char* const invalidNoFaces = "no faces detected in image";

// File paths
const char* const imageFile = "/tmp/imagefile.jpg";
const char* const responseFile
        = "/local/courses/csse2310/resources/a4/responsefile";
const char* const totalThreadCountFile = "/tmp/csse2310.totalthreadcount.txt";
const char* const activeSocketCountFile = "/tmp/csse2310.activesocketcount.txt";
const char* const activeThreadCountFile = "/tmp/csse2310.activethreadcount.txt";
// Max values
const char* const maxConnections = "10000";
const char* const maxSize = "4294967295";

/* -------------------------------------------------------------------------- */
// Enums

// Helpful named constants
typedef enum {
    DECIMAL_BASE = 10,
    BUFFER_SIZE = 4096,
    UINT32_NUM_BYTES = 4,
    DEGREES_IN_CIRCLE = 360,
    COLOUR_MAX = 255,
    LINE_THICKNESS = 3,
    MIN_NEIGHBOURS = 3,
    LINE_TYPE = 8,
    EYE_MIN_SIZE = 15,
    FACE_MIN_SIZE = 30
} MagicNumbers;

// Program Exit Codes
typedef enum {
    EXIT_USAGE_STATUS = 11,
    EXIT_FILEWRITE_STATUS = 1,
    EXIT_CASCADE_STATUS = 18,
    EXIT_SERVERPORT_STATUS = 5
} ExitStatus;

// Command Line Parameters
typedef struct {
    unsigned int maxconnections;
    uint32_t maxsize;
    const char* portnum;
} CmdLineParams;

// Mutex Struct
typedef struct {
    pthread_mutex_t fileAndCascadeMutex;
    int totalThreadCount;
    int activeThreadCount;
    int activeSocketCount;
} SharedState;

// Thread Args
typedef struct {
    int clientFd;
    uint32_t imgSize;
    CmdLineParams* params;
    SharedState* shared;
    int opType;
} ClientArgs;

// Protocol Results
typedef enum {
    PROTOCOL_SUCCESS = 1,
    PROTOCOL_ERROR = 0,
    COMMUNICATION_ERROR = -1
} ProtocolResult;

// For OpenCV processing results
typedef enum {
    OPENCV_SUCCESS = 0,
    OPENCV_INVALID_IMAGE = -1,
    OPENCV_NO_FACES = -2
} OpenCVResult;

/* -------------------------------------------------------------------------- */
// Function Prototypes

CmdLineParams cmd_line_parser(int argc, char* argv[]);
const char* get_port(int argc, char* argv[]);
bool is_number(const char* str);
bool valid_range(const char* str, const char* maxValue);
void start_server(CmdLineParams* params, SharedState* shared);
void* client_handler(void* args);
void send_responsefile(FILE* sockf, int fd);
ProtocolResult handle_protocol_prefix(FILE* sockf, int fd);
ProtocolResult handle_protocol_header(FILE* sockf, void* args);
ProtocolResult handle_protocol_image(FILE* sockf, void* args);
bool save_image(const char* filename, const uint8_t* data, uint32_t size);
int detect_and_draw_faces_mutexed(SharedState* shared, const char* filename);
uint8_t* read_file_to_buffer(const char* filename, long* length);
bool send_protocol_image(FILE* sockf, long size, uint8_t* data);
void usage_error(void);
int setup_listen_socket(const char* portnum);
void print_port_number(int listenFd);
CvHaarClassifierCascade* load_cascade(const char* path);
IplImage* make_greyscale(IplImage* img);
void draw_faces_and_eyes(IplImage* img, CvSeq* faces, CvMemStorage* storage,
        CvHaarClassifierCascade* eyeCascade, IplImage* grey);
int detect_and_draw_faces(const char* filename);
void write_count_to_file(const char* path, int count);
void decrement_thread_and_socket_counts(SharedState* shared);
int replace_faces_in_memory(const char* filename);
int replace_faces_mutexed(SharedState* shared, const char* filename);
bool read_and_save_image(FILE* sockf, const char* filename, uint32_t size);
void cascade_check(void);

/* ------------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    cascade_check();
    CmdLineParams params = cmd_line_parser(argc, argv);

    SharedState shared;
    pthread_mutex_init(&shared.fileAndCascadeMutex, NULL);

    shared.totalThreadCount = 0;
    shared.activeThreadCount = 0;
    shared.activeSocketCount = 0;
    write_count_to_file(totalThreadCountFile, 0);
    write_count_to_file(activeThreadCountFile, 0);
    write_count_to_file(activeSocketCountFile, 0);

    start_server(&params, &shared);
    pthread_mutex_destroy(&shared.fileAndCascadeMutex);
    return 0;
}

/* cmd_line_parser()
 * -----------------
 * Parses and validates command line arguments for maxconnections, maxsize,
 * and optional port number. Validates that numeric arguments are within
 * acceptable ranges.
 *
 * argc: number of command line arguments
 * argv: array of command line argument strings
 *
 * Returns: CmdLineParams structure with parsed and validated parameters
 * Errors: exits with code 11 if invalid arguments provided
 */
CmdLineParams cmd_line_parser(int argc, char* argv[])
{
    CmdLineParams params = {0};
    argv++; // Skip over program name
    argc--;

    if (argc < 2) { // Check if less than 2 args given
        usage_error();
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '\0') { // Reject empty-string args
            usage_error();
        }
    }

    if (!is_number(argv[0])) { // Validate max connections
        usage_error();
    }
    const char* maxConStr = argv[0];
    if (maxConStr[0] == '+') {
        maxConStr++;
    }
    if (!valid_range(maxConStr, maxConnections)) {
        usage_error();
    }
    // Add to MaxValues struct
    params.maxconnections = strtoul(maxConStr, NULL, DECIMAL_BASE);
    if (!is_number(argv[1])) { // Validate maxsize
        usage_error();
    }
    const char* maxSizeStr = argv[1];
    if (!valid_range(maxSizeStr, maxSize)) {
        usage_error();
    }
    // Add to MaxValues
    params.maxsize = strtoul(maxSizeStr, NULL, DECIMAL_BASE);

    // Get the port
    params.portnum = get_port(argc - 2, argv + 2);
    return params;
}

/* get_port()
 * ----------
 * Extracts the optional port number from the remaining command line arguments.
 * Validates that exactly zero or one additional argument is provided.
 *
 * argc: number of remaining arguments after maxconnections and maxsize
 * argv: array of remaining argument strings
 *
 * Returns: port number string if provided, NULL if no port specified
 * Errors: exits with code 11 if more than one extra argument given
 */
const char* get_port(int argc, char* argv[])
{
    if (argc == 0) { // No port number given
        return NULL;
    }
    if (argc == 1) {
        if (argv[0][0] == '\0') { // Check if empty string
            usage_error();
        }
        return argv[0];
    }
    usage_error(); // More than one extra arg given; invalid
    return NULL;
}

/* is_number()
 * -----------
 * Checks if a string represents a valid positive integer, optionally
 * with a leading '+' sign.
 *
 * str: string to validate as a number
 *
 * Returns: true if string is a valid positive integer, false otherwise
 */
bool is_number(const char* str)
{
    if (!str || *str == '\0') { // Check if empty
        return false;
    }

    // Check if negative number
    if (*str == '+') {
        str++;
        if (*str == '\0') {
            return false; // Only '+' was given
        }
    }

    while (*str) {
        if (!isdigit((unsigned char)*str)) {
            return false;
        }
        str++;
    }
    return true;
}

/* valid_range()
 * -------------
 * Checks if a numeric string is within the valid range by comparing
 * string length and lexicographic order against maximum value.
 *
 * str: string representation of number to check
 * maxValue: string representation of maximum allowed value
 *
 * Returns: true if str represents a number <= maxValue, false otherwise
 */
bool valid_range(const char* str, const char* maxValue)
{
    size_t strLength = strlen(str);
    size_t maxLength = strlen(maxValue);
    if (strLength < maxLength) {
        return true;
    }
    if (strLength > maxLength) {
        return false;
    }
    return strcmp(str, maxValue) <= 0;
}

/* start_server()
 * --------------
 * Initializes and runs the main server loop. Sets up listening socket,
 * accepts client connections, and spawns detached threads to handle
 * each client connection.
 *
 * params: pointer to command line parameters containing server configuration
 * shared: pointer to shared state structure for thread synchronization
 *
 * Returns: does not return (runs indefinitely)
 * Errors: exits with code 5 if unable to set up listening socket
 */
void start_server(CmdLineParams* params, SharedState* shared)
{
    // Set up socket
    int listenFd = setup_listen_socket(params->portnum);
    if (listenFd == -1) {
        fprintf(stderr, portErrorMessage,
                params->portnum ? params->portnum : "0");
        exit(EXIT_SERVERPORT_STATUS);
    }
    print_port_number(listenFd); // Print port to stderr

    while (1) {
        int clientFd = accept(listenFd, NULL, NULL);
        if (clientFd < 0) {
            continue;
        }
        pthread_mutex_lock(&shared->fileAndCascadeMutex);
        shared->activeSocketCount++;
        write_count_to_file(activeSocketCountFile, shared->activeSocketCount);
        pthread_mutex_unlock(&shared->fileAndCascadeMutex);

        // Prep thread arguments
        ClientArgs* clientArgs = malloc(sizeof(ClientArgs));
        if (!clientArgs) {
            close(clientFd);
            continue;
        }
        clientArgs->clientFd = clientFd;
        clientArgs->params = params;
        clientArgs->shared = shared;

        // Spawn detached thread to handle client
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, clientArgs) != 0) {
            close(clientFd);
            free(clientArgs);
            continue;
        }
        pthread_detach(tid);
    }
}

/* client_handler()
 * ----------------
 * Thread function that handles communication with a single client.
 * Processes protocol messages, performs face detection/replacement,
 * and sends responses back to client.
 *
 * args: pointer to ClientArgs structure containing client information
 *
 * Returns: NULL when client session ends
 * Global variables modified: updates thread and socket counts in shared state
 */
void* client_handler(void* args)
{
    ClientArgs* clientArgs = (ClientArgs*)args;
    SharedState* shared = clientArgs->shared;
    int fd = clientArgs->clientFd;
    // Open a FILE* for buffered reading/writing
    FILE* sockf = fdopen(fd, "r+b");
    if (!sockf) {
        close(fd);
        free(args);
        return NULL;
    }

    // Mutex
    pthread_mutex_lock(&shared->fileAndCascadeMutex);
    shared->totalThreadCount++;
    shared->activeThreadCount++;
    write_count_to_file(totalThreadCountFile, shared->totalThreadCount);
    write_count_to_file(activeThreadCountFile, shared->activeThreadCount);
    pthread_mutex_unlock(&shared->fileAndCascadeMutex);

    while (true) {
        ProtocolResult prefixResult = handle_protocol_prefix(sockf, fd);
        if (prefixResult == COMMUNICATION_ERROR) { // Communication error
            break;
        }
        if (prefixResult == PROTOCOL_ERROR) { // Protocol error
            continue; // Continue to next request
        }
        ProtocolResult headerResult = handle_protocol_header(sockf, args);
        if (headerResult == COMMUNICATION_ERROR) { // Communication error
            break;
        }
        if (headerResult == PROTOCOL_ERROR) { // Protocol error
            continue;
        }
        ProtocolResult imageResult = handle_protocol_image(sockf, args);
        if (imageResult == COMMUNICATION_ERROR) { // Communication error
            break;
        }
        if (imageResult == PROTOCOL_SUCCESS) { // Success!
            continue;
        }
    }

    fclose(sockf);
    free(args);
    decrement_thread_and_socket_counts(shared);
    return NULL;
}

/* decrement_thread_and_socket_counts()
 * ------------------------------------
 * Decrements both active thread count and active socket count in shared
 * state and updates corresponding files. Used during client cleanup.
 *
 * shared: pointer to shared state structure
 *
 * Returns: void
 * Global variables modified: decrements activeThreadCount and activeSocketCount
 */
void decrement_thread_and_socket_counts(SharedState* shared)
{
    pthread_mutex_lock(&shared->fileAndCascadeMutex);
    shared->activeThreadCount--;
    write_count_to_file(activeThreadCountFile, shared->activeThreadCount);
    shared->activeSocketCount--;
    write_count_to_file(activeSocketCountFile, shared->activeSocketCount);
    pthread_mutex_unlock(&shared->fileAndCascadeMutex);
}

/* send_responsefile()
 * -------------------
 * Sends the contents of the response file to the client when an invalid
 * protocol prefix is received. Used for HTTP requests to the server.
 *
 * sockf: FILE stream for client communication
 * fd: socket file descriptor for shutdown operations
 *
 * Returns: void
 */
void send_responsefile(FILE* sockf, int fd)
{
    FILE* resp = fopen(responseFile, "rb");
    if (resp) {
        char buf[BUFFER_SIZE];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), resp)) > 0) {
            fwrite(buf, 1, n, sockf);
        }
        fclose(resp);
        fflush(sockf);
        shutdown(fd, SHUT_WR);
    }
}

/* handle_protocol_prefix()
 * ------------------------
 * Reads and validates the protocol prefix from client. If prefix is invalid,
 * sends the response file and closes connection.
 *
 * sockf: FILE stream for client communication
 * fd: socket file descriptor for shutdown operations
 *
 * Returns: PROTOCOL_SUCCESS if valid prefix, PROTOCOL_ERROR for protocol
 *          error, COMMUNICATION_ERROR for connection termination
 */
ProtocolResult handle_protocol_prefix(FILE* sockf, int fd)
{
    uint32_t prefix = 0;
    if (read_uint32_le(sockf, &prefix) != 0) {
        send_protocol_error_file(sockf, invalidMessage);
        fflush(sockf);
        return COMMUNICATION_ERROR;
    }

    if (prefix != PROTOCOL_PREFIX) {
        send_responsefile(sockf, fd);
        return COMMUNICATION_ERROR;
    }
    return PROTOCOL_SUCCESS;
}

/* handle_protocol_header()
 * ------------------------
 * Reads and validates the protocol header containing operation type and
 * image size. Checks operation type and image size constraints.
 *
 * sockf: FILE stream for client communication
 * args: pointer to ClientArgs structure to store parsed header information
 *
 * Returns: PROTOCOL_SUCCESS if valid header, PROTOCOL_ERROR for protocol
 *          error, COMMUNICATION_ERROR for connection failure
 */
ProtocolResult handle_protocol_header(FILE* sockf, void* args)
{
    ClientArgs* clientArgs = (ClientArgs*)args;
    int opType = fgetc(sockf);
    if (opType == EOF) {
        send_protocol_error_file(sockf, invalidMessage);
        return COMMUNICATION_ERROR;
    }
    unsigned char opTypeByte = (unsigned char)opType;
    if (opTypeByte != OP_FACE_DETECT && opTypeByte != OP_FACE_REPLACE) {
        send_protocol_error_file(sockf, invalidOpType);
        fflush(sockf);
        return PROTOCOL_ERROR;
    }
    uint32_t imgSize;
    if (read_uint32_le(sockf, &imgSize) != 0) {
        send_protocol_error_file(sockf, invalidMessage);
        return COMMUNICATION_ERROR;
    }
    if (imgSize == 0) {
        send_protocol_error_file(sockf, imageZeroBytes);
        fflush(sockf);
        return PROTOCOL_ERROR;
    }
    uint32_t maxImageSize = clientArgs->params->maxsize;
    if (maxImageSize != 0 && imgSize > maxImageSize) {
        send_protocol_error_file(sockf, imageTooLarge);
        fflush(sockf);
        return PROTOCOL_ERROR;
    }
    clientArgs->opType = opTypeByte;
    clientArgs->imgSize = imgSize;
    return PROTOCOL_SUCCESS;
}

/* handle_protocol_image()
 * -----------------------
 * Reads image data from client, performs face detection or replacement
 * operation, and sends the processed image back to client.
 *
 * sockf: FILE stream for client communication
 * args: pointer to ClientArgs structure containing operation parameters
 *
 * Returns: PROTOCOL_SUCCESS if operation completed successfully,
 *          PROTOCOL_ERROR for processing errors, COMMUNICATION_ERROR
 *          for connection failures
 */
ProtocolResult handle_protocol_image(FILE* sockf, void* args)
{
    ClientArgs* clientArgs = (ClientArgs*)args;
    SharedState* shared = clientArgs->shared;
    if (!read_and_save_image(sockf, imageFile, clientArgs->imgSize)) {
        return COMMUNICATION_ERROR;
    }
    OpenCVResult opencvResult;
    if (clientArgs->opType == OP_FACE_REPLACE) {
        uint32_t faceSize;
        if (read_uint32_le(sockf, &faceSize) != 0) {
            return COMMUNICATION_ERROR;
        }
        // Save face image to the same file (overwriting)
        if (!read_and_save_image(sockf, imageFile, faceSize)) {
            return COMMUNICATION_ERROR;
        }
        opencvResult = (OpenCVResult)replace_faces_mutexed(shared, imageFile);
    } else {
        opencvResult = (OpenCVResult)detect_and_draw_faces_mutexed(
                shared, imageFile);
    }
    switch (opencvResult) { // Handle OpenCV results
    case OPENCV_INVALID_IMAGE:
        send_protocol_error_file(sockf, invalidImage);
        fflush(sockf);
        return PROTOCOL_ERROR;
    case OPENCV_NO_FACES:
        send_protocol_error_file(sockf, invalidNoFaces);
        fflush(sockf);
        return PROTOCOL_ERROR;
    case OPENCV_SUCCESS:
        break; // Continue to send image
    default: // Unexpected result, treat as invalid image
        send_protocol_error_file(sockf, invalidImage);
        fflush(sockf);
        return PROTOCOL_ERROR;
    }
    long outSize = 0;
    uint8_t* outBuf = read_file_to_buffer(imageFile, &outSize);
    if (!outBuf) {
        return COMMUNICATION_ERROR;
    }
    if (!send_protocol_image(sockf, outSize, outBuf)) {
        free(outBuf);
        return COMMUNICATION_ERROR;
    }
    free(outBuf);
    return PROTOCOL_SUCCESS;
}

/* read_and_save_image()
 * ---------------------
 * Reads a specified amount of image data from a socket stream and saves
 * it to a file. Used to receive image data from clients.
 *
 * sockf: FILE stream to read image data from
 * filename: path to file where image data should be saved
 * size: number of bytes to read and save
 *
 * Returns: true on success, false on read or write failure
 */
bool read_and_save_image(FILE* sockf, const char* filename, uint32_t size)
{
    uint8_t* buf = malloc(size);
    if (!buf || fread(buf, 1, size, sockf) != size) {
        free(buf);
        return false;
    }
    bool ok = save_image(filename, buf, size);
    free(buf);
    return ok;
}

/* save_image()
 * ------------
 * Writes image data from a buffer to a file. Used to store received
 * image data for processing.
 *
 * filename: path to file where image should be saved
 * data: buffer containing image data
 * size: number of bytes to write
 *
 * Returns: true if all data written successfully, false otherwise
 */
bool save_image(const char* filename, const uint8_t* data, uint32_t size)
{
    FILE* f = fopen(filename, "wb");
    if (!f) {
        return false;
    }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

/* detect_and_draw_faces_mutexed()
 * -------------------------------
 * Thread-safe wrapper for detect_and_draw_faces() that uses mutex locking
 * to prevent concurrent access to OpenCV cascade classifiers.
 *
 * shared: pointer to shared state containing mutex
 * filename: path to image file to process
 *
 * Returns: 0 on success, -1 on failure, -2 if no faces detected
 */
int detect_and_draw_faces_mutexed(SharedState* shared, const char* filename)
{
    int result;
    pthread_mutex_lock(&shared->fileAndCascadeMutex);
    result = detect_and_draw_faces(filename);
    pthread_mutex_unlock(&shared->fileAndCascadeMutex);
    return result;
}

/* replace_faces_mutexed()
 * -----------------------
 * Thread-safe wrapper for replace_faces_in_memory() that uses mutex locking
 * to prevent concurrent access to OpenCV cascade classifiers.
 *
 * shared: pointer to shared state containing mutex
 * filename: path to file containing both target and replacement images
 *
 * Returns: 0 on success, -1 on failure, -2 if no faces detected
 */
int replace_faces_mutexed(SharedState* shared, const char* filename)
{
    int result;
    pthread_mutex_lock(&shared->fileAndCascadeMutex);
    result = replace_faces_in_memory(filename);
    pthread_mutex_unlock(&shared->fileAndCascadeMutex);
    return result;
}

/* read_file_to_buffer()
 * ---------------------
 * Reads the entire contents of a file into a dynamically allocated buffer.
 * Used to load processed images for transmission back to clients.
 *
 * filename: path to file to read
 * length: pointer to store the file size
 *
 * Returns: pointer to allocated buffer containing file contents, or NULL on
 * failure
 */
uint8_t* read_file_to_buffer(const char* filename, long* length)
{
    FILE* f = fopen(filename, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *length = size;
    return buf;
}

/* send_protocol_image()
 * ---------------------
 * Sends a processed image back to the client using the proper protocol
 * format including prefix, operation type, size, and image data.
 *
 * sockf: FILE stream for client communication
 * size: size of image data to send
 * data: buffer containing image data
 *
 * Returns: true on successful transmission, false on error
 */
bool send_protocol_image(FILE* sockf, long size, uint8_t* data)
{
    uint32_t prefix = PROTOCOL_PREFIX;
    uint32_t imageSize = (uint32_t)size;
    uint8_t opType = OP_OUTPUT_IMAGE;

    if (fwrite(&prefix, 1, UINT32_NUM_BYTES, sockf) != UINT32_NUM_BYTES) {
        return false;
    }
    if (fputc(opType, sockf) == EOF) {
        return false;
    }
    if (fwrite(&imageSize, 1, UINT32_NUM_BYTES, sockf) != UINT32_NUM_BYTES) {
        return false;
    }
    if (fwrite(data, 1, size, sockf) != (size_t)size) {
        return false;
    }
    if (fflush(sockf) != 0) {
        return false;
    }

    return true;
}

/* setup_listen_socket()
 * ---------------------
 * Creates and configures a listening socket bound to the specified port.
 * Uses getaddrinfo() for address resolution and sets SO_REUSEADDR option.
 *
 * portnum: string representation of port number, or NULL for ephemeral port
 *
 * Returns: file descriptor of listening socket, or -1 on failure
 */
int setup_listen_socket(const char* portnum)
{
    struct addrinfo hints = {0}, *res, *rp;
    int listenFd = -1;
    int optVal = 1;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Use "0" for ephemeral port if portnum is NULL
    if (!portnum) {
        portnum = "0";
    }

    int gai = getaddrinfo(NULL, portnum, &hints, &res);
    if (gai != 0) {
        return -1;
    }

    // Try each address until one works
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        listenFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenFd == -1) {
            continue;
        }
        // Allow reuse after close
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
        if (bind(listenFd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // Success
        }
        close(listenFd);
        listenFd = -1;
    }
    freeaddrinfo(res);

    if (listenFd == -1) {
        return -1;
    }

    if (listen(listenFd, SOMAXCONN) != 0) {
        close(listenFd);
        return -1;
    }
    return listenFd;
}

/* print_port_number()
 * -------------------
 * Retrieves and prints the actual port number that the server is listening
 * on to stderr. Used to report ephemeral port assignments.
 *
 * listenFd: file descriptor of the listening socket
 *
 * Returns: void
 */
void print_port_number(int listenFd)
{
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(listenFd, (struct sockaddr*)&addr, &addrlen) == 0) {
        fprintf(stderr, "%u\n", ntohs(addr.sin_port));
        fflush(stderr);
    }
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

/* load_cascade()
 * --------------
 * Loads a Haar cascade classifier from the specified file path for use
 * in face or eye detection.
 *
 * path: file path to the cascade classifier XML file
 *
 * Returns: pointer to loaded cascade classifier, or NULL on failure
 */
CvHaarClassifierCascade* load_cascade(const char* path)
{
    return (CvHaarClassifierCascade*)cvLoad(path, 0, 0, 0);
}

/* make_greyscale()
 * ----------------
 * Converts a color image to greyscale and applies histogram equalization
 * to improve contrast for feature detection.
 *
 * img: pointer to source color image
 *
 * Returns: pointer to newly created greyscale image
 */

IplImage* make_greyscale(IplImage* img)
{
    IplImage* grey = cvCreateImage(cvGetSize(img), IPL_DEPTH_8U, 1);
    cvCvtColor(img, grey, CV_BGR2GRAY);
    cvEqualizeHist(grey, grey);
    return grey;
}

/* draw_faces_and_eyes()
 * ---------------------
 * Draws detection markers on an image for detected faces and eyes.
 * Draws magenta ellipses around faces and green circles around eyes.
 *
 * img: image to draw on
 * faces: sequence of detected face rectangles
 * storage: memory storage for eye detection
 * eyeCascade: cascade classifier for eye detection (may be NULL)
 * grey: greyscale version of image for eye detection
 *
 * Returns: void
 */
void draw_faces_and_eyes(IplImage* img, CvSeq* faces, CvMemStorage* storage,
        CvHaarClassifierCascade* eyeCascade, IplImage* grey)
{
    for (int i = 0; i < (faces ? faces->total : 0); ++i) {
        CvRect* r = (CvRect*)cvGetSeqElem(faces, i);
        CvPoint center = {cvRound(r->x + r->width * HALF),
                cvRound(r->y + r->height * HALF)};
        cvEllipse(img, center, cvSize(r->width / 2, r->height / 2), 0, 0,
                DEGREES_IN_CIRCLE, cvScalar(COLOUR_MAX, 0, COLOUR_MAX, 0),
                LINE_THICKNESS, LINE_TYPE, 0);
        if (eyeCascade) {
            cvSetImageROI(grey, *r);
            CvSeq* eyes = cvHaarDetectObjects(grey, eyeCascade, storage,
                    SCALE_FACTOR, LINE_THICKNESS, 0,
                    cvSize(EYE_MIN_SIZE, EYE_MIN_SIZE), cvSize(0, 0));
            for (int j = 0; j < (eyes ? eyes->total : 0); ++j) {
                CvRect* er = (CvRect*)cvGetSeqElem(eyes, j);
                CvPoint eyeCentre = {r->x + er->x + er->width / 2,
                        r->y + er->y + er->height / 2};
                int radius
                        = cvRound((er->width + er->height) * EYE_RADIUS_FACTOR);
                cvCircle(img, eyeCentre, radius, cvScalar(0, COLOUR_MAX, 0, 0),
                        LINE_THICKNESS, LINE_TYPE, 0);
            }
            cvResetImageROI(grey);
        }
    }
}

/* detect_and_draw_faces()
 * -----------------------
 * Loads an image file, detects faces and eyes using Haar cascade classifiers,
 * draws detection markers, and saves the annotated image back to the file.
 *
 * filename: path to image file to process
 *
 * Returns: 0 on success, -1 on file/cascade loading failure, -2 if no faces
 */
int detect_and_draw_faces(const char* filename)
{
    CvHaarClassifierCascade* faceCascade = load_cascade(FACE_CASCADE);
    CvHaarClassifierCascade* eyeCascade = load_cascade(EYE_CASCADE);
    if (!faceCascade || !eyeCascade) {
        return -1;
    }
    IplImage* img = cvLoadImage(filename, CV_LOAD_IMAGE_COLOR);
    if (!img) {
        // Free cascades before return
        cvRelease((void**)&faceCascade);
        cvRelease((void**)&eyeCascade);
        return -1;
    }
    IplImage* grey = make_greyscale(img);
    CvMemStorage* storage = cvCreateMemStorage(0);
    CvSeq* faces = cvHaarDetectObjects(grey, faceCascade, storage, SCALE_FACTOR,
            MIN_NEIGHBOURS, 0, cvSize(FACE_MIN_SIZE, FACE_MIN_SIZE),
            cvSize(0, 0));
    if (!faces || faces->total == 0) {
        // Free all
        cvReleaseImage(&img);
        cvReleaseImage(&grey);
        cvRelease((void**)&faceCascade);
        cvRelease((void**)&eyeCascade);
        cvReleaseMemStorage(&storage);
        return -2; // No faces
    }
    draw_faces_and_eyes(img, faces, storage, eyeCascade, grey);
    int saved = cvSaveImage(filename, img, 0);
    cvReleaseImage(&img);
    cvReleaseImage(&grey);
    cvRelease((void**)&faceCascade);
    cvRelease((void**)&eyeCascade);
    cvReleaseMemStorage(&storage);
    return saved ? 0 : -1;
}

/* replace_faces_in_memory()
 * -------------------------
 * Detects faces in an image and replaces them with a scaled version of
 * a replacement face image. Both images are loaded from the same file path.
 *
 * filename: path to file containing both the target image and replacement face
 *
 * Returns: 0 on success, -1 on loading failure, -2 if no faces detected
 */
int replace_faces_in_memory(const char* filename)
{
    CvHaarClassifierCascade* faceCascade = load_cascade(FACE_CASCADE);
    if (!faceCascade) {
        return -1;
    }
    IplImage* img = cvLoadImage(filename, CV_LOAD_IMAGE_COLOR); // main image
    if (!img) {
        cvRelease((void**)&faceCascade);
        return -1;
    }

    IplImage* face = cvLoadImage(filename, CV_LOAD_IMAGE_COLOR); // face image
    if (!face) {
        cvReleaseImage(&img);
        cvRelease((void**)&faceCascade);
        return -1;
    }

    IplImage* grey = make_greyscale(img);
    CvMemStorage* storage = cvCreateMemStorage(0);
    CvSeq* faces = cvHaarDetectObjects(grey, faceCascade, storage, SCALE_FACTOR,
            MIN_NEIGHBOURS, 0, cvSize(FACE_MIN_SIZE, FACE_MIN_SIZE),
            cvSize(0, 0));
    if (!faces || faces->total == 0) {
        cvReleaseImage(&img);
        cvReleaseImage(&face);
        cvReleaseImage(&grey);
        cvRelease((void**)&faceCascade);
        cvReleaseMemStorage(&storage);
        return -2;
    }
    for (int i = 0; i < faces->total; ++i) {
        CvRect* r = (CvRect*)cvGetSeqElem(faces, i);
        IplImage* resized = cvCreateImage(
                cvSize(r->width, r->height), face->depth, face->nChannels);
        cvResize(face, resized, CV_INTER_LINEAR);
        cvSetImageROI(img, *r);
        cvCopy(resized, img, NULL);
        cvResetImageROI(img);
        cvReleaseImage(&resized);
    }
    int saved = cvSaveImage(filename, img, 0);
    cvReleaseImage(&img);
    cvReleaseImage(&face);
    cvReleaseImage(&grey);
    cvRelease((void**)&faceCascade);
    cvReleaseMemStorage(&storage);
    return saved ? 0 : -1;
}

/* write_count_to_file()
 * ---------------------
 * Writes an integer count value to a file as text, used for tracking
 * thread and socket statistics.
 *
 * path: file path to write the count to
 * count: integer value to write to the file
 *
 * Returns: void
 */
void write_count_to_file(const char* path, int count)
{
    FILE* f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d\n", count);
        fclose(f);
    }
}

/* cascade_check()
 * ---------------
 * Verifies that required Haar cascade classifier files can be loaded
 * at program startup. Exits if cascades cannot be loaded.
 *
 * Returns: void
 * Errors: exits with code 18 if cascade files cannot be loaded
 */
void cascade_check(void)
{
    CvHaarClassifierCascade* faceCascade
            = (CvHaarClassifierCascade*)cvLoad(FACE_CASCADE, 0, 0, 0);
    CvHaarClassifierCascade* eyeCascade
            = (CvHaarClassifierCascade*)cvLoad(EYE_CASCADE, 0, 0, 0);
    if (!faceCascade || !eyeCascade) {
        fprintf(stderr, cascadeErrorMessage);
        exit(EXIT_CASCADE_STATUS);
    }
    cvRelease((void**)&faceCascade);
    cvRelease((void**)&eyeCascade);
}