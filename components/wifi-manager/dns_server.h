/*
Copyright (c) 2019 Tony Pottier

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

@file dns_server.h
@author Tony Pottier
@brief Defines an extremly basic DNS server for captive portal functionality.

Contains the freeRTOS task for the DNS server that processes the requests.

@see https://idyl.io
@see https://github.com/tonyp7/esp32-wifi-manager
@see http://www.zytrax.com/books/dns/ch15
*/

#ifndef MAIN_DNS_SERVER_H_
#define MAIN_DNS_SERVER_H_
#include <esp_system.h>
#include <stdbool.h>
#include "squeezelite-ota.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 12 byte header, 64 byte domain name, 4 byte qtype/qclass. This NOT compliant with the RFC, but it's good enough for a captive portal
 * if a DNS query is too big it just wont be processed. */
#define DNS_QUERY_MAX_SIZE 80

/** Query + 2 byte ptr, 2 byte type, 2 byte class, 4 byte TTL, 2 byte len, 4 byte data */
#define DNS_ANSWER_MAX_SIZE (DNS_QUERY_MAX_SIZE + 16)

/**
 * @brief RCODE values used in a DNS header message
 */
typedef enum dns_reply_code_t {
    DNS_REPLY_CODE_NO_ERROR = 0,
    DNS_REPLY_CODE_FORM_ERROR = 1,
    DNS_REPLY_CODE_SERVER_FAILURE = 2,
    DNS_REPLY_CODE_NON_EXISTANT_DOMAIN = 3,
    DNS_REPLY_CODE_NOT_IMPLEMENTED = 4,
    DNS_REPLY_CODE_REFUSED = 5,
    DNS_REPLY_CODE_YXDOMAIN = 6,
    DNS_REPLY_CODE_YXRRSET = 7,
    DNS_REPLY_CODE_NXRRSET = 8
} dns_reply_code_t;

/**
 * @brief OPCODE values used in a DNS header message
 */
typedef enum dns_opcode_code_t { DNS_OPCODE_QUERY = 0, DNS_OPCODE_IQUERY = 1, DNS_OPCODE_STATUS = 2 } dns_opcode_code_t;

/**
 * @brief Represents a 12 byte DNS header.
 * __packed__ is needed to prevent potential unwanted memory alignments
 */
typedef struct __attribute__((__packed__)) dns_header_t {
    uint16_t ID;        // identification number
    uint8_t RD : 1;     // recursion desired
    uint8_t TC : 1;     // truncated message
    uint8_t AA : 1;     // authoritive answer
    uint8_t OPCode : 4; // message_type
    uint8_t QR : 1;     // query/response flag
    uint8_t RCode : 4;  // response code
    uint8_t Z : 3;      // its z! reserved
    uint8_t RA : 1;     // recursion available
    uint16_t QDCount;   // number of question entries
    uint16_t ANCount;   // number of answer entries
    uint16_t NSCount;   // number of authority entries
    uint16_t ARCount;   // number of resource entries
} dns_header_t;

typedef enum dns_answer_type_t {
    DNS_ANSWER_TYPE_A = 1,
    DNS_ANSWER_TYPE_NS = 2,
    DNS_ANSWER_TYPE_CNAME = 5,
    DNS_ANSWER_TYPE_SOA = 6,
    DNS_ANSWER_TYPE_WKS = 11,
    DNS_ANSWER_TYPE_PTR = 12,
    DNS_ANSWER_TYPE_MX = 15,
    DNS_ANSWER_TYPE_SRV = 33,
    DNS_ANSWER_TYPE_AAAA = 28
} dns_answer_type_t;

typedef enum dns_answer_class_t { DNS_ANSWER_CLASS_IN = 1 } dns_answer_class_t;

typedef struct __attribute__((__packed__)) dns_answer_t {
    uint16_t NAME;     /* for the sake of simplicity only 16 bit pointers are supported */
    uint16_t TYPE;     /* Unsigned 16 bit value. The resource record types - determines the content of the RDATA field. */
    uint16_t CLASS;    /* Class of response. */
    uint32_t TTL;      /* The time in seconds that the record may be cached. A value of 0 indicates the record should not be cached. */
    uint16_t RDLENGTH; /* Unsigned 16-bit value that defines the length in bytes of the RDATA record. */
    uint32_t RDATA;    /* For the sake of simplicity only ipv4 is supported, and as such it's a unsigned 32 bit */
} dns_answer_t;

void dns_server(void* pvParameters);
void dns_server_start(esp_netif_t* netif);
void dns_server_stop();

#ifdef __cplusplus
}
#endif

#endif /* MAIN_DNS_SERVER_H_ */
