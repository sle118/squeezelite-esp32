
/*
 * HairTunes - RAOP packet handler and slave-clocked replay engine
 * Copyright (c) James Laird 2011
 * All rights reserved.
 *
 * Modularisation: philippe_44@outlook.com, 2019
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <assert.h>

#include "platform.h"
#include "rtp.h"
#include "raop_sink.h"
#include "log_util.h"
#include "util.h"

#ifdef WIN32
#include <openssl/aes.h>
#include "alac_wrapper.h"
#define MSG_DONTWAIT 0
#else
#include "esp_pthread.h"
#include "esp_system.h"
#include <mbedtls/version.h>
#include <mbedtls/aes.h>
#include "alac_wrapper.h"
#endif

#define NTP2MS(ntp) ((((ntp) >> 10) * 1000L) >> 22)
#define MS2NTP(ms) (((((u64_t)(ms)) << 22) / 1000) << 10)
#define NTP2TS(ntp, rate) ((((ntp) >> 16) * (rate)) >> 16)
#define TS2NTP(ts, rate) (((((u64_t)(ts)) << 16) / (rate)) << 16)
#define MS2TS(ms, rate) ((((u64_t)(ms)) * (rate)) / 1000)
#define TS2MS(ts, rate) NTP2MS(TS2NTP(ts, rate))

extern log_level raop_loglevel;
static log_level* loglevel = &raop_loglevel;

//#define __RTP_STORE

// default buffer size
#define BUFFER_FRAMES_MAX ((RAOP_SAMPLE_RATE * 10) / 352)
#define BUFFER_FRAMES_MIN ((150 * RAOP_SAMPLE_RATE * 2) / (352 * 100))
#define MAX_PACKET 1408
#define MIN_LATENCY 11025
#define MAX_LATENCY ((120 * RAOP_SAMPLE_RATE * 2) / 100)

#define RTP_STACK_SIZE (4 * 1024)

#define RTP_SYNC (0x01)
#define NTP_SYNC (0x02)

#define RESEND_TO 250

enum { DATA = 0, CONTROL, TIMING };

static const u8_t silence_frame[MAX_PACKET] = {0};
uint32_t buffer_frames = ((150 * RAOP_SAMPLE_RATE * 2) / (352 * 100));

typedef u16_t seq_t;
typedef struct __attribute__((__packed__)) audio_buffer_entry { // decoded audio packets
    u32_t rtptime, last_resend;
    s16_t* data;
    u16_t len;
    u8_t ready;
    u8_t allocated;
} abuf_t;

typedef struct rtp_s {
#ifdef __RTP_STORE
    FILE *rtpIN, *rtpOUT;
#endif
    bool running;
    unsigned char aesiv[16];
#ifdef WIN32
    AES_KEY aes;
#else
    mbedtls_aes_context aes;
#endif
    bool decrypt;
    u8_t* decrypt_buf;
    u32_t frame_size, frame_duration;
    u32_t in_frames, out_frames;
    struct in_addr host;
    struct sockaddr_in rtp_host;
    struct {
        unsigned short rport, lport;
        int sock;
    } rtp_sockets[3]; // data, control, timing
    struct timing_s {
        u64_t local, remote;
    } timing;
    struct {
        u32_t rtp, time;
        u8_t status;
    } synchro;
    struct {
        u32_t time;
        seq_t seqno;
        u32_t rtptime;
    } record;
    int latency;                  // rtp hold depth in samples
    u32_t resent_req, resent_rec; // total resent + recovered frames
    u32_t silent_frames;          // total silence frames
    u32_t discarded;
    abuf_t audio_buffer[BUFFER_FRAMES_MAX];
    seq_t ab_read, ab_write;
    pthread_mutex_t ab_mutex;
#ifdef WIN32
    pthread_t thread;
#else
    TaskHandle_t thread, joiner;
    StaticTask_t* xTaskBuffer;
    StackType_t xStack[RTP_STACK_SIZE] __attribute__((aligned(4)));
#endif

    struct alac_codec_s* alac_codec;
    int flush_seqno;
    bool playing;
    int stalled;
    raop_data_cb_t data_cb;
    raop_cmd_cb_t cmd_cb;
} rtp_t;

#define BUFIDX(seqno) ((seq_t)(seqno) % buffer_frames)
static void buffer_alloc(abuf_t* audio_buffer, int size, uint8_t* buf, size_t buf_size);
static void buffer_release(abuf_t* audio_buffer);
static void buffer_reset(abuf_t* audio_buffer);
static void buffer_push_packet(rtp_t* ctx);
static bool rtp_request_resend(rtp_t* ctx, seq_t first, seq_t last);
static bool rtp_request_timing(rtp_t* ctx);
static int seq_order(seq_t a, seq_t b);
#ifdef WIN32
static void* rtp_thread_func(void* arg);
#else
static void rtp_thread_func(void* arg);
#endif

/*---------------------------------------------------------------------------*/
static struct alac_codec_s* alac_init(int fmtp[32]) {
    struct alac_codec_s* alac;
    unsigned sample_rate, block_size;
    unsigned char sample_size, channels;
    struct {
        uint32_t frameLength;
        uint8_t compatibleVersion;
        uint8_t bitDepth;
        uint8_t pb;
        uint8_t mb;
        uint8_t kb;
        uint8_t numChannels;
        uint16_t maxRun;
        uint32_t maxFrameBytes;
        uint32_t avgBitRate;
        uint32_t sampleRate;
    } config;

    config.frameLength = htonl(fmtp[1]);
    config.compatibleVersion = fmtp[2];
    config.bitDepth = fmtp[3];
    config.pb = fmtp[4];
    config.mb = fmtp[5];
    config.kb = fmtp[6];
    config.numChannels = fmtp[7];
    config.maxRun = htons(fmtp[8]);
    config.maxFrameBytes = htonl(fmtp[9]);
    config.avgBitRate = htonl(fmtp[10]);
    config.sampleRate = htonl(fmtp[11]);

    alac = alac_create_decoder(sizeof(config), (unsigned char*)&config, &sample_size, &sample_rate, &channels, &block_size);
    if(!alac) {
        LOG_ERROR("cannot create alac codec", NULL);
        return NULL;
    }

    return alac;
}

/*---------------------------------------------------------------------------*/
rtp_resp_t rtp_init(struct in_addr host, int latency, char* aeskey, char* aesiv, char* fmtpstr, short unsigned pCtrlPort, short unsigned pTimingPort,
    uint8_t* buffer, size_t size, raop_cmd_cb_t cmd_cb, raop_data_cb_t data_cb) {
    int i = 0;
    char* arg;
    int fmtp[32];
    bool rc = true;
    rtp_t* ctx = calloc(1, sizeof(rtp_t));
    rtp_resp_t resp = {0, 0, 0, NULL};

    if(!ctx) return resp;

    ctx->host = host;
    ctx->decrypt = false;
    ctx->cmd_cb = cmd_cb;
    ctx->data_cb = data_cb;
    ctx->rtp_host.sin_family = AF_INET;
    ctx->rtp_host.sin_addr.s_addr = INADDR_ANY;
    pthread_mutex_init(&ctx->ab_mutex, 0);
    ctx->flush_seqno = -1;
    ctx->latency = latency;
    ctx->ab_read = ctx->ab_write;

#ifdef __RTP_STORE
    ctx->rtpIN = fopen("airplay.rtpin", "wb");
    ctx->rtpOUT = fopen("airplay.rtpout", "wb");
#endif

    ctx->rtp_sockets[CONTROL].rport = pCtrlPort;
    ctx->rtp_sockets[TIMING].rport = pTimingPort;

    if(aesiv && aeskey) {
        memcpy(ctx->aesiv, aesiv, 16);
#ifdef WIN32
        AES_set_decrypt_key((unsigned char*)aeskey, 128, &ctx->aes);
#else
        memset(&ctx->aes, 0, sizeof(mbedtls_aes_context));
        mbedtls_aes_setkey_dec(&ctx->aes, (unsigned char*)aeskey, 128);
#endif
        ctx->decrypt = true;
        ctx->decrypt_buf = malloc(MAX_PACKET);
    }

    memset(fmtp, 0, sizeof(fmtp));
    while((arg = strsep(&fmtpstr, " \t")) != NULL) fmtp[i++] = atoi(arg);

    ctx->frame_size = fmtp[1];
    ctx->frame_duration = (ctx->frame_size * 1000) / RAOP_SAMPLE_RATE;

    // alac decoder
    ctx->alac_codec = alac_init(fmtp);
    rc &= ctx->alac_codec != NULL;

    buffer_alloc(ctx->audio_buffer, ctx->frame_size * 4, buffer, size);

    // create rtp ports
    for(i = 0; i < 3; i++) {
        ctx->rtp_sockets[i].sock = bind_socket(&ctx->rtp_sockets[i].lport, SOCK_DGRAM);
        rc &= ctx->rtp_sockets[i].sock > 0;
    }

    // create http port and start listening
    resp.cport = ctx->rtp_sockets[CONTROL].lport;
    resp.tport = ctx->rtp_sockets[TIMING].lport;
    resp.aport = ctx->rtp_sockets[DATA].lport;

    ctx->running = true;

#ifdef WIN32
    pthread_create(&ctx->thread, NULL, rtp_thread_func, (void*)ctx);
#else
    ctx->xTaskBuffer = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ctx->thread = xTaskCreateStaticPinnedToCore((TaskFunction_t)rtp_thread_func, "RTP_thread", RTP_STACK_SIZE, ctx,
        CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT + 1, ctx->xStack, ctx->xTaskBuffer, CONFIG_PTHREAD_TASK_CORE_DEFAULT);
#endif

    // cleanup everything if we failed
    if(!rc) {
        LOG_ERROR("[%p]: cannot start RTP", ctx);
        rtp_end(ctx);
        ctx = NULL;
    }

    resp.ctx = ctx;
    return resp;
}

/*---------------------------------------------------------------------------*/
void rtp_end(rtp_t* ctx) {
    int i;

    if(!ctx) return;

    if(ctx->running) {
#if !defined WIN32
        ctx->joiner = xTaskGetCurrentTaskHandle();
#endif
        ctx->running = false;
#ifdef WIN32
        pthread_join(ctx->thread, NULL);
#else
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        vTaskDelete(ctx->thread);
        SAFE_PTR_FREE(ctx->xTaskBuffer);
#endif
    }

    for(i = 0; i < 3; i++) closesocket(ctx->rtp_sockets[i].sock);

    if(ctx->alac_codec) alac_delete_decoder(ctx->alac_codec);
    if(ctx->decrypt_buf) free(ctx->decrypt_buf);

    pthread_mutex_destroy(&ctx->ab_mutex);
    buffer_release(ctx->audio_buffer);

    free(ctx);

#ifdef __RTP_STORE
    fclose(ctx->rtpIN);
    fclose(ctx->rtpOUT);
#endif
}

/*---------------------------------------------------------------------------*/
bool rtp_flush(rtp_t* ctx, unsigned short seqno, unsigned int rtptime, bool exit_locked) {
    bool rc = true;
    u32_t now = gettime_ms();

    if(now < ctx->record.time + 250 || (ctx->record.seqno == seqno && ctx->record.rtptime == rtptime)) {
        rc = false;
        LOG_ERROR("[%p]: FLUSH ignored as same as RECORD (%hu - %u)", ctx, seqno, rtptime);
    } else {
        pthread_mutex_lock(&ctx->ab_mutex);
        buffer_reset(ctx->audio_buffer);
        ctx->playing = false;
        ctx->flush_seqno = seqno;
        if(!exit_locked) pthread_mutex_unlock(&ctx->ab_mutex);
    }

    LOG_INFO("[%p]: flush %hu %u", ctx, seqno, rtptime);

    return rc;
}

/*---------------------------------------------------------------------------*/
void rtp_flush_release(rtp_t* ctx) { pthread_mutex_unlock(&ctx->ab_mutex); }

/*---------------------------------------------------------------------------*/
void rtp_record(rtp_t* ctx, unsigned short seqno, unsigned rtptime) {
    ctx->record.seqno = seqno;
    ctx->record.rtptime = rtptime;
    ctx->record.time = gettime_ms();

    LOG_INFO("[%p]: record %hu %u", ctx, seqno, rtptime);
}

/*---------------------------------------------------------------------------*/
static void buffer_alloc(abuf_t* audio_buffer, int size, uint8_t* buf, size_t buf_size) {
    for(buffer_frames = 0; buf && buf_size >= size && buffer_frames < BUFFER_FRAMES_MAX; buffer_frames++) {
        audio_buffer[buffer_frames].data = (s16_t*)buf;
        audio_buffer[buffer_frames].allocated = 0;
        audio_buffer[buffer_frames].ready = 0;
        buf += size;
        buf_size -= size;
    }

    LOG_INFO("allocated %d buffers (min=%d) from buffer of %zu bytes", buffer_frames, BUFFER_FRAMES_MIN, buf_size + buffer_frames * size);

    for(; buffer_frames < BUFFER_FRAMES_MIN; buffer_frames++) {
        audio_buffer[buffer_frames].data = malloc(size);
        audio_buffer[buffer_frames].allocated = 1;
        audio_buffer[buffer_frames].ready = 0;
    }
}

/*---------------------------------------------------------------------------*/
static void buffer_release(abuf_t* audio_buffer) {
    int i;
    for(i = 0; i < buffer_frames; i++) {
        if(audio_buffer[i].allocated) free(audio_buffer[i].data);
    }
}

/*---------------------------------------------------------------------------*/
static void buffer_reset(abuf_t* audio_buffer) {
    int i;
    for(i = 0; i < buffer_frames; i++) audio_buffer[i].ready = 0;
}

/*---------------------------------------------------------------------------*/
// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static int seq_order(seq_t a, seq_t b) {
    s16_t d = b - a;
    return d > 0;
}

/*---------------------------------------------------------------------------*/
static void alac_decode(rtp_t* ctx, s16_t* dest, char* buf, int len, u16_t* outsize) {
    unsigned char iv[16];
    int aeslen;
    assert(len <= MAX_PACKET);

    if(ctx->decrypt) {
        aeslen = len & ~0xf;
        memcpy(iv, ctx->aesiv, sizeof(iv));
#ifdef WIN32
        AES_cbc_encrypt((unsigned char*)buf, ctx->decrypt_buf, aeslen, &ctx->aes, iv, AES_DECRYPT);
#else
        mbedtls_aes_crypt_cbc(&ctx->aes, MBEDTLS_AES_DECRYPT, aeslen, iv, (unsigned char*)buf, ctx->decrypt_buf);
#endif
        memcpy(ctx->decrypt_buf + aeslen, buf + aeslen, len - aeslen);
        alac_to_pcm(ctx->alac_codec, (unsigned char*)ctx->decrypt_buf, (unsigned char*)dest, 2, (unsigned int*)outsize);
    } else {
        alac_to_pcm(ctx->alac_codec, (unsigned char*)buf, (unsigned char*)dest, 2, (unsigned int*)outsize);
    }

    *outsize *= 4;
}

/*---------------------------------------------------------------------------*/
static void buffer_put_packet(rtp_t* ctx, seq_t seqno, unsigned rtptime, bool first, char* data, int len) {
    abuf_t* abuf = NULL;
    u32_t playtime;

    pthread_mutex_lock(&ctx->ab_mutex);

    if(!ctx->playing) {
        if((ctx->flush_seqno == -1 || seq_order(ctx->flush_seqno, seqno)) && (ctx->synchro.status & RTP_SYNC) && (ctx->synchro.status & NTP_SYNC)) {
            ctx->ab_write = seqno - 1;
            ctx->ab_read = seqno;
            ctx->flush_seqno = -1;
            ctx->playing = true;
            ctx->resent_req = ctx->resent_rec = ctx->silent_frames = ctx->discarded = 0;
            playtime = ctx->synchro.time + ((rtptime - ctx->synchro.rtp) * 10) / (RAOP_SAMPLE_RATE / 100);
            ctx->cmd_cb(RAOP_PLAY, playtime);
        } else {
            pthread_mutex_unlock(&ctx->ab_mutex);
            return;
        }
    }

    if(seqno == (u16_t)(ctx->ab_write + 1)) {
        // expected packet
        abuf = ctx->audio_buffer + BUFIDX(seqno);
        ctx->ab_write = seqno;
        LOG_SDEBUG("packet expected seqno:%hu rtptime:%u (W:%hu R:%hu)", seqno, rtptime, ctx->ab_write, ctx->ab_read);
    } else if(seq_order(ctx->ab_write, seqno)) {
        // newer than expected
        if(ctx->latency && seq_order(ctx->latency / ctx->frame_size, seqno - ctx->ab_write - 1)) {
            // this is a shitstorm, reset buffer
            LOG_WARN("[%p] too many missing frames %hu seq: %hu, (W:%hu R:%hu)", ctx, seqno - ctx->ab_write - 1, seqno, ctx->ab_write, ctx->ab_read);
            ctx->ab_read = seqno;
        } else {
            // request re-send missed frames and evaluate resent date as a whole *after*
            rtp_request_resend(ctx, ctx->ab_write + 1, seqno - 1);

            // resend date is after all requests have been sent
            u32_t now = gettime_ms();

            // set expected timing of missed frames for buffer_push_packet and set last_resend date
            for(seq_t i = ctx->ab_write + 1; seq_order(i, seqno); i++) {
                ctx->audio_buffer[BUFIDX(i)].rtptime = rtptime - (seqno - i) * ctx->frame_size;
                ctx->audio_buffer[BUFIDX(i)].last_resend = now;
            }
            LOG_DEBUG("[%p]: packet newer seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
        }

        abuf = ctx->audio_buffer + BUFIDX(seqno);
        ctx->ab_write = seqno;
    } else if(seq_order(ctx->ab_read, seqno + 1)) {
        // recovered packet, not yet sent
        abuf = ctx->audio_buffer + BUFIDX(seqno);
        ctx->resent_rec++;
        LOG_DEBUG("[%p]: packet recovered seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
    } else {
        // too late
        LOG_DEBUG("[%p]: packet too late seqno:%hu rtptime:%u (W:%hu R:%hu)", ctx, seqno, rtptime, ctx->ab_write, ctx->ab_read);
    }

    if(ctx->in_frames++ > 1000) {
        LOG_INFO("[%p]: fill [level:%hu rec:%u] [W:%hu R:%hu]", ctx, ctx->ab_write - ctx->ab_read, ctx->resent_rec, ctx->ab_write, ctx->ab_read);
        ctx->in_frames = 0;
    }

    if(abuf) {
        alac_decode(ctx, abuf->data, data, len, &abuf->len);
        abuf->ready = 1;
        // this is the local rtptime when this frame is expected to play
        abuf->rtptime = rtptime;
        buffer_push_packet(ctx);

#ifdef __RTP_STORE
        fwrite(data, len, 1, ctx->rtpIN);
        fwrite(abuf->data, abuf->len, 1, ctx->rtpOUT);
#endif
    }

    pthread_mutex_unlock(&ctx->ab_mutex);
}

/*---------------------------------------------------------------------------*/
// push as many frames as possible through callback
static void buffer_push_packet(rtp_t* ctx) {
    abuf_t* curframe = NULL;
    u32_t now, playtime, hold = max((ctx->latency * 1000) / (8 * RAOP_SAMPLE_RATE), 100);

    // not ready to play yet
    if(!ctx->playing || ctx->synchro.status != (RTP_SYNC | NTP_SYNC)) return;

    // there is always at least one frame in the buffer
    do {
        // re-evaluate time in loop in case data callback blocks ...
        now = gettime_ms();

        // try to manage playtime so that we overflow as late as possible if we miss NTP (2^31 / 10 / 44100)
        curframe = ctx->audio_buffer + BUFIDX(ctx->ab_read);
        playtime = ctx->synchro.time + ((curframe->rtptime - ctx->synchro.rtp) * 10) / (RAOP_SAMPLE_RATE / 100);

        if(now > playtime) {
            LOG_DEBUG("[%p]: discarded frame now:%u missed by:%d (W:%hu R:%hu)", ctx, now, now - playtime, ctx->ab_write, ctx->ab_read);
            ctx->discarded++;
            curframe->ready = 0;
        } else if(playtime - now <= hold) {
            if(curframe->ready) {
                ctx->data_cb((const u8_t*)curframe->data, curframe->len, playtime);
                curframe->ready = 0;
            } else {
                LOG_DEBUG("[%p]: created zero frame (W:%hu R:%hu)", ctx, ctx->ab_write, ctx->ab_read);
                ctx->data_cb(silence_frame, ctx->frame_size * 4, playtime);
                ctx->silent_frames++;
            }
        } else if(curframe->ready) {
            ctx->data_cb((const u8_t*)curframe->data, curframe->len, playtime);
            curframe->ready = 0;
        } else {
            break;
        }

        ctx->ab_read++;
        ctx->out_frames++;

    } while(seq_order(ctx->ab_read, ctx->ab_write));

    if(ctx->out_frames > 1000) {
        LOG_INFO("[%p]: drain [level:%hd head:%d ms] [W:%hu R:%hu] [req:%u sil:%u dis:%u]", ctx, ctx->ab_write - ctx->ab_read, playtime - now,
            ctx->ab_write, ctx->ab_read, ctx->resent_req, ctx->silent_frames, ctx->discarded);
        ctx->out_frames = 0;
    }

    LOG_SDEBUG("playtime %u %d [W:%hu R:%hu] %d", playtime, playtime - now, ctx->ab_write, ctx->ab_read, curframe->ready);

    // each missing packet will be requested up to (latency_frames / 16) times
    for(int i = 0; seq_order(ctx->ab_read + i, ctx->ab_write); i += 16) {
        abuf_t* frame = ctx->audio_buffer + BUFIDX(ctx->ab_read + i);
        if(!frame->ready && now - frame->last_resend > RESEND_TO) {
            // stop if one fails
            if(!rtp_request_resend(ctx, ctx->ab_read + i, ctx->ab_read + i)) break;
            frame->last_resend = now;
        }
    }
}

/*---------------------------------------------------------------------------*/
#ifdef WIN32
static void* rtp_thread_func(void* arg) {
#else
static void rtp_thread_func(void* arg) {
#endif
    fd_set fds;
    int i, sock = -1;
    int count = 0;
    bool ntp_sent;
    char* packet = malloc(MAX_PACKET);
    rtp_t* ctx = (rtp_t*)arg;

    for(i = 0; i < 3; i++) {
        if(ctx->rtp_sockets[i].sock > sock) sock = ctx->rtp_sockets[i].sock;
        // send synchro request 3 times
        ntp_sent = rtp_request_timing(ctx);
    }

    while(ctx->running) {
        ssize_t plen;
        char type;
        socklen_t rtp_client_len = sizeof(struct sockaddr_in);
        int idx = 0;
        char* pktp = packet;
        struct timeval timeout = {0, 100 * 1000};

        FD_ZERO(&fds);
        for(i = 0; i < 3; i++) { FD_SET(ctx->rtp_sockets[i].sock, &fds); }

        if(select(sock + 1, &fds, NULL, NULL, &timeout) <= 0) {
            if(ctx->stalled++ == 30 * 10) ctx->cmd_cb(RAOP_STALLED);
            continue;
        }

        for(i = 0; i < 3; i++)
            if(FD_ISSET(ctx->rtp_sockets[i].sock, &fds)) idx = i;

        plen = recvfrom(ctx->rtp_sockets[idx].sock, packet, MAX_PACKET, MSG_DONTWAIT, (struct sockaddr*)&ctx->rtp_host, &rtp_client_len);

        if(!ntp_sent) {
            LOG_WARN("[%p]: NTP request not send yet", ctx);
            ntp_sent = rtp_request_timing(ctx);
        }

        if(plen <= 0) {
            LOG_WARN("Nothing received on a readable socket %d", plen);
            continue;
        }

        assert(plen <= MAX_PACKET);
        ctx->stalled = 0;

        type = packet[1] & ~0x80;
        pktp = packet;

        switch(type) {
            seq_t seqno;
            unsigned rtptime;

        // re-sent packet
        case 0x56: {
            pktp += 4;
            plen -= 4;
        }
        // fall through

        // data packet
        case 0x60: {
            seqno = ntohs(*(u16_t*)(pktp + 2));
            rtptime = ntohl(*(u32_t*)(pktp + 4));

            // adjust pointer and length
            pktp += 12;
            plen -= 12;

            LOG_SDEBUG("[%p]: seqno:%hu rtp:%u (type: %x, first: %u)", ctx, seqno, rtptime, type, packet[1] & 0x80);

            // check if packet contains enough content to be reasonable
            if(plen < 16) break;

            if((packet[1] & 0x80) && (type != 0x56)) { LOG_INFO("[%p]: 1st audio packet received", ctx); }

            buffer_put_packet(ctx, seqno, rtptime, packet[1] & 0x80, pktp, plen);

            break;
        }

        // sync packet
        case 0x54: {
            u32_t rtp_now_latency = ntohl(*(u32_t*)(pktp + 4));
            u64_t remote = (((u64_t)ntohl(*(u32_t*)(pktp + 8))) << 32) + ntohl(*(u32_t*)(pktp + 12));
            u32_t rtp_now = ntohl(*(u32_t*)(pktp + 16));
            u16_t flags = ntohs(*(u16_t*)(pktp + 2));
            u32_t remote_gap = NTP2MS(remote - ctx->timing.remote);

            // try to get NTP every 3 sec or every time if we are not synced
            if(!count-- || !(ctx->synchro.status & NTP_SYNC)) {
                rtp_request_timing(ctx);
                count = 3;
            }

            // something is wrong, we should not have such gap
            if(remote_gap > 10000) {
                LOG_WARN("discarding remote timing information %u", remote_gap);
                break;
            }

            pthread_mutex_lock(&ctx->ab_mutex);

            // re-align timestamp and expected local playback time (and magic 11025 latency)
            ctx->latency = rtp_now - rtp_now_latency;
            if(flags == 7 || flags == 4) ctx->latency += 11025;
            if(ctx->latency < MIN_LATENCY)
                ctx->latency = MIN_LATENCY;
            else if(ctx->latency > MAX_LATENCY)
                ctx->latency = MAX_LATENCY;
            ctx->synchro.rtp = rtp_now - ctx->latency;
            ctx->synchro.time = ctx->timing.local + remote_gap;

            // now we are synced on RTP frames
            ctx->synchro.status |= RTP_SYNC;

            // 1st sync packet received (signals a restart of playback)
            if(packet[0] & 0x10) { LOG_INFO("[%p]: 1st sync packet received", ctx); }

            pthread_mutex_unlock(&ctx->ab_mutex);

            LOG_DEBUG("[%p]: sync packet latency:%d rtp_latency:%u rtp:%u remote ntp:%llx, local time:%u local rtp:%u (now:%u)", ctx, ctx->latency,
                rtp_now_latency, rtp_now, remote, ctx->synchro.time, ctx->synchro.rtp, gettime_ms());

            if((ctx->synchro.status & RTP_SYNC) && (ctx->synchro.status & NTP_SYNC)) ctx->cmd_cb(RAOP_TIMING);

            break;
        }

        // NTP timing packet
        case 0x53: {
            u32_t reference = ntohl(*(u32_t*)(pktp + 12)); // only low 32 bits in our case
            u64_t remote = (((u64_t)ntohl(*(u32_t*)(pktp + 16))) << 32) + ntohl(*(u32_t*)(pktp + 20));
            u32_t roundtrip = gettime_ms() - reference;

            // better discard sync packets when roundtrip is suspicious
            if(roundtrip > 100) {
                // ask for another one only if we are not synced already
                if(!(ctx->synchro.status & NTP_SYNC)) rtp_request_timing(ctx);
                LOG_WARN("[%p]: discarding NTP roundtrip of %u ms", ctx, roundtrip);
                break;
            }

            /*
				  The expected elapsed remote time should be exactly the same as
				  elapsed local time between the two request, corrected by the
				  drifting
				u64_t expected = ctx->timing.remote + MS2NTP(reference - ctx->timing.local);
				*/

            ctx->timing.remote = remote;
            ctx->timing.local = reference;

            // now we are synced on NTP (mutex not needed)
            ctx->synchro.status |= NTP_SYNC;

            LOG_DEBUG("[%p]: Timing references local:%llu, remote:%llx (delta:%lld, sum:%lld, adjust:%lld, gaps:%d)", ctx, ctx->timing.local,
                ctx->timing.remote);

            break;
        }

        default: {
            LOG_WARN("Unknown packet received %x", (int)type);
            break;
        }
        }
    }

    free(packet);
    LOG_INFO("[%p]: terminating", ctx);

#ifndef WIN32
    xTaskNotifyGive(ctx->joiner);
    vTaskSuspend(NULL);
#else
    return NULL;
#endif
}

/*---------------------------------------------------------------------------*/
static bool rtp_request_timing(rtp_t* ctx) {
    unsigned char req[32];
    u32_t now = gettime_ms();
    int i;
    struct sockaddr_in host;

    LOG_DEBUG("[%p]: timing request now:%u (port: %hu)", ctx, now, ctx->rtp_sockets[TIMING].rport);

    req[0] = 0x80;
    req[1] = 0x52 | 0x80;
    *(u16_t*)(req + 2) = htons(7);
    *(u32_t*)(req + 4) = htonl(0); // dummy
    for(i = 0; i < 16; i++) req[i + 8] = 0;
    *(u32_t*)(req + 24) = 0;
    *(u32_t*)(req + 28) = htonl(now); // this is not a real NTP, but a 32 ms counter in the low part of the NTP

    if(ctx->host.s_addr != INADDR_ANY) {
        host.sin_family = AF_INET;
        host.sin_addr = ctx->host;
    } else
        host = ctx->rtp_host;

    // no address from sender, need to wait for 1st packet to be received
    if(host.sin_addr.s_addr == INADDR_ANY) return false;

    host.sin_port = htons(ctx->rtp_sockets[TIMING].rport);

    if(sizeof(req) != sendto(ctx->rtp_sockets[TIMING].sock, req, sizeof(req), MSG_DONTWAIT, (struct sockaddr*)&host, sizeof(host))) {
        LOG_WARN("[%p]: SENDTO failed (%s)", ctx, strerror(errno));
    }

    return true;
}

/*---------------------------------------------------------------------------*/
static bool rtp_request_resend(rtp_t* ctx, seq_t first, seq_t last) {
    unsigned char req[8]; // *not* a standard RTCP NACK

    // do not request silly ranges (happens in case of network large blackouts)
    if(seq_order(last, first) || last - first > buffer_frames / 2) return false;

    ctx->resent_req += (seq_t)(last - first) + 1;

    LOG_DEBUG("resend request [W:%hu R:%hu first=%hu last=%hu]", ctx->ab_write, ctx->ab_read, first, last);

    req[0] = 0x80;
    req[1] = 0x55 | 0x80;                         // Apple 'resend'
    *(u16_t*)(req + 2) = htons(1);                // our seqnum
    *(u16_t*)(req + 4) = htons(first);            // missed seqnum
    *(u16_t*)(req + 6) = htons(last - first + 1); // count

    ctx->rtp_host.sin_port = htons(ctx->rtp_sockets[CONTROL].rport);

    if(sizeof(req) !=
        sendto(ctx->rtp_sockets[CONTROL].sock, req, sizeof(req), MSG_DONTWAIT, (struct sockaddr*)&ctx->rtp_host, sizeof(ctx->rtp_host))) {
        LOG_WARN("[%p]: SENDTO failed (%s)", ctx, strerror(errno));
        return false;
    }

    return true;
}
