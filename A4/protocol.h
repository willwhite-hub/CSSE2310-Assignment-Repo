/* CSSE2310 2025 Assignment Four
 * protocol.h
 *
 * Written by William White
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define PROTOCOL_PREFIX 0x23107231

typedef enum {
    OP_FACE_DETECT = 0,
    OP_FACE_REPLACE = 1,
    OP_OUTPUT_IMAGE = 2,
    OP_ERROR_MSG = 3
} OperationType;

typedef struct {
    unsigned char opType;
    unsigned char* detectImage;
    size_t detectSize;
    unsigned char* replaceImage;
    size_t replaceSize;
} ServerRequest;

// Function Prototypes
int read_uint32_le(FILE* stream, uint32_t* outValue);
int send_request(FILE* to, const unsigned char* detectData, size_t detectSize,
        const unsigned char* replaceData, size_t replaceSize);
int receive_request(FILE* from, FILE* outputFile);
void communication_error(void);
int validate_prefix(FILE* from);
int send_protocol_error(int fd, const char* errmsg);
void send_protocol_error_file(FILE* sockf, const char* msg);
#endif