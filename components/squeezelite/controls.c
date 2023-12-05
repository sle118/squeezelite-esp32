/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include "squeezelite.h"
// #include "Configurator.h"
#pragma message("fixme: look for TODO below")
#include "audio_controls.h"

static log_level loglevel = lINFO;

#define DOWN_OFS	0x10000
#define UP_OFS		0x20000

// numbers are simply 0..9 but are not used

// arrow_right.down	= 0001000e seems to be missing ...
enum { 	BUTN_POWER_FRONT = 0X0A, BUTN_ARROW_UP, BUTN_ARROW_DOWN, BUTN_ARROW_LEFT, BUTN_KNOB_PUSH, BUTN_SEARCH,
		BUTN_REW, BUTN_FWD, BUTN_PLAY, BUTN_ADD, BUTN_BRIGHTNESS, BUTN_NOW_PLAYING,
		BUTN_PAUSE = 0X17, BUTN_BROWSE, BUTN_VOLUP_FRONT, BUTN_VOLDOWN_FRONT, BUTN_SIZE, BUTN_VISUAL, BUTN_VOLUMEMODE,
		BUTN_PRESET_0 = 0x22, BUTN_PRESET_1, BUTN_PRESET_2, BUTN_PRESET_3, BUTN_PRESET_4, BUTN_PRESET_5, BUTN_PRESET_6, BUTN_PRESET_7, BUTN_PRESET_8, BUTN_PRESET_9, 
        BUTN_SNOOZE,
		BUTN_KNOB_LEFT = 0X5A, BUTN_KNOB_RIGHT };

#define BUTN_ARROW_RIGHT BUTN_KNOB_PUSH
		
#pragma pack(push, 1)

struct BUTN_header {
	char  opcode[4];
	u32_t length;
	u32_t jiffies;
	u32_t button;
};

struct IR_header {
	char  opcode[4];
	u32_t length;
	u32_t jiffies;
	u8_t format; // unused
	u8_t bits;	// unused
	u32_t code;
};

#pragma pack(pop)

static in_addr_t server_ip;
static u16_t server_hport;
static u16_t server_cport;
static int cli_sock = -1;
static u8_t mac[6];
static void	(*chained_notify)(in_addr_t, u16_t, u16_t);
static bool raw_mode;

static void cli_send_cmd(char *cmd);

/****************************************************************************************
 * Send BUTN
 */
static void sendBUTN(int code, bool pressed) {
	struct BUTN_header pkt_header;
		
	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "BUTN", 4);

	pkt_header.length = htonl(sizeof(pkt_header) - 8);
	pkt_header.jiffies = htonl(gettime_ms());
	pkt_header.button = htonl(code + (pressed ? DOWN_OFS : UP_OFS));
		
	LOG_DEBUG("sending BUTN code %04x %s", code, pressed ? "down" : "up");	

	LOCK_P;
	send_packet((uint8_t *) &pkt_header, sizeof(pkt_header));
	UNLOCK_P;
}

/****************************************************************************************
 * Send IR
 */
static void sendIR(u16_t addr, u16_t cmd) {
	struct IR_header pkt_header;
		
	memset(&pkt_header, 0, sizeof(pkt_header));
	memcpy(&pkt_header.opcode, "IR  ", 4);

	pkt_header.length = htonl(sizeof(pkt_header) - 8);
	pkt_header.jiffies = htonl(gettime_ms());
	pkt_header.format = pkt_header.bits = 0;
	pkt_header.code = htonl((addr << 16) | cmd);
		
	LOG_INFO("sending IR code %04x", (addr << 16) | cmd);	

	LOCK_P;
	send_packet((uint8_t *) &pkt_header, sizeof(pkt_header));
	UNLOCK_P;
}

static void lms_toggle(bool pressed) {
	if (raw_mode) {
		sendBUTN(BUTN_PAUSE, pressed);
	} else {
		cli_send_cmd("pause");
	}	
}

static void lms_pause(bool pressed) {
	if (raw_mode) {
		sendBUTN(BUTN_PAUSE, pressed);
	} else {
		cli_send_cmd("pause 1");
	}	
}

static void lms_stop(bool pressed) {
	cli_send_cmd("button stop");
}

#define LMS_CALLBACK(N,B,E)					\
static void lms_##N (bool pressed) {    	\
	if (raw_mode) {							\
		sendBUTN( BUTN_##B , pressed );		\
	} else {								\
		cli_send_cmd("button" " " #E); 		\
	}                                       \
}

LMS_CALLBACK(power, POWER_FRONT, power)
LMS_CALLBACK(play, PLAY, play.single)

LMS_CALLBACK(volup, VOLUP_FRONT, volup)
LMS_CALLBACK(voldown, VOLDOWN_FRONT, voldown)

LMS_CALLBACK(rew, REW, rew.repeat)
LMS_CALLBACK(fwd, FWD, fwd.repeat)
LMS_CALLBACK(prev, REW, rew)
LMS_CALLBACK(next, FWD, fwd)

LMS_CALLBACK(up, ARROW_UP, arrow_up)
LMS_CALLBACK(down, ARROW_DOWN, arrow_down)
LMS_CALLBACK(left, ARROW_LEFT, arrow_left)
LMS_CALLBACK(right, ARROW_RIGHT, arrow_right)

LMS_CALLBACK(pre0, PRESET_0, preset_0.single)
LMS_CALLBACK(pre1, PRESET_1, preset_1.single)
LMS_CALLBACK(pre2, PRESET_2, preset_2.single)
LMS_CALLBACK(pre3, PRESET_3, preset_3.single)
LMS_CALLBACK(pre4, PRESET_4, preset_4.single)
LMS_CALLBACK(pre5, PRESET_5, preset_5.single)
LMS_CALLBACK(pre6, PRESET_6, preset_6.single)
LMS_CALLBACK(pre7, PRESET_7, preset_7.single)
LMS_CALLBACK(pre8, PRESET_8, preset_8.single)
LMS_CALLBACK(pre9, PRESET_9, preset_9.single)

LMS_CALLBACK(knob_left, KNOB_LEFT, knob_left)
LMS_CALLBACK(knob_right, KNOB_RIGHT, knob_right)
LMS_CALLBACK(knob_push, KNOB_PUSH, knob_push)

const actrls_t LMS_controls = {
	lms_power,
	lms_volup, lms_voldown,	// volume up, volume down
	lms_toggle, lms_play,	// toggle, play
	lms_pause, lms_stop,	// pause, stop
	lms_rew, lms_fwd,		// rew, fwd
	lms_prev, lms_next,		// prev, next
	lms_up, lms_down,
	lms_left, lms_right, 
	lms_pre0, lms_pre1, lms_pre2, lms_pre3, lms_pre4, lms_pre5, lms_pre6, lms_pre7, lms_pre8, lms_pre9, 
	lms_knob_left, lms_knob_right, lms_knob_push,
};

/****************************************************************************************
 * 
 */
static void connect_cli_socket(void) {
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = server_ip,
		.sin_port = htons(server_cport),
	};
	socklen_t addrlen = sizeof(addr);
	
	cli_sock = socket(AF_INET, SOCK_STREAM, 0);
	
	if (connect(cli_sock, (struct sockaddr *) &addr, addrlen) < 0) {
		LOG_ERROR("unable to connect to server %s:%hu with cli", inet_ntoa(server_ip), server_cport);
		closesocket(cli_sock);
		cli_sock = -1;
	}
}

/****************************************************************************************
 * 
 */
static void cli_send_cmd(char *cmd) {
	char packet[96];
	int len;
	
	len = sprintf(packet, "%02x:%02x:%02x:%02x:%02x:%02x %s\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], cmd);
	LOG_DEBUG("sending command %s at %s:%hu", packet, inet_ntoa(server_ip), server_cport);
	
	if (cli_sock < 0) connect_cli_socket();

	if (send(cli_sock, packet, len, MSG_DONTWAIT) < 0) {
		closesocket(cli_sock);
		cli_sock = -1;
		LOG_WARN("cannot send CLI %s", packet);
	}
	
	// need to empty the RX buffer otherwise we'll lock the TCP/IP stack
	len = recv(cli_sock, packet, 96, MSG_DONTWAIT);
}

/****************************************************************************************
 * Notification when server changes
 */
static void notify(in_addr_t ip, u16_t hport, u16_t cport) {
	server_ip = ip;
	server_hport = hport;
	server_cport = cport;
	
	// close existing CLI connection and open new one
	if (cli_sock >= 0) closesocket(cli_sock);
	connect_cli_socket();
	
	LOG_INFO("notified server %s hport %hu cport %hu", inet_ntoa(ip), hport, cport);
	
	if (chained_notify) (*chained_notify)(ip, hport, cport);
}

/****************************************************************************************
 * IR handler
 */
static bool ir_handler(u16_t addr, u16_t cmd) {
	sendIR(addr, cmd);
	return true;
}

/****************************************************************************************
 * Initialize controls - shall be called once from output_init_embedded
 */
void sb_controls_init(void) {
	// TODO: Add support for the commented code
	// char *p = config_alloc_get_default(NVS_TYPE_STR, "lms_ctrls_raw", "n", 0);
	// raw_mode = p && (*p == '1' || *p == 'Y' || *p == 'y');
	// free(p);
	
	// LOG_INFO("initializing audio (buttons/rotary/ir) controls (raw:%u)", raw_mode);
	
	// get_mac(mac);
	// actrls_set_default(LMS_controls, raw_mode, NULL, ir_handler);
	
	// chained_notify = server_notify;
	// server_notify = notify;
}
