/*
 * CSV Format Functions
 *
 * by William R. Fraser
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "growbuf.h"
#include "csvformat.h"

//#define DEBUG
#define DEBUG if (false)

/**
 * same as strchr() except it looks for multiple characters.
 *
 * Arguments:
 *   haystack	- string to be searched
 *   chars	- characters to search for
 *   nchars	- number of characters in chars
 *
 * Return Value:
 *   Pointer to the first one of chars in haystack, or NULL if none found.
 */
const char* strchrs(const char* haystack, const char* chars, size_t nchars)
{
    const char* end = haystack + strlen(haystack);
    while (haystack < end) {
        for (size_t i = 0; i < nchars; i++) {
            if (*haystack == chars[i]) {
                return haystack;
            }
        }
	haystack++;
    }

    return NULL;
}

/**
 * Print a CSV field, with appropriate double-quotes.
 *
 * No double-quotes are used, unless the field contains a comma, or a newline.
 *
 * Arguments:
 *   field	- field to print
 *   output	- file pointer to print to
 */
void print_csv_field(const char* field, FILE* output)
{
    if (NULL != strchrs(field, ",\n", 2)) {
        fprintf(output, "\"");
        for (size_t i = 0; i < strlen(field); i++) {
            if (field[i] == '"') {
                fprintf(output, "\"\"");
            }
            else {
                fprintf(output, "%c", field[i]);
            }
        }
        fprintf(output, "\"");
    }
    else {
        fprintf(output, "%s", field);
    }
}

