/*
 * Copyright (c) 2020 YuQing <384681@qq.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "fastcommon/logger.h"
#include "fastdir/client/fdir_client.h"

static void usage(char *argv[])
{
    fprintf(stderr, "Usage: %s [-c config_filename] [-m mode] "
            "[-u uid] [-g gid] <-n namespace> <path>\n", argv[0]);
}

int main(int argc, char *argv[])
{
	int ch;
    const char *config_filename = "/etc/fdir/client.conf";
    char *ns;
    char *path;
    FDIRDEntryFullName fullname;
	int result;
    int base;
    char *endptr;
    FDIRClientOwnerModePair omp;
    FDIRDEntryInfo dentry;

    if (argc < 2) {
        usage(argv);
        return 1;
    }

    omp.mode = 0755;
    omp.uid = geteuid();
    omp.gid = getegid();
    ns = NULL;
    while ((ch=getopt(argc, argv, "hc:g:m:n:u:")) != -1) {
        switch (ch) {
            case 'h':
                usage(argv);
                break;
            case 'n':
                ns = optarg;
                break;
            case 'c':
                config_filename = optarg;
                break;
            case 'm':
                if (optarg[0] == '0') {
                    base = 8;
                } else {
                    base = 10;
                }
                omp.mode = strtol(optarg, &endptr, base);
                break;
            case 'u':
                omp.uid = strtol(optarg, &endptr, 10);
                break;
            case 'g':
                omp.gid = strtol(optarg, &endptr, 10);
                break;
            default:
                usage(argv);
                return 1;
        }
    }

    if (ns == NULL || optind >= argc) {
        usage(argv);
        return 1;
    }

    log_init();
    //g_log_context.log_level = LOG_DEBUG;

    path = argv[optind];
    if ((result=fdir_client_simple_init(config_filename)) != 0) {
        return result;
    }

    omp.mode |= S_IFDIR;
    FC_SET_STRING(fullname.ns, ns);
    FC_SET_STRING(fullname.path, path);
    return fdir_client_create_dentry(&g_fdir_client_vars.client_ctx,
                    &fullname, &omp, &dentry);
}
