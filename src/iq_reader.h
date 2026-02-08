#ifndef IQ_READER_H
#define IQ_READER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    FILE *f;
    size_t block_len;
    uint8_t *u8_buf;
    float *iq_buf;
} IQReader;


/* Opna IQ lesara */
IQReader *iq_reader_open(const char *path, size_t blocklen);

/* Lesa IQ block */
size_t iq_reader_read(IQReader *r);

/* Loka reader og frelsa minni */
void iq_reader_close(IQReader *r);

#endif