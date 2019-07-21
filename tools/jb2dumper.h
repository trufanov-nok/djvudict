#ifndef JB2DUMPER_H
#define JB2DUMPER_H

#include "djvudirreader.h"
#include <string>

#define CHUNK_ID_AT_AND_T 0x41542654
#define CHUNK_ID_FORM     0x464F524D
#define ID_DJVU           0x444A5655
#define ID_DJVM           0x444A564D
#define ID_DIRM           0x4449524D

typedef struct IFFChunk
{
    uint32 id;
    uint32 length;
    uint32 skipped;
    struct IFFChunk *parent;
} IFFChunk;

struct SharedDictInfo
{
    mdjvu_bitmap_t * bitmaps;
    int32 count;
    const char* id; // not own
};

class Counters
{
public:
    enum CountersType
    {
        //copy of JB2RecordType from jb2const.h
        jb2_start_of_image,
        jb2_new_symbol_add_to_image_and_library,
        jb2_new_symbol_add_to_library_only,
        jb2_new_symbol_add_to_image_only,
        jb2_matched_symbol_with_refinement_add_to_image_and_library,
        jb2_matched_symbol_with_refinement_add_to_library_only,
        jb2_matched_symbol_with_refinement_add_to_image_only,
        jb2_matched_symbol_copy_to_image_without_refinement,
        jb2_non_symbol_data,
        jb2_require_dictionary_or_reset,
        jb2_comment,
        jb2_end_of_data,
        // end of JB2RecordType values
        SharedDictUsage,
        LocalDictUsage,
        ElementsOnPage,
        UniqElementsOnPage,
        BitmapsAddedToLocalDict,
        LastCounter
    };

    Counters(){}
    ~Counters(){}

    void count(CountersType, int size = 0, int val = 1);
    std::string getValue(CountersType cntr, bool total = false);
    void resetPageCounters();
    void clear();

private:
    int m_counters[LastCounter];
    int m_total_counters[LastCounter];
    int m_sizes[LastCounter];
    int m_total_sizes[LastCounter];
};

class JB2Dumper
{
public:
    JB2Dumper();
    ~JB2Dumper();
    void close();
    int dumpMultiPage(FILE *f, const DIRM_Entry* entries, int size, const char* out_path, mdjvu_error_t *perr);
private:
    int dumpDjbz(FILE *f, IFFChunk *form, const char* out_path, SharedDictInfo *local_dict, mdjvu_error_t* p_err);
    int dumpSjbz(FILE *f, IFFChunk *form, const char* out_path, mdjvu_error_t* p_err);
    mdjvu_image_t loadAndDumpJB2Image(FILE * f, int32 length, const SharedDictInfo* shared_library, SharedDictInfo* local_dict, const char* out_path, mdjvu_error_t *perr);

    Counters m_counters;

    SharedDictInfo* m_shared_dicts;
    int m_shared_dict_cnt;
    int m_dict_buf_allocated;
    const char* m_cur_output_folder;
    int m_cur_dpi;
    bool m_verbose;
};

class LogFile
{
public:
    LogFile(Counters* counters = NULL, bool totals = false): m_counters(counters), m_stats_f(NULL), m_totals(totals) { }
    ~LogFile() { close(); }
    void open(const char* fname);
    void log(const char* val);
    void log(const char* fmt, int32 val);
    void logAction(int32 action, int32 idx, bool in_shared_lib = false);
    void logAction(int32 action);
    void close();
private:
    Counters* m_counters;
    FILE* m_stats_f;
    bool m_totals;
};


/* ========================================================================= */

static uint32 read_uint32_most_significant_byte_first(FILE *f)
{
    uint32 r = fgetc(f) << 24;
    r |= fgetc(f) << 16;
    r |= fgetc(f) << 8;
    r |= fgetc(f);
    return r;
}

static uint16 read_uint16_most_significant_byte_first(FILE *f)
{
    return fgetc(f) << 8 | fgetc(f);
}


static void skip_in_chunk(IFFChunk *chunk, unsigned len)
{   // tells every parent chunk that we skip len bytes
    while (chunk)
    {
        chunk->skipped += len;
        chunk = chunk->parent;
    }
}

static void skip_to_next_sibling_chunk(FILE *file, IFFChunk *chunk)
{
    skip_in_chunk(chunk->parent, chunk->length + 8);
    fseek(file, (chunk->length + 1) & ~1, SEEK_CUR);
    chunk->id = read_uint32_most_significant_byte_first(file);
    chunk->length = read_uint32_most_significant_byte_first(file);
    chunk->skipped = 0;
}

static void get_child_chunk(FILE *file, IFFChunk *chunk, IFFChunk *parent)
{
    chunk->id = read_uint32_most_significant_byte_first(file);
    chunk->length = read_uint32_most_significant_byte_first(file);
    chunk->skipped = 0;
    chunk->parent = parent;
    skip_in_chunk(parent, 8);
}

static int find_sibling_chunk(FILE *file, IFFChunk *chunk, uint32 id)
{
    while (chunk->id != id)
    {
        if (chunk->parent && chunk->parent->skipped >= chunk->parent->length)
            return 0;
        skip_to_next_sibling_chunk(file, chunk);
    }
    return 1;
}



#endif // JB2DUMPER_H
