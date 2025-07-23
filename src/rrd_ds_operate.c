//
// Created by ytq on 25-7-23.

#include "rrd_tool.h"
#include "rrd_restore.h"
#include "rrd_create.h"
#include "rrd_rpncalc.h"
#include <string.h>

// 添加 debug 打印宏
#define DEBUG_PRINT(...) fprintf(stderr, "[DEBUG] " __VA_ARGS__)

// 封装打印 DS 结构体内容的调试函数
static void print_ds_def(const char *label, ds_def_t *ds) {
    DEBUG_PRINT("%s:\n", label);
    DEBUG_PRINT("  ds_nam     = %s\n", ds->ds_nam);
    DEBUG_PRINT("  dst        = %s\n", ds->dst);
    DEBUG_PRINT("  heartbeat  = %lu\n", ds->par[DS_mrhb_cnt].u_cnt);
    DEBUG_PRINT("  min        = %lf\n", ds->par[DS_min_val].u_val);
    DEBUG_PRINT("  max        = %lf\n", ds->par[DS_max_val].u_val);
}

static int update_single_ds(rrd_t *rrd, const char *filename, const char *ds_arg, const char **require_version) {
    DEBUG_PRINT("Parsing update string: %s\n", ds_arg);

    char *colon1 = strchr(ds_arg, ':');
    if (!colon1) {
        rrd_set_error("Invalid DS format (missing ':')");
        return -1;
    }

    size_t old_len = colon1 - ds_arg;
    if (old_len >= DS_NAM_SIZE) {
        rrd_set_error("Old DS name too long");
        return -1;
    }

    char old_ds_name[DS_NAM_SIZE];
    strncpy(old_ds_name, ds_arg, old_len);
    old_ds_name[old_len] = '\0';

    DEBUG_PRINT("Old DS name: '%s'\n", old_ds_name);

    const char *new_ds_args = colon1 + 1;
    ds_def_t update_ds;

    DEBUG_PRINT("New DS args: %s\n", new_ds_args);
    if (parseDS(new_ds_args, &update_ds, rrd, lookup_DS, NULL, require_version) != 0) {
        fprintf(stderr, "Failed to parse new DS definition for '%s': %s\n", old_ds_name, rrd_get_error());
        return -1;
    }

    print_ds_def("Parsed new DS", &update_ds);

    int found = 0;
    for (unsigned long j = 0; j < rrd->stat_head->ds_cnt; ++j) {
        if (strcmp(rrd->ds_def[j].ds_nam, old_ds_name) == 0) {
            DEBUG_PRINT("Found matching DS at index %lu\n", j);
            print_ds_def("Original DS", &rrd->ds_def[j]);

            memcpy(&rrd->ds_def[j], &update_ds, sizeof(ds_def_t));

            print_ds_def("Updated DS", &rrd->ds_def[j]);
            found = 1;
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "Error: DS named '%s' not found in file %s\n", old_ds_name, filename);
        return -1;
    }

    return 0;
}

int rrd_update_ds(int argc, const char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: rrdtool updateds filename [DS:old_name:new_name:DST:dst arguments] [...]\n");
        return -1;
    }

    const char *filename = argv[1];
    DEBUG_PRINT("Opening RRD file: %s\n", filename);
    rrd_thread_init();
    rrd_clear_error();

    rrd_t rrd;
    rrd_init(&rrd);
    int result = -1;
    const char *require_version = NULL;

    rrd_file_t *rrd_file = rrd_open(filename, &rrd, RRD_READWRITE | RRD_LOCK);
    if (!rrd_file) {
        fprintf(stderr, "Failed to open RRD file (%s): %s\n", filename, rrd_get_error());
        goto done;
    }

    DEBUG_PRINT("RRD file opened. DS count: %lu\n", rrd.stat_head->ds_cnt);

    for (int i = 2; i < argc; ++i) {
        DEBUG_PRINT("Processing arg[%d]: %s\n", i, argv[i]);

        if (strncmp(argv[i], "DS:", 3) != 0) {
            rrd_set_error("Invalid DS format (must start with 'DS:')");
            fprintf(stderr, "%s\n", rrd_get_error());
            goto done;
        }

        if (update_single_ds(&rrd, filename, argv[i] + 3, &require_version) != 0) {
            goto done;
        }
    }

    if (require_version != NULL) {
        strncpy(rrd.stat_head->version, require_version, 4);
        rrd.stat_head->version[4] = '\0';
    }

    DEBUG_PRINT("Writing updated RRD file...\n");
    if (rrd_seek(rrd_file, 0, SEEK_SET) != 0 ||
    rrd_write(rrd_file, rrd.stat_head, sizeof(stat_head_t) * 1) ||
    rrd_write(rrd_file, rrd.ds_def, sizeof(ds_def_t) * rrd.stat_head->ds_cnt)) {
        fprintf(stderr, "Failed to write header: %s\n", rrd_get_error());
        goto done;
    }

    DEBUG_PRINT("Write successful.\n");
    result = 0;

done:
    if (rrd_file)
        rrd_close(rrd_file);
    rrd_free(&rrd);

    DEBUG_PRINT("Finished.\n");
    return result;
}