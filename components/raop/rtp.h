#ifndef _HAIRTUNES_H_
#define _HAIRTUNES_H_

#include "raop_sink.h"
#include "util.h"

typedef struct {
    unsigned short cport, tport, aport;
    struct rtp_s* ctx;
} rtp_resp_t;

rtp_resp_t rtp_init(struct in_addr host, int latency, char* aeskey, char* aesiv, char* fmtpstr, short unsigned pCtrlPort, short unsigned pTimingPort,
    uint8_t* buffer, size_t size, raop_cmd_cb_t cmd_cb, raop_data_cb_t data_cb);
void rtp_end(struct rtp_s* ctx);
bool rtp_flush(struct rtp_s* ctx, unsigned short seqno, unsigned rtptime, bool exit_locked);
void rtp_flush_release(struct rtp_s* ctx);
void rtp_record(struct rtp_s* ctx, unsigned short seqno, unsigned rtptime);
void rtp_metadata(struct rtp_s* ctx, struct metadata_s* metadata);

#endif
