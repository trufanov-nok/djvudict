/*
 * bs.cpp - BZZ-coder from DjVuLibre, a general purpose compressor
 * based on the Burrows-Wheeler (or "block sorting") transform.
 */

#include "../src/base/mdjvucfg.h"
#include "../include/minidjvu-mod/minidjvu-mod.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsdecoder.h"

// ========================================
// --- Global Definitions

                        
// Sorting tresholds
enum { FREQMAX=4, CTXIDS=3 };
// Limits on block sizes
enum { MINBLOCK=10, MAXBLOCK=4096 };



#ifdef OVERFLOW
#undef OVERFLOW
#endif
// Overflow required when encoding
static const int OVERFLOW=32;

// Sorting tresholds
static const int RANKSORT_THRESH=10;
static const int QUICKSORT_STACK=512;
static const int PRESORT_THRESH=10;
static const int PRESORT_DEPTH=8;
static const int RADIX_THRESH=32768;

static const int FREQS0=100000;
static const int FREQS1=1000000;

// ========================================
// -- Sorting Routines

    
class _BSort    // DJVU_CLASS
{
public:
    ~_BSort();
    _BSort(unsigned char *data, int size);
    void run(int &markerpos);
private:
    // Members
    int                        size;
    unsigned char *data;
    unsigned int    *posn;
    int                        *rank;
    // Helpers
    inline int GT(int p1, int p2, int depth);
    inline int GTD(int p1, int p2, int depth);
    // -- final in-depth sort
    void ranksort(int lo, int hi, int d);
    // -- doubling sort
    int    pivot3r(int *rr, int lo, int hi);
    void quicksort3r(int lo, int hi, int d);
    // -- presort to depth PRESORT_DEPTH
    unsigned char pivot3d(unsigned char *dd, int lo, int hi);
    void quicksort3d(int lo, int hi, int d);
    // -- radixsort
    void radixsort16(void);
    void radixsort8(void);
};


// blocksort -- the main entry point

static void blocksort(unsigned char *data, int size, int &markerpos)
{
    _BSort bsort(data, size);
    bsort.run(markerpos);
}


// _BSort construction

_BSort::_BSort(unsigned char *xdata, int xsize)
    : size(xsize), data(xdata)
{
    posn = (unsigned int *)    calloc (size, sizeof(unsigned int));
    rank = (int *) calloc (size+1, sizeof(int));

    assert(size>0 && size<0x1000000);
    rank[size] = -1;
}

_BSort::~_BSort()
{
    free(posn);
    free(rank);
}



// GT -- compare suffixes using rank information

inline int _BSort::GT(int p1, int p2, int depth)
{
    int r1, r2;
    int twod = depth + depth;
    while (1)
    {
        r1=rank[p1+depth]; r2=rank[p2+depth];
        p1+=twod;    p2+=twod;
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1]; r2=rank[p2];
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1+depth]; r2=rank[p2+depth];
        p1+=twod;    p2+=twod;
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1]; r2=rank[p2];
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1+depth]; r2=rank[p2+depth];
        p1+=twod;    p2+=twod;
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1]; r2=rank[p2];
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1+depth]; r2=rank[p2+depth];
        p1+=twod;    p2+=twod;
        if (r1!=r2) 
            return (r1>r2);
        r1=rank[p1]; r2=rank[p2];
        if (r1!=r2) 
            return (r1>r2);
    };
}


// _BSort::ranksort -- 
// -- a simple insertion sort based on GT

void _BSort::ranksort(int lo, int hi, int depth)
{
    int i,j;
    for (i=lo+1; i<=hi; i++)
    {
        int tmp = posn[i];
        for(j=i-1; j>=lo && GT(posn[j], tmp, depth); j--)
            posn[j+1] = posn[j];
        posn[j+1] = tmp;
    }
    for(i=lo;i<=hi;i++) 
        rank[posn[i]]=i;
}

// pivot -- return suitable pivot

int _BSort::pivot3r(int *rr, int lo, int hi)
{
    int c1, c2, c3;
    if (hi-lo > 256)
    {
        c1 = pivot3r(rr, lo, (6*lo+2*hi)/8);
        c2 = pivot3r(rr, (5*lo+3*hi)/8, (3*lo+5*hi)/8);
        c3 = pivot3r(rr, (2*lo+6*hi)/8, hi);
    }
    else
    {
        c1 = rr[posn[lo]];
        c2 = rr[posn[(lo+hi)/2]];
        c3 = rr[posn[hi]];
    }
    // Extract median
    if (c1>c3)
        { int tmp=c1; c1=c3; c3=tmp; }
    if (c2<=c1)
        return c1;
    else if (c2>=c3)
        return c3;
    else
        return c2;
}


// _BSort::quicksort3r -- Three way quicksort algorithm 
//        Sort suffixes based on rank at pos+depth
//        The algorithm breaks into ranksort when size is 
//        smaller than RANKSORT_THRESH

static inline int mini(int a, int b) 
{
    return (a<=b) ? a : b;
}

static inline void vswap(int i, int j, int n, unsigned int *x)
{
    while (n-- > 0) 
    {
        int tmp = x[i]; x[i++]=x[j]; x[j++]=tmp;
    }
}

void _BSort::quicksort3r(int lo, int hi, int depth)
{
    /* Initialize stack */
    int slo[QUICKSORT_STACK];
    int shi[QUICKSORT_STACK];
    int sp = 1;
    slo[0] = lo;
    shi[0] = hi;
    // Recursion elimination loop
    while (--sp>=0)
    {
        lo = slo[sp];
        hi = shi[sp];
        // Test for insertion sort
        if (hi-lo<RANKSORT_THRESH)
        {
            ranksort(lo, hi, depth);
        }
        else
        {
            int tmp;
            int *rr=rank+depth;
            int med = pivot3r(rr,lo,hi);
            // -- positions are organized as follows:
            //     [lo..l1[ [l1..l[ ]h..h1] ]h1..hi]
            //            =                <             >                =
            int l1 = lo;
            int h1 = hi;
            while (rr[posn[l1]]==med && l1<h1) { l1++; }
            while (rr[posn[h1]]==med && l1<h1) { h1--; }
            int l = l1;
            int h = h1;
            // -- partition set
            for (;;)
            {
                while (l<=h)
                {
                    int c = rr[posn[l]] - med;
                    if (c > 0) break;
                    if (c == 0) { tmp=posn[l]; posn[l]=posn[l1]; posn[l1++]=tmp; }
                    l++;
                }
                while (l<=h)
                {
                    int c = rr[posn[h]] - med;
                    if (c < 0) break;
                    if (c == 0) { tmp=posn[h]; posn[h]=posn[h1]; posn[h1--]=tmp; }
                    h--;
                }
                if (l>h) break;
                tmp=posn[l]; posn[l]=posn[h]; posn[h]=tmp;
            }
            // -- reorganize as follows
            //     [lo..l1[ [l1..h1] ]h1..hi]
            //            <                =                > 
            tmp = mini(l1-lo, l-l1);
            vswap(lo, l-tmp, tmp, posn);
            l1 = lo + (l-l1);
            tmp = mini(hi-h1, h1-h);
            vswap(hi-tmp+1, h+1, tmp, posn);
            h1 = hi - (h1-h);
            // -- process segments
            assert(sp+2<QUICKSORT_STACK);
            // ----- middle segment (=?) [l1, h1]
            for(int i=l1;i<=h1;i++) 
                rank[posn[i]] = h1;
            // ----- lower segment (<) [lo, l1[
            if (l1 > lo)
            {
                for(int i=lo;i<l1;i++) 
                    rank[posn[i]]=l1-1;
                slo[sp]=lo;
                shi[sp]=l1-1;
                if (slo[sp] < shi[sp])    
                    sp++;
            }
            // ----- upper segment (>) ]h1, hi]
            if (h1 < hi)
            {
                slo[sp]=h1+1;
                shi[sp]=hi;
                if (slo[sp] < shi[sp])    
                    sp++;
            }
        }
    }
}






// GTD -- compare suffixes using data information 
//    (up to depth PRESORT_DEPTH)

inline int _BSort::GTD(int p1, int p2, int depth)
{
    unsigned char c1, c2;
    p1+=depth; p2+=depth;
    while (depth < PRESORT_DEPTH)
    {
        // Perform two
        c1=data[p1]; c2=data[p2];
        if (c1!=c2) 
            return (c1>c2);
        c1=data[p1+1]; c2=data[p2+1];
        p1+=2;    p2+=2; depth+=2;
        if (c1!=c2) 
            return (c1>c2);
    }
    if (p1<size && p2<size)
        return 0;
    return (p1<p2);
}

// pivot3d -- return suitable pivot

unsigned char _BSort::pivot3d(unsigned char *rr, int lo, int hi)
{
    unsigned char c1, c2, c3;
    if (hi-lo > 256)
    {
        c1 = pivot3d(rr, lo, (6*lo+2*hi)/8);
        c2 = pivot3d(rr, (5*lo+3*hi)/8, (3*lo+5*hi)/8);
        c3 = pivot3d(rr, (2*lo+6*hi)/8, hi);
    }
    else
    {
        c1 = rr[posn[lo]];
        c2 = rr[posn[(lo+hi)/2]];
        c3 = rr[posn[hi]];
    }
    // Extract median
    if (c1>c3)
        { int tmp=c1; c1=c3; c3=tmp; }
    if (c2<=c1)
        return c1;
    else if (c2>=c3)
        return c3;
    else
        return c2;
}


// _BSort::quicksort3d -- Three way quicksort algorithm 
//        Sort suffixes based on strings until reaching
//        depth rank at pos+depth
//        The algorithm breaks into ranksort when size is 
//        smaller than PRESORT_THRESH

void _BSort::quicksort3d(int lo, int hi, int depth)
{
    /* Initialize stack */
    int slo[QUICKSORT_STACK];
    int shi[QUICKSORT_STACK];
    int sd[QUICKSORT_STACK];
    int sp = 1;
    slo[0] = lo;
    shi[0] = hi;
    sd[0] = depth;
    // Recursion elimination loop
    while (--sp>=0)
    {
        lo = slo[sp];
        hi = shi[sp];
        depth = sd[sp];
        // Test for insertion sort
        if (depth >= PRESORT_DEPTH)
        {
            for (int i=lo; i<=hi; i++)
                rank[posn[i]] = hi;
        }
        else if (hi-lo<PRESORT_THRESH)
        {
            int i,j;
            for (i=lo+1; i<=hi; i++)
            {
                int tmp = posn[i];
                for(j=i-1; j>=lo && GTD(posn[j], tmp, depth); j--)
                    posn[j+1] = posn[j];
                posn[j+1] = tmp;
            }
            for(i=hi;i>=lo;i=j)
            {
                int tmp = posn[i];
                rank[tmp] = i;
                for (j=i-1; j>=lo && !GTD(tmp,posn[j],depth); j--)
                    rank[posn[j]] = i;
            }
        }
        else
        {
            int tmp;
            unsigned char *dd=data+depth;
            unsigned char med = pivot3d(dd,lo,hi);
            // -- positions are organized as follows:
            //     [lo..l1[ [l1..l[ ]h..h1] ]h1..hi]
            //            =                <             >                =
            int l1 = lo;
            int h1 = hi;
            while (dd[posn[l1]]==med && l1<h1) { l1++; }
            while (dd[posn[h1]]==med && l1<h1) { h1--; }
            int l = l1;
            int h = h1;
            // -- partition set
            for (;;)
            {
                while (l<=h)
                {
                    int c = (int)dd[posn[l]] - (int)med;
                    if (c > 0) break;
                    if (c == 0) { tmp=posn[l]; posn[l]=posn[l1]; posn[l1++]=tmp; }
                    l++;
                }
                while (l<=h)
                {
                    int c = (int)dd[posn[h]] - (int)med;
                    if (c < 0) break;
                    if (c == 0) { tmp=posn[h]; posn[h]=posn[h1]; posn[h1--]=tmp; }
                    h--;
                }
                if (l>h) break;
                tmp=posn[l]; posn[l]=posn[h]; posn[h]=tmp;
            }
            // -- reorganize as follows
            //     [lo..l1[ [l1..h1] ]h1..hi]
            //            <                =                > 
            tmp = mini(l1-lo, l-l1);
            vswap(lo, l-tmp, tmp, posn);
            l1 = lo + (l-l1);
            tmp = mini(hi-h1, h1-h);
            vswap(hi-tmp+1, h+1, tmp, posn);
            h1 = hi - (h1-h);
            // -- process segments
            assert(sp+3<QUICKSORT_STACK);
            // ----- middle segment (=?) [l1, h1]
            l = l1; h = h1;
            if (med==0) // special case for marker [slow]
                for (int i=l; i<=h; i++)
                    if ((int)posn[i]+depth == size-1)
                    { 
                        tmp=posn[i]; posn[i]=posn[l]; posn[l]=tmp; 
                        rank[tmp]=l++; break; 
                    }
            if (l<h)
            { 
                slo[sp] = l; shi[sp] = h; sd[sp++] = depth+1;
            }
            else if (l==h)
            {
                rank[posn[h]] = h;
            }
            // ----- lower segment (<) [lo, l1[
            l = lo;
            h = l1-1;
            if (l<h)
            {
                slo[sp] = l; shi[sp] = h; sd[sp++] = depth;
            }
            else if (l==h)
            {
                rank[posn[h]] = h;
            }
            // ----- upper segment (>) ]h1, hi]
            l = h1+1;
            h = hi;
            if (l<h)
            {
                slo[sp] = l; shi[sp] = h; sd[sp++] = depth;
            }
            else if (l==h)
            {
                rank[posn[h]] = h;
            }
        }
    }
}




// _BSort::radixsort8 -- 8 bit radix sort

void _BSort::radixsort8(void)
{
    int i;
    // Initialize frequency array
    int lo[256], hi[256];
    for (i=0; i<256; i++)
        hi[i] = lo[i] = 0;
    // Count occurences
    for (i=0; i<size-1; i++)
        hi[data[i]] ++;
    // Compute positions (lo)
    int last = 1;
    for (i=0; i<256; i++)
    {
        lo[i] = last;
        hi[i] = last + hi[i] - 1;
        last = hi[i] + 1;
    }
    for (i=0; i<size-1; i++)
    {
        posn[ lo[data[i]]++ ] = i;
        rank[ i ] = hi[data[i]];
    }
    // Process marker "$"
    posn[0] = size-1;
    rank[size-1] = 0;
    // Extra element
    rank[size] = -1;
}


// _BSort::radixsort16 -- 16 bit radix sort

void _BSort::radixsort16(void)
{
    int i;
    // Initialize frequency array
    int *ftab = (int *) calloc(65536, sizeof(int));
    for (i=0; i<65536; i++)
        ftab[i] = 0;
    // Count occurences
    unsigned char c1 = data[0];
    for (i=0; i<size-1; i++)
    {
        unsigned char c2 = data[i+1];
        ftab[(c1<<8)|c2] ++;
        c1 = c2;
    }
    // Generate upper position
    for (i=1;i<65536;i++)
        ftab[i] += ftab[i-1];
    // Fill rank array with upper bound
    c1 = data[0];
    for (i=0; i<size-2; i++)
    {
        unsigned char c2 = data[i+1];
        rank[i] = ftab[(c1<<8)|c2];
        c1 = c2;
    }
    // Fill posn array (backwards)
    c1 = data[size-2];
    for (i=size-3; i>=0; i--)
    {
        unsigned char c2 = data[i];
        posn[ ftab[(c2<<8)|c1]-- ] = i;
        c1 = c2;
    }
    // Fixup marker stuff
    assert(data[size-1]==0);
    c1 = data[size-2];
    posn[0] = size-1;
    posn[ ftab[(c1<<8)] ] = size-2;
    rank[size-1] = 0;
    rank[size-2] = ftab[(c1<<8)];
    // Extra element
    rank[size] = -1;
}



// _BSort::run -- main sort loop

void _BSort::run(int &markerpos)
{
    int lo, hi;
    assert(size>0);
    assert(data[size-1]==0);
    // Step 1: Radix sort 
    int depth = 0;
    if (size > RADIX_THRESH)
    { 
        radixsort16();
        depth=2;
    }
    else
    { 
        radixsort8(); 
        depth=1;
    }
    // Step 2: Perform presort to depth PRESORT_DEPTH
    for (lo=0; lo<size; lo++)
    {
        hi = rank[posn[lo]];
        if (lo < hi)
            quicksort3d(lo, hi, depth);
        lo = hi;
    }
    depth = PRESORT_DEPTH;
    // Step 3: Perform rank doubling
    int again = 1;
    while (again)
    {
        again = 0;
        int sorted_lo = 0;
        for (lo=0; lo<size; lo++)
        {
            hi = rank[posn[lo]&0xffffff];
            if (lo == hi)
            {
                lo += (posn[lo]>>24) & 0xff;
            }
            else
            {
                if (hi-lo < RANKSORT_THRESH)
                {
                    ranksort(lo, hi, depth);
                }
                else
                {
                    again += 1;
                    while (sorted_lo < lo-1)
                    {
                        int step = mini(255, lo-1-sorted_lo);
                        posn[sorted_lo] = (posn[sorted_lo]&0xffffff) | (step<<24);
                        sorted_lo += step+1;
                    }
                    quicksort3r(lo, hi, depth);
                    sorted_lo = hi + 1;
                }
                lo = hi;
            }
        }
        // Finish threading
        while (sorted_lo < lo-1)
        {
            int step = mini(255, lo-1-sorted_lo);
            posn[sorted_lo] = (posn[sorted_lo]&0xffffff) | (step<<24);
            sorted_lo += step+1;
        }
        // Double depth
        depth += depth;
    }
    // Step 4: Permute data
    int i;
    markerpos = -1;
    for (i=0; i<size; i++)
        rank[i] = data[i];
    for (i=0; i<size; i++)
    {
        int j = posn[i] & 0xffffff;
        if (j>0) 
        { 
            data[i] = rank[j-1];
        } 
        else 
        {
            data[i] = 0;
            markerpos = i;
        }
    }
    assert(markerpos>=0 && markerpos<size);
}


// ========================================
// -- Encoding

static int decode_raw(ZPDecoder &zp, int bits)
{
    int n = 1;
    int m = (1<<bits);
    while (n < m)
    {
        int b = zp.decode_without_context();
        n = (n<<1) | b;
    }
    return n - m;
}

static int decode_binary(ZPDecoder &zp, ZPBitContext *ctx, int bits)
{
    int n = 1;
    int m = (1<<bits);
    ctx = ctx - 1;
    while (n < m)
    {
        int b = zp.decode(ctx[n]);
        n = (n<<1) | b;
    }
    return n - m;
}

unsigned int BSDecoder::decode()
{ 
    /////////////////////////////////
    //////////// Decode input stream

    // Header
    ZPDecoder &zp = gzp;
    size = decode_raw(gzp, 24);
    if (!size)
      return 0;
    if (size>MAXBLOCK*1024)
//      G_THROW( ERR_MSG("ByteStream.corrupt") );
        return 0;

    // Allocate
    if ((int)blocksize < size)
      {
        blocksize = size;
        if (data)
        {
          free(data);
        }
        bptr = 0;
        data = (unsigned char *) calloc(blocksize, sizeof(unsigned char));
      }


    // Decode Estimation Speed
    int fshift = 0;
    if (zp.decode_without_context())
      {
        fshift += 1;
        if (zp.decode_without_context())
          fshift += 1;
      }
    // Prepare Quasi MTF
    static const unsigned char xmtf[256]={
      0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
      0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
      0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
      0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
      0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
      0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
      0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
      0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
      0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,
      0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
      0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,
      0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
      0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,
      0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
      0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,
      0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
      0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,
      0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
      0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
      0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
      0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,
      0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
      0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,
      0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
      0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,
      0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
      0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
      0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
      0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,
      0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
      0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
      0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF};
    unsigned char mtf[256];
    memcpy(mtf,xmtf,sizeof(xmtf));
    unsigned int freq[FREQMAX];
    memset(freq,0,sizeof(freq));
    int fadd = 4;
    // Decode
    int i;
    int mtfno = 3;
    int markerpos = -1;
    for (i=0; i<size; i++)
      {
        int ctxid = CTXIDS-1;
        if (ctxid>mtfno) ctxid=mtfno;
        ZPBitContext *cx = ctx;
        if (zp.decode(cx[ctxid]))
          { mtfno=0; data[i]=mtf[mtfno]; goto rotate; }
        cx+=CTXIDS;
        if (zp.decode(cx[ctxid]))
          { mtfno=1; data[i]=mtf[mtfno]; goto rotate; }
        cx+=CTXIDS;
        if (zp.decode(cx[0]))
          { mtfno=2+decode_binary(zp,cx+1,1); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+1;
        if (zp.decode(cx[0]))
          { mtfno=4+decode_binary(zp,cx+1,2); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+3;
        if (zp.decode(cx[0]))
          { mtfno=8+decode_binary(zp,cx+1,3); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+7;
        if (zp.decode(cx[0]))
          { mtfno=16+decode_binary(zp,cx+1,4); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+15;
        if (zp.decode(cx[0]))
          { mtfno=32+decode_binary(zp,cx+1,5); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+31;
        if (zp.decode(cx[0]))
          { mtfno=64+decode_binary(zp,cx+1,6); data[i]=mtf[mtfno]; goto rotate; }
        cx+=1+63;
        if (zp.decode(cx[0]))
          { mtfno=128+decode_binary(zp,cx+1,7); data[i]=mtf[mtfno]; goto rotate; }
        mtfno=256;
        data[i]=0;
        markerpos=i;
        continue;
        // Rotate mtf according to empirical frequencies (new!)
    rotate:
        // Adjust frequencies for overflow
        int k;
        fadd = fadd + (fadd>>fshift);
        if (fadd > 0x10000000)
          {
            fadd    >>= 24;
            freq[0] >>= 24;
            freq[1] >>= 24;
            freq[2] >>= 24;
            freq[3] >>= 24;
            for (k=4; k<FREQMAX; k++)
              freq[k] = freq[k]>>24;
          }
        // Relocate new char according to new freq
        unsigned int fc = fadd;
        if (mtfno < FREQMAX)
          fc += freq[mtfno];
        for (k=mtfno; k>=FREQMAX; k--)
          mtf[k] = mtf[k-1];
        for (; k>0 && fc>=freq[k-1]; k--)
          {
            mtf[k] = mtf[k-1];
            freq[k] = freq[k-1];
          }
        mtf[k] = data[i];
        freq[k] = fc;
      }


    /////////////////////////////////
    ////////// Reconstruct the string

    if (markerpos<1 || markerpos>=size) return 0;
//      G_THROW( ERR_MSG("ByteStream.corrupt") );
    // Allocate pointers
    unsigned int *posn = (unsigned int*) ::operator new(sizeof(unsigned int) * blocksize);
    memset(posn, 0, sizeof(unsigned int)*size);
    // Prepare count buffer
    int count[256];
    for (i=0; i<256; i++)
      count[i] = 0;
    // Fill count buffer
    for (i=0; i<markerpos; i++)
      {
        unsigned char c = data[i];
        posn[i] = (c<<24) | (count[c] & 0xffffff);
        count[c] += 1;
      }
    for (i=markerpos+1; i<size; i++)
      {
        unsigned char c = data[i];
        posn[i] = (c<<24) | (count[c] & 0xffffff);
        count[c] += 1;
      }
    // Compute sorted char positions
    int last = 1;
    for (i=0; i<256; i++)
      {
        int tmp = count[i];
        count[i] = last;
        last += tmp;
      }
    // Undo the sort transform
    i = 0;
    last = size-1;
    while (last>0)
      {
        unsigned int n = posn[i];
        unsigned char c = (posn[i]>>24);
        data[--last] = c;
        i = count[c] + (n & 0xffffff);
      }
    // Free and check
    ::operator delete(posn);
    if (i != markerpos)
        return 0;
//      G_THROW( ERR_MSG("ByteStream.corrupt") );
    return size;
}

// ========================================
// --- Construction

BSDecoder::BSDecoder(FILE *f, int len)
        : offset(0), bptr(0), blocksize(0), size(0), data(NULL), eof(false), gzp(f, len)
{
    // Initialize context array
    memset(ctx, 0, sizeof(ctx));
}

void BSDecoder::close()
{
    // Free allocated memory
    free(data);
    data = NULL;
}

BSDecoder::~BSDecoder()
{
    if (data) close();
}

// ========================================
// -- ByteStream interface

long BSDecoder::tell() const
{
  return offset;
}

size_t BSDecoder::read(void *buffer, size_t sz)
{
    if (eof)
      return 0;
    // Loop
    int copied = 0;
    while (sz > 0 && !eof)
    {
        // Decode if needed
        if (!size)
          {
            bptr = 0;
            if (! decode())
            {
              size = 1 ;
              eof = true;
            }
            size -= 1;
          }
        // Compute remaining
        int bytes = size;
        if (bytes > (int)sz)
          bytes = sz;
        // Transfer
        if (buffer && bytes)
          {
            memcpy(buffer, data+bptr, bytes);
            buffer = (void*)((char*)buffer + bytes);
          }
        size -= bytes;
        bptr += bytes;
        sz -= bytes;
        copied += bytes;
        offset += bytes;
      }
    // Return copied bytes
    return copied;
}

