/**
 * Tab-Separated Values (TSV) Functions
 *
 * by William R. Fraser, 10/19/2011
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sysexits.h>

#include "growbuf.h"
#include "tsv.h"

const size_t initial_col_count = 10;

#define DEBUG if (false)

/**
 * Get the lengths of the fields (columns) in a TSV file.
 *
 * Args:
 *  input           - file to read
 *  field_lengths   - initialized growbuf to store the lengths in (as size_t)
 *
 * Returns:
 *  The number of fields in the file.
 */
size_t tsv_get_field_lengths(FILE* input, growbuf* field_lengths)
{
    size_t num_fields = 0;
    size_t field_len  = 0;

    do {
        field_len = locate_field(input, num_fields++, field_lengths);
        growbuf_append(field_lengths, (void*)&field_len, sizeof(size_t));
    } while (field_len != 0);
   
    return num_fields;
}

/**
 * Figure out the length of the Nth field in a TSV file.
 *
 * This needs the lengths of fields < N in order to work properly.
 *
 * Args:
 *  input           - file to read
 *  index           - index of the field to find
 *  field_lengths   - growbuf with the lengths of all the fields up to this one
 *
 * Returns:
 *  Length of the field, or 0 if it is the last field (this means it continues
 *  to EOL).
 */
size_t locate_field(FILE* input, size_t index, const growbuf* field_lengths)
{
    char buf[512];

    //
    // seek to the end of all the previous fields on the first line
    //

    long startpos = 0;
    for (size_t i = 0; i < index; i++) {
        startpos += ((size_t*)field_lengths->buf)[i];
    }

    DEBUG fprintf(stderr, "startpos: %ld\n", startpos);

    fseek(input, startpos, SEEK_SET);

    //
    // find a field on the first line
    //

    bool found_field = false;
    size_t field_len = 0;
    while (!found_field) {
        size_t bytes_read = fread(buf, 1, sizeof(buf), input);

        if (0 == bytes_read) {
            return 0;
        }

        bool in_whitespace = false;
        for (size_t i = 0; i < bytes_read; i++) {
            
            if (in_whitespace) {
                if (buf[i] == '\n') {
                    // this is the last field
                    // special case, subsequent fields may be longer
                    // go with length of 0

                    DEBUG fprintf(stderr, "hit end of line\n");

                    return 0;
                }

                if (buf[i] != ' ') {
                    field_len += i;

                    DEBUG fprintf(stderr, "think field is length %zu\n", field_len);

                    break;
                }
            }
            else {
                if (buf[i] == '\n') {
                    // last field, see above

                    DEBUG fprintf(stderr, "hit end of line\n");

                    return 0;
                }

                if (buf[i] == ' ') {
                    in_whitespace = true;
                }
            }
        }

        //
        // found what we think is a field on the first line, check for it below
        //

        // reset pos to end of field
        if (0 != fseek(input, startpos + field_len, SEEK_SET)) {
            perror("fseek");
            return 0;
        }

        bool again = false;
        while (!feof(input)) {
            nextline(input);

            //TODO: detect and ignore lines of all dashes or underscores here
            
            fseek(input, startpos + field_len - 1, SEEK_CUR);

            int c = fgetc(input);
            if (c != EOF && c != ' ') {
                // found data, field must be longer

                DEBUG fprintf(stderr, "again, case 1\n");

                again = true;
            }
            else {
                c = fgetc(input);
                if (c != EOF && c == ' ') {
                    // didn't find start of new data, field must be longer

                    DEBUG fprintf(stderr, "again, case 2\n");

                    again = true;
                }
            }

            if (again) {
                // need to keep reading ahead for end of field
                fseek(input, startpos + field_len, SEEK_SET);
                break;
            }

            DEBUG fprintf(stderr, "line was okay\n");
        }

        if (!again) {
            DEBUG fprintf(stderr, "found field\n");

            found_field = true;
        }
    } // while (!found_field)

    return field_len;
}

/**
 * Seek to the start of the next line in a file.
 *
 * Args:
 *  input   - file to seek in
 */
void nextline(FILE* input)
{
    char buf[512];
    bool newline_reached = false;

    while (!newline_reached) {
        long   startpos   = ftell(input);
        size_t bytes_read = fread(buf, 1, sizeof(buf), input);

        if (0 == bytes_read) {
            return;
        }

        for (size_t i = 0; i < bytes_read; i++) {
            if (buf[i] == '\n') {
                newline_reached = true;
                fseek(input, startpos + i + 1, SEEK_SET);
                break;
            }
        }
    }
}

