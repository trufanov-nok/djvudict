/*
 * minidjvu.c - an example of using the library
 */

#include <minidjvu/minidjvu.h>
#include "../src/base/mdjvucfg.h" /* for i18n, HAVE_LIBTIFF */
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <locale.h>
#include "djvudirreader.h"
#include "jb2dumper.h"

bool verbose = true;

const char *link_to_filename(const char *path_to_djvu) {
    int32 pos = strlen(path_to_djvu);
    while (pos > 0 && path_to_djvu[pos] != '/' && path_to_djvu[pos] != '\\') pos--;
    return path_to_djvu + pos;
}

uint32 dump_djvu_dict(const char *djvu_filepath, const char *out_path, mdjvu_error_t *perr)
{
    if (perr) {
        *perr = NULL;
    }

    FILE *f = fopen(djvu_filepath, "rb");
    if (!f) {
        if (perr) *perr = mdjvu_get_error(mdjvu_error_fopen_read);
        return 0;
    }

    uint32 id = read_uint32_most_significant_byte_first(f);
    if (id != CHUNK_ID_AT_AND_T)
    {
        fprintf(stderr, "No AT&T tag found.\n");
        if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_djvu);
        return 0;
    }

    IFFChunk FORM;
    get_child_chunk(f, &FORM, NULL);
    if (!find_sibling_chunk(f, &FORM, CHUNK_ID_FORM))
    {
        fprintf(stderr, "No FORM tag found.\n");
        if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_djvu);
        return 0;
    }

    id = read_uint32_most_significant_byte_first(f);
    if (id == ID_DJVU)
    { // single-page DjVu
        DIRM_Entry single_page;
        single_page.size = FORM.length;
        single_page.type = Page;
        // should be FORM start
        single_page.offset = ftell(f) - 8 /*FORM header*/ - 4 /*DJVU tag*/;
        const char flags = 0b10000001;
        single_page.str_flags = &flags;
        single_page.id_str = link_to_filename(djvu_filepath);
        single_page.name_str = NULL;
        single_page.title_str = NULL;

        JB2Dumper dumper;
        dumper.dumpMultiPage(f, &single_page, 1, out_path, perr);
    } else if (id == ID_DJVM)
    { // multi-page DjVu
        IFFChunk DIRM;
        get_child_chunk(f, &DIRM, &FORM);
        if (DIRM.id != ID_DIRM) {
            fprintf(stderr, "No DIRM tag found.\n");
            if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_djvu);
            return 0;
        }

        DjVuDirReader dir;
        int readed_len = dir.decode(f, DIRM.length - DIRM.skipped, perr, verbose);
        skip_in_chunk(&DIRM, readed_len);
        if (DIRM.length % 2) {
            fseek(f, 1, SEEK_CUR); // align file pos
        }
        if (*perr || !readed_len) {
            fprintf(stderr, "No DIRM tag found.\n");
            if (perr) *perr = mdjvu_get_error(mdjvu_error_corrupted_djvu);
            return 0;
        }
        JB2Dumper dumper;
        dumper.dumpMultiPage(f, dir.entries(), dir.count(), out_path, perr);

    } else {
        fprintf(stderr, "No DJVU or DJVM tag found.\n");
        if (perr) *perr = mdjvu_get_error(mdjvu_error_wrong_djvu_type);
        return 0;
    }


    fclose(f);
    return 1;
}

#define DICT_DUMPER_VERSION "0.0.1"

static void show_usage_and_exit(void)           /* {{{ */
{
    const char *what_it_does = _("dumps djvu dictinaries content and usage statistics");
    printf("djvudictdump %s - %s\n", DICT_DUMPER_VERSION, what_it_does);
    printf(_("Usage:\n"));
    printf(_("    djvudictdump [options] <input file> <output folder>\n"));
    printf(_("Formats supported:\n"));
    printf(_("    DjVu (single-page), DjVu (bundled multi-page)\n"));
//    printf(_("Options:\n"));
//    printf(_("    -A, --Averaging:               compute \"average\" representatives\n"));
    exit(2);
}                   /* }}} */

static int decide_if_djvu(const char *path)
{
    return mdjvu_ends_with_ignore_case(path, ".djvu")
        || mdjvu_ends_with_ignore_case(path, ".djv");
}

int main(int argc, char **argv)
{
    //int arg_start;
    setlocale(LC_ALL, "");

    if (argc < 3 || !decide_if_djvu(argv[1])) {
        show_usage_and_exit();
        return 0;
    }

    mdjvu_error_t perr;
    if (!dump_djvu_dict(argv[1], argv[2], &perr)) {
        fprintf(stderr, "%s", mdjvu_get_error_message(perr));
        exit(1);
    }
    return 0;
}
