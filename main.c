/**
 * Tab-Separated Values (TSV) to Comma-Separated Values (CSV) converter.
 *
 * by William R. Fraser, 10/19/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sysexits.h>

#include "growbuf.h"
#include "tsv.h"

const size_t initial_field_count = 10;

#define DEBUG if (false)

/**
 * Prints command-line usage to standard error.
 */
void usage()
{
    fprintf(stderr, "usage:\n\ttsv tab-delimited-input > csv-output\n");
}

/**
 * Read from the file to the next end of line.
 *
 * Args:
 *  input       - file to read from
 *  bytes_read  - set to the number of bytes read
 *
 * Returns:
 *  Buffer containing the read result.
 */
char* read_to_eol(FILE* input, size_t* bytes_read)
{
    char*    retbuf = NULL;
    char     buf[512];
    growbuf* gb = NULL;

    gb = growbuf_create(512);

    while (!feof(input)) {
        size_t chunk_bytes_read = fread(buf, 1, sizeof(buf), input);

        bool   got_newline = false;
        size_t bytes_append;
        for (bytes_append = 0; bytes_append < chunk_bytes_read; bytes_append++) {
            if (buf[bytes_append] == '\n') {
                got_newline = true;
                break;
            }
        }

        growbuf_append(gb, buf, bytes_append);

        if (got_newline) {
            DEBUG fprintf(stderr, "read %zu bytes, newline after %zu, rewinding %zu\n", chunk_bytes_read, bytes_append, chunk_bytes_read-bytes_append);

            fseek(input, -1 * (chunk_bytes_read - bytes_append - 1), SEEK_CUR);
            goto cleanup;
        }
    }

cleanup:
    
    //
    // free just the growbuf structure, and return the inner buffer
    //

    retbuf = gb->buf;
    *bytes_read = gb->size;

    free(gb);
    return retbuf;
}

/**
 * Find the trimmed length of a string.
 *
 * Args:
 *  string  - string to inspect
 *  length  - length of the string (not including any null terminator)
 *
 * Returns:
 *  Length of the string, not including any trailing whitespace.
 */
size_t trimmed_length(const char* string, size_t length)
{
    for (size_t i = length - 1; i < length; i--) {
        if (string[i] != ' ' && string[i] != '\n') {
            return i + 1;
        }
    }

    return 0;
}

/**
 * Program main entry point
 *
 * Args:
 *  Standard args.
 *
 * Return:
 *  One of the EX_* constants from sysexit.h
 */
int main(int argc, char** argv)
{
    int         retval        = EX_OK;
    const char* inFilename    = NULL;
    FILE*       input         = NULL;
    FILE*       output        = stdout;
    growbuf*    field_lengths = NULL;
    size_t      num_fields    = 0;
    char*       buf           = NULL;
    size_t      bytes_read    = 0;
    size_t      field_len     = 0;
    size_t      trimmed_len   = 0;

    if (argc != 2) {
        usage();
        retval = EX_USAGE;
        goto cleanup;
    }

    inFilename = argv[1];

    if (0 != access(inFilename, R_OK)) {
        perror("Error reading input file");
        retval = EX_NOINPUT;
        goto cleanup;
    }

    input = fopen(inFilename, "r");
    if (NULL == input) {
        perror("Error opening input stream");
        retval = EX_NOINPUT;
        goto cleanup;
    }

    field_lengths = growbuf_create(initial_field_count * sizeof(size_t));
    if (NULL == field_lengths) {
        fprintf(stderr, "malloc failed\n");
        retval = EX_OSERR;
        goto cleanup;
    }

    //
    // Figure out the field lengths.
    //

    num_fields = tsv_get_field_lengths(input, field_lengths);
    
    DEBUG
    for (size_t i = 0; i < num_fields; i++) {
        fprintf(stderr, "field %zu: %zu\n", i, ((size_t*)field_lengths->buf)[i]);
    }

    fseek(input, 0, SEEK_SET);

    //
    // Read the fields.
    //

    while (!feof(input)) {

        for (size_t i = 0; i < num_fields; i++) {
            field_len = ((size_t*)field_lengths->buf)[i];

            if (0 == field_len) {
                //
                // 0 is a special case, it means "read to end of line"
                //

                buf = read_to_eol(input, &bytes_read);

                DEBUG fprintf(stderr, "got %zu bytes to eol: ", bytes_read);
                DEBUG fwrite(buf, 1, bytes_read, stderr);
            }
            else {
                buf = (char*)malloc(field_len);
                if (NULL == buf) {
                    fprintf(stderr, "malloc failed\n");
                    retval = EX_OSERR;
                    goto cleanup;
                }

                bytes_read = fread(buf, 1, field_len, input);

                DEBUG fprintf(stderr, "got %zu bytes: ", bytes_read);
                DEBUG fwrite(buf, 1, bytes_read, stderr);
            }

            if (0 == bytes_read) {
                //
                // EOL or error; don't continue
                //
                if (NULL != buf) {
                    free(buf);
                    buf = NULL;
                }
                break;
            }

            //
            // trim any whitespace from the end of the field
            //

            trimmed_len = trimmed_length(buf, bytes_read);

            DEBUG fprintf(stderr, "trimmed_len(%zu)\n", trimmed_len);

            //
            // write the csv field
            //

            fwrite("\"", 1, 1, output);
            for (size_t j = 0; j < trimmed_len; j++) {
                if (buf[j] == '"') {
                    fwrite("\"\"", 1, 2, output);
                }
                else {
                    fwrite(&(buf[j]), 1, 1, output);
                }
            }
            fwrite("\"", 1, 1, output);

            if (i == num_fields - 1) {
                fwrite("\n", 1, 1, output);
            }
            else {
                fwrite(",", 1, 1, output);
            }

            free(buf);
            buf = NULL;

        } // fields

    } // lines

cleanup:
    if (NULL != input) {
        fclose(input);
    }

    if (NULL != field_lengths) {
        growbuf_free(field_lengths);
    }

    if (NULL != buf) {
        free(buf);
    }

    return retval;
}

