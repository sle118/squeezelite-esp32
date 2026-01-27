/*
 * tinysvcmdns - a tiny MDNS implementation for publishing services
 * Copyright (C) 2011 Darell Tan
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __TINYSVCMDNS_H__
#define __TINYSVCMDNS_H__

#include <stdint.h>
#include <stdbool.h>
#ifdef _WIN32
#include <inaddr.h>
#else
#include <netinet/in.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct mdnsd;
struct mdns_service;


// starts a MDNS responder instance
// returns NULL if unsuccessful
struct mdnsd *mdnsd_start(struct in_addr host, bool verbose);

// stops the given MDNS responder instance
void mdnsd_stop(struct mdnsd *s);

// sets the hostname for the given MDNS responder instance
void mdnsd_set_hostname(struct mdnsd *svr, const char *hostname, struct in_addr addr);

// registers a service with the MDNS responder instance
struct mdns_service *mdnsd_register_svc(struct mdnsd *svr, const char *instance_name, 
		const char *type, uint16_t port, const char *hostname, const char *txt[]);

// destroys the mdns_service struct returned by mdnsd_register_svc()
void mdns_service_destroy(struct mdns_service *srv);

// remove AND destroys the mdns_service struct returned by mdnsd_register_svc()
void mdns_service_remove(struct mdnsd *svr, struct mdns_service *svc);

#ifdef __cplusplus
}
#endif


#endif/*!__MDNSD_H__*/
