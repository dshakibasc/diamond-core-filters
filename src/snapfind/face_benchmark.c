
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>
#include <libgen.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>

#include "searchlet_api.h"
#include "ring.h"
#include "face_search.h"
#include "fil_histo.h"
#include "image_tools.h"
#include "face_tools.h"


/*
 * this need to be global because we share it with the main
 * GUI.  XXX hack, do it right later.
 */
ls_search_handle_t shandle;

uint64_t
parse_uint64_string(const char *s)
{
    int             i,
                    o;
    unsigned int    x;          // Will actually hold an unsigned char
    uint64_t        u = 0u;

    assert(s);
    for (i = 0; i < 8; i++) {
        o = 3 * i;
        assert(isxdigit(s[o]) && isxdigit(s[o + 1]));
        assert((s[o + 2] == ':') || (s[o + 2] == '\0'));
        sscanf(s + o, "%2x", &x);
        u <<= 8;
        u += x;
    }
    return u;
}


/*
 * This initializes the search  state.
 */

void
init_search()
{

    shandle = ls_init_search();
    if (shandle == NULL) {
        printf("failed to initialize:  !! \n");
        exit(1);
    }

}

void
set_searchlist(int n, groupid_t * gids)
{
    int             err;

    err = ls_set_searchlist(shandle, n, gids);
    if (err) {
        printf("Failed to set searchlist on  err %d \n", err);
        exit(1);
    }
}

int
load_searchlet(char *obj_file, char *spec_file)
{
    int             err;
    char           *full_spec;
    char           *full_obj;
    char           *path_name;
    char           *res;

    full_spec = (char *) malloc(MAX_PATH);
    if (full_spec == NULL) {
        exit(1);                /* XXX */
    }
    full_obj = (char *) malloc(MAX_PATH);
    if (full_obj == NULL) {
        exit(1);                /* XXX */
    }

    path_name = (char *) malloc(MAX_PATH);
    if (path_name == NULL) {
        exit(1);                /* XXX */
    }

    res = getcwd(path_name, MAX_PATH);
    if (res == NULL) {
        exit(1);
    }

    sprintf(full_obj, "%s/%s", path_name, obj_file);
    sprintf(full_spec, "%s/%s", path_name, spec_file);

    err = ls_set_searchlet(shandle, DEV_ISA_IA32, full_obj, full_spec);
    if (err) {
        printf("Failed to set searchlet on err %d \n", err);
        exit(1);
    }

    return (0);
}

#define	MAX_DEVS	24
#define	MAX_FILTS	24

void
dump_dev_stats(dev_stats_t * dev_stats, int stat_len)
{
    int             i;

    fprintf(stdout, "Total objects: %d \n", dev_stats->ds_objs_total);
    fprintf(stdout, "Processed objects: %d \n", dev_stats->ds_objs_processed);
    fprintf(stdout, "Dropped objects: %d \n", dev_stats->ds_objs_dropped);
    fprintf(stdout, "System Load: %d \n", dev_stats->ds_system_load);
    fprintf(stdout, "Avg obj time: %lld \n", dev_stats->ds_avg_obj_time);

    for (i = 0; i < dev_stats->ds_num_filters; i++) {
        fprintf(stdout, "\tFilter: %s \n",
                dev_stats->ds_filter_stats[i].fs_name);
        fprintf(stdout, "\tProcessed: %d \n",
                dev_stats->ds_filter_stats[i].fs_objs_processed);
        fprintf(stdout, "\tDropped: %d \n",
                dev_stats->ds_filter_stats[i].fs_objs_dropped);
        fprintf(stdout, "\tAvg Time: %lld \n\n",
                dev_stats->ds_filter_stats[i].fs_avg_exec_time);

    }



}


void
dump_state()
{
    int             err,
                    i;
    int             dev_count;
    ls_dev_handle_t dev_list[MAX_DEVS];
    dev_stats_t    *dev_stats;
    int             stat_len;


    dev_count = MAX_DEVS;
    err = ls_get_dev_list(shandle, dev_list, &dev_count);
    if (err) {
        fprintf(stderr, "Failed to get device list %d \n", err);
        exit(1);
    }

    dev_stats = (dev_stats_t *) malloc(DEV_STATS_SIZE(MAX_FILTS));
    assert(dev_stats != NULL);

    for (i = 0; i < dev_count; i++) {
        stat_len = DEV_STATS_SIZE(MAX_FILTS);
        err = ls_get_dev_stats(shandle, dev_list[i], dev_stats, &stat_len);
        if (err) {
            fprintf(stderr, "Failed to get device list %d \n", err);
            exit(1);
        }
        dump_dev_stats(dev_stats, stat_len);
    }

    free(dev_stats);

    /*
     * Dump the dclt state.  XXX assumes this is in the local path */
    err = system("./dctl -r");
    if (err == -1) {
        fprintf(stderr, "Failed to execute dctl \n");
        exit(1);
    }

}


/*
 * This function initiates the search by building a filter
 * specification, setting the searchlet and then starting the search.
 */
int
do_search()
{
    int             err;
    int             count = 0;
    ls_obj_handle_t cur_obj;


    /*
     * Go ahead and start the search.
     */
    err = ls_start_search(shandle);
    if (err) {
        printf("Failed to start search on err %d \n", err);
        exit(1);
    }

    while (1) {
        err = ls_next_object(shandle, &cur_obj, 0);
        if (err == ENOENT) {
            return (count);
        } else if (err == 0) {
            count++;
            ls_release_object(shandle, cur_obj);
        } else {
            fprintf(stderr, "get_next_obj: failed on %d \n", err);
            exit(1);
        }

    }

}


/*
 * This stops the currently executing search.
 */
static void
stop_search(void *data)
{
    int             err;

    // fprintf(stderr, "stop search (ls_terminate_search)..\n");
    err = ls_terminate_search(shandle);
    if (err != 0) {
        printf("XXX failed to terminate search \n");
        exit(1);
    }
    fprintf(stderr, "stopped search !!! \n");
}

int
configure_devices(char *config_file)
{
    int             err;

    sleep(5);
    err = system(config_file);

    return (err);

}

void
usage()
{
    printf("usage: XXX \n");
}


#define	MAX_GROUPS	32

int
main(int argc, char **argv)
{
    int             err;
    groupid_t       gid = 2;
    char           *searchlet_file = NULL;
    char           *fspec_file = NULL;
    char           *config_file = NULL;
    int             gid_count = 0;
    groupid_t       gid_list[MAX_GROUPS];
    struct timeval  tv1,
                    tv2;
    struct timezone tz;
    int             secs;
    int             usec;
    int             c,
                    count;


    /*
     * Parse the command line args.
     */
    while (1) {
        c = getopt(argc, argv, "c:f:g:hs:");
        if (c == -1) {
            break;
        }

        switch (c) {

        case 'c':
            config_file = optarg;
            break;

        case 'f':
            fspec_file = optarg;
            break;

        case 'g':
            gid_list[gid_count] = parse_uint64_string(optarg);
            gid_count++;
            assert(gid_count < MAX_GROUPS);
            break;

        case 'h':
            usage();
            exit(0);
            break;

        case 's':
            searchlet_file = optarg;
            break;

        default:
            fprintf(stderr, "unknown option %c\n", c);
            usage();
            exit(1);
            break;
        }
    }


    if ((searchlet_file == NULL) || (fspec_file == NULL) ||
        (config_file == NULL) || (gid_count == 0)) {

        usage();
        exit(1);
    }



    /*
     * setup the search 
     */
    init_search();

    set_searchlist(gid_count, gid_list);

    err = load_searchlet(searchlet_file, fspec_file);

    err = configure_devices(config_file);

    /*
     * get start time 
     */
    err = gettimeofday(&tv1, &tz);

    count = do_search();

    /*
     * get stop time 
     */
    err = gettimeofday(&tv2, &tz);

    secs = tv2.tv_sec - tv1.tv_sec;
    usec = tv2.tv_usec - tv1.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        secs -= 1;
    }

    fprintf(stdout, " Found %d items in %d.%d  \n", count, secs, usec);

    dump_state();


}
