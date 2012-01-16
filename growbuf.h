/**
 * Growable Buffer
 *
 * by William R. Fraser, 10/19/2011
 */

#ifndef GROWBUF_H
#define GROWBUF_H

typedef struct _growbuf
{
    void*  buf;
    size_t allocated_size;
    size_t size;
} growbuf;

growbuf* growbuf_create(size_t initial_size);
void     growbuf_free(growbuf* gb);
int      growbuf_append(growbuf* gb, const void* buf, size_t len);
int      growbuf_append_byte(growbuf* gb, char byte);

//
// Get the element of given type at given index.
//
#define growbuf_index(g, index, type) (((type*)((g)->buf))[(index)])

//
// Get the number of elements if growbuf holds given type.
//
#define growbuf_num_elems(g, type) ((g)->size / sizeof(type))

//
// This is a bit of a hack because it uses __typeof__ which is a GNU-ism
//
// Returns true if growbuf g contains thing. Is typesafe.
//
#define growbuf_contains(g, thing)                                          \
    ({                                                                      \
        bool ret = false;                                                   \
        __typeof__(thing) _thing = (thing);                                 \
        for (size_t i = 0; i < growbuf_num_elems((g), _thing); i++) {       \
            if (growbuf_index((g), i, __typeof__(thing)) == _thing) {       \
                ret = true;                                                 \
                break;                                                      \
            }                                                               \
        }                                                                   \
        ret;                                                                \
    })

#endif //GROWBUF_H
