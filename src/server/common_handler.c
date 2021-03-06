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

//common_handler.c

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include "fastcommon/logger.h"
#include "fastcommon/sockopt.h"
#include "fastcommon/shared_func.h"
#include "fastcommon/pthread_func.h"
#include "fastcommon/sched_thread.h"
#include "sf/sf_func.h"
#include "sf/sf_nio.h"
#include "sf/sf_global.h"
#include "common/fdir_proto.h"
#include "server_global.h"
#include "common_handler.h"

int handler_deal_task_done(struct fast_task_info *task)
{
    FDIRProtoHeader *proto_header;
    int r;
    int time_used;
    int log_level;
    char time_buff[32];

    if (TASK_ARG->context.log_level != LOG_NOTHING &&
            RESPONSE.error.length > 0)
    {
        log_it_ex(&g_log_context, TASK_ARG->context.log_level,
                "file: "__FILE__", line: %d, "
                "peer %s:%u, cmd: %d (%s), req body length: %d, %s",
                __LINE__, task->client_ip, task->port, REQUEST.header.cmd,
                fdir_get_cmd_caption(REQUEST.header.cmd),
                REQUEST.header.body_len, RESPONSE.error.message);
    }

    if (!TASK_ARG->context.need_response) {
        time_used = (int)(get_current_time_us() - TASK_ARG->req_start_time);

        switch (REQUEST.header.cmd) {
            case SF_PROTO_ACTIVE_TEST_RESP:
                log_level = LOG_NOTHING;
                break;
            case FDIR_REPLICA_PROTO_PUSH_BINLOG_REQ:
            case FDIR_REPLICA_PROTO_PUSH_BINLOG_RESP:
                log_level = LOG_DEBUG;
                break;
            default:
                //log_level = LOG_INFO;
                log_level = LOG_DEBUG;
                break;
        }

        if (FC_LOG_BY_LEVEL(log_level)) {
            log_it_ex(&g_log_context, log_level, "file: "__FILE__", line: %d, "
                    "client %s:%u, req cmd: %d (%s), req body_len: %d, "
                    "status: %d, time used: %s us", __LINE__,
                    task->client_ip, task->port, REQUEST.header.cmd,
                    fdir_get_cmd_caption(REQUEST.header.cmd),
                    REQUEST.header.body_len, RESPONSE_STATUS,
                    long_to_comma_str(time_used, time_buff));
        }

        if (RESPONSE_STATUS == 0) {
            task->offset = task->length = 0;
            return sf_set_read_event(task);
        }
        return RESPONSE_STATUS > 0 ? -1 * RESPONSE_STATUS : RESPONSE_STATUS;
    }

    proto_header = (FDIRProtoHeader *)task->data;
    if (!TASK_ARG->context.response_done) {
        RESPONSE.header.body_len = RESPONSE.error.length;
        if (RESPONSE.error.length > 0) {
            memcpy(task->data + sizeof(FDIRProtoHeader),
                    RESPONSE.error.message, RESPONSE.error.length);
        }
    }

    short2buff(RESPONSE_STATUS >= 0 ? RESPONSE_STATUS : -1 * RESPONSE_STATUS,
            proto_header->status);
    proto_header->cmd = RESPONSE.header.cmd;
    int2buff(RESPONSE.header.body_len, proto_header->body_len);
    task->length = sizeof(FDIRProtoHeader) + RESPONSE.header.body_len;

    r = sf_send_add_event(task);
    time_used = (int)(get_current_time_us() - TASK_ARG->req_start_time);
    if (time_used > 100 * 1000) {
        logWarning("file: "__FILE__", line: %d, "
                "process a request timed used: %s us, "
                "cmd: %d (%s), req body len: %d, resp cmd: %d (%s), "
                "status: %d, resp body len: %d", __LINE__,
                long_to_comma_str(time_used, time_buff),
                REQUEST.header.cmd, fdir_get_cmd_caption(REQUEST.header.cmd),
                REQUEST.header.body_len, RESPONSE.header.cmd,
                fdir_get_cmd_caption(RESPONSE.header.cmd),
                RESPONSE_STATUS, RESPONSE.header.body_len);
    }

    switch (REQUEST.header.cmd) {
        case FDIR_CLUSTER_PROTO_PING_MASTER_REQ:
        case SF_PROTO_ACTIVE_TEST_REQ:
            log_level = LOG_NOTHING;
            break;
        case SF_SERVICE_PROTO_REPORT_REQ_RECEIPT_REQ:
            log_level = LOG_DEBUG;
            break;
        default:
            //log_level = LOG_INFO;
            log_level = LOG_DEBUG;
            break;
    }

    if (FC_LOG_BY_LEVEL(log_level)) {
        log_it_ex(&g_log_context, log_level, "file: "__FILE__", line: %d, "
                "client %s:%u, req cmd: %d (%s), req body_len: %d, "
                "resp cmd: %d (%s), status: %d, resp body_len: %d, "
                "time used: %s us", __LINE__,
                task->client_ip, task->port, REQUEST.header.cmd,
                fdir_get_cmd_caption(REQUEST.header.cmd),
                REQUEST.header.body_len, RESPONSE.header.cmd,
                fdir_get_cmd_caption(RESPONSE.header.cmd),
                RESPONSE_STATUS, RESPONSE.header.body_len,
                long_to_comma_str(time_used, time_buff));
    }

    return r == 0 ? RESPONSE_STATUS : r;
}
