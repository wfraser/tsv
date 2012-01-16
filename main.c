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
    fprintf(stderr,
"usage: tsv [options] [input-file]\n"
"         > csv-output\n"
"\n"
"Options:\n"
"  +<start line>    Line (1-based) to start on. Default = 1.\n"
"  -t <tab width>   Specify the width of a tab character. Default = 8.\n"
"  --notabs         Use this if the input data contains no tab characters.\n"
"                   This increases performance by reading directly from the\n"
"                   input file instead of converting all tabs to spaces into a\n"
"                   temp file first. With this option, the input must be a\n"
"                   seekable stream.\n"
            );
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
 * Return a copy of a string with the whitespace trimmed off the start and end.
 *
 * Args:
 *  string  - string to trim
 *  length  - length of the string (not including any null terminator)
 *
 * Returns:
 *  Copy of the string, with no leading or trailing whitespace.
 */
char* trim(const char* string, size_t length)
{
    char* newbuf = (char*)malloc(length+1);
    bool  start_found = false;

    size_t newbuf_len = 0;

    for (size_t i = 0; i < length; i++) {
        if (start_found) {
            newbuf[newbuf_len++] = string[i];
        }
        else if (string[i] != ' ') {
            newbuf[newbuf_len++] = string[i];
            start_found = true;
        }
    }

    newbuf[newbuf_len] = '\0';
    
    for (size_t i = newbuf_len - 1; i >= 0; i--) {
        if (newbuf[i] == ' ') {
            newbuf[i] = '\0';
        }
        else {
            break;
        }
    }

    return newbuf;
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
    size_t      start_line    = 1;
    int         tab_width     = 8;
    long        file_startpos = 0;
    bool        convert_tabs  = true;
    bool        parse_flags   = true;

    for (size_t i = 1; i < argc; i++) {
        if (0 == strcmp("--", argv[i])) {
            parse_flags = false;
        }
        else if (parse_flags && 
                    (0 == strcmp("--help", argv[i])
                        || 0 == strcmp("-h", argv[i])))
        {
            usage();
            retval = EX_USAGE;
            goto cleanup;
        }
        else if (parse_flags && argv[i][0] == '+') {
            start_line = atoi(argv[i] + 1);
        }
        else if (parse_flags && 0 == strcmp("--notabs", argv[i])) {
            convert_tabs = false;
        }
        else if (parse_flags && 
                    (0 == strcmp("--tabwidth", argv[i])
                        || 0 == strcmp("-t", argv[i])
                    )
                )
        {
            if (i + 1 == argc) {
                fprintf(stderr, "the -t/--tabwidth flag requires an argument.\n");
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
        else if (NULL == inFilename) {
            inFilename = argv[i];
        }
        else {
            fprintf(stderr, "Error: extra unknown argument \"%s\"\n", argv[i]);
            retval = EX_USAGE;
            goto cleanup;
        }
    }

    if (NULL == inFilename) {
        inFilename = "/dev/stdin";
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

    for (size_t line_no = 1; line_no < start_line; /* nothing */) {
        int c = fgetc(input);
        if ((char)c == '\n') {
            line_no++;
        }
        if (c == EOF) {
            goto cleanup;
        }
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
                buf = (char*)malloc(field_len + 1);
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
            // trim any whitespace from the field
            //

            char* trimmed = trim(buf, bytes_read);

            //
            // write the csv field
            //

            print_csv_field(trimmed, output);

            if (i == num_fields - 1) {
                fwrite("\n", 1, 1, output);
            }
            else {
                fwrite(",", 1, 1, output);
            }

            free(buf);
            free(trimmed);
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

