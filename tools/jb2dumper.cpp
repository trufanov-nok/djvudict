#include "jb2dumper.h"

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include <assert.h>
#include <string.h>

#include "../src/jb2/zp.h"
#include "../src/jb2/jb2coder.h"

#if (defined(windows) || defined(WIN32))
#include <windows.h>
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
const char _dir_sep = '\\';
#else
#include <sys/stat.h>
const char _dir_sep = '/';
#endif

#ifdef HAVE_LIBSQLITE3
static int _save_to_sql = 0;
#endif

static char dir_sep_used(const std::string& s)
{
    const bool slash = s.find_first_of('/',0) !=std::string::npos;
    const bool backslash = s.find_first_of('\\',0) !=std::string::npos;
    if (slash != backslash) return slash? '/' : '\\';
    return _dir_sep;
}

static int mkpath(std::string path, int mode = 0755)
{
    char used_sep = dir_sep_used(path);

    if (path.empty()) {
        return 0;
    }


    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }

    int pos = 0, res = 0;
    while( (pos = path.find_first_of(used_sep, pos) ) != std::string::npos ) {
        std::string dir = path.substr(0, pos++);
        if (dir.size()) {
            res = mkdir(dir.c_str(), mode);
            if (res == -1 && errno != EEXIST) {
                fprintf(stderr, "mkdir failed for %s (error code %u, errno: %d - %s)", path.data(), res, errno, strerror(errno));
                return res;
            }
        }
    }
    return 0;
}

static std::string get_subdir(std::string path, const std::string dir, int id)
{
    char used_sep = dir_sep_used(path);
    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }
    return path + std::to_string(id) + '_' + dir + used_sep;
}

static std::string get_filename(std::string path, std::string name)
{
    char used_sep = dir_sep_used(path);
    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }
    return path + name + ".bmp";
}

static std::string get_filename(std::string path, std::string prefix, int id, int padding = 5)
{
    char used_sep = dir_sep_used(path);
    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }
    std::string num = std::to_string(id);
    while (num.length() < padding) num = '0' + num;

    if (!prefix.empty()) prefix += '_';
    return path + prefix + num + ".bmp";
}

static std::string get_statsname(std::string path, std::string filename)
{
    char used_sep = dir_sep_used(path);
    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }
    return path + filename;
}

static std::string get_sqlname(std::string path)
{
    char used_sep = dir_sep_used(path);
    if ( path[path.length()-1] != used_sep) {
        path += used_sep;
    }
    return path + "djvu_sqlite.db";
}

JB2Dumper::JB2Dumper(): m_shared_dicts(NULL), m_shared_dict_cnt(0), m_dict_buf_allocated(0), m_cur_dpi(600), m_cur_entry_no(0)
{
}

JB2Dumper::~JB2Dumper()
{
    close();
}

void JB2Dumper::close()
{
    if (m_shared_dict_cnt) {
        for (int32 i = 0; i < m_shared_dict_cnt; i++) {
            SharedDictInfo& dict = m_shared_dicts[i];
            if (dict.bitmaps) {
                for (int32 j = 0; j < dict.count; j++) {
                    mdjvu_bitmap_destroy(dict.bitmaps[j]);
                }
                free(dict.bitmaps);
            }
        }
        free(m_shared_dicts);
        m_dict_buf_allocated = 0;
        m_shared_dict_cnt = 0;
    }
}

static mdjvu_bitmap_t * clone_library(mdjvu_bitmap_t * library, int32 size)
{
    if (!library || size == 0) {
        return NULL;
    }
    mdjvu_bitmap_t * res = (mdjvu_bitmap_t *) malloc(size * sizeof(mdjvu_bitmap_t));
    for (int32 i = 0; i < size; i++) {
        res[i] = mdjvu_bitmap_clone(library[i]);
    }
    return res;
}

////////////////////////////////////////
//  Some code copied from jb2load.cpp

// A piece of my old code... should it be eliminated?
template<class T> inline T *
append_to_list(T *&list, int32 &count, int32 &allocated)
{
    if (!allocated) {
        allocated = 1;
        list = (T *) malloc(allocated * sizeof(T));
    } else if (allocated == count) {
        allocated <<= 1;
        list = (T *) realloc(list, allocated * sizeof(T));
    }
    return &list[count++];
}

static mdjvu_bitmap_t decode_lib_shape/*{{{*/
(JB2Decoder &jb2, mdjvu_image_t img, bool with_blit, mdjvu_bitmap_t proto, int32 *img_x = 0, int32 *img_y = 0)
{
    int32 blit = -1; // to please compilers
    int32 index = mdjvu_image_get_bitmap_count(img);

    mdjvu_bitmap_t shape = jb2.decode(img, proto);
    if (with_blit)
    {
        blit = jb2.decode_blit(img, index);
    }

    int32 x, y;
    mdjvu_bitmap_remove_margins(shape, &x, &y);

    if (with_blit)
    {
        x = mdjvu_image_get_blit_x(img, blit) + x;
        y = mdjvu_image_get_blit_y(img, blit) + y;
        mdjvu_image_set_blit_x(img, blit, x);
        mdjvu_image_set_blit_y(img, blit, y);
        if (img_x) *img_x = x;
        if (img_y) *img_y = y;
    }

    return shape;
}/*}}}*/


#define COMPLAIN \
{ \
    if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_jb2); \
    return NULL; \
    }
//
////////////////////////////////////////

// function below is a modified mdjvu_file_load_jb2() from  jb2load.cpp

mdjvu_image_t JB2Dumper::loadAndDumpJB2Image(FILE * f, int32 length, const SharedDictInfo* shared_library, SharedDictInfo* local_dict, const char* out_path, mdjvu_error_t *perr)
{
    if (perr) *perr = NULL;

    m_counters.resetPageCounters();

    LogFile log(&m_counters);
    log.open(get_statsname(out_path, "stats.log").data());
    LogFile actions;
    actions.open(get_statsname(out_path, "actions.log").data());

    JB2Decoder jb2(f, length);
    ZPDecoder &zp = jb2.zp;

    int32 t = jb2.decode_record_type();
    m_counters.count((Counters::CountersType)t);
    actions.logAction(t);

    int32 lib_count = 0, lib_alloc = 128;
    mdjvu_bitmap_t * library = NULL;

    int32 shared_lib_size_used = 0;

    if (t == jb2_require_dictionary_or_reset)
    {
        lib_alloc = lib_count = zp.decode(jb2.required_dictionary_size);
        shared_lib_size_used = lib_count;
        log.log("Using shared dictionary with size:\t%u\n", lib_count);
        if (! shared_library || !shared_library->count) {
            fprintf(stderr, "JB2 Image requires %u images from shared library which wasn't provided\n", lib_count);
            if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_jb2);
            //COMPLAIN;
        }

        if (shared_library) {
            if (shared_library->count < lib_count) {
                fprintf(stderr, "JB2 Image requires %u images but shared library has only", shared_library->count);
                COMPLAIN;
            }
            library = clone_library(shared_library->bitmaps, lib_count);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                m_sql.use_djbz(shared_library->id);
            }
#endif
        }
        t = jb2.decode_record_type(); // read jb2_start_of_image
        m_counters.count((Counters::CountersType)t);
        actions.logAction(t);
    } else {
        log.log("Using local dictionary\n", lib_count);
        library = (mdjvu_bitmap_t *) malloc(lib_alloc * sizeof(mdjvu_bitmap_t));
    }

    if (t != jb2_start_of_image) COMPLAIN;

    const int32 page_w = zp.decode(jb2.image_size);
    const int32 page_h = zp.decode(jb2.image_size);
    zp.decode(jb2.eventual_image_refinement); // dropped
    jb2.symbol_column_number.set_interval(1, !page_w?1:page_w);
    jb2.symbol_row_number.set_interval(1, !page_h?1:page_h);

    mdjvu_image_t img = mdjvu_image_create(page_w, page_h); /* d is dropped for now - XXX*/

    while(1)
    {
        t = jb2.decode_record_type();
        long int size = 0;
        switch(t)
        {
        case jb2_new_symbol_add_to_image_and_library: {
            int32 img_x; int32 img_y;
            size = ftell(zp.file);
            *(append_to_list<mdjvu_bitmap_t>(library, lib_count, lib_alloc))
                    = decode_lib_shape(jb2, img, true, NULL, &img_x, &img_y);
            const std::string filename = get_filename(out_path, "lib", lib_count-1);
            mdjvu_save_bmp(library[lib_count-1], filename.data(), m_cur_dpi, perr);
            if (page_h) {
                img_y = page_h - img_y; // return (0,0) to left bottom corner
                assert(img_y >= 0);
            }
            actions.logAction(t, lib_count-1, false, img_x, img_y);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::BitmapsAddedToLocalDict, size);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                const mdjvu_bitmap_t l_img = library[lib_count-1];
                const int img_w = mdjvu_bitmap_get_width(l_img);
                const int img_h = mdjvu_bitmap_get_height(l_img);
                m_sql.add_letter(lib_count-1, img_x, img_y,  img_w,  img_h,
                                 1 /*to_image*/, 1 /*to_library*/, 0 /*is_symbol*/,
                                 -1 /*ref_local_id*/, 0 /*from_djbz*/,
                                 0 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_new_symbol_add_to_library_only: {
            size = ftell(zp.file);
            *(append_to_list<mdjvu_bitmap_t>(library, lib_count, lib_alloc))
                    = decode_lib_shape(jb2, img, false, NULL);

            const std::string filename = get_filename(out_path, "lib", lib_count-1);
            mdjvu_save_bmp(library[lib_count-1], filename.data(), m_cur_dpi, perr);
            actions.logAction(t, lib_count-1, false);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::BitmapsAddedToLocalDict, size);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                const int img_w = mdjvu_bitmap_get_width(library[lib_count-1]);
                const int img_h = mdjvu_bitmap_get_height(library[lib_count-1]);
                m_sql.add_letter(lib_count-1, 0, 0,  img_w,  img_h,
                                 0 /*to_image*/, 1 /*to_library*/, 0 /*is_symbol*/,
                                 -1 /*ref_local_id*/, 0 /*from_djbz*/,
                                 0 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_new_symbol_add_to_image_only: {
            size = ftell(zp.file);
            jb2.decode(img);
            int32 index = mdjvu_image_get_bitmap_count(img);
            jb2.decode_blit(img, index-1);

            const std::string filename = get_filename(out_path, "img", index);
            const mdjvu_bitmap_t bitmap = mdjvu_image_get_bitmap(img, index);
            mdjvu_save_bmp(bitmap, filename.data(), m_cur_dpi, perr);

            int32 last_blit = mdjvu_image_get_blit_count(img) - 1;
            const int x = mdjvu_image_get_blit_x(img, last_blit);
            int y = mdjvu_image_get_blit_y(img, last_blit);
            if (page_h) {
                y = page_h - y; // return (0,0) to left bottom corner
                assert(y >= 0);
            }

            actions.logAction(t, index, false, x, y);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::UniqElementsOnPage, size);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                const int img_w = mdjvu_bitmap_get_width(bitmap);
                const int img_h = mdjvu_bitmap_get_height(bitmap);
                m_sql.add_letter(-1, x, y,  img_w,  img_h,
                                 1 /*to_image*/, 0 /*to_library*/, 0 /*is_symbol*/,
                                 -1 /*ref_local_id*/, 0 /*from_djbz*/,
                                 0 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_matched_symbol_with_refinement_add_to_image_and_library: {
            size = ftell(zp.file);
            if (!lib_count)
            {
                mdjvu_image_destroy(img);
                free(library);
                COMPLAIN;
            }
            jb2.matching_symbol_index.set_interval(0, lib_count - 1);
            int32 match = zp.decode(jb2.matching_symbol_index);
            int32 img_x; int32 img_y;
            *(append_to_list<mdjvu_bitmap_t>(library, lib_count, lib_alloc))
                    = decode_lib_shape(jb2, img, true, library[match], &img_x, &img_y);
            if (page_h) {
                img_y = page_h - img_y; // return (0,0) to left bottom corner
                assert(img_y >= 0);
            }

            const std::string filename = get_filename(out_path, "lib", lib_count-1);
            mdjvu_save_bmp(library[lib_count-1], filename.data(), m_cur_dpi, perr);
            actions.logAction(t, lib_count-1, false, img_x, img_y);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::BitmapsAddedToLocalDict, size);

#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                const int img_w = mdjvu_bitmap_get_width(library[lib_count-1]);
                const int img_h = mdjvu_bitmap_get_height(library[lib_count-1]);
                m_sql.add_letter(lib_count-1, img_x, img_y,  img_w,  img_h,
                                 1 /*to_image*/, 1 /*to_library*/, 0 /*is_symbol*/,
                                 match /*ref_local_id*/, match < shared_lib_size_used /*from_djbz*/,
                                 1 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_matched_symbol_with_refinement_add_to_library_only: {
            size = ftell(zp.file);
            if (!lib_count)
            {
                mdjvu_image_destroy(img);
                free(library);
                COMPLAIN;
            }
            jb2.matching_symbol_index.set_interval(0, lib_count - 1);
            int32 match = zp.decode(jb2.matching_symbol_index);
            *(append_to_list<mdjvu_bitmap_t>(library, lib_count, lib_alloc))
                    = decode_lib_shape(jb2, img, false, library[match]);

            const std::string filename = get_filename(out_path, "lib", lib_count-1);
            mdjvu_save_bmp(library[lib_count-1], filename.data(), m_cur_dpi, perr);
            actions.logAction(t, lib_count-1, false);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::BitmapsAddedToLocalDict, size);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                int32 last_blit = mdjvu_image_get_blit_count(img) - 1;
                const int x = mdjvu_image_get_blit_x(img, last_blit);
                int y = mdjvu_image_get_blit_y(img, last_blit);
                if (page_h) {
                    y = page_h - y; // return (0,0) to left bottom corner
                    assert(y >= 0);
                }

                const int img_w = mdjvu_bitmap_get_width(library[lib_count-1]);
                const int img_h = mdjvu_bitmap_get_height(library[lib_count-1]);
                m_sql.add_letter(lib_count-1, x, y,  img_w,  img_h,
                                 0 /*to_image*/, 1 /*to_library*/, 0 /*is_symbol*/,
                                 match /*ref_local_id*/, match < shared_lib_size_used /*from_djbz*/,
                                 1 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_matched_symbol_with_refinement_add_to_image_only: {
            size = ftell(zp.file);
            if (!lib_count)
            {
                mdjvu_image_destroy(img);
                free(library);
                COMPLAIN;
            }
            jb2.matching_symbol_index.set_interval(0, lib_count - 1);
            int32 match = zp.decode(jb2.matching_symbol_index);
            jb2.decode(img, library[match]);
            int32 index = mdjvu_image_get_bitmap_count(img);
            jb2.decode_blit(img, index-1);

            const std::string filename = get_filename(out_path, "img", index);
            const mdjvu_bitmap_t bitmap = mdjvu_image_get_bitmap(img, index);
            mdjvu_save_bmp(bitmap, filename.data(), m_cur_dpi, perr);
            int32 last_blit = mdjvu_image_get_blit_count(img) - 1;
            const int32 x = mdjvu_image_get_blit_x(img, last_blit);
            int32 y = mdjvu_image_get_blit_y(img, last_blit);
            if (page_h) {
                y = page_h - y; // return (0,0) to left bottom corner
                assert(y >= 0);
            }

            actions.logAction(t, index, index < shared_lib_size_used, x, y);
            size = ftell(zp.file) - size;
            if (index < shared_lib_size_used) {
                m_counters.count(Counters::SharedDictUsage, size);
            } else {
                m_counters.count(Counters::LocalDictUsage), size;
            }
            m_counters.count(Counters::UniqElementsOnPage, size);

#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                m_sql.add_letter(-1, x, y,  mdjvu_bitmap_get_width(bitmap),  mdjvu_bitmap_get_height(bitmap),
                                 1 /*to_image*/, 0 /*to_library*/, 0 /*is_symbol*/,
                                 match /*ref_local_id*/, match < shared_lib_size_used /*from_djbz*/,
                                 1 /*is_refinement*/, filename.data());
            }
#endif

        } break;
        case jb2_matched_symbol_copy_to_image_without_refinement: {
            size = ftell(zp.file);
            if (!lib_count)
            {
                mdjvu_image_destroy(img);
                free(library);
                COMPLAIN;
            }
            jb2.matching_symbol_index.set_interval(0, lib_count - 1);
            int32 match = zp.decode(jb2.matching_symbol_index);

            ////////////////////////////////////
            // There was just a single line:
            // jb2.decode_blit(img, match);
            // but we had to pass a shape from library there
            // as there is no such match in img itself
            // and it's decode_character_position was private
            //
            mdjvu_bitmap_t shape = library[match];
            int32 ws = mdjvu_bitmap_get_width(shape);
            int32 hs = mdjvu_bitmap_get_height(shape);
            int32 x, y;
            jb2.decode_character_position(x, y, ws, hs);
            if (page_h) {
                y = page_h - y; // return (0,0) to left bottom corner
                assert(y >= 0);
            }
            mdjvu_image_add_blit(img, x, y, shape);

            const std::string filename = get_filename(out_path, "lib", match);
            mdjvu_save_bmp(shape, filename.data(), m_cur_dpi, perr);
            actions.logAction(t, match, match < shared_lib_size_used, x, y);
            size = ftell(zp.file) - size;
            if (match < shared_lib_size_used) {
                m_counters.count(Counters::SharedDictUsage, size);
            } else {
                m_counters.count(Counters::LocalDictUsage, size);
            }

#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                m_sql.add_letter(-1, x, y,  ws,  hs,
                                 1 /*to_image*/, 0 /*to_library*/, 0 /*is_symbol*/,
                                 match /*ref_local_id*/, match < shared_lib_size_used /*from_djbz*/,
                                 1 /*is_refinement*/, filename.data());
            }
#endif
        } break;
        case jb2_non_symbol_data: {
            size = ftell(zp.file);
            mdjvu_bitmap_t bmp = jb2.decode(img);
            int32 x = zp.decode(jb2.symbol_column_number) - 1;
            int32 y = zp.decode(jb2.symbol_row_number);
            if (page_h) {
                y = page_h - y; // return (0,0) to left bottom corner
                assert(y >= 0);
            }
            int32 index = mdjvu_image_get_bitmap_count(img);
            mdjvu_image_add_blit(img, x, y, bmp);
            const std::string filename = get_filename(out_path, "non_symb", index);
            mdjvu_save_bmp(bmp, filename.data(), m_cur_dpi, perr);
            actions.logAction(t, index, false, x, y);
            size = ftell(zp.file) - size;
            m_counters.count(Counters::UniqElementsOnPage, size);
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                const int img_w = mdjvu_bitmap_get_width(bmp);
                const int img_h = mdjvu_bitmap_get_height(bmp);
                m_sql.add_letter(-1, x, y,  img_w,  img_h,
                                 1 /*to_image*/, 0 /*to_library*/, 1 /*is_symbol*/,
                                 -1 /*ref_local_id*/, 0 /*from_djbz*/,
                                 0 /*is_refinement*/, filename.data());
            }
#endif
        } break;

        case jb2_require_dictionary_or_reset: {
            jb2.reset();
            actions.logAction(t);
        } break;

        case jb2_comment: {
            actions.logAction(t);
            int32 len = zp.decode(jb2.comment_length);
            while (len--) zp.decode(jb2.comment_octet);
        } break;

        case jb2_end_of_data: {
            if (local_dict) {
                local_dict->bitmaps = clone_library(library, lib_count);
                local_dict->count = lib_count;
            }

            m_counters.count(Counters::ElementsOnPage, mdjvu_image_get_blit_count(img));
            actions.logAction(t);
            free(library);
            return img;
        }
        default:
            free(library);
            mdjvu_image_destroy(img);
            COMPLAIN;
        } // switch

        m_counters.count((Counters::CountersType)t, size);
    } // while(1)
}/*}}}*/


#define ID_DJVI           0x444A5649
#define CHUNK_ID_Sjbz     0x536A627A
#define CHUNK_ID_Djbz     0x446A627A
#define CHUNK_ID_INCL     0x494E434C
#define CHUNK_ID_INFO     0x494E464F

static void skip_whole_chunk_aligned(FILE* f, IFFChunk *chunk, unsigned len) {
    skip_in_chunk(chunk, len);
    unsigned aligned_len = (len + 1) & ~1; // should be even (DjVu3Spec.pdf p.9)
    if (aligned_len > len) {
        fseek(f, 1, SEEK_CUR);
    }
}

int JB2Dumper::dumpDjbz(FILE *f, IFFChunk *form, const char* out_path, SharedDictInfo* local_dict, mdjvu_error_t* p_err)
{   // Form marked as DJVI
#ifdef HAVE_LIBSQLITE3
    if (_save_to_sql) {
        m_sql.start_new_djbz();
    }
#endif

    IFFChunk dict;
    get_child_chunk(f, &dict, form);
    if (find_sibling_chunk(f, &dict, CHUNK_ID_Djbz)) {
        if (mkpath(out_path) == 0) {
            mdjvu_image_t res = loadAndDumpJB2Image(f, dict.length, NULL, local_dict, out_path, p_err);
            if (res) {
                mdjvu_image_destroy(res);
                skip_whole_chunk_aligned(f, dict.parent, dict.length);
                return 1;
            }
        }
    }
#ifdef HAVE_LIBSQLITE3
    if (_save_to_sql) {
        m_sql.endof_djbz();
    }
#endif

    return 0;
}

int JB2Dumper::dumpSjbz(FILE *f, IFFChunk *form, const char* out_path, mdjvu_error_t* p_err, const struct Options* opts)
{   // Form marked as DJVU
    assert(form);

    SharedDictInfo* shared_dict_for_page = NULL;
    m_cur_dpi = 600;

    IFFChunk chunk;
    while (form->skipped < form->length) {
        get_child_chunk(f, &chunk, form);

        switch (chunk.id) {
        case CHUNK_ID_INFO: {
            unsigned char * info =  (unsigned char *) calloc(chunk.length, sizeof(char));
            const int32 readen = fread(info, 1, chunk.length, f);
            assert(readen == chunk.length);
            assert(chunk.length >= 10);
            m_cur_dpi = info[6] | info[7] << 8;

            if (opts->verbose) {
                fprintf(stdout, "Reading page Info: w:%u h:%u ver:%u dpi:%u\n",
                        info[1]|info[0]<<8, info[3]|info[2]<<8, (info[5]<<8)+info[4], m_cur_dpi);
            }
#ifdef HAVE_LIBSQLITE3
            if (_save_to_sql) {
                m_sql.start_new_sjbz(info[1]|info[0]<<8, info[3]|info[2]<<8, (info[5]<<8)+info[4], m_cur_dpi);
            }
#endif
            free(info);
        }
            break;
        case CHUNK_ID_INCL: {
            shared_dict_for_page = NULL;
            void * id = calloc(chunk.length, sizeof(unsigned char));
            int32 readed = fread(id, 1, chunk.length, f);
            assert(readed = chunk.length);
            for (int i = 0; i < m_shared_dict_cnt; i++) {
                if (memcmp(m_shared_dicts[i].id, id, chunk.length) == 0) {
                    shared_dict_for_page = &m_shared_dicts[i];
                    break;
                }
            }
            free(id);
        }
            break;
        case CHUNK_ID_Sjbz: {
            if (mkpath(out_path) == 0) {
                mdjvu_image_t res = loadAndDumpJB2Image(f, chunk.length, shared_dict_for_page, NULL, out_path, p_err);
                if (!res) { return 0; }
                mdjvu_bitmap_t bitmap = mdjvu_render(res);
                mdjvu_save_bmp(bitmap, get_filename(out_path, "page").data(), m_cur_dpi, p_err);
                mdjvu_bitmap_destroy(bitmap);
                mdjvu_image_destroy(res);
                skip_whole_chunk_aligned(f, form, chunk.length+8);
                return 1;
            }
            return 0;
        }
            break;
        default:
            fseek(f, chunk.length, SEEK_CUR);
            break;
        }
        skip_whole_chunk_aligned(f, form, chunk.length+8);
    }
    return 0;
}

int JB2Dumper::dumpMultiPage(FILE * f, const DIRM_Entry* entries, int size, const char* out_path, mdjvu_error_t *p_err, const Options *opts)
{
    if (mkpath(out_path)) {
        return 0;
    }

    LogFile totalLog(&m_counters, true);
    totalLog.open(get_statsname(out_path, "stats.log").data());
    m_counters.clear();

#ifdef HAVE_LIBSQLITE3
    _save_to_sql = opts->save_to_sql;
    if (_save_to_sql) {
        const std::string sql_path = get_sqlname(out_path);
        if ( !m_sql.init(sql_path.c_str()) ) {
            exit(3);
        };
    }
#endif


    m_cur_entry_no = 0;
    for (m_cur_entry_no = 0; m_cur_entry_no < size; m_cur_entry_no++)
    {
        const DIRM_Entry& entry = entries[m_cur_entry_no];

        if (entry.type == Thumbnails) {
            continue;
        }

        if (fseek(f, entry.offset, SEEK_SET)) {
            fprintf(stderr, "ERROR: can't fseek to %u", entry.offset);
            if (p_err) *p_err = mdjvu_get_error(mdjvu_error_corrupted_djvu);
            return 0;
        }

        IFFChunk FORM;
        get_child_chunk(f, &FORM, NULL); // no parent chunk as we already know all offsets and can fseek
        if (!find_sibling_chunk(f, &FORM, CHUNK_ID_FORM))
        {
            fprintf(stderr, "No FORM tag found.\n");
            if (p_err) *p_err = mdjvu_get_error(mdjvu_error_corrupted_djvu);
            return 0;
        }

        uint32 id = read_uint32_most_significant_byte_first(f);
        skip_in_chunk(&FORM, 4);

        const std::string dump_path = get_subdir(out_path, entry.id_str, m_cur_entry_no);
#ifdef HAVE_LIBSQLITE3
        if (_save_to_sql) {
            m_sql.start_new_form(m_cur_entry_no, entry.id_str, dump_path.data());
        }
#endif

        if (id == ID_DJVI) {
            SharedDictInfo res;

            if (dumpDjbz(f, &FORM, dump_path.data(), &res, p_err)) {
                SharedDictInfo* dict = append_to_list<SharedDictInfo>(m_shared_dicts, m_shared_dict_cnt, m_dict_buf_allocated);
                *dict = res;
                dict->id = entry.id_str;
            } else {
              //  return 0;
            }
        } else if (id == ID_DJVU) {
            dumpSjbz(f, &FORM, dump_path.data(), p_err, opts);
        }
#ifdef HAVE_LIBSQLITE3
        if (_save_to_sql) {
            m_sql.endof_form();
        }
#endif
    }

    totalLog.close();
#ifdef HAVE_LIBSQLITE3
        if (_save_to_sql) {
            m_sql.save_on_disk();
        }
#endif
    return 1;
}

void LogFile::open(const char* fname)
{
    if (m_stats_f) {
        close();
    }
    m_stats_f = fopen(fname, "wb");
}


// names for enum JB2RecordType and others
const char* val_names[Counters::LastCounter] = {
                               "Records jb2_start_of_image",
                               "Records jb2_new_symbol_add_to_image_and_library",
                               "Records jb2_new_symbol_add_to_library_only",
                               "Records jb2_new_symbol_add_to_image_only",
                               "Records jb2_matched_symbol_with_refinement_add_to_image_and_library",
                               "Records jb2_matched_symbol_with_refinement_add_to_library_only",
                               "Records jb2_matched_symbol_with_refinement_add_to_image_only",
                               "Records jb2_matched_symbol_copy_to_image_without_refinement",
                               "Records jb2_non_symbol_data",
                               "Records jb2_require_dictionary_or_reset",
                               "Records jb2_comment",
                               "Records jb2_end_of_data",
                               "Shared dictionary usage count",
                               "Local dictionary usage count",
                               "Elements on page",
                               "Unique bitmaps used on page",
                               "Bitmaps added to local dictionary"
                          };


void LogFile::log(const char* fmt, int32 val)
{
    if (m_stats_f) {
        fprintf(m_stats_f, fmt, val);
    }
}

void LogFile::log(const char* val)
{
    if (m_stats_f) {
        fprintf(m_stats_f, "%s", val);
    }
}

void LogFile::logAction(int32 action, int32 idx, bool in_shared_lib)
{
    assert(action < 12);
    if (m_stats_f) {
        fprintf(m_stats_f, "%s:\t%u%s\n", val_names[action], idx, in_shared_lib?" [shared dictionary usage]":"");
        if (!in_shared_lib) {

        } else {

        }
    }
}

void LogFile::logAction(int32 action, int32 idx, bool in_shared_lib, int x, int y)
{
    assert(action < 12);
    if (m_stats_f) {
        fprintf(m_stats_f, "%s:\tid: %u\tx: %u\ty: %u\t%s\n", val_names[action], idx, x, y, in_shared_lib?" [shared dictionary usage]":"");
        if (!in_shared_lib) {

        } else {

        }
    }
}

void LogFile::logAction(int32 action)
{
    assert(action < 12);
    if (m_stats_f) {
        fprintf(m_stats_f, "%s\n", val_names[action]);
    }
}

void LogFile::close()
{
    if (m_stats_f) {
        if (m_counters) {
            for ( int i = 0; i < Counters::LastCounter; i++) {
                log(m_counters->getValue((Counters::CountersType)i, m_totals).data());
            }
        }
        fclose(m_stats_f);
        m_stats_f = NULL;
    }
}

void Counters::resetPageCounters() {
    for (int i = 0; i < LastCounter; i++) {
        m_counters[i] = 0;
        m_sizes[i] = 0;
    }
}

void Counters::clear() {
    resetPageCounters();
    memset(m_total_counters, 0, LastCounter*sizeof(int));
    memset(m_total_sizes, 0, LastCounter*sizeof(int));
}

void Counters::count(CountersType cntr, int size, int val)
{
    assert(cntr < LastCounter);
    m_counters[cntr] += val;
    m_total_counters[cntr] += val;
    m_sizes[cntr] += size;
    m_total_sizes[cntr] += size;
}

std::string Counters::getValue(CountersType cntr, bool total)
{
    assert(cntr < LastCounter);
    int* buf = total? m_total_counters : m_counters;
    int* buf_size = total? m_total_sizes : m_sizes;
    std::string val = ":\t" + std::to_string(buf[cntr]) +
            " (" + std::to_string((double)(buf_size[cntr]*100/1024)/100) + " Kb";
    if (buf[cntr]) {
        val += ", Avg.: " + std::to_string((double)buf_size[cntr]/buf[cntr]) + " b)\n";
    } else val += ")\n";
    return val_names[cntr] + val;
}
