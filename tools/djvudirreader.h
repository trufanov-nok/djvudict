#ifndef DJVUDIRREADER_H
#define DJVUDIRREADER_H
#include "../include/minidjvu-mod/minidjvu-mod.h"
#include "djvudict_options.h"
#include <stdio.h>

enum DIRM_EntryType {
    SharedFile,
    Page,
    Thumbnails
};

struct DIRM_Entry
{
    int32 offset;
    int32 size;
    const char* str_flags;
    DIRM_EntryType type;
    const char* id_str;
    const char* name_str;
    const char* title_str;
};

class DjVuDirReader
{
public:
    DjVuDirReader();
    ~DjVuDirReader();

    int decode(FILE * f, int32 size, mdjvu_error_t *perr, struct Options* opts);
    void close();

    inline DIRM_Entry* entries() const { return m_entries; }
    inline int32 count() const { return m_entries_cnt; }
private:
    FILE * m_f;
    int32 m_entries_cnt;
    DIRM_Entry* m_entries;
    char* m_buf;
};

#endif // DJVUDIRREADER_H
