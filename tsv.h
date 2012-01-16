/**
 * Tab-Separated Values (TSV) Functions
 *
 * by William R. Fraser, 10/19/2011
 */

#ifndef TSV_H
#define TSV_H

#include "growbuf.h"

size_t tsv_get_field_lengths(FILE* input, growbuf* field_lengths, long file_startpos);
size_t locate_field(FILE* input, size_t index, const growbuf* field_lengths, long file_startpos);
void get_line_lengths(FILE* input);
size_t nextline(FILE* input);

#endif
