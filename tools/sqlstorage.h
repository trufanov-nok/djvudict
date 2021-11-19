#ifndef SQLSTORAGE_H
#define SQLSTORAGE_H

#include "config.h"

#ifdef HAVE_LIBSQLITE3

#include <sqlite3.h>

class SQLStorage
{
public:
    SQLStorage();
    bool init(const char* filename);
    void save_on_disk();
    ~SQLStorage();

    void start_new_form(int position, const char* entry_name, const char* dump_path);
    void start_new_sjbz(int w, int h, int version, int dpi);
    void start_new_djbz();
    void endof_form() { m_cur_form_id = -1; }
    void endof_djbz() { m_cur_djbz_id = -1; }

    void use_djbz(const char* entry_name);

    void add_letter(int local_id, int x, int y, int w, int h,
                    int to_image, int to_library, int is_non_symbol,
                    int ref_local_id, int from_djbz,
                    int is_refinement, const char* filename);

private:
    bool open(const char* filename);
    bool clear();
    bool create();
    void close();
private:
    sqlite3 * m_storage;
    sqlite3 * m_storage_on_disk;
    int m_cur_form_id;
    int m_cur_djbz_id;
};

#endif // HAVE_LIBSQLITE3

#endif // SQLSTORAGE_H
