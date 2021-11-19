#include "sqlstorage.h"
#include "../src/base/mdjvucfg.h" /* for i18n, HAVE_LIBTIFF */
#include <iostream>
#include <cassert>
#include <cstring>

SQLStorage::SQLStorage(): m_storage(nullptr)
{
    m_cur_form_id = m_cur_djbz_id = -1;
}

bool
SQLStorage::init(const char* filename)
{
    return ( open(filename) &&
             clear() &&
             create()
             );
}

SQLStorage::~SQLStorage()
{
    close();
}


bool
SQLStorage::open(const char* filename) {
    int res = sqlite3_open_v2(":memory:", &m_storage, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY, nullptr);

    if( res != SQLITE_OK ) {
        fprintf(stderr, _("Can't create in-memory sql database: %d (%s)\n"), res, sqlite3_errmsg(m_storage));
        return false;
    }

    res = sqlite3_open_v2(filename, &m_storage_on_disk, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if( res != SQLITE_OK ) {
        fprintf(stderr, _("Can't open sql database (%s): %d (%s)\n"), filename, res, sqlite3_errmsg(m_storage));
        return false;
    }

    return true;
}

bool
SQLStorage::clear()
{
    const char* sql = "DROP INDEX IF EXISTS index_letters; "
                      "DROP TABLE IF EXISTS letters; "
                      "DROP TABLE IF EXISTS sjbz_info; "
                      "DROP TABLE IF EXISTS forms; ";

    char *err = nullptr;
    int res = sqlite3_exec(m_storage_on_disk, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::clear() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        return false;
    }
    return true;
}


bool
SQLStorage::create()
{
    const char* sql =
            "PRAGMA foreign_keys = ON; "
            "PRAGMA temp_store=MEMORY; "
            "PRAGMA journal_mode=MEMORY; "
"CREATE TABLE forms ( "
"    id           INTEGER PRIMARY KEY AUTOINCREMENT "
"                         UNIQUE, "
"    position     INTEGER UNIQUE "
"                         NOT NULL, "
"    entry_name   STRING  NOT NULL, "
"    type         INTEGER NOT NULL, " // 0 - not known, 1 - sjbz, 2 - djbz
"    path_to_dump STRING "
"); "

"CREATE TABLE sjbz_info ( "
"    form_id  INTEGER REFERENCES forms (id)  "
"                     NOT NULL "
"                     UNIQUE, "
"    djbz_id  INTEGER REFERENCES forms (id), "
"    width    INTEGER NOT NULL, "
"    height   INTEGER NOT NULL, "
"    dpi      INTEGER NOT NULL, "
"    version  INTEGER NOT NULL "
"); "

"CREATE TABLE letters ( "
"    id                 INTEGER PRIMARY KEY AUTOINCREMENT "
"                               NOT NULL "
"                               UNIQUE, "
"    form_id            REFERENCES forms (id)  "
"                               NOT NULL, "
"    local_id           INTEGER, "
"    x                  INTEGER, "
"    y                  INTEGER, "
"    width              INTEGER NOT NULL, "
"    height             INTEGER NOT NULL, "
"    in_image           INTEGER NOT NULL, "
"    in_library         INTEGER NOT NULL, "
"    is_non_symbol      INTEGER NOT NULL, "
"    reference_id       INTEGER REFERENCES letters (id), " // if not NULL then
"    is_refinement      INTEGER, " // 0 - copy of reference_id, 1 - refinement of reference_id

"    filename           STRING "
"); "

"CREATE INDEX index_letters ON letters(form_id, local_id); ";


    char *err = nullptr;
    const int res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::create() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        return false;
    }
    return true;
}


void
SQLStorage::close()
{
    if (m_storage_on_disk) {
        sqlite3_close(m_storage_on_disk);
    }

    if (m_storage) {
        sqlite3_close(m_storage);
    }
}

void
SQLStorage::start_new_form(int position, const char* entry_name, const char* dump_path)
{
    assert(m_cur_form_id == -1);

    char *err = nullptr;
    char sql[1024];
    sprintf(sql, "INSERT INTO forms VALUES(NULL, %d, '%s', %u, '%s'); ", position, entry_name, 0, dump_path);

    const int res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::start_new_form() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }

    m_cur_form_id = sqlite3_last_insert_rowid(m_storage);
    assert(m_cur_form_id != -1);
}

void
SQLStorage::start_new_sjbz(int w, int h, int version, int dpi)
{
    assert(m_cur_form_id != -1);

    char *err = nullptr;
    char sql[1024];

    sprintf(sql, "UPDATE forms SET type = 1 WHERE id = %u; ", m_cur_form_id);

    int res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::start_new_sjbz() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }


    sprintf(sql, "INSERT INTO sjbz_info VALUES(%u, ", m_cur_form_id);

    if (m_cur_djbz_id != -1) {
        sprintf(sql+strlen(sql), "%u, ", m_cur_djbz_id);
    } else {
        sprintf(sql+strlen(sql), "NULL, ");
    }

    sprintf(sql+strlen(sql), "%u, %u, %u, %u); ", w, h, dpi, version);

    res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::start_new_sjbz():2 SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }

}

void
SQLStorage::start_new_djbz()
{
    assert(m_cur_form_id != -1);
    assert(m_cur_djbz_id == -1);

    char *err = nullptr;
    char sql[1024];

    sprintf(sql, "UPDATE forms SET type = 2 WHERE id = %u; ", m_cur_form_id);

    const int res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::start_new_djbz() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }

    m_cur_djbz_id = sqlite3_last_insert_rowid(m_storage);
    assert(m_cur_djbz_id != -1);
}

static int callback_set_id(void* id_to_set,int clmn_count,char** clmn_vals, char** /*clmn_names*/)
{
    assert(id_to_set);
    assert(clmn_count == 1);
    assert(clmn_vals[0]);
    *((int*)id_to_set) = atoi(clmn_vals[0]);
    return 0;
}

void
SQLStorage::use_djbz(const char* entry_name)
{
    assert(m_cur_djbz_id == -1);
    char *err = nullptr;
    char sql[1024];

    sprintf(sql, " SELECT id FROM forms WHERE entry_name = '%s'; ", entry_name);

    const int res = sqlite3_exec(m_storage, sql, callback_set_id, &m_cur_djbz_id, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::use_djbz() SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }
}

void
SQLStorage::add_letter(int local_id, int x, int y, int w, int h,
                       int to_image, int to_library, int is_non_symbol,
                       int ref_local_id, int from_djbz,
                       int is_refinement, const char* filename)
{
    assert(m_cur_form_id != -1);

    char *err = nullptr;
    char sql[1024];

    int entity_ref = -1;
    int _ref_global_id = -1;

    if (ref_local_id != -1) {
        entity_ref = from_djbz ? m_cur_djbz_id : m_cur_form_id;
        assert(entity_ref != -1);

        sprintf(sql, "SELECT id FROM letters WHERE form_id = %u AND local_id = %u ; ",
                entity_ref, ref_local_id);

        const int res = sqlite3_exec(m_storage, sql, callback_set_id, &_ref_global_id, &err);
        if ( res != SQLITE_OK ) {
            fprintf(stderr, _("Error in SQLStorage::add_letter():1 SQL exec: %d (%s)\n"), res, err);
            sqlite3_free(err);
            exit(3);
        }
        assert(_ref_global_id != -1);
    }

    sprintf(sql, "INSERT INTO letters VALUES (NULL, %u, ",
            m_cur_form_id);

    if (local_id != -1) {
        sprintf(sql+strlen(sql), "%u, ", local_id);
    } else {
        sprintf(sql+strlen(sql), "NULL, ");
    }

    sprintf(sql+strlen(sql), "%u, %u, %u, %u, %u, %u, %u, ",
            x, y, w, h, to_image, to_library, is_non_symbol);


    if (_ref_global_id != -1) {
        sprintf(sql+strlen(sql), "%u, ", _ref_global_id);
    } else {
        sprintf(sql+strlen(sql), "NULL, ");
    }

    sprintf(sql+strlen(sql), "%u, '%s'); ",
            is_refinement, filename);

    const int res = sqlite3_exec(m_storage, sql, nullptr, nullptr, &err);
    if ( res != SQLITE_OK ) {
        fprintf(stderr, _("Error in SQLStorage::add_letter():2 SQL exec: %d (%s)\n"), res, err);
        sqlite3_free(err);
        exit(3);
    }
}

void
SQLStorage::save_on_disk()
{
    sqlite3_backup* backup = sqlite3_backup_init(m_storage_on_disk, "main", m_storage, "main");
    if (!backup) {
        fprintf(stderr, _("Error in SQLStorage::save_on_disk(): can't init backup object\n"));
        exit(3);
    }

    int res;
    do {
        res = sqlite3_backup_step(backup, -1);
        if (    res != SQLITE_DONE &&
                res != SQLITE_OK &&
                res != SQLITE_BUSY &&
                res != SQLITE_LOCKED) {
            fprintf(stderr, _("Error in SQLStorage::save_on_disk(): sqlite3_backup_step returned %d\n"), res);
            exit(3);
        }
    } while (res != SQLITE_DONE);

    res = sqlite3_backup_finish(backup);
    if (res != SQLITE_OK) {
        fprintf(stderr, _("Error in SQLStorage::save_on_disk(): sqlite3_backup_finish returned %d\n"), res);
        exit(3);
    }
}
