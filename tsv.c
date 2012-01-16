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

static growbuf *line_lengths = NULL;

typedef struct {
    size_t start, end;
} linelen_pair;

/**
 * Get the lengths of the fields (columns) in a TSV file.
 *
 * Args:
 *  input           - file to read
 *  field_lengths   - initialized growbuf to store the lengths in (as size_t)
 *  file_startpos   - position in the file where TSV data starts
 *
 * Returns:
 *  The number of fields in the file.
 */
size_t tsv_get_field_lengths(FILE* input, growbuf* field_lengths, long file_startpos)
{
    size_t num_fields = 0;
    size_t field_len  = 0;

    fseek(input, file_startpos, SEEK_SET);
    get_line_lengths(input);

    do {
        field_len = locate_field(input, num_fields++, field_lengths, file_startpos);
        growbuf_append(field_lengths, (void*)&field_len, sizeof(size_t));
    } while (field_len != 0);

    growbuf_free(line_lengths);
   
    return num_fields;
}

/**
 * Check whether a column in a file contains only spaces.
 *
 * Arguments:
 *  input   - file to read
 *  linepos - column (0-based) to check
 *
 * Returns:
 *  true if all lines contain a space at this position, or are shorter and
 *      don't have a character at this position.
 *  false if any row contains a non-space character at this position.
 */
bool check_column(FILE* input, size_t linepos)
{
    long pos = ftell(input);

    size_t line_no = 1;
    int c;
    do {
        size_t line_len = nextline(input);
        if (line_len == 0) {
            // past the last line
            c = EOF;
            break;
        }
        else if (linepos >= line_len) {
            DEBUG fprintf(stderr, "line too sort; continuing.\n");
            c = ' ';
            continue;
        }
        else {
            fseek(input, linepos, SEEK_CUR);
            c = fgetc(input);
            DEBUG fprintf(stderr, "line %zu: got (%c)\n", line_no++, (char)c);
        }
    } while (c == ' ');

    fseek(input, pos, SEEK_SET);
    return (c == EOF);
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
 *  file_startpos   - position in the file where TSV data starts
 *
 * Returns:
 *  Length of the field, or 0 if it is the last field (this means it continues
 *  to EOL).
 */
size_t locate_field(FILE* input, size_t index, const growbuf* field_lengths, long file_startpos)
{
    //
    // seek to the end of all the previous fields on the first line
    //

    long line_startpos = 0;
    for (size_t i = 0; i < index; i++) {
        line_startpos += ((size_t*)field_lengths->buf)[i];
    }

    DEBUG fprintf(stderr, "file start pos: %ld\n", file_startpos);
    DEBUG fprintf(stderr, "line start pos: %ld\n", line_startpos);

    fseek(input, file_startpos + line_startpos, SEEK_SET);

    //
    // find a field on the first line
    //

    size_t field_len = 1;
    bool in_whitespace = (fgetc(input) == ' ');
    int c;
    do {
        c = fgetc(input);
        field_len++;

        if (in_whitespace && (char)c != ' ') {
            DEBUG fprintf(stderr, "white -> non transition at %lu\n", line_startpos + field_len);
            if (check_column(input, line_startpos + field_len - 2)) {
                field_len -= 1;
                DEBUG fprintf(stderr, "found a field of length %zu\n", field_len);
                break;
            }
            else {
                DEBUG fprintf(stderr, "nope\n");
            }
            in_whitespace = false;
        }
        else if (!in_whitespace && (char)c == ' ') {
            DEBUG fprintf(stderr, "non -> white transition at %lu\n", line_startpos + field_len);
            if (check_column(input, line_startpos + field_len - 1)) {
                DEBUG fprintf(stderr, "found a field of length %zu\n", field_len);
                break;
            }
            else {
                DEBUG fprintf(stderr, "nope\n");
            }
            in_whitespace = true;
        }
    } while (c != EOF && (char)c != '\n');

    if ((char)c == '\n') {
        //
        // special case: the last field on the line is given as length 0
        //
        DEBUG fprintf(stderr, "found last field\n");
        field_len = 0;
    }
    
    return field_len;
}

/**
 * Compute the lengths of all lines of the given file.
 * This is used by nextline() to avoid having to do any unnecessary repeated 
 * reads on the file. It won't work without first calling this method.
 *
 * Note that because this uses a static variable, this means nextline() will
 * only work on the file given to this function. (TODO: fix this limitation)
 */
void get_line_lengths(FILE* input)
{
    long startpos = ftell(input);

    line_lengths = growbuf_create(10*sizeof(linelen_pair));
    
    size_t pos   = 0;
    size_t start = 0;
    size_t line_no = 1;
    int    c;
    do {
        c = fgetc(input);
        if ((char)c == '\n' || c == EOF) {
            linelen_pair p = { .start = start, .end = pos };
            DEBUG fprintf(stderr, "line %zu, (%zX - %zX)\n", line_no++, start, pos);
            growbuf_append(line_lengths, &p, sizeof(linelen_pair));
            start = pos+1;
        }
        pos++;
    } while (c != EOF);

    // reset to original position
    fseek(input, startpos, SEEK_SET);
}

/**
 * Seek to the start of the next line in a file.
 *
 * Args:
 *  input   - file to seek in
 *
 * Returns:
 *  The length of the new line now positioned on.
 */
size_t nextline(FILE* input)
{
    int pos = ftell(input);

    DEBUG fprintf(stderr, "position is %X\n", pos);

    for (size_t i = 0; i < growbuf_num_elems(line_lengths, linelen_pair); i++) {
        linelen_pair p = growbuf_index(line_lengths, i, linelen_pair);
        if (pos >= p.start && pos <= p.end) {
            DEBUG fprintf(stderr, "on line %zu\n", i+1);
            DEBUG fprintf(stderr, "line starts at %zX and ends at %zX\n", p.start, p.end);
            fseek(input, p.end - pos + 1, SEEK_CUR);

            linelen_pair next = growbuf_index(line_lengths, i+1, linelen_pair);
            return (next.end - next.start);
        }
    }

    return 0;
}

