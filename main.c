/**
 * Tab-Separated Values (TSV) to Comma-Separated Values (CSV) converter.
 *
 * by William R. Fraser, 10/19/2011
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sysexits.h>

#include "growbuf.h"
#include "csvformat.h"
#include "tsv.h"

const size_t initial_field_count = 10;

#define DEBUG if (false)

/**
 * Prints command-line usage to standard error.
 */
void usage()
{
    fprintf(stderr, "usage: tsv {tab-delimited-input} [+{start line}] [+notabs] [-t {tab-width}]\n"
                    "         > csv-output\n"
                    "\n"
                    "use +{start-line} to indicate the line (1-based) on which TSV data starts.\n"
                    "(default is 1)\n"
                    "\n"
                    "you can use +notabs if the TSV data contains no TAB characters (it uses all\n"
                    "space characters for separation). This increases performance by reading directly\n"
                    "from the input file, instead of converting to an all-spaces temp file first by\n"
                    "default.\n"
                    "(Note that with this option, the input must be a seekable stream. /dev/stdin\n"
                    "will NOT work!)\n");
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
    char        tempFilename[16] = "";
    FILE*       input         = NULL;
    FILE*       output        = stdout;
    growbuf*    field_lengths = NULL;
    size_t      num_fields    = 0;
    char*       buf           = NULL;
    size_t      bytes_read    = 0;
    size_t      field_len     = 0;
    size_t      trimmed_len   = 0;
    size_t      start_line    = 1;
    int         tab_width     = 8;
    long        file_startpos = 0;
    bool        convert_tabs  = true;

    for (size_t i = 1; i < argc; i++) {
        if (NULL == inFilename) {
            inFilename = argv[i];
        }
        else if ('+' == argv[i][0]) {
            if (0 == strcmp("+notabs", argv[i])) {
                convert_tabs = false;
            }
            else {
               start_line = atoi(argv[i]);
            }
        }
        else if (0 == strcmp("-t", argv[i])) {
            if (i + 1 == argc) {
                fprintf(stderr, "the -t flag requires an argument.\n");
                retval = EX_USAGE;
                goto cleanup;
            }
            
            tab_width = atoi(argv[i+1]);
            if (tab_width < 1) {
                fprintf(stderr, "invalid tab width.\n");
                retval = EX_USAGE;
                goto cleanup;
            }

            i++;
        }
    }

    if (NULL == inFilename) {
        usage();
        retval = EX_USAGE;
        goto cleanup;
    }

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

    if (convert_tabs) {
        //
        // Convert input file to an all space-separated temp file
        //

        int   fd         = -1;
        FILE* tempOutput = NULL;

        strncpy(tempFilename, "/tmp/tsv.XXXXXX", sizeof(tempFilename));
        fd = mkstemp(tempFilename);
        if (-1 == fd) {
            perror("Error making temporary file");
            retval = EX_OSERR;
            goto cleanup;
        }

        DEBUG fprintf(stderr, "converting tabs to temp file %s\n", tempFilename);

        tempOutput = fdopen(fd, "w");
        if (NULL == tempOutput) {
            perror("Error opening temp file");
            retval = EX_OSERR;
            goto cleanup;
        }
        
        size_t i = 0;
        int c;
        while (EOF != (c = fgetc(input))) {
            if ('\t' == c) {
                for (size_t j = (i % tab_width); j < tab_width; j++) {
                    fputc(' ', tempOutput);
                    i++;
                }
            }
            else {
                if ('\n' == c) {
                    i = 0;
                }
                else {
                    i++;
                }
                fputc(c, tempOutput);
            }
        }

        fclose(input);
        fclose(tempOutput);
        input = fopen(tempFilename, "r");
    }

    field_lengths = growbuf_create(initial_field_count * sizeof(size_t));
    if (NULL == field_lengths) {
        fprintf(stderr, "malloc failed\n");
        retval = EX_OSERR;
        goto cleanup;
    }

    //
    // Skip to the start line
    //

    for (size_t i = 1; i < start_line; i++) {
        nextline(input);
    }
    file_startpos = ftell(input);

    //
    // Figure out the field lengths.
    //

    num_fields = tsv_get_field_lengths(input, field_lengths, file_startpos);
    
    DEBUG
    for (size_t i = 0; i < num_fields; i++) {
        fprintf(stderr, "field %zu: %zu\n", i, ((size_t*)field_lengths->buf)[i]);
    }

    fseek(input, file_startpos, SEEK_SET);

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
            buf[trimmed_len] = '\0';

            DEBUG fprintf(stderr, "trimmed_len(%zu)\n", trimmed_len);

            //
            // write the csv field
            //

            print_csv_field(buf, output);

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

    if (convert_tabs) {
        unlink(tempFilename);
    }

    return retval;
}

