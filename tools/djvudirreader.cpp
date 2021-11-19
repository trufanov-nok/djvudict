#include "djvudirreader.h"
#include <cstdlib>
#include <memory>
#include <assert.h>
#include "bsdecoder.h"

static uint32 read_uint32_most_significant_byte_first(FILE *f)
{
    return fgetc(f) << 24 | fgetc(f) << 16 | fgetc(f) << 8 | fgetc(f);
}

static uint16 read_uint16_most_significant_byte_first(FILE *f)
{
    return fgetc(f) << 8 | fgetc(f);
}

static uint32 read_uint24_most_significant_byte_first_buf(const char* b)
{
    return b[0] << 16 | b[1] << 8 | b[2];
}

inline void skip_till_next_str(const char* buf, int& pos)
{
    while (buf[pos]) pos++;
    pos++;
}

DjVuDirReader::DjVuDirReader(): m_entries_cnt(0), m_entries(NULL), m_buf(NULL) {}

int DjVuDirReader::decode(FILE * f, int32 size, mdjvu_error_t *perr, Options *opts)
{
    assert(size >= 3);

    close(); // free and null buffers

    unsigned char flags = (unsigned char) fgetc(f);
    if (!(flags & 0x80 /*0b10000000*/)) {
        fprintf(stderr, "DjVu is inderectly coded. We support only bundled multi-page DjVu documents.\n");
        if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_djvu);
        return 0;
    }
    if (opts->verbose) {
        fprintf(stdout, "Bundled DjVu found. DIRM reports format version %u\n", flags & 0x7F /*0b01111111*/);
    }

    m_entries_cnt = read_uint16_most_significant_byte_first(f);

    if (opts->verbose) {
        fprintf(stdout, "Files bundled %u\n", m_entries_cnt);
    }

    m_entries = (DIRM_Entry*) malloc (m_entries_cnt* sizeof(DIRM_Entry));
    for (uint16 i = 0; i < m_entries_cnt; i++) { //read file offsets
        m_entries[i].offset = read_uint32_most_significant_byte_first(f);
    }


//    skip_in_chunk(&DIRM, 1+2+4*m_entries_cnt);

    if (!m_entries_cnt) {
        return size;
    }

    int32 size_left = size - (1 + 2 + 4*m_entries_cnt);
    assert(size_left >= 0);

    BSDecoder decoder(f, size_left);
    int32 data_len = decoder.decode();
    m_buf = (char*) malloc (data_len);
    int readed = decoder.read(m_buf, data_len);
    decoder.close();
//    skip_in_chunk(&DIRM, size_left);
    assert(readed == data_len);

    int32 buf_pos = 0;
    for (int32 i = 0; i < m_entries_cnt; i++) { //read file sizes
        m_entries[i].size = read_uint24_most_significant_byte_first_buf(m_buf+buf_pos);
        buf_pos += 3;
    }
    for (int32 i = 0; i < m_entries_cnt; i++, buf_pos++) { //read file str_flags
        m_entries[i].str_flags = m_buf + buf_pos;
    }

    for (int32 i = 0; i < m_entries_cnt; i++) { //read strings
        m_entries[i].id_str = m_buf + buf_pos;
        skip_till_next_str(m_buf, buf_pos);

        unsigned char flag = *m_entries[i].str_flags;
        if (flag & 0x80 /*0b10000000*/) {
            m_entries[i].name_str = m_buf + buf_pos;
            skip_till_next_str(m_buf, buf_pos);
        } else {
            m_entries[i].name_str = NULL;
        }

        if (flag & 0x40 /*0b01000000*/) {
            m_entries[i].title_str = m_buf + buf_pos;
            skip_till_next_str(m_buf, buf_pos);
        } else {
            m_entries[i].name_str = NULL;
        }

        m_entries[i].type = (DIRM_EntryType) (flag & 0x3F /*0b00111111*/);
    }

    return 1 + 2 + 4*m_entries_cnt + size_left;

}
void DjVuDirReader::close()
{
    if (m_entries) {
        free(m_entries);
        m_entries = NULL;
    }

    m_entries_cnt = 0;

    if (m_buf) {
        free(m_buf);
        m_buf = NULL;
    }
}

DjVuDirReader::~DjVuDirReader()
{
    close();
}
