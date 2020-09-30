//binlog_reader.h

#ifndef _BINLOG_READER_H_
#define _BINLOG_READER_H_

#include "sf/sf_binlog_writer.h"
#include "binlog_types.h"

typedef struct {
    char filename[PATH_MAX];
    int fd;
    SFBinlogFilePosition position;
    SFBinlogBuffer binlog_buffer;
} ServerBinlogReader;

#ifdef __cplusplus
extern "C" {
#endif

int binlog_reader_init(ServerBinlogReader *reader,
        const SFBinlogFilePosition *hint_pos,
        const int64_t last_data_version);

void binlog_reader_destroy(ServerBinlogReader *reader);

int binlog_reader_read(ServerBinlogReader *reader);

int binlog_reader_integral_read(ServerBinlogReader *reader, char *buff,
        const int size, int *read_bytes, int64_t *data_version);

int binlog_reader_next_record(ServerBinlogReader *reader,
        FDIRBinlogRecord *record);

int binlog_get_first_record_version(const int file_index,
        int64_t *data_version);

int binlog_get_last_record_version(const int file_index,
        int64_t *data_version);

int binlog_get_max_record_version(int64_t *data_version);

int binlog_check_consistency(const string_t *sbinlog,
        const SFBinlogFilePosition *hint_pos,
        uint64_t *first_unmatched_dv);

#ifdef __cplusplus
}
#endif

#endif
