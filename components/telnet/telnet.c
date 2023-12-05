	/**
 * Test the telnet functions.
 *
 * Perform a test using the telnet functions.
 * This code exports two new global functions:
 *
 * void telnet_listenForClients(void (*callback)(uint8_t *buffer, size_t size))
 * void telnet_sendData(uint8_t *buffer, size_t size)
 *
 * For additional details and documentation see:
 * * Free book on ESP32 - https://leanpub.com/kolban-ESP32
 *
 *
 * Neil Kolban <kolban1@kolban.com>
 *
 * ****************************
 * Additional portions were taken from
 * https://github.com/PocketSprite/8bkc-sdk/blob/master/8bkc-components/8bkc-hal/vfs-stdout.c
 *
 */
#include <stdlib.h> // Required for libtelnet.h
#include <esp_log.h>
#include "libtelnet.h"
#include "stdbool.h"
#include <lwip/def.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/ringbuf.h"
#include "esp_app_trace.h"
#include "telnet.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "esp_attr.h"
#include "soc/uart_struct.h"
#include "driver/uart.h"
#include "config.h"
#include "platform_esp32.h"
#include "messaging.h"
#include "tools.h"
#include "Configurator.h"
#include "accessors.h"
/************************************
 * Globals
 */

#define TELNET_STACK_SIZE 4096
#define TELNET_RX_BUF 1024

extern bool bypass_network_manager;

struct telnetUserData {
	int sockfd;
	telnet_t *tnHandle;
	char * rxbuf;
};

const static char TAG[] = "telnet";
static int uart_fd;
static RingbufHandle_t buf_handle;
static size_t send_chunk = 512;
static size_t log_buf_size = 4*1024;
static bool bIsEnabled=false;
static int partnerSocket;
static telnet_t *tnHandle;
static bool bMirrorToUART;

/************************************
 * Forward declarations
 */
static void 	telnet_task(void *data);
static int 		stdout_open(const char * path, int flags, int mode);
static int 		stdout_fstat(int fd, struct stat * st);
static ssize_t 	stdout_write(int fd, const void * data, size_t size);
static void 	handle_telnet_conn();
static size_t 	process_logs( UBaseType_t bytes, bool make_room);

void init_telnet(){
	sys_Telnet * telnet_svc=NULL;
	if(!SYS_SERVICES_TELNET(telnet_svc) || !telnet_svc->enable){
		ESP_LOGI(TAG,"Telnet support disabled");
		return;
	}

	#pragma message("Do we still need this logic for telnet?")
	// // if wifi manager is bypassed, there will possibly be no wifi available
	// bMirrorToUART = (strcasestr("D",val)!=NULL);
	// if (!bMirrorToUART && bypass_network_manager){
	// 	// This isn't supposed to happen, as telnet won't start if wifi manager isn't
	// 	// started. So this is a safeguard only.
	// 	ESP_LOGW(TAG,"Wifi manager is not active.  Forcing console on Serial output.");
	// }

	if (telnet_svc->block > 0) send_chunk = telnet_svc->block;
	if (telnet_svc->block > 0) log_buf_size = telnet_svc->buffer;

	// Redirect the output to our telnet handler as soon as possible
	StaticRingbuffer_t *buffer_struct = (StaticRingbuffer_t *) heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	// All non-split ring buffer must have their memory alignment set to 32 bits.
	uint8_t *buffer_storage = (uint8_t *)heap_caps_malloc(sizeof(uint8_t)*log_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
	buf_handle = xRingbufferCreateStatic(log_buf_size, RINGBUF_TYPE_BYTEBUF, buffer_storage, buffer_struct);
	if (buf_handle == NULL) {
		ESP_LOGE(TAG,"Failed to create ring buffer for telnet!");
		messaging_post_message(MESSAGING_ERROR,MESSAGING_CLASS_SYSTEM,"Failed to allocate memory for telnet buffer");

		return;
	}

	ESP_LOGI(TAG, "***Redirecting log output to telnet");
	esp_vfs_t vfs = { };
	vfs.flags = ESP_VFS_FLAG_DEFAULT;
	vfs.write = &stdout_write;
	vfs.open = &stdout_open;
	vfs.fstat = &stdout_fstat;

	if (bMirrorToUART) uart_fd = open("/dev/uart/0", O_RDWR);

	ESP_ERROR_CHECK(esp_vfs_register("/dev/pkspstdout", &vfs, NULL));
	freopen("/dev/pkspstdout", "w", stdout);
	freopen("/dev/pkspstdout", "w", stderr);

	bIsEnabled=true;

}

void start_telnet(void * pvParameter){
	static bool isStarted=false;

	if (isStarted || !bIsEnabled) return;

	isStarted=true;	

	StaticTask_t *xTaskBuffer = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	StackType_t *xStack = heap_caps_malloc(TELNET_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	
	xTaskCreateStatic( (TaskFunction_t) &telnet_task, "telnet", TELNET_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN, xStack, xTaskBuffer);

}

static void telnet_task(void *data) {
	int serverSocket;
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(23);

	while (1) {
		serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) >= 0 &&	listen(serverSocket, 1) >= 0) break;
		close(serverSocket);		
		ESP_LOGI(TAG, "can't bind Telnet socket");
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	while (1) {
		socklen_t len = sizeof(serverAddr);
		int sock = accept(serverSocket, (struct sockaddr *)&serverAddr, &len);

		if (sock >= 0) {
			partnerSocket = sock;
			ESP_LOGI(TAG, "We have a new client connection %d", sock);
			handle_telnet_conn();
			ESP_LOGI(TAG, "Telnet connection terminated %d", sock);
		} else {
			ESP_LOGW(TAG, "accept: %d (%s)", errno, strerror(errno));
		}
	}

	// we should not be here
	close(serverSocket);
	vTaskDelete(NULL);
}

/**
 * Telnet handler.
 */
static void handle_telnet_events(telnet_t *thisTelnet, telnet_event_t *event, void *userData) {
	struct telnetUserData *telnetUserData = (struct telnetUserData *)userData;

	switch(event->type) {
	case TELNET_EV_SEND:
		send(telnetUserData->sockfd, event->data.buffer, event->data.size, 0);
		break;
	case TELNET_EV_DATA:
		console_push(event->data.buffer, event->data.size);
		break;
	case TELNET_EV_TTYPE:
		telnet_ttype_send(telnetUserData->tnHandle);
		break;
	default:
		break;
	}
}

static size_t process_logs(UBaseType_t bytes, bool make_room){
	UBaseType_t pending;

	vRingbufferGetInfo(buf_handle, NULL, NULL, NULL, NULL, &pending);

	// nothing to do or we can do 
	if (!partnerSocket || (make_room && log_buf_size - pending > bytes)) return pending;

	// can't send more than what we have
	if (bytes > pending) bytes = pending;

	while (bytes > 0) {
		size_t size;
		char *item = (char *)xRingbufferReceiveUpTo(buf_handle, &size, pdMS_TO_TICKS(50), bytes);
		
		if (!item || !partnerSocket) break;

		bytes -= size;
		telnet_send_text(tnHandle, item, size);

		vRingbufferReturnItem(buf_handle, (void *)item);
	}

	return pending - bytes;
}

static void handle_telnet_conn() {
	static const telnet_telopt_t my_telopts[] = {
		{ TELNET_TELOPT_ECHO,      TELNET_WONT, TELNET_DO },
		{ TELNET_TELOPT_TTYPE,     TELNET_WILL, TELNET_DONT },
		{ TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO   },
		{ TELNET_TELOPT_ZMP,       TELNET_WONT, TELNET_DO   },
		{ TELNET_TELOPT_MSSP,      TELNET_WONT, TELNET_DO   },
		{ TELNET_TELOPT_BINARY,    TELNET_WILL, TELNET_DO   },
		{ TELNET_TELOPT_NAWS,      TELNET_WILL, TELNET_DONT },
		{TELNET_TELOPT_LINEMODE,   TELNET_WONT, TELNET_DO },
		{ -1, 0, 0 }
	};
	struct telnetUserData *pTelnetUserData = (struct telnetUserData *)heap_caps_malloc(sizeof(struct telnetUserData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	tnHandle = telnet_init(my_telopts, handle_telnet_events, 0, pTelnetUserData);

	pTelnetUserData->rxbuf = (char *) heap_caps_malloc(TELNET_RX_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	pTelnetUserData->tnHandle = tnHandle;
	pTelnetUserData->sockfd = partnerSocket;

	bool pending = true;

	while(1) {
		fd_set rfds, wfds;
		struct timeval timeout = {0, 200*1000};

		FD_ZERO(&rfds);
		FD_SET(partnerSocket, &rfds);

		FD_ZERO(&wfds);
		if (pending) FD_SET(partnerSocket, &wfds);

		int res = select(partnerSocket + 1, &rfds, &wfds, NULL, &timeout);
		if (res < 0) break;

		if (FD_ISSET(partnerSocket, &rfds)) { 
			int len = recv(partnerSocket, pTelnetUserData->rxbuf, TELNET_RX_BUF, 0);
			if (!len) break;
			telnet_recv(tnHandle, pTelnetUserData->rxbuf, len);
		}

		if (FD_ISSET(partnerSocket, &wfds)) {	
			pending = process_logs(send_chunk, false) > 0;
		} else {
			pending = true;
		}
  	} 
	
	telnet_free(tnHandle);
	tnHandle = NULL;

	free(pTelnetUserData->rxbuf);
	free(pTelnetUserData);

	close(partnerSocket);
	partnerSocket = 0;
}

// ******************* stdout/stderr Redirection to ringbuffer
static ssize_t stdout_write(int fd, const void * data, size_t size) {
	// flush the buffer and send item
	if (buf_handle) {
		process_logs(size, true);
		xRingbufferSend(buf_handle, data, size, 0);
	}
	
	// mirror to uart if required
	return (bMirrorToUART || !buf_handle) ? write(uart_fd, data, size) : size;
}

static int stdout_open(const char * path, int flags, int mode) {
	return 0;
}

static int stdout_fstat(int fd, struct stat * st) {
	st->st_mode = S_IFCHR;
	return 0;
}
