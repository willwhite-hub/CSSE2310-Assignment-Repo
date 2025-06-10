/* CSSE2310 2025 Assignment Four
 * protocol.c
 *
 * Written by William White
 */

#include "protocol.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#define BYTE_MASK 0xFF

// Exit codes
typedef enum {
    EXIT_COMMERR_STATUS = 13,
    EXIT_ERRMESSAGE_STATUS = 9,
} ExitStatus;

typedef enum {
    BYTE_SHIFT_1 = 8,
    BYTE_SHIFT_2 = 16,
    BYTE_SHIFT_3 = 24,
    BYTE_INDEX_3 = 3,
    UINT32_NUM_BYTES = 4,
    BYTE_0 = 0,
    BYTE_1 = 1,
    BYTE_2 = 2,
    BYTE_3 = 3,
    BYTE_4 = 4
} OperationBytes;

// Error Messages
static const char* const serverErrorMessage
        = "uqfaceclient: received the following error message: \"%s\"\n";
static const char* const communicationErrorMessage
        = "uqfaceclient: a communication error occurred\n";

/* write_all()
 * -----------
 * Ensures all bytes in a buffer are written to a stream, handling
 * partial writes by continuing until all data is sent or an error occurs.
 *
 * stream: FILE stream to write to
 * buffer: data buffer to write
 * size: number of bytes to write
 *
 * Returns: 0 on success, -1 on error
 *
 */
static int write_all(FILE* stream, const void* buffer, size_t size)
{
    size_t totalWritten = 0;
    const unsigned char* buf = buffer;
    while (totalWritten < size) {
        size_t written
                = fwrite(buf + totalWritten, 1, size - totalWritten, stream);
        if (written == 0) {
            if (ferror(stream)) {
                return -1;
            }

            return -1;
        }
        totalWritten += written;
    }
    return 0;
}

/* read_all()
 * ----------
 * Ensures all requested bytes are read from a stream, handling
 * partial reads by continuing until all data is received or EOF/error.
 *
 * stream: FILE stream to read from
 * buffer: buffer to store read data
 * size: number of bytes to read
 *
 * Returns: 0 on success, -1 on error or premature EOF
 */
static int read_all(FILE* stream, void* buffer, size_t size)
{
    size_t totalRead = 0;
    unsigned char* buf = buffer;
    while (totalRead < size) {
        size_t read = fread(buf + totalRead, 1, size - totalRead, stream);
        totalRead += read;
        if (totalRead < size) {
            // Didn't read anything
            if (feof(stream)) {
                return -1; // EOF reached prematurely
            }
            if (ferror(stream)) {
                return -1; // Read error
            }
        }
    }
    return 0;
}

/* write_uint32_le()
 * -----------------
 * Writes a 32-bit unsigned integer to a stream in little-endian byte order
 * for cross-platform compatibility in network protocols.
 *
 * stream: FILE stream to write to
 * value: 32-bit unsigned integer to write
 *
 * Returns: 0 on success, -1 on error
 */
static int write_uint32_le(FILE* stream, uint32_t value)
{
    unsigned char bytes[BYTE_4];
    bytes[BYTE_0] = (unsigned char)(value & BYTE_MASK);
    bytes[BYTE_1] = (unsigned char)((value >> BYTE_SHIFT_1) & BYTE_MASK);
    bytes[BYTE_2] = (unsigned char)((value >> BYTE_SHIFT_2) & BYTE_MASK);
    bytes[BYTE_3] = (unsigned char)((value >> BYTE_SHIFT_3) & BYTE_MASK);
    return fwrite(bytes, 1, UINT32_NUM_BYTES, stream) == UINT32_NUM_BYTES ? 0
                                                                          : -1;
}

/* read_uint32_le()
 * ----------------
 * Reads a 32-bit unsigned integer from a stream in little-endian byte order
 * and converts it to host byte order for use in the program.
 *
 * stream: FILE stream to read from
 * outvalue: pointer to store the read integer value
 *
 * Returns: 0 on success, -1 on error
 */
int read_uint32_le(FILE* stream, uint32_t* outvalue)
{
    unsigned char bytes[UINT32_NUM_BYTES];
    if (read_all(stream, bytes, UINT32_NUM_BYTES) != 0) {
        return -1;
    }
    *outvalue = ((uint32_t)bytes[0]) | ((uint32_t)bytes[1] << BYTE_SHIFT_1)
            | ((uint32_t)bytes[2] << BYTE_SHIFT_2)
            | ((uint32_t)bytes[BYTE_3] << BYTE_SHIFT_3);
    return 0;
}

/* send_request()
 * --------------
 * Sends a complete client request to the server including protocol prefix,
 * operation type, and image data. Handles both face detection (single image)
 * and face replacement (dual image) operations.
 *
 * to: FILE stream to write request to
 * detectData: buffer containing image data for face detection
 * detectSize: size of detection image data
 * replaceData: buffer containing replacement face image (may be NULL)
 * replaceSize: size of replacement image data (0 if replaceData is NULL)
 *
 * Returns: 0 on success
 * Errors: exits with code 13 on communication error
 */
int send_request(FILE* to, const unsigned char* detectData, size_t detectSize,
        const unsigned char* replaceData, size_t replaceSize)
{
    if (!to || !detectData || detectSize == 0) {
        communication_error(); // Cannot send a request without detect image
    }

    if (write_uint32_le(to, PROTOCOL_PREFIX) != 0) { // Write prefix
        communication_error();
    }

    // Determine operation type
    unsigned char opType = (replaceData && replaceSize > 0) ? OP_FACE_REPLACE
                                                            : OP_FACE_DETECT;

    if (fputc(opType, to) == EOF) { // Write operation type
        communication_error();
    }

    // Write detect image size
    if (write_uint32_le(to, (uint32_t)detectSize) != 0) {
        communication_error();
    }

    // Write detect image data
    if (write_all(to, detectData, detectSize) != 0) {
        communication_error();
    }

    if (opType == OP_FACE_REPLACE) {
        // Write replace image size (4 bytes LE)
        if (write_uint32_le(to, (uint32_t)replaceSize) != 0) {
            communication_error();
        }

        // Write replace image data
        if (write_all(to, replaceData, replaceSize) != 0) {
            communication_error();
        }
    }

    // Flush to ensure all data sent
    if (fflush(to) != 0) {
        communication_error();
    }
    return 0; // success
}

/* receive_request()
 * -----------------
 * Receives a complete response from the server and processes it based on
 * operation type. Writes image data to output file or handles error messages.
 *
 * from: FILE stream to read response from
 * outputFile: FILE stream to write received image data to
 *
 * Returns: 0 on successful image reception
 * Errors: exits with code 13 on communication error, code 9 on server error
 * message
 */
int receive_request(FILE* from, FILE* outputFile)
{
    if (!from || !outputFile) {
        communication_error();
    }
    uint32_t prefix = 0; // Read and verify prefix
    if (read_uint32_le(from, &prefix) != 0 || prefix != PROTOCOL_PREFIX) {
        // Invalid prefix -> communication error
        communication_error();
    }
    int opTypeInt = fgetc(from); // Read operation type
    if (opTypeInt == EOF) {
        communication_error();
    }
    unsigned char opType = (unsigned char)opTypeInt;
    uint32_t dataSize = 0;
    if (read_uint32_le(from, &dataSize) != 0) { // Read message size
        communication_error();
    }
    if (dataSize == 0) { // 0 length response is invalid communication error
        communication_error();
    }
    unsigned char* buffer = malloc(dataSize); // Allocate buffer
    if (!buffer) {
        communication_error();
    }
    if (read_all(from, buffer, dataSize) != 0) { // Read the data
        free(buffer);
        communication_error();
    }
    if (opType == OP_OUTPUT_IMAGE) { // Write image data to output file
        if (write_all(outputFile, buffer, dataSize) != 0) {
            free(buffer);
            communication_error();
        }
        if (fflush(outputFile) != 0) {
            free(buffer);
            communication_error();
        }
        free(buffer);
        return 0; // success
    }
    if (opType == OP_ERROR_MSG) {
        fprintf(stderr, serverErrorMessage, buffer);
        free(buffer);
        exit(EXIT_ERRMESSAGE_STATUS);
    }
    free(buffer);
    communication_error();
    return -1;
}

/* validate_prefix()
 * -----------------
 * Reads and validates the protocol prefix from a stream to ensure the
 * message follows the expected protocol format.
 *
 * from: FILE stream to read prefix from
 *
 * Returns: 0 on valid prefix, -1 on wrong value, -2 on insufficient bytes
 */
int validate_prefix(FILE* from)
{
    uint32_t prefix = 0;
    // Read the prefix and check it maches protocol constant
    size_t n = fread(&prefix, 1, sizeof(prefix), from);
    if (n != sizeof(prefix)) {
        return -2; // Not enough bytes
    }
    if (prefix != PROTOCOL_PREFIX) {
        return -1; // Wrong value
    }
    return 0; // Success
}

/* communication_error()
 * ---------------------
 * Prints a communication error message to stderr and exits the program
 * with the communication error status code (13).
 *
 * Returns: does not return (exits program)
 */
void communication_error(void)
{
    fprintf(stderr, communicationErrorMessage);
    exit(EXIT_COMMERR_STATUS);
}

/* send_protocol_error_file()
 * --------------------------
 * Sends a protocol error message to the client with proper protocol
 * formatting including prefix, error operation type, message length, and
 * message. Flushes the stream to ensure immediate delivery.
 *
 * sockf: FILE stream to send error message to
 * msg: error message string to send
 *
 * Returns: void (silently fails if write operations fail)
 */
void send_protocol_error_file(FILE* sockf, const char* msg)
{
    uint32_t len = strlen(msg);

    if (write_uint32_le(sockf, PROTOCOL_PREFIX) != 0
            || fputc(OP_ERROR_MSG, sockf) == EOF
            || write_uint32_le(sockf, len) != 0
            || fwrite(msg, 1, len, sockf) != len) {
        return; // Error occurred
    }
    fflush(sockf);
}