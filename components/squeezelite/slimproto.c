/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additions (c) Paul Hermann, 2015-2017 under the same license terms
 *   -Control of Raspberry pi GPIO for amplifier power
 *   -Launch script on power status change from LMS
 */

#include "squeezelite.h"
#include "slimproto.h"

static log_level loglevel;

#define SQUEEZENETWORK "mysqueezebox.com:3483"

#define PORT 3483

#define MAXBUF 4096

#if SL_LITTLE_ENDIAN
#define LOCAL_PLAYER_IP   0x0100007f // 127.0.0.1
#define LOCAL_PLAYER_PORT 0x9b0d     // 3483
#else
#define LOCAL_PLAYER_IP   0x7f000001 // 127.0.0.1
#define LOCAL_PLAYER_PORT 0x0d9b     // 3483
#endif

static sockfd sock = -1;
static in_addr_t slimproto_ip = 0;
static u16_t slimproto_hport = 9000;
static u16_t slimproto_cport = 9090;
static u8_t	player_id;
bool (*slimp_handler)(u8_t *data, int len) = NULL;
void (*slimp_loop)(void) = NULL;
void (*server_notify)(in_addr_t ip, u16_t hport, u16_t cport) = NULL;

extern struct buffer *streambuf;
extern struct buffer *outputbuf;

extern struct streamstate stream;
extern struct outputstate output;
extern struct decodestate decode;

extern struct codec *codecs[];
#if IR
extern struct irstate ir;
#endif

event_event wake_e;

#define LOCK_S   mutex_lock(streambuf->mutex)
#define UNLOCK_S mutex_unlock(streambuf->mutex)
#define LOCK_O   mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#define LOCK_D   mutex_lock(decode.mutex)
#define UNLOCK_D mutex_unlock(decode.mutex)
#if !EMBEDDED
#define LOCK_P
#define UNLOCK_P
#endif 
#if IR
#define LOCK_I   mutex_lock(ir.mutex)
#define UNLOCK_I mutex_unlock(ir.mutex)
#endif

static struct {
	u32_t updated;
	u32_t stream_start;
	u32_t stream_full;
	u32_t stream_size;
	u64_t stream_bytes;
	u32_t output_full;
	u32_t output_size;
	u32_t frames_played;
	u32_t device_frames;
	u32_t current_sample_rate;
	u32_t last;
	stream_state stream_state;
} status;

static int autostart;
static bool sentSTMu, sentSTMo, sentSTMl;
static u32_t new_server;
static char *new_server_cap;
#define PLAYER_NAME_LEN 64
static char player_name[PLAYER_NAME_LEN + 1] = "";
static const char *name_file = NULL;

void slimproto_send_packet(u8_t *packet, size_t len) {
	u8_t *ptr = packet;
	unsigned try = 0;
	ssize_t n;

	while (len) {
		n = send(sock, ptr, len, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && last_error() == ERROR_WOULDBLOCK && try < 10) {
				LOG_DEBUG("retrying (%d) writing to socket", ++try);
				usleep(1000);
				continue;
			}
			LOG_INFO("failed writing to socket: %s", strerror(last_error()));
			return;
		}
		ptr += n;
		len -= n;
	}
}

static void sendHELO(bool reconnect, const char *fixed_cap, const char *var_cap, u8_t mac[6]) {
#ifndef BASE_CAP	
#define BASE_CAP "Model=squeezelite,AccuratePlayPoints=1,HasDigitalOut=1,HasPolarityInversion=1,Balance=1,Firmware=" VERSION
#endif	
	#define SSL_CAP "CanHTTPS=1"
	const char *base_cap;
	struct HELO_packet pkt;
	
#if USE_SSL
#if !LINKALL && !NO_SSLSYM
	if (ssl_loaded) base_cap = SSL_CAP "," BASE_CAP;
	else base_cap = BASE_CAP;
#endif	
	base_cap = SSL_CAP "," BASE_CAP;
#else
	base_cap = BASE_CAP;
#endif	

	if (!reconnect) player_id = PLAYER_ID;

	memset(&pkt, 0, sizeof(pkt));
	memcpy(&pkt.opcode, "HELO", 4);
	pkt.length = htonl(sizeof(struct HELO_packet) - 8 + strlen(base_cap) + strlen(fixed_cap) + strlen(var_cap));
	pkt.deviceid = player_id;
	pkt.revision = 0;
	packn(&pkt.wlan_channellist, reconnect ? 0x4000 : 0x0000);
	packN(&pkt.bytes_received_H, (u64_t)status.stream_bytes >> 32);
	packN(&pkt.bytes_received_L, (u64_t)status.stream_bytes & 0xffffffff);
	memcpy(pkt.mac, mac, 6);

	LOG_INFO("mac: %02x:%02x:%02x:%02x:%02x:%02x", pkt.mac[0], pkt.mac[1], pkt.mac[2], pkt.mac[3], pkt.mac[4], pkt.mac[5]);

	LOG_INFO("cap: %s%s%s", base_cap, fixed_cap, var_cap);
	LOCK_P;
	send_packet((u8_t *)&pkt, sizeof(pkt));
	send_packet((u8_t *)base_cap, strlen(base_cap));
	send_packet((u8_t *)fixed_cap, strlen(fixed_cap));
	send_packet((u8_t *)var_cap, strlen(var_cap));
	UNLOCK_P;
}

static void sendSTAT(const char *event, u32_t server_timestamp) {
	struct STAT_packet pkt;
	u32_t now = gettime_ms();
	u32_t ms_played;

	if (status.current_sample_rate && status.frames_played && status.frames_played > status.device_frames) {
		ms_played = (u32_t)(((u64_t)(status.frames_played - status.device_frames) * (u64_t)1000) / (u64_t)status.current_sample_rate);
		if (now > status.updated) ms_played += (now - status.updated);
		LOG_SDEBUG("ms_played: %u (frames_played: %u device_frames: %u)", ms_played, status.frames_played, status.device_frames);
	} else if (status.frames_played && now > status.stream_start) {
		ms_played = now - status.stream_start;
		LOG_SDEBUG("ms_played: %u using elapsed time (frames_played: %u device_frames: %u)", ms_played, status.frames_played, status.device_frames);
	} else {
		LOG_SDEBUG("ms_played: 0");
		ms_played = 0;
	}

	memset(&pkt, 0, sizeof(struct STAT_packet));
	memcpy(&pkt.opcode, "STAT", 4);
	pkt.length = htonl(sizeof(struct STAT_packet) - 8);
	memcpy(&pkt.event, event, 4);
	// num_crlf
	// mas_initialized; mas_mode;
	packN(&pkt.stream_buffer_fullness, status.stream_full);
	packN(&pkt.stream_buffer_size, status.stream_size);
	packN(&pkt.bytes_received_H, (u64_t)status.stream_bytes >> 32);
	packN(&pkt.bytes_received_L, (u64_t)status.stream_bytes & 0xffffffff);
#if EMBEDDED
	packn(&pkt.signal_strength, get_RSSI());
	packn(&pkt.voltage, (get_battery() << 4) | get_plugged());
#else 
	pkt.signal_strength = 0xffff;
#endif	
	packN(&pkt.jiffies, now);
	packN(&pkt.output_buffer_size, status.output_size);
	packN(&pkt.output_buffer_fullness, status.output_full);
	packN(&pkt.elapsed_seconds, ms_played / 1000);
	packN(&pkt.elapsed_milliseconds, ms_played);
	pkt.server_timestamp = server_timestamp; // keep this is server format - don't unpack/pack
	// error_code;

	LOG_DEBUG("STAT: %s", event);

	if (loglevel == lSDEBUG) {
		LOG_SDEBUG("received bytesL: %u streambuf: %u outputbuf: %u calc elapsed: %u real elapsed: %u (diff: %d) device: %u delay: %d",
				   (u32_t)status.stream_bytes, status.stream_full, status.output_full, ms_played, now - status.stream_start,
				   ms_played - now + status.stream_start, status.device_frames * 1000 / status.current_sample_rate, now - status.updated);
	}

	LOCK_P;
	send_packet((u8_t *)&pkt, sizeof(pkt));
	UNLOCK_P;
}

static void sendDSCO(disconnect_code disconnect) {
	struct DSCO_packet pkt;

	memset(&pkt, 0, sizeof(pkt));
	memcpy(&pkt.opcode, "DSCO", 4);
	pkt.length = htonl(sizeof(pkt) - 8);
	pkt.reason = disconnect & 0xFF;

	LOG_DEBUG("DSCO: %d", disconnect);

	LOCK_P;
	send_packet((u8_t *)&pkt, sizeof(pkt));
	UNLOCK_P;
}

static void sendRESP(const char *header, size_t len) {
	struct RESP_header pkt_header;

	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "RESP", 4);
	pkt_header.length = htonl(sizeof(pkt_header) + len - 8);

	LOG_DEBUG("RESP");
	
	LOCK_P;
	send_packet((u8_t *)&pkt_header, sizeof(pkt_header));
	send_packet((u8_t *)header, len);
	UNLOCK_P;
}

static void sendMETA(const char *meta, size_t len) {
	struct META_header pkt_header;

	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "META", 4);
	pkt_header.length = htonl(sizeof(pkt_header) + len - 8);

	LOG_DEBUG("META");

	LOCK_P;
	send_packet((u8_t *)&pkt_header, sizeof(pkt_header));
	send_packet((u8_t *)meta, len);
	UNLOCK_P;
}

static void sendSETDName(const char *name) {
	struct SETD_header pkt_header;

	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "SETD", 4);

	pkt_header.id = 0; // id 0 is playername S:P:Squeezebox2
	pkt_header.length = htonl(sizeof(pkt_header) + strlen(name) + 1 - 8);

	LOG_DEBUG("set playername: %s", name);

	LOCK_P;
	send_packet((u8_t *)&pkt_header, sizeof(pkt_header));
	send_packet((u8_t *)name, strlen(name) + 1);
	UNLOCK_P;
}

#if IR
void sendIR(u32_t code, u32_t ts) {
	struct IR_packet pkt;

	memset(&pkt, 0, sizeof(pkt));
	memcpy(&pkt.opcode, "IR  ", 4);
	pkt.length = htonl(sizeof(pkt) - 8);

	packN(&pkt.jiffies, ts);
	pkt.ir_code = htonl(code);

	LOG_DEBUG("IR: ir code: 0x%x ts: %u", code, ts);
	LOCK_P;
	send_packet((u8_t *)&pkt, sizeof(pkt));
	UNLOCK_P;
}
#endif

static void process_strm(u8_t *pkt, int len) {
	struct strm_packet *strm = (struct strm_packet *)pkt;

	LOG_DEBUG("strm command %c", strm->command);
	
	switch(strm->command) {
	case 't':
		sendSTAT("STMt", strm->replay_gain); // STMt replay_gain is no longer used to track latency, but support it
		break;
	case 'f':	
	case 'q':
		decode_flush(strm->command == 'q');
		if (!output.external) output_flush();
		status.frames_played = 0;
		if (stream_disconnect() && strm->command == 'f') sendSTAT("STMf", 0);
		buf_flush(streambuf);
		output.stop_time = gettime_ms();
		break;
	case 'p':
		{
			unsigned interval = unpackN(&strm->replay_gain);
			LOCK_O;
			output.pause_frames = interval * status.current_sample_rate / 1000;
			if (interval) {
				output.state = OUTPUT_PAUSE_FRAMES;
			} else if (output.state != OUTPUT_OFF) {
				output.state = OUTPUT_STOPPED;
				output.stop_time = gettime_ms();
			}
			UNLOCK_O;
			if (!interval) sendSTAT("STMp", 0);
			LOG_DEBUG("pause interval: %u", interval);
		}
		break;
	case 'a':
		{
			unsigned interval = unpackN(&strm->replay_gain);
			LOCK_O;
			output.skip_frames = interval * status.current_sample_rate / 1000;
			output.state = OUTPUT_SKIP_FRAMES;				
			UNLOCK_O;
			LOG_DEBUG("skip ahead interval: %u", interval);
		}
		break;
	case 'u':
		{
			unsigned jiffies = unpackN(&strm->replay_gain);
			LOCK_O;
			output.state = jiffies ? OUTPUT_START_AT : OUTPUT_RUNNING;
			output.start_at = jiffies;
			UNLOCK_O;

			LOG_DEBUG("unpause at: %u now: %u", jiffies, gettime_ms());
			sendSTAT("STMr", 0);
		}
		break;
	case 's':
		{
			unsigned header_len = len - sizeof(struct strm_packet);
			char *header = (char *)(pkt + sizeof(struct strm_packet));
			in_addr_t ip = (in_addr_t)strm->server_ip; // keep in network byte order
			u16_t port = strm->server_port; // keep in network byte order
			if (ip == 0) ip = slimproto_ip; 

			LOG_DEBUG("strm s autostart: %c transition period: %u transition type: %u codec: %c", 
					  strm->autostart, strm->transition_period, strm->transition_type - '0', strm->format);
			
			autostart = strm->autostart - '0';

			sendSTAT("STMf", 0);
			if (header_len > MAX_HEADER -1) {
				LOG_WARN("header too long: %u", header_len);
				break;
			}
			if (strm->format != '?') {
				codec_open(strm->format, strm->pcm_sample_size, strm->pcm_sample_rate, strm->pcm_channels, strm->pcm_endianness);
			} else if (autostart >= 2) {
				// extension to slimproto to allow server to detect codec from response header and send back in codc message
				LOG_DEBUG("streaming unknown codec");
			} else {
				LOG_WARN("unknown codec requires autostart >= 2");
				break;
			}
			if (ip == LOCAL_PLAYER_IP && port == LOCAL_PLAYER_PORT) {
				// extension to slimproto for LocalPlayer - header is filename not http header, don't expect cont
				stream_file(header, header_len, strm->threshold * 1024);
				autostart -= 2;
			} else {
				stream_sock(ip, port, header, header_len, strm->threshold * 1024, autostart >= 2);
			}
			sendSTAT("STMc", 0);
			sentSTMu = sentSTMo = sentSTMl = false;
#if EMBEDDED
			// not protected so that restore can call synchronously sink handlers
			if (output.external) decode_restore(output.external);
			LOCK_O;
			output.external = 0;
			_buf_limit(outputbuf, 0);
#else
			LOCK_O;
#endif
			output.threshold = strm->output_threshold;
			output.next_replay_gain = unpackN(&strm->replay_gain);
			output.fade_mode = strm->transition_type - '0';
			output.fade_secs = strm->transition_period;
			output.invert = (strm->flags & 0x03) == 0x03;
			output.channels = (strm->flags & 0x0c) >> 2;
			LOG_DEBUG("set fade: %u, channels: %u, invert: %u", output.fade_mode, output.channels, output.invert);
			UNLOCK_O;
		}
		break;
	default:
		LOG_WARN("unhandled strm %c", strm->command);
		break;
	}
}

static void process_cont(u8_t *pkt, int len) {
	struct cont_packet *cont = (struct cont_packet *)pkt;
	cont->metaint = unpackN(&cont->metaint);

	LOG_DEBUG("cont metaint: %u loop: %u", cont->metaint, cont->loop);

	if (autostart > 1) {
		autostart -= 2;
		LOCK_S;
		if (stream.state == STREAMING_WAIT) {
			stream.state = STREAMING_BUFFERING;
			stream.meta_interval = stream.meta_next = cont->metaint;
		}
		UNLOCK_S;
		wake_controller();
	}
}

static void process_codc(u8_t *pkt, int len) {
	struct codc_packet *codc = (struct codc_packet *)pkt;

	LOG_DEBUG("codc: %c", codc->format);
	codec_open(codc->format, codc->pcm_sample_size, codc->pcm_sample_rate, codc->pcm_channels, codc->pcm_endianness);
}

static void process_aude(u8_t *pkt, int len) {
	struct aude_packet *aude = (struct aude_packet *)pkt;

	LOG_DEBUG("enable spdif: %d dac: %d", aude->enable_spdif, aude->enable_dac);
    
#if EMBEDDED
    powering(aude->enable_spdif),
#endif    

	LOCK_O;
	if (!aude->enable_spdif && output.state != OUTPUT_OFF) {
		output.state = OUTPUT_OFF;
	}
	if (aude->enable_spdif && output.state == OUTPUT_OFF && !output.idle_to) {
		output.state = OUTPUT_STOPPED;
		output.stop_time = gettime_ms();
	}
	UNLOCK_O;
}

static void process_audg(u8_t *pkt, int len) {
	struct audg_packet *audg = (struct audg_packet *)pkt;
	audg->gainL = unpackN(&audg->gainL);
	audg->gainR = unpackN(&audg->gainR);

	LOG_DEBUG("audg gainL: %u gainR: %u adjust: %u", audg->gainL, audg->gainR, audg->adjust);

	set_volume(audg->adjust ? audg->gainL : FIXED_ONE, audg->adjust ? audg->gainR : FIXED_ONE);
}

static void process_dsco(u8_t *pkt, int len) {
	LOG_INFO("got DSCO, switching from id %u to 12", (int) player_id);
	player_id = 12;
}

static void process_setd(u8_t *pkt, int len) {
	struct setd_packet *setd = (struct setd_packet *)pkt;

	// handle player name query and change
	if (setd->id == 0) {
		if (len == 5) {
			if (strlen(player_name)) {
				sendSETDName(player_name);
			}
		} else if (len > 5) {
			strncpy(player_name, setd->data, PLAYER_NAME_LEN);
			player_name[PLAYER_NAME_LEN] = '\0';
			LOG_INFO("set name: %s", setd->data);
			// confirm change to server
			sendSETDName(setd->data);
#if EMBEDDED
			set_name(player_name);
#endif			
			// write name to name_file if -N option set
			if (name_file) {
				FILE *fp = fopen(name_file, "w");
				if (fp) {
					LOG_INFO("storing name in %s", name_file);
					fputs(player_name, fp);
					fclose(fp);
				} else {
					LOG_WARN("unable to store new name in %s", name_file);
				}
			}
		}
	}
}

#define SYNC_CAP ",SyncgroupID="
#define SYNC_CAP_LEN 13

static void process_serv(u8_t *pkt, int len) {
	struct serv_packet *serv = (struct serv_packet *)pkt;

	unsigned slimproto_port = 0;
	char squeezeserver[] = SQUEEZENETWORK;
	
	if(pkt[4] == 0 && pkt[5] == 0 && pkt[6] == 0 && pkt[7] == 1) {
		server_addr(squeezeserver, &new_server, &slimproto_port);
	} else {
		new_server = serv->server_ip;
	}

	LOG_INFO("switch server");

	if (len - sizeof(struct serv_packet) == 10) {
		if (!new_server_cap) {
			new_server_cap = malloc(SYNC_CAP_LEN + 10 + 1);
		}
		new_server_cap[0] = '\0';
		strcat(new_server_cap, SYNC_CAP);
		strncat(new_server_cap, (const char *)(pkt + sizeof(struct serv_packet)), 10);
	} else {
		if (new_server_cap) {
			free(new_server_cap);
			new_server_cap = NULL;
		}
	}		
}

struct handler {
	char opcode[5];
	void (*handler)(u8_t *, int);
};

static struct handler handlers[] = {
	{ "strm", process_strm },
	{ "cont", process_cont },
	{ "codc", process_codc },
	{ "aude", process_aude },
	{ "audg", process_audg },
	{ "setd", process_setd },
	{ "serv", process_serv },
	{ "dsco", process_dsco },
	{ "",     NULL  },
};

static void process(u8_t *pack, int len) {
	struct handler *h = handlers;
	while (h->handler && strncmp((char *)pack, h->opcode, 4)) { h++; }

	if (h->handler) {
		LOG_DEBUG("%s", h->opcode);
		h->handler(pack, len);
	} else if (!slimp_handler || !(*slimp_handler)(pack, len)) {
		pack[4] = '\0';
		LOG_WARN("unhandled %s", (char *)pack);
	}
}

static bool running;

static void slimproto_run() {
	static u8_t EXT_BSS buffer[MAXBUF];
	int  expect = 0;
	int  got    = 0;
	u32_t now;
	static u32_t last = 0;
	event_handle ehandles[2];
	int timeouts = 0;
	
	set_readwake_handles(ehandles, sock, wake_e);

	while (running && !new_server) {

		bool wake = false;
		event_type ev;

		if ((ev = wait_readwake(ehandles, 1000)) != EVENT_TIMEOUT) {
	
			if (ev == EVENT_READ) {

				if (expect > 0) {
					int n = recv(sock, buffer + got, expect, 0);
					if (n <= 0) {
						if (n < 0 && last_error() == ERROR_WOULDBLOCK) {
							continue;
						}
						LOG_INFO("error reading from socket: %s", n ? strerror(last_error()) : "closed");
						return;
					}
					expect -= n;
					got += n;
					if (expect == 0) {
						process(buffer, got);
						got = 0;
					}
				} else if (expect == 0) {
					int n = recv(sock, buffer + got, 2 - got, 0);
					if (n <= 0) {
						if (n < 0 && last_error() == ERROR_WOULDBLOCK) {
							continue;
						}
						LOG_INFO("error reading from socket: %s", n ? strerror(last_error()) : "closed");
						return;
					}
					got += n;
					if (got == 2) {
						expect = buffer[0] << 8 | buffer[1]; // length pack 'n'
						got = 0;
						if (expect > MAXBUF) {
							LOG_ERROR("FATAL: slimproto packet too big: %d > %d", expect, MAXBUF);
							return;
						}
					}
				} else {
					LOG_ERROR("FATAL: negative expect");
					return;
				}

			}

			if (ev == EVENT_WAKE) {
				wake = true;
			}

			timeouts = 0;

		} else if (++timeouts > 35) {

			// expect message from server every 5 seconds, but 30 seconds on mysb.com so timeout after 35 seconds
			LOG_INFO("No messages from server - connection dead");
			return;
		}

		// update playback state when woken or every 100ms
		now = gettime_ms();

		if (wake || now - last > 100 || last > now) {
			bool _sendSTMs = false;
			bool _sendDSCO = false;
			bool _sendRESP = false;
			bool _sendMETA = false;
			bool _sendSTMd = false;
			bool _sendSTMt = false;
			bool _sendSTMl = false;
			bool _sendSTMu = false;
			bool _sendSTMo = false;
			bool _sendSTMn = false;
			bool _stream_disconnect = false;
			bool _start_output = false;
			decode_state _decode_state;
			disconnect_code disconnect_code;
			static char EXT_BSS header[MAX_HEADER];
			size_t header_len = 0;
#if IR
			bool _sendIR   = false;
			u32_t ir_code, ir_ts;
#endif
			
			last = now;

			LOCK_S;
			status.stream_full = _buf_used(streambuf);
			status.stream_size = streambuf->size;
			status.stream_bytes = stream.bytes;
			status.stream_state = stream.state;
						
			if (stream.state == DISCONNECT) {
				disconnect_code = stream.disconnect;
				stream.state = STOPPED;
				_sendDSCO = true;
			}
			if (!stream.sent_headers && 
				(stream.state == STREAMING_HTTP || stream.state == STREAMING_WAIT || stream.state == STREAMING_BUFFERING)) {
				header_len = stream.header_len;
				memcpy(header, stream.header, header_len);
				_sendRESP = true;
				stream.sent_headers = true;
			}
			if (stream.meta_send) {
				header_len = stream.header_len;
				memcpy(header, stream.header, header_len);
				_sendMETA = true;
				stream.meta_send = false;
			}
			UNLOCK_S;

			LOCK_D;
			if ((status.stream_state == STREAMING_HTTP || status.stream_state == STREAMING_FILE ||
				(status.stream_state == DISCONNECT && stream.disconnect == DISCONNECT_OK)) &&
				!sentSTMl && decode.state == DECODE_READY) {
				if (autostart == 0) {
					decode.state = DECODE_RUNNING;
					_sendSTMl = true;
					sentSTMl = true;
				} else if (autostart == 1) {
					decode.state = DECODE_RUNNING;
					_start_output = true;
				}
				// autostart 2 and 3 require cont to be received first
			}
			if (decode.state == DECODE_COMPLETE || decode.state == DECODE_ERROR) {
				if (decode.state == DECODE_COMPLETE) _sendSTMd = true;
				if (decode.state == DECODE_ERROR)    _sendSTMn = true;
				decode.state = DECODE_STOPPED;
				if (status.stream_state == STREAMING_HTTP || status.stream_state == STREAMING_FILE) {
					_stream_disconnect = true;
				}
			}
			_decode_state = decode.state;
			UNLOCK_D;
			
			LOCK_O;
			if (!output.external) {
				status.output_full = _buf_used(outputbuf);
				status.output_size = outputbuf->size;
				status.frames_played = output.frames_played_dmp;
				status.current_sample_rate = output.current_sample_rate;
				status.updated = output.updated;
				status.device_frames = output.device_frames;
									
				if (output.track_started) {
					_sendSTMs = true;
					output.track_started = false;
					status.stream_start = output.track_start_time;
				}
#if PORTAUDIO
				if (output.pa_reopen) {
					_pa_open();
					output.pa_reopen = false;
				}
#endif
				if (_start_output && (output.state == OUTPUT_STOPPED || output.state == OUTPUT_OFF)) {
					output.state = OUTPUT_BUFFER;
				}
				if (output.state == OUTPUT_RUNNING && !sentSTMu && status.output_full == 0 && status.stream_state <= DISCONNECT &&
					_decode_state == DECODE_STOPPED) {

					_sendSTMu = true;
					sentSTMu = true;
					LOG_DEBUG("output underrun");
					output.state = OUTPUT_STOPPED;
					output.stop_time = now;
				}
				if (output.state == OUTPUT_RUNNING && !sentSTMo && status.output_full == 0 && status.stream_state == STREAMING_HTTP) {
					_sendSTMo = true;
					sentSTMo = true;
				}
			}	
			if (output.state == OUTPUT_STOPPED && output.idle_to && (now - output.stop_time > output.idle_to)) {
				output.state = OUTPUT_OFF;
				LOG_DEBUG("output timeout");
			}			
			if (output.state == OUTPUT_RUNNING && now - status.last > 1000) {
				_sendSTMt = true;
				status.last = now;
			}
			UNLOCK_O;

#if IR
			LOCK_I;
			if (ir.code) {
				_sendIR = true;
				ir_code = ir.code;
				ir_ts   = ir.ts;
				ir.code = 0;
			}
			UNLOCK_I;
#endif

			if (_stream_disconnect) stream_disconnect();

			// send packets once locks released as packet sending can block
			if (_sendDSCO) sendDSCO(disconnect_code);
			if (_sendSTMs) sendSTAT("STMs", 0);
			if (_sendSTMd) sendSTAT("STMd", 0);
			if (_sendSTMt) sendSTAT("STMt", 0);
			if (_sendSTMl) sendSTAT("STMl", 0);
			if (_sendSTMu) sendSTAT("STMu", 0);
			if (_sendSTMo) sendSTAT("STMo", 0);
			if (_sendSTMn) sendSTAT("STMn", 0);
			if (_sendRESP) sendRESP(header, header_len);
			if (_sendMETA) sendMETA(header, header_len);
#if IR
			if (_sendIR)   sendIR(ir_code, ir_ts);
#endif
			if (*slimp_loop) (*slimp_loop)();
		}
	}
}

// called from other threads to wake state machine above
void wake_controller(void) {
	wake_signal(wake_e);
}

in_addr_t discover_server(char *default_server, int max) {
	struct sockaddr_in d;
	struct sockaddr_in s;
	char buf[32], port_d[] = "JSON", clip_d[] = "CLIP";
	struct pollfd pollinfo;
	unsigned port;
	u8_t len;

	int disc_sock = socket(AF_INET, SOCK_DGRAM, 0);

	socklen_t enable = 1;
	setsockopt(disc_sock, SOL_SOCKET, SO_BROADCAST, (const void *)&enable, sizeof(enable));

	len = sprintf(buf,"e%s%c%s", port_d, '\0', clip_d) + 1;

	memset(&d, 0, sizeof(d));
	d.sin_family = AF_INET;
	d.sin_port = htons(PORT);
	d.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	pollinfo.fd = disc_sock;
	pollinfo.events = POLLIN;

	do {

		LOG_INFO("sending discovery %u", max);
		memset(&s, 0, sizeof(s));

		if (sendto(disc_sock, buf, len, 0, (struct sockaddr *)&d, sizeof(d)) < 0) {
			LOG_INFO("error sending discovery");
		}

		if (poll(&pollinfo, 1, 5000) == 1) {
			char readbuf[64], *p;
			socklen_t slen = sizeof(s);
			memset(readbuf, 0, sizeof(readbuf));
			recvfrom(disc_sock, readbuf, sizeof(readbuf) - 1, 0, (struct sockaddr *)&s, &slen);
			LOG_INFO("got response from: %s:%d", inet_ntoa(s.sin_addr), ntohs(s.sin_port));

			 if ((p = strstr(readbuf, port_d)) != NULL) {
				p += strlen(port_d);
				slimproto_hport = atoi(p + 1);
			}

			 if ((p = strstr(readbuf, clip_d)) != NULL) {
				p += strlen(clip_d);
				slimproto_cport = atoi(p + 1);
			}
		}

		if (default_server) {
			server_addr(default_server, &s.sin_addr.s_addr, &port);
		}

	} while (s.sin_addr.s_addr == 0 && running && (!max || --max));

	closesocket(disc_sock);

	return s.sin_addr.s_addr;
}

#define FIXED_CAP_LEN 256
#define VAR_CAP_LEN   128

void slimproto(log_level level, char *server, u8_t mac[6], const char *name, const char *namefile, const char *modelname, int maxSampleRate) {
	struct sockaddr_in serv_addr;
	static char fixed_cap[FIXED_CAP_LEN], var_cap[VAR_CAP_LEN] = "";
	bool reconnect = false;
	unsigned failed_connect = 0;
	unsigned slimproto_port = 0;
	in_addr_t previous_server = 0;
	int i;

	memset(&status, 0, sizeof(status));

	wake_create(wake_e);

	loglevel = level;
	running = true;

	if (server) {
		server_addr(server, &slimproto_ip, &slimproto_port);
	}

	if (!slimproto_ip) {
#if EMBEDDED        
        // on first attempt, try really hard to connect before exiting (and only exit if we are not on another sink)
		slimproto_ip = discover_server(server, MAX_SERVER_RETRIES * 5);
        if (!slimproto_ip && !output.external) return;
#else        
    	slimproto_ip = discover_server(server, 0);
#endif    
	}

	if (!slimproto_port) {
		slimproto_port = PORT;
	}

	if (name) {
		strncpy(player_name, name, PLAYER_NAME_LEN);
		player_name[PLAYER_NAME_LEN] = '\0';
	}

	if (namefile) {
		FILE *fp;
		name_file = namefile;
		fp = fopen(namefile, "r");
		if (fp) {
			if (!fgets(player_name, PLAYER_NAME_LEN, fp)) {
				player_name[PLAYER_NAME_LEN] = '\0';
			} else {
				// strip any \n from fgets response
				int len = strlen(player_name);
				if (len > 0 && player_name[len - 1] == '\n') {
					player_name[len - 1] = '\0';
				}
				LOG_INFO("retrieved name %s from %s", player_name, name_file);
			}
			fclose(fp);
		}
	}

	if (!running) return;

	LOCK_O;
	snprintf(fixed_cap, FIXED_CAP_LEN, ",ModelName=%s,MaxSampleRate=%u", modelname ? modelname : MODEL_NAME_STRING,
#if RESAMPLE || RESAMPLE16	
			 ((maxSampleRate > 0) ? maxSampleRate : output.supported_rates[0]));
#else
			 ((maxSampleRate > 0 && maxSampleRate < output.supported_rates[0]) ? maxSampleRate : output.supported_rates[0]));
#endif
	
	for (i = 0; i < MAX_CODECS; i++) {
		if (codecs[i] && codecs[i]->id && strlen(fixed_cap) < FIXED_CAP_LEN - 10) {
			strcat(fixed_cap, ",");
			strcat(fixed_cap, codecs[i]->types);
		}
	}
	UNLOCK_O;

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = slimproto_ip;
	serv_addr.sin_port = htons(slimproto_port);

	LOG_INFO("connecting to %s:%d", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

	new_server = 0;

	while (running) {

		if (new_server) {
			previous_server = slimproto_ip;
			slimproto_ip = serv_addr.sin_addr.s_addr = new_server;
			LOG_INFO("switching server to %s:%d", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
			new_server = 0;
			reconnect = false;
		}

		sock = socket(AF_INET, SOCK_STREAM, 0);

		set_nonblock(sock);
		set_nosigpipe(sock);

		if (connect_timeout(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr), 5) != 0) {

			if (previous_server) {
				slimproto_ip = serv_addr.sin_addr.s_addr = previous_server;
				LOG_INFO("new server not reachable, reverting to previous server %s:%d", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
			} else {
				LOG_INFO("unable to connect to server %u", failed_connect);
				sleep(5);
			}

#if EMBEDDED
			// in embedded we give up after a while no matter what
			if (++failed_connect > MAX_SERVER_RETRIES && !server) {
				slimproto_ip = serv_addr.sin_addr.s_addr = discover_server(NULL, MAX_SERVER_RETRIES);
				if (!slimproto_ip) return;
			} else if (reconnect && MAX_SERVER_RETRIES && failed_connect > 5 * MAX_SERVER_RETRIES) return;
#else
			// rediscover server if it was not set at startup or exit 
			if (!server && ++failed_connect > 5) {
				slimproto_ip = serv_addr.sin_addr.s_addr = discover_server(NULL, 0);
			} 
#endif	

		} else {

			struct sockaddr_in our_addr;
			socklen_t len;

			LOG_INFO("connected");

			var_cap[0] = '\0';
			failed_connect = 0;

			// check if this is a local player now we are connected & signal to server via 'loc' format
			// this requires LocalPlayer server plugin to enable direct file access
			len = sizeof(our_addr);
			getsockname(sock, (struct sockaddr *) &our_addr, &len);

			if (our_addr.sin_addr.s_addr == serv_addr.sin_addr.s_addr) {
				LOG_INFO("local player");
				strcat(var_cap, ",loc");
			}

			// add on any capablity to be sent to the new server
			if (new_server_cap) {
				strcat(var_cap, new_server_cap);
				free(new_server_cap);
				new_server_cap = NULL;
			}

			sendHELO(reconnect, fixed_cap, var_cap, mac);

#if EMBEDDED
			if (server_notify) (*server_notify)(slimproto_ip, slimproto_hport, slimproto_cport);
#endif

			slimproto_run();

			if (!reconnect) {
				reconnect = true;
			}

			usleep(100000);
		}

		previous_server = 0;

		closesocket(sock);
	}
}

void slimproto_stop(void) {
	LOG_INFO("slimproto stop");
	running = false;
}
