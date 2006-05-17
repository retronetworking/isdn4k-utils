/*  $Id$
 *
 *  This file is part of ANT (Ant is Not a Telephone)
 *
 *  Authors:
 *    - Martin Bachem, m.bachem@gmx.de
 *    - heavy capitalized on: isdn4k-utils/[pppdcapiplugin,capiiinfo]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <capi20.h>

#include <linux/capi.h>
#include <unistd.h>
#include <stddef.h>

#include "session.h"
#include "globals.h"
#include "isdn_capi.h"
#include "capiconn.h"


static char msgbuf[4096];


static void
ant_capi_incoming(capi_connection *cp,
				unsigned contr,
				unsigned cipvalue,
				char *callednumber,
				char *callingnumber)
{
	printf ("ant_capi_incoming\n");
}

static void
ant_capi_connected(capi_connection *cp, _cstruct NCPI)
{
	printf ("JEHOVA: ant_capi_connected\n");
}

static void
ant_capi_disconnected(capi_connection *cp,
				int localdisconnect,
				unsigned reason,
				unsigned reason_b3)
{
	printf ("ant_capi_disconnected\n");
}

static char *ant_capi_conninfo(capi_connection *p)
{
	static char buf[1024];
	capi_conninfo *cp = capiconn_getinfo(p);
	char *callingnumber = "";
	char *callednumber = "";

	if (cp->callingnumber && cp->callingnumber[0] > 2)
		callingnumber = cp->callingnumber+3;
	if (cp->callednumber && cp->callednumber[0] > 1)
		callednumber = cp->callednumber+2;

	if (debug) {
		snprintf(buf, sizeof(buf),
			"\"%s\" -> \"%s\" %s (pcli=0x%x/ncci=0x%x)",
				callingnumber, callednumber,
				cp->isincoming ? "incoming" : "outgoing",
				cp->plci, cp->ncci
				);
	} else {
		snprintf(buf, sizeof(buf),
			"\"%s\" -> \"%s\" %s",
				callingnumber, callednumber,
				cp->isincoming ? "incoming" : "outgoing");
	}
	buf[sizeof(buf)-1] = 0;
	return buf;
}


static void
ant_capi_chargeinfo(capi_connection *cp, unsigned long charge, int inunits)
{
	if (debug) {
		if (inunits) {
	        	printf ("%s: charge in units: %ul", ant_capi_conninfo(cp), charge);
		} else {
	        	printf ("%s: charge in currency: %ul", ant_capi_conninfo(cp), charge);
		}
	}
}

static int
ant_capi_put_message(unsigned appid, unsigned char *msg)
{
	unsigned err;
	err = capi20_put_message (appid, msg);
	if (err && debug) {
		fprintf(stderr, "putmessage(appid=%d) - %s 0x%x",
		        appid, capi_info2str(err), err);
	}
	return 0;
}

static void
ant_capi_msg(const char *prefix, const char *format, va_list args)
{
	char *s = msgbuf;
	char *e = msgbuf + sizeof(msgbuf);

	(void)snprintf(s, e-s, "%s", prefix);
	s += strlen(s);
	(void)vsnprintf(s, e-s, format, args);
	s += strlen(s);

	printf("%s\n", msgbuf);
}


static void
ant_capi_err(const char *format, ...)
{
	va_list args;

	if (debug) {
		va_start(args, format);
		ant_capi_msg("CAPI Error: ", format, args);
		va_end(args);
	}
}

static void
ant_capi_dbg(const char *format, ...)
{
	va_list args;

	if (debug > 1) {
		va_start(args, format);
		ant_capi_msg("CAPI Debug: ", format, args);
		va_end(args);
	}
}

static void
ant_capi_info(const char *format, ...)
{
	va_list args;

	if (debug > 2) {
		va_start(args, format);
		ant_capi_msg("CAPI Info: ", format, args);
		va_end(args);
	}
}


capiconn_callbacks ant_capi_callbacks = {
	malloc: malloc,
	free: free,

	disconnected: ant_capi_disconnected,
	incoming: ant_capi_incoming,
	connected: ant_capi_connected,
	chargeinfo: ant_capi_chargeinfo,
	capi_put_message: ant_capi_put_message,

	received: 0, 
	datasent: 0, 
	dtmf_received: 0,

	debugmsg: (void (*)(const char *, ...))ant_capi_dbg,
	infomsg: (void (*)(const char *, ...))ant_capi_info,
	errmsg: (void (*)(const char *, ...))ant_capi_err
};

int ant_capi_init(session_t *session)
{
	int serrno, err;

	if (CAPI20_ISINSTALLED() != CapiNoError) {
		fprintf(stderr, "CAPI not installed - %s (%d)\n", strerror(errno), errno);
		return -2;
	}

	if ((err = capi20_register (2, 8, 2048, &session->applid)) != 0) {
		serrno = errno;
		fprintf(stderr, "CAPI_REGISTER failed - %s (0x%04x) [%s (%d)]\n",
				capi_info2str(err), err, strerror(serrno), errno);
		return(-3);
        }

	if (capi20ext_set_flags(session->applid, 1) < 0) {
		serrno = errno;
		(void)capi20_release(session->applid);
		fprintf(stderr, "CAPI: failed to set highjacking mode - %s (%d)\n",
				 strerror(serrno), serrno);
		return(-4);
	}

	if ((session->ctx = capiconn_getcontext(session->applid, &ant_capi_callbacks)) == 0) {
		(void)capi20_release(session->applid);
		fprintf(stderr, "CAPI: get_context failed\n");
		return(-5);
	}

	if (capiconn_addcontr(session->ctx, session->capi_contr, &session->cinfo) != CAPICONN_OK) {
		(void)capiconn_freecontext(session->ctx);
		(void)capi20_release(session->applid);
		fprintf(stderr, "CAPI: add controller %d failed", session->capi_contr);
		return(-1);
	}

	return(0);
}

void ant_capi_listen(session_t *session)
{
/*
		call 
		conn = capiconn_connect(ctx,
				session->opt_contr, // contr 
				1, // cipvalue
				number_to_call, 
				session->msn,
				1, 1, 0,
				0, 0, 0, 0, 0);
*/
	(void)capiconn_listen(session->ctx, session->capi_contr, 0x1FFF03FF, 0);
}

void ant_capi_free(session_t *session)
{
	(void)capiconn_freecontext(session->ctx);
	(void)capi20_release(session->applid);
}


/* attention: get called periodically by timer defined TIMER_DELAY in gtk.c,
 * so be quick ;)
 */
void ant_capi_messages(session_t *session)
{
	unsigned char *msg = 0;
	struct timeval tv;
	tv.tv_sec  = 0;
	tv.tv_usec = 100;
	if (capi20_waitformessage(session->applid, &tv) == 0) {
		if (capi20_get_message(session->applid, &msg) == 0)
			capiconn_inject(session->applid, msg);
	}
}