#include "iq_reader.h"
#include <stdlib.h>

IQReader *iq_reader_open(const char *path, size_t block_len)
{
    IQReader *r = (IQReader *)malloc(sizeof(IQReader));
    if (!r) return NULL;

    r->f = fopen(path,"rb");
    if(!r->f) {
        free(r);
        return NULL;
    }
    r->block_len = block_len;
    r->u8_buf = (uint8_t *)malloc(2 * block_len);
    r->iq_buf = (float *)malloc(2 * block_len * sizeof(float));

    if(!r->u8_buf || !r->iq_buf) {
        iq_reader_close(r);
        return NULL;
    }

    return r;
}

size_t iq_reader_read(IQReader *r)
{
    if(!r || !r->f) return 0;

    size_t bytes_read = fread(r->u8_buf, 1, 2 * r->block_len, r->f);
    size_t samples = bytes_read / 2; // Complex samples

    // Convert to float IQ in range -1 to 1
    for(size_t i = 0; i < samples; i++) {
        r->iq_buf[2*i+0] = ((float)r->u8_buf[2*i+0] - 127.5) / 127.5f;
        r->iq_buf[2*i+1] = ((float)r->u8_buf[2*i+1] - 127.5) / 127.5f;
    }

    return samples;
}

void iq_reader_close(IQReader *r)
{
    if (!r) return;

    if(r->f) fclose(r->f);
    if(r->u8_buf) free(r->u8_buf);
    if(r->iq_buf) free(r->iq_buf);

    free(r);
}