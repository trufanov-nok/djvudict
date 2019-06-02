/*
 * bsdecoder.h - BZZ-decoder from DjVuLibre, a general purpose compressor
 * based on the Burrows-Wheeler (or "block sorting") transform.
 */

#ifndef MDJVU_BSDECODER_H
#define MDJVU_BSDECODER_H
#include "../src/jb2/zp.h"

class BSDecoder
{
    public:
        BSDecoder(FILE *f, int len);
        ~BSDecoder();
        
        long tell(void) const;
        void close(void);
        unsigned int decode(void);
        
        size_t read(void *buffer, size_t sz);       
    private:

        // Data
        long            offset;
        int             bptr;
        unsigned int   blocksize;
        int             size;
        unsigned char  *data;
        bool            eof;
        
        // Decoder
        ZPDecoder gzp;
        ZPBitContext ctx[300];
};

#endif
