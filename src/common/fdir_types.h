#ifndef _FDIR_TYPES_H
#define _FDIR_TYPES_H

#include "fastcommon/common_define.h"

#define FDIR_ERROR_INFO_SIZE   256

#define FDIR_NETWORK_TIMEOUT_DEFAULT    30
#define FDIR_CONNECT_TIMEOUT_DEFAULT    30

#define FDIR_SERVER_DEFAULT_INNER_PORT  11011
#define FDIR_SERVER_DEFAULT_OUTER_PORT  11011

#define FDIR_MAX_PATH_COUNT  128

typedef struct {
    unsigned char cmd;  //response command
    int body_len;       //response body length
} FDIRRequestInfo;

typedef struct {
    unsigned char cmd;   //response command
    int body_len;    //response body length
    int status;
    struct {
        int length;
        char message[FDIR_ERROR_INFO_SIZE];
    } error;
} FDIRResponseInfo;

typedef struct fdir_dentry_info {
    string_t ns;
    string_t path;
} FDIRDEntryInfo;

#endif
