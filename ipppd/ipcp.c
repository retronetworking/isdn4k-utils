/*
 * ipcp.c - PPP IP Control Protocol.
 *
 * Copyright (c) 1989 Carnegie Mellon University.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Carnegie Mellon University.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

char ipcp_rcsid[] = "$Id$";

/*
 * TODO:
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "fsm.h"
#include "ipppd.h"
#include "ipcp.h"
#include "pathnames.h"
#include "environ.h"

/* global vars */
ipcp_options ipcp_wantoptions[NUM_PPP];	/* Options that we want to request */
ipcp_options ipcp_gotoptions[NUM_PPP];	/* Options that peer ack'd */
ipcp_options ipcp_allowoptions[NUM_PPP];	/* Options we allow peer to request */
ipcp_options ipcp_hisoptions[NUM_PPP];	/* Options that we ack'd */

/* local vars */
static int cis_received[NUM_PPP];		/* # Conf-Reqs received */

/*
 * Callbacks for fsm code.  (CI = Configuration Information)
 */
static void ipcp_resetci __P((fsm *));	/* Reset our CI */
static int  ipcp_cilen __P((fsm *));	        /* Return length of our CI */
static void ipcp_addci __P((fsm *, u_char *, int *)); /* Add our CI */
static int  ipcp_ackci __P((fsm *, u_char *, int));	/* Peer ack'd our CI */
static int  ipcp_nakci __P((fsm *, u_char *, int));	/* Peer nak'd our CI */
static int  ipcp_rejci __P((fsm *, u_char *, int));	/* Peer rej'd our CI */
static int  ipcp_reqci __P((fsm *, u_char *, int *, int)); /* Rcv CI */
static void ipcp_up __P((fsm *));		/* We're UP */
static void ipcp_down __P((fsm *));		/* We're DOWN */
static void ipcp_script __P((fsm *, char *)); /* Run an up/down script */

fsm ipcp_fsm[NUM_PPP];		/* IPCP fsm structure */

static fsm_callbacks ipcp_callbacks = { /* IPCP callback routines */
    ipcp_resetci,		/* Reset our Configuration Information */
    ipcp_cilen,			/* Length of our Configuration Information */
    ipcp_addci,			/* Add our Configuration Information */
    ipcp_ackci,			/* ACK our Configuration Information */
    ipcp_nakci,			/* NAK our Configuration Information */
    ipcp_rejci,			/* Reject our Configuration Information */
    ipcp_reqci,			/* Request peer's Configuration Information */
    ipcp_up,			/* Called when fsm reaches OPENED state */
    ipcp_down,			/* Called when fsm leaves OPENED state */
    NULL,			/* Called when we want the lower layer up */
    NULL,			/* Called when we want the lower layer down */
    NULL,			/* Called when Protocol-Reject received */
    NULL,			/* Retransmission is necessary */
    NULL,			/* Called to handle protocol-specific codes */
    "IPCP"			/* String name of protocol */
};

/*
 * Protocol entry points from main code.
 */
static void ipcp_init __P((int));
static void ipcp_open __P((int));
static void ipcp_close __P((int, char *));
static void ipcp_lowerup __P((int));
static void ipcp_lowerdown __P((int));
static void ipcp_input __P((int, u_char *, int));
static void ipcp_protrej __P((int));
static int  ipcp_printpkt __P((u_char *, int,
                   void (*) __P((void *, char *, ...)), void *));

static void create_resolv __P((u_int32_t, u_int32_t));

struct protent ipcp_protent = {
    PPP_IPCP,
    ipcp_init,
    ipcp_input,
    ipcp_protrej,
    ipcp_lowerup,
    ipcp_lowerdown,
    ipcp_open,
    ipcp_close,
    ipcp_printpkt,
    NULL,
    1,
    "IPCP",
	NULL,
	NULL,
	NULL,
};

/*
 * Lengths of configuration options.
 */
#define CILEN_VOID	2
#define CILEN_COMPRESS	4	/* min length for compression protocol opt. */
#define CILEN_VJ	6	/* length for RFC1332 Van-Jacobson opt. */
#define CILEN_ADDR	6	/* new-style single address option */
#define CILEN_ADDRS	10	/* old-style dual address option */


#define CODENAME(x)	((x) == CONFACK ? "ACK" : \
			 (x) == CONFNAK ? "NAK" : "REJ")


/*
 * Make a string representation of a network IP address.
 */
char *ip_ntoa(u_int32_t ipaddr)
{
    static char b[64];

    ipaddr = ntohl(ipaddr);

    sprintf(b, "%d.%d.%d.%d",
	    (u_char)(ipaddr >> 24),
	    (u_char)(ipaddr >> 16),
	    (u_char)(ipaddr >> 8),
	    (u_char)(ipaddr));
    return b;
}


/*
 * ipcp_init - Initialize IPCP.
 */
void ipcp_init(int unit)
{
    fsm *f = &ipcp_fsm[unit];
    ipcp_options *wo = &ipcp_wantoptions[unit];
    ipcp_options *ao = &ipcp_allowoptions[unit];

	memset(f,0,sizeof(fsm));

    f->unit = -1;
    f->protocol = PPP_IPCP;
    f->callbacks = &ipcp_callbacks;
    f->inuse = 0;
    fsm_init(f);

	memset(wo, 0, sizeof(*wo));
	memset(ao, 0, sizeof(*ao));
    wo->neg_addr = 1;

    wo->neg_vj = 1;
    wo->old_vj = 0;
    wo->vj_protocol = IPCP_VJ_COMP;
    wo->maxslotindex = MAX_STATES - 1; /* really max index */
    wo->cflag = 1;

    /* max slots and slot-id compression are currently hardwired in */
    /* ppp_if.c to 16 and 1, this needs to be changed (among other */
    /* things) gmc */

    ao->neg_addr = 1;
    ao->neg_vj = 1;
    ao->maxslotindex = MAX_STATES - 1;
    ao->cflag = 1;

    /*
     * XXX These control whether the user may use the proxyarp
     * and defaultroute options.
     */
    ao->proxy_arp = 1;
    ao->default_route = 1;
}

int ipcp_getunit(int linkunit)
{
  int i;
  for(i=0;i<NUM_PPP;i++)
    if(!ipcp_fsm[i].inuse)
    {
      ipcp_fsm[i].inuse = 1;
      ipcp_fsm[i].state = INITIAL;
      ipcp_fsm[i].flags = 0;
      ipcp_fsm[i].id = 0;
      ipcp_fsm[i].unit = linkunit;
      return i;
    }
  syslog(LOG_ERR,"No more ipcp_units available");
  return -1;
}

void ipcp_freeunit(int unit)
{
  ipcp_fsm[unit].inuse = 0;
  ipcp_fsm[unit].unit = -1;
}


/*
 * ipcp_open - IPCP is allowed to come up.
 */
void ipcp_open(int unit)
{
  if(unit < 0)
  {
    syslog(LOG_ERR,"ipcp_open: illegal ipcp_unit");
    return;
  }
  fsm_open(&ipcp_fsm[unit]);
}


/*
 * ipcp_close - Take IPCP down.
 */
static void ipcp_close(int unit,char *reason)
{
  if(unit < 0)
  {
    syslog(LOG_ERR,"ipcp_close: illegal ipcp_unit");
    return;
  }
  fsm_close(&ipcp_fsm[unit],reason);
}


/*
 * ipcp_lowerup - The lower layer is up.
 */
static void ipcp_lowerup(int unit)
{
	if(unit < 0) {
		syslog(LOG_ERR,"ipcp_lowerup: illegal ipcp_unit");
		return;
	}
	fsm_lowerup(&ipcp_fsm[unit]);
}


/*
 * ipcp_lowerdown - The lower layer is down.
 */
void ipcp_lowerdown(int unit)
{
  if(unit < 0)
  {
    syslog(LOG_ERR,"ipcp_lowerdown: illegal ipcp_unit");
    return;
  }
  fsm_lowerdown(&ipcp_fsm[unit]);
}


/*
 * ipcp_input - Input IPCP packet.
 */
void ipcp_input(int linkunit,u_char *p,int len)
{
  int unit = lns[linkunit].ipcp_unit;
  if(unit < 0)
  {
    syslog(LOG_ERR,"ipcp_input: illegal ipcp_unit for linkunit %d",linkunit);
    return;
  }
  fsm_input(&ipcp_fsm[unit], p, len);
}


/*
 * ipcp_protrej - A Protocol-Reject was received for IPCP.
 *
 * Pretend the lower layer went down, so we shut up.
 */
void ipcp_protrej(int linkunit)
{
	int unit = lns[linkunit].ipcp_unit;
	if(unit < 0) {
		syslog(LOG_ERR,"ipcp_portrej: illegal ipcp_unit for linkunit %d",linkunit);
		return;
	}
	fsm_lowerdown(&ipcp_fsm[unit]);
}


/*
 * ipcp_resetci - Reset our CI.
 */
static void ipcp_resetci(fsm *f)
{
    ipcp_options *wo = &ipcp_wantoptions[lns[f->unit].ipcp_unit];

    wo->req_addr = wo->neg_addr && ipcp_allowoptions[lns[f->unit].ipcp_unit].neg_addr;
    if (wo->ouraddr == 0)
	wo->accept_local = 1;
    if (wo->hisaddr == 0)
	wo->accept_remote = 1;
    ipcp_gotoptions[lns[f->unit].ipcp_unit] = *wo;
    cis_received[lns[f->unit].ipcp_unit] = 0;
}


/*
 * ipcp_cilen - Return length of our CI.
 */
static int ipcp_cilen(fsm *f)
{
	ipcp_options *go = &ipcp_gotoptions[lns[f->unit].ipcp_unit];
	ipcp_options *wo = &ipcp_wantoptions[lns[f->unit].ipcp_unit];
	ipcp_options *ho = &ipcp_hisoptions[lns[f->unit].ipcp_unit];

#define LENCIVJ(neg, old)	(neg ? (old? CILEN_COMPRESS : CILEN_VJ) : 0)
#define LENCIADDR(neg, old)	(neg ? (old? CILEN_ADDRS : CILEN_ADDR) : 0)

	/*
	 * First see if we want to change our options to the old
	 * forms because we have received old forms from the peer.
	 */
	if (wo->neg_addr && !go->neg_addr && !go->old_addrs) {
		/* use the old style of address negotiation */
		go->neg_addr = 1;
		go->old_addrs = 1;
	}
	if (wo->neg_vj && !go->neg_vj && !go->old_vj) {
		/* try an older style of VJ negotiation */
		if (cis_received[f->unit] == 0) {
			/* keep trying the new style until we see some CI from the peer */
			go->neg_vj = 1;
		} else {
			/* use the old style only if the peer did */
			if (ho->neg_vj && ho->old_vj) {
				go->neg_vj = 1;
				go->old_vj = 1;
				go->vj_protocol = ho->vj_protocol;
			}
		}
	}

	return (LENCIADDR(go->neg_addr, go->old_addrs) +
		LENCIVJ(go->neg_vj, go->old_vj) +
		LENCIADDR(go->neg_dns1, 0) +
		LENCIADDR(go->neg_dns2, 0) +
		LENCIADDR(go->neg_wins1, 0) +
		LENCIADDR(go->neg_wins2, 0));
}


/*
 * ipcp_addci - Add our desired CIs to a packet.
 */
static void ipcp_addci(fsm *f,u_char *ucp,int *lenp)
{
	ipcp_options *go = &ipcp_gotoptions[lns[f->unit].ipcp_unit];
	int len = *lenp;

#define ADDCIVJ(opt, neg, val, old, maxslotindex, cflag) \
    if (neg) { \
	int vjlen = old? CILEN_COMPRESS : CILEN_VJ; \
	if (len >= vjlen) { \
	    PUTCHAR(opt, ucp); \
	    PUTCHAR(vjlen, ucp); \
	    PUTSHORT(val, ucp); \
	    if (!old) { \
		PUTCHAR(maxslotindex, ucp); \
		PUTCHAR(cflag, ucp); \
	    } \
	    len -= vjlen; \
	} else \
	    neg = 0; \
    }

#define ADDCIADDR(opt, neg, old, val1, val2) \
    if (neg) { \
	int addrlen = (old? CILEN_ADDRS: CILEN_ADDR); \
	if (len >= addrlen) { \
	    u_int32_t l; \
	    PUTCHAR(opt, ucp); \
	    PUTCHAR(addrlen, ucp); \
	    l = ntohl(val1); \
	    PUTLONG(l, ucp); \
	    if (old) { \
		l = ntohl(val2); \
		PUTLONG(l, ucp); \
	    } \
	    len -= addrlen; \
	} else \
	    neg = 0; \
    }

    ADDCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), go->neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    ADDCIVJ(CI_COMPRESSTYPE, go->neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

	ADDCIADDR(CI_MS_DNS1, go->neg_dns1, 0, go->dnsaddr[0], 0);

	ADDCIADDR(CI_MS_DNS2, go->neg_dns2, 0, go->dnsaddr[1], 0);

	ADDCIADDR(CI_MS_WINS1, go->neg_wins1, 0, go->winsaddr[0], 0);

	ADDCIADDR(CI_MS_WINS2, go->neg_wins2, 0, go->winsaddr[1], 0);

    *lenp -= len;
}


/*
 * ipcp_ackci - Ack our CIs.
 *
 * Returns:
 *	0 - Ack was bad.
 *	1 - Ack was good.
 */
static int ipcp_ackci(fsm *f,u_char *p,int len)
{
    ipcp_options *go = &ipcp_gotoptions[lns[f->unit].ipcp_unit];
    u_short cilen, citype, cishort;
    u_int32_t cilong;
    u_char cimaxslotindex, cicflag;

    /*
     * CIs must be in exactly the same order that we sent...
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */

#define ACKCIVJ(opt, neg, val, old, maxslotindex, cflag) \
    if (neg) { \
	int vjlen = old? CILEN_COMPRESS : CILEN_VJ; \
	if ((len -= vjlen) < 0) \
	    goto bad; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != vjlen || \
	    citype != opt)  \
	    goto bad; \
	GETSHORT(cishort, p); \
	if (cishort != val) \
	    goto bad; \
	if (!old) { \
	    GETCHAR(cimaxslotindex, p); \
	    if (cimaxslotindex != maxslotindex) \
		goto bad; \
	    GETCHAR(cicflag, p); \
	    if (cicflag != cflag) \
		goto bad; \
	} \
    }

#define ACKCIADDR(opt, neg, old, val1, val2) \
    if (neg) { \
	int addrlen = (old? CILEN_ADDRS: CILEN_ADDR); \
	u_int32_t l; \
	if ((len -= addrlen) < 0) \
	    goto bad; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != addrlen || \
	    citype != opt) \
	    goto bad; \
	GETLONG(l, p); \
	cilong = htonl(l); \
	if (val1 != cilong) \
	    goto bad; \
	if (old) { \
	    GETLONG(l, p); \
	    cilong = htonl(l); \
	    if (val2 != cilong) \
		goto bad; \
	} \
    }

    ACKCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), go->neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    ACKCIVJ(CI_COMPRESSTYPE, go->neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

    ACKCIADDR(CI_MS_DNS1, go->neg_dns1, 0, go->dnsaddr[0], 0);

    ACKCIADDR(CI_MS_DNS2, go->neg_dns2, 0, go->dnsaddr[1], 0);

    ACKCIADDR(CI_MS_WINS1, go->neg_wins1, 0, go->winsaddr[0], 0);

    ACKCIADDR(CI_MS_WINS2, go->neg_wins2, 0, go->winsaddr[1], 0);

    /*
     * If there are any remaining CIs, then this packet is bad.
     */
    if (len != 0)
	goto bad;
    return (1);

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_ackci: received bad Ack!"));
    return (0);
}

/*
 * ipcp_nakci - Peer has sent a NAK for some of our CIs.
 * This should not modify any state if the Nak is bad
 * or if IPCP is in the OPENED state.
 *
 * Returns:
 *	0 - Nak was bad.
 *	1 - Nak was good.
 */
static int ipcp_nakci(fsm *f,u_char *p,int len)
{
    ipcp_options *go = &ipcp_gotoptions[lns[f->unit].ipcp_unit];
    u_char cimaxslotindex, cicflag;
    u_char citype, cilen, *next;
    u_short cishort;
    u_int32_t ciaddr1, ciaddr2, l;
    ipcp_options no;		/* options we've seen Naks for */
    ipcp_options try;		/* options to request next time */

    BZERO(&no, sizeof(no));
    try = *go;

    /*
     * Any Nak'd CIs must be in exactly the same order that we sent.
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */
#define NAKCIADDR(opt, neg, old, code) \
    if (go->neg && \
	len >= (cilen = (old? CILEN_ADDRS: CILEN_ADDR)) && \
	p[1] == cilen && \
	p[0] == opt) { \
	len -= cilen; \
	INCPTR(2, p); \
	GETLONG(l, p); \
	ciaddr1 = htonl(l); \
	if (old) { \
	    GETLONG(l, p); \
	    ciaddr2 = htonl(l); \
	    no.old_addrs = 1; \
	} else \
	    ciaddr2 = 0; \
	no.neg = 1; \
	code \
    }

#define NAKCIVJ(opt, neg, code) \
    if (go->neg && \
	((cilen = p[1]) == CILEN_COMPRESS || cilen == CILEN_VJ) && \
	len >= cilen && \
	p[0] == opt) { \
	len -= cilen; \
	INCPTR(2, p); \
	GETSHORT(cishort, p); \
	no.neg = 1; \
        code \
    }

    /*
     * Accept the peer's idea of {our,his} address, if different
     * from our idea, only if the accept_{local,remote} flag is set.
     */
    NAKCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), neg_addr, go->old_addrs,
	      if (go->accept_local && ciaddr1) { /* Do we know our address? */
		  try.ouraddr = ciaddr1;
		  IPCPDEBUG((LOG_INFO, "local IP address %s",
			     ip_ntoa(ciaddr1)));
	      }
	      if (go->accept_remote && ciaddr2) { /* Does he know his? */
		  try.hisaddr = ciaddr2;
		  IPCPDEBUG((LOG_INFO, "remote IP address %s",
			     ip_ntoa(ciaddr2)));
	      }
	      );

    /*
     * Accept the peer's value of maxslotindex provided that it
     * is less than what we asked for.  Turn off slot-ID compression
     * if the peer wants.  Send old-style compress-type option if
     * the peer wants.
     */
    NAKCIVJ(CI_COMPRESSTYPE, neg_vj,
	    if (cilen == CILEN_VJ) {
		GETCHAR(cimaxslotindex, p);
		GETCHAR(cicflag, p);
		if (cishort == IPCP_VJ_COMP) {
		    try.old_vj = 0;
		    if (cimaxslotindex < go->maxslotindex)
			try.maxslotindex = cimaxslotindex;
		    if (!cicflag)
			try.cflag = 0;
		} else {
		    try.neg_vj = 0;
		}
	    } else {
		if (cishort == IPCP_VJ_COMP || cishort == IPCP_VJ_COMP_OLD) {
		    try.old_vj = 1;
		    try.vj_protocol = cishort;
		} else {
		    try.neg_vj = 0;
		}
	    }
	    );

    NAKCIADDR(CI_MS_DNS1, neg_dns1, 0, try.dnsaddr[0]=ciaddr1; );

    NAKCIADDR(CI_MS_DNS2, neg_dns2, 0, try.dnsaddr[1]=ciaddr1; );

    NAKCIADDR(CI_MS_WINS1, neg_wins1, 0, try.winsaddr[0]=ciaddr1; );

    NAKCIADDR(CI_MS_WINS2, neg_wins2, 0, try.winsaddr[1]=ciaddr1; );


    /*
     * There may be remaining CIs, if the peer is requesting negotiation
     * on an option that we didn't include in our request packet.
     * If they want to negotiate about IP addresses, we comply.
     * If they want us to ask for compression, we refuse.
     */
    while (len > CILEN_VOID) {
	GETCHAR(citype, p);
	GETCHAR(cilen, p);
	if( (len -= cilen) < 0 )
	    goto bad;
	next = p + cilen - 2;

	switch (citype) {
	case CI_COMPRESSTYPE:
	    if (go->neg_vj || no.neg_vj ||
		(cilen != CILEN_VJ && cilen != CILEN_COMPRESS))
		goto bad;
	    no.neg_vj = 1;
	    break;
	case CI_ADDRS:
	    if ((go->neg_addr && go->old_addrs) || no.old_addrs
		|| cilen != CILEN_ADDRS)
		goto bad;
	    try.neg_addr = 1;
	    try.old_addrs = 1;
	    GETLONG(l, p);
	    ciaddr1 = htonl(l);
	    if (ciaddr1 && go->accept_local)
		try.ouraddr = ciaddr1;
	    GETLONG(l, p);
	    ciaddr2 = htonl(l);
	    if (ciaddr2 && go->accept_remote)
		try.hisaddr = ciaddr2;
	    no.old_addrs = 1;
	    break;
	case CI_ADDR:
	    if (go->neg_addr || no.neg_addr || cilen != CILEN_ADDR)
		goto bad;
	    try.old_addrs = 0;
	    GETLONG(l, p);
	    ciaddr1 = htonl(l);
	    if (ciaddr1 && go->accept_local)
		try.ouraddr = ciaddr1;
	    if (try.ouraddr != 0)
		try.neg_addr = 1;
	    no.neg_addr = 1;
	    break;
	}
	p = next;
    }

    /* If there is still anything left, this packet is bad. */
    if (len != 0)
	goto bad;

    /*
     * OK, the Nak is good.  Now we can update state.
     */
    if (f->state != OPENED)
	*go = try;

    return 1;

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_nakci: received bad Nak!"));
    return 0;
}


/*
 * ipcp_rejci - Reject some of our CIs.
 */
static int ipcp_rejci(fsm *f,u_char *p,int len)
{
    ipcp_options *go = &ipcp_gotoptions[lns[f->unit].ipcp_unit];
    u_char cimaxslotindex, ciflag, cilen;
    u_short cishort;
    u_int32_t cilong;
    ipcp_options try;		/* options to request next time */

    try = *go;
    /*
     * Any Rejected CIs must be in exactly the same order that we sent.
     * Check packet length and CI length at each step.
     * If we find any deviations, then this packet is bad.
     */
#define REJCIADDR(opt, neg, old, val1, val2) \
    if (go->neg && \
	len >= (cilen = old? CILEN_ADDRS: CILEN_ADDR) && \
	p[1] == cilen && \
	p[0] == opt) { \
	u_int32_t l; \
	len -= cilen; \
	INCPTR(2, p); \
	GETLONG(l, p); \
	cilong = htonl(l); \
	/* Check rejected value. */ \
	if (cilong != val1) \
	    goto bad; \
	if (old) { \
	    GETLONG(l, p); \
	    cilong = htonl(l); \
	    /* Check rejected value. */ \
	    if (cilong != val2) \
		goto bad; \
	} \
	try.neg = 0; \
    }

#define REJCIVJ(opt, neg, val, old, maxslot, cflag) \
    if (go->neg && \
	p[1] == (old? CILEN_COMPRESS : CILEN_VJ) && \
	len >= p[1] && \
	p[0] == opt) { \
	len -= p[1]; \
	INCPTR(2, p); \
	GETSHORT(cishort, p); \
	/* Check rejected value. */  \
	if (cishort != val) \
	    goto bad; \
	if (!old) { \
	   GETCHAR(cimaxslotindex, p); \
	   if (cimaxslotindex != maxslot) \
	     goto bad; \
	   GETCHAR(ciflag, p); \
	   if (ciflag != cflag) \
	     goto bad; \
        } \
	try.neg = 0; \
     }

    REJCIADDR((go->old_addrs? CI_ADDRS: CI_ADDR), neg_addr,
	      go->old_addrs, go->ouraddr, go->hisaddr);

    REJCIVJ(CI_COMPRESSTYPE, neg_vj, go->vj_protocol, go->old_vj,
	    go->maxslotindex, go->cflag);

	REJCIADDR(CI_MS_DNS1, neg_dns1, 0, go->dnsaddr[0], 0);

	REJCIADDR(CI_MS_DNS2, neg_dns2, 0, go->dnsaddr[1], 0);

	REJCIADDR(CI_MS_WINS1, neg_wins1, 0, go->winsaddr[0], 0);

	REJCIADDR(CI_MS_WINS2, neg_wins2, 0, go->winsaddr[1], 0);

    /*
     * If there are any remaining CIs, then this packet is bad.
     */
    if (len != 0)
	goto bad;
    /*
     * Now we can update state.
     */
    if (f->state != OPENED)
	*go = try;
    return 1;

bad:
    IPCPDEBUG((LOG_INFO, "ipcp_rejci: received bad Reject!"));
    return 0;
}


/*
 * ipcp_reqci - Check the peer's requested CIs and send appropriate response.
 *
 * Returns: CONFACK, CONFNAK or CONFREJ and input packet modified
 * appropriately.  If reject_if_disagree is non-zero, doesn't return
 * CONFNAK; returns CONFREJ if it can't return CONFACK.
 */
static int ipcp_reqci(fsm *f,u_char *inp,int *len,int reject_if_disagree)
{
    struct link_struct *tlns = &lns[f->unit];
    ipcp_options *wo = &ipcp_wantoptions[tlns->ipcp_unit];
    ipcp_options *ho = &ipcp_hisoptions[tlns->ipcp_unit];
    ipcp_options *ao = &ipcp_allowoptions[tlns->ipcp_unit];
    ipcp_options *go = &ipcp_gotoptions[tlns->ipcp_unit];
    u_char *cip, *next;		/* Pointer to current and next CIs */
    u_short cilen, citype;	/* Parsed len, type */
    u_short cishort;		/* Parsed short value */
    u_int32_t tl, ciaddr1, ciaddr2;/* Parsed address values */
    int rc = CONFACK;		/* Final packet return code */
    int orc;			/* Individual option return code */
    u_char *p;			/* Pointer to next char to parse */
    u_char *ucp = inp;		/* Pointer to current output char */
    int l = *len;		/* Length left */
    u_char maxslotindex, cflag;
    int d;

    cis_received[f->unit] = 1;

    /*
     * Reset all his options.
     */
    BZERO(ho, sizeof(*ho));
    
    /*
     * Process all his options.
     */
    next = inp;
    while (l) {
	orc = CONFACK;			/* Assume success */
	cip = p = next;			/* Remember begining of CI */
	if (l < 2 ||			/* Not enough data for CI header or */
	    p[1] < 2 ||			/*  CI length too small or */
	    p[1] > l) {			/*  CI length too big? */
	    IPCPDEBUG((LOG_INFO, "ipcp_reqci: bad CI length!"));
	    orc = CONFREJ;		/* Reject bad CI */
	    cilen = l;			/* Reject till end of packet */
	    l = 0;			/* Don't loop again */
	    goto endswitch;
	}
	GETCHAR(citype, p);		/* Parse CI type */
	GETCHAR(cilen, p);		/* Parse CI length */
	l -= cilen;			/* Adjust remaining length */
	next += cilen;			/* Step to next CI */

	switch (citype) {		/* Check CI type */
	case CI_ADDRS:
	    IPCPDEBUG((LOG_INFO, "ipcp: received ADDRS "));
	    if (!ao->neg_addr ||
		cilen != CILEN_ADDRS) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }

	    /*
	     * If he has no address, or if we both have his address but
	     * disagree about it, then NAK it with our idea.
	     * In particular, if we don't know his address, but he does,
	     * then accept it.
	     */
	    GETLONG(tl, p);		/* Parse source address (his) */
	    ciaddr1 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "(%s:", ip_ntoa(ciaddr1)));
	    if (ciaddr1 != wo->hisaddr
		&& (ciaddr1 == 0 || !wo->accept_remote)) {
		orc = CONFNAK;
		if (!reject_if_disagree) {
		    DECPTR(sizeof(u_int32_t), p);
		    tl = ntohl(wo->hisaddr);
		    PUTLONG(tl, p);
		}
	    } else if (ciaddr1 == 0 && wo->hisaddr == 0) {
		/*
		 * If neither we nor he knows his address, reject the option.
		 */
		orc = CONFREJ;
		wo->req_addr = 0;	/* don't NAK with 0.0.0.0 later */
		break;
	    }

	    /*
	     * If he doesn't know our address, or if we both have our address
	     * but disagree about it, then NAK it with our idea.
	     */
	    GETLONG(tl, p);		/* Parse desination address (ours) */
	    ciaddr2 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "%s)", ip_ntoa(ciaddr2)));
	    if (ciaddr2 != wo->ouraddr) {
		if (ciaddr2 == 0 || !wo->accept_local) {
		    orc = CONFNAK;
		    if (!reject_if_disagree) {
			DECPTR(sizeof(u_int32_t), p);
			tl = ntohl(wo->ouraddr);
			PUTLONG(tl, p);
		    }
		} else {
		    go->ouraddr = ciaddr2;	/* accept peer's idea */
		}
	    }

	    ho->neg_addr = 1;
	    ho->old_addrs = 1;
	    ho->hisaddr = ciaddr1;
	    ho->ouraddr = ciaddr2;
	    break;

	case CI_ADDR:
	    IPCPDEBUG((LOG_INFO, "ipcp: received ADDR "));

	    if (!ao->neg_addr ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }

	    /*
	     * If he has no address, or if we both have his address but
	     * disagree about it, then NAK it with our idea.
	     * In particular, if we don't know his address, but he does,
	     * then accept it.
	     */
	    GETLONG(tl, p);	/* Parse source address (his) */
	    ciaddr1 = htonl(tl);
	    IPCPDEBUG((LOG_INFO, "(%s)", ip_ntoa(ciaddr1)));
	    if (ciaddr1 != wo->hisaddr
		&& (ciaddr1 == 0 || !wo->accept_remote)) {
		orc = CONFNAK;
		if (!reject_if_disagree) {
		    DECPTR(sizeof(u_int32_t), p);
		    tl = ntohl(wo->hisaddr);
		    PUTLONG(tl, p);
		}
	    } else if (ciaddr1 == 0 && wo->hisaddr == 0) {
		/*
		 * Don't ACK an address of 0.0.0.0 - reject it instead.
		 */
		orc = CONFREJ;
		wo->req_addr = 0;	/* don't NAK with 0.0.0.0 later */
		break;
	    }
	
	    ho->neg_addr = 1;
	    ho->hisaddr = ciaddr1;
	    break;

	case CI_MS_DNS1:
	case CI_MS_DNS2:
		/* Microsoft primary or secondary DNS request */
		d = citype == CI_MS_DNS2;
		IPCPDEBUG((LOG_INFO, "ipcp: received DNS%d Request ", d+1));

	    /* If we do not have a DNS address then we cannot send it */
		if (ao->dnsaddr[d] == 0 ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }
		GETLONG(tl, p);
		if (htonl(tl) != ao->dnsaddr[d]) {
		DECPTR(sizeof(u_int32_t), p);
		tl = ntohl(ao->dnsaddr[d]);
		PUTLONG(tl, p);
		orc = CONFNAK;
            }
            break;


	case CI_MS_WINS1:
	case CI_MS_WINS2:
		/* Microsoft primary or secondary WINS request */
		d = citype == CI_MS_WINS2;
		IPCPDEBUG((LOG_INFO, "ipcp: received WINS%d Request ", d+1));

	    /* If we do not have a WINS address then we cannot send it */
		if (ao->winsaddr[d] == 0 ||
		cilen != CILEN_ADDR) {	/* Check CI length */
		orc = CONFREJ;		/* Reject CI */
		break;
	    }
		GETLONG(tl, p);
		if (htonl(tl) != ao->winsaddr[d]) {
		DECPTR(sizeof(u_int32_t), p);
		tl = ntohl(ao->winsaddr[d]);
		PUTLONG(tl, p);
		orc = CONFNAK;
            }
            break;


	case CI_COMPRESSTYPE:
	    IPCPDEBUG((LOG_INFO, "ipcp: received COMPRESSTYPE "));
	    if (!ao->neg_vj ||
		(cilen != CILEN_VJ && cilen != CILEN_COMPRESS)) {
		orc = CONFREJ;
		break;
	    }
	    GETSHORT(cishort, p);
	    IPCPDEBUG((LOG_INFO, "(%d)", cishort));

	    if (!(cishort == IPCP_VJ_COMP ||
		  (cishort == IPCP_VJ_COMP_OLD && cilen == CILEN_COMPRESS))) {
		orc = CONFREJ;
		break;
	    }

	    ho->neg_vj = 1;
	    ho->vj_protocol = cishort;
	    if (cilen == CILEN_VJ) {
		GETCHAR(maxslotindex, p);
		if (maxslotindex > ao->maxslotindex) { 
		    orc = CONFNAK;
		    if (!reject_if_disagree){
			DECPTR(1, p);
			PUTCHAR(ao->maxslotindex, p);
		    }
		}
		GETCHAR(cflag, p);
		if (cflag && !ao->cflag) {
		    orc = CONFNAK;
		    if (!reject_if_disagree){
			DECPTR(1, p);
			PUTCHAR(wo->cflag, p);
		    }
		}
		ho->maxslotindex = maxslotindex;
		ho->cflag = cflag;
	    } else {
		ho->old_vj = 1;
		ho->maxslotindex = MAX_STATES - 1;
		ho->cflag = 1;
	    }
	    break;

	default:
	    orc = CONFREJ;
	    break;
	}

endswitch:
	IPCPDEBUG((LOG_INFO, " (%s)\n", CODENAME(orc)));

	if (orc == CONFACK &&		/* Good CI */
	    rc != CONFACK)		/*  but prior CI wasnt? */
	    continue;			/* Don't send this one */

	if (orc == CONFNAK) {		/* Nak this CI? */
	    if (reject_if_disagree)	/* Getting fed up with sending NAKs? */
		orc = CONFREJ;		/* Get tough if so */
	    else {
		if (rc == CONFREJ)	/* Rejecting prior CI? */
		    continue;		/* Don't send this one */
		if (rc == CONFACK) {	/* Ack'd all prior CIs? */
		    rc = CONFNAK;	/* Not anymore... */
		    ucp = inp;		/* Backup */
		}
	    }
	}

	if (orc == CONFREJ &&		/* Reject this CI */
	    rc != CONFREJ) {		/*  but no prior ones? */
	    rc = CONFREJ;
	    ucp = inp;			/* Backup */
	}

	/* Need to move CI? */
	if (ucp != cip)
	    BCOPY(cip, ucp, cilen);	/* Move it */

	/* Update output pointer */
	INCPTR(cilen, ucp);
    }

    /*
     * If we aren't rejecting this packet, and we want to negotiate
     * their address, and they didn't send their address, then we
     * send a NAK with a CI_ADDR option appended.  We assume the
     * input buffer is long enough that we can append the extra
     * option safely.
     */
    if (rc != CONFREJ && !ho->neg_addr &&
	wo->req_addr && !reject_if_disagree) {
	if (rc == CONFACK) {
	    rc = CONFNAK;
	    ucp = inp;			/* reset pointer */
	    wo->req_addr = 0;		/* don't ask again */
	}
	PUTCHAR(CI_ADDR, ucp);
	PUTCHAR(CILEN_ADDR, ucp);
	tl = ntohl(wo->hisaddr);
	PUTLONG(tl, ucp);
    }

    *len = ucp - inp;			/* Compute output length */
    IPCPDEBUG((LOG_INFO, "ipcp: returning Configure-%s", CODENAME(rc)));
    return (rc);			/* Return final code */
}

/*
 * ipcp_up - IPCP has come UP.
 *
 * Configure the IP network interface appropriately and bring it up.
 */
static void ipcp_up(fsm *f)
{
	u_int32_t mask;
	struct link_struct *tlns = &lns[f->unit];
	ipcp_options *ho = &ipcp_hisoptions[tlns->ipcp_unit];
	ipcp_options *go = &ipcp_gotoptions[tlns->ipcp_unit];

	IPCPDEBUG((LOG_INFO, "ipcp: up"));
	go->default_route = 0;
	go->proxy_arp = 0;

	/*
	 * We must have a non-zero IP address for both ends of the link.
	 */
	if (!ho->neg_addr)
		ho->hisaddr = ipcp_wantoptions[tlns->ipcp_unit].hisaddr;

	if (ho->hisaddr == 0) {
		syslog(LOG_ERR, "Could not determine remote IP address");
		ipcp_close(tlns->ipcp_unit,"Could not determine remote IP address");
		return;
	}
	if (go->ouraddr == 0) {
		syslog(LOG_ERR, "Could not determine local IP address");
		ipcp_close(tlns->ipcp_unit,"Could not determine local IP address");
		return;
	}

	script_unsetenv_prefix("MS_DNS");
	if (go->dnsaddr[0] || go->dnsaddr[1]) {
		script_setenv("USEPEERDNS", "1");
		if (go->dnsaddr[0]) {
			script_setenv("DNS1", ip_ntoa(go->dnsaddr[0]));
			script_setenv("MS_DNS1", ip_ntoa(go->dnsaddr[0]));
		}
		if (go->dnsaddr[1]) {
			script_setenv("DNS2", ip_ntoa(go->dnsaddr[1]));
			script_setenv("MS_DNS2", ip_ntoa(go->dnsaddr[1]));
		}
		create_resolv(go->dnsaddr[0], go->dnsaddr[1]);
	}

	script_unsetenv_prefix("MS_WINS");

	if ( go->winsaddr[0] )
		script_setenv("MS_WINS1", ip_ntoa(go->winsaddr[0]));

	if ( go->winsaddr[1] )
		script_setenv("MS_WINS2", ip_ntoa(go->winsaddr[1]));

    /*
     * Check that the peer is allowed to use the IP address it wants.
     */
	if (!auth_ip_addr(f->unit, ho->hisaddr)) {
		syslog(LOG_ERR, "Peer is not authorized to use remote address %s",
		ip_ntoa(ho->hisaddr));
		ipcp_close(tlns->ipcp_unit,"Peer is not authorized to use remote address");
		return;
	}

	syslog(LOG_NOTICE, "local  IP address %s", ip_ntoa(go->ouraddr));
	syslog(LOG_NOTICE, "remote IP address %s", ip_ntoa(ho->hisaddr));

	/*
	 * Set IP addresses and (if specified) netmask.
	 */
	mask = GetMask(go->ouraddr);
	if (!sifaddr(f->unit, go->ouraddr, ho->hisaddr, mask)) {
		IPCPDEBUG((LOG_WARNING, "sifaddr failed"));
		ipcp_close(tlns->ipcp_unit,"sifaddr failed");
		return;
	}

	/*
	 * set tcp compression 
	 */
	sifvjcomp(f->unit, ho->neg_vj, ho->cflag, ho->maxslotindex);

	/*
	 * bring the interface up for IP
	 */
	if (!sifup(f->unit)) {
		IPCPDEBUG((LOG_WARNING, "sifup failed"));
		ipcp_close(tlns->ipcp_unit,"sifup failed");
		return;
	}

	/*
	 * assign a default route through the interface if required 
	 */
	if (ipcp_wantoptions[tlns->ipcp_unit].default_route) 
		if (sifdefaultroute(f->unit, ho->hisaddr))
			go->default_route = 1;

        /*
	 * Make a proxy ARP entry if requested.
	 */
	if (ipcp_wantoptions[tlns->ipcp_unit].proxy_arp)
		if (sifproxyarp(f->unit, ho->hisaddr))
			go->proxy_arp = 1;

#ifdef RADIUS
	/*
	 * Now we are really armed with enough information to send
	 * accounting START request to RADIUS
	 * we need hisaddr if we wont to send peers FRAMED_IP address
	 * to RADIUS server 
	 */
	 if ( useradacct && !tlns->radius_in )
	 {
	 	radius_acct_start (tlns->ipcp_unit) ;
	 }
#endif

	/*
	 * Execute the ip-up script, like this:
	 * /etc/ppp/ip-up interface tty speed local-IP remote-IP
	 */
	ipcp_script(f, _PATH_IPUP);

}


/*
 * ipcp_down - IPCP has gone DOWN.
 *
 * Take the IP network interface down, clear its addresses
 * and delete routes through it.
 */
static void ipcp_down(fsm *f)
{
    u_int32_t ouraddr, hisaddr;
    struct link_struct *tlns = &lns[f->unit];

    IPCPDEBUG((LOG_INFO, "ipcp: down"));

    ouraddr = ipcp_gotoptions[tlns->ipcp_unit].ouraddr;
    hisaddr = ipcp_hisoptions[tlns->ipcp_unit].hisaddr;
    if (ipcp_gotoptions[tlns->ipcp_unit].proxy_arp)
	cifproxyarp(f->unit, hisaddr);
    if (ipcp_gotoptions[tlns->ipcp_unit].default_route) 
	cifdefaultroute(f->unit, hisaddr);

#ifndef ISDN4LINUX_PATCH
    sifdown(f->unit);
    cifaddr(f->unit, ouraddr, hisaddr);
#endif

    /* Execute the ip-down script */
    ipcp_script(f, _PATH_IPDOWN);
}


/*
 * ipcp_script - Execute a script with arguments
 * interface-name tty-name speed local-IP remote-IP.
 */
static void ipcp_script(fsm *f,char *script)
{
    char strspeed[32], strlocal[32], strremote[32];
    char *argv[8];
    struct link_struct *tlns = &lns[f->unit];

    sprintf(strspeed, "%d", baud_rate);
    strcpy(strlocal, ip_ntoa(ipcp_gotoptions[tlns->ipcp_unit].ouraddr));
    strcpy(strremote, ip_ntoa(ipcp_hisoptions[tlns->ipcp_unit].hisaddr));

    argv[0] = script;
    argv[1] = lns[f->unit].ifname;
    argv[2] = lns[f->unit].devnam;
    argv[3] = strspeed;
    argv[4] = strlocal;
    argv[5] = strremote;
    argv[6] = ipparam;
    argv[7] = NULL;
    run_program(script, argv, debug,f->unit);
}

/*
 * ipcp_printpkt - print the contents of an IPCP packet.
 */
char *ipcp_codenames[] = {
    "ConfReq", "ConfAck", "ConfNak", "ConfRej",
    "TermReq", "TermAck", "CodeRej"
};

int ipcp_printpkt(u_char *p,int plen,void (*printer) __P((void *, char *, ...)),void *arg)
{
    int code, id, len, olen;
    u_char *pstart, *optend;
    u_short cishort;
    u_int32_t cilong;

    if (plen < HEADERLEN)
	return 0;
    pstart = p;
    GETCHAR(code, p);
    GETCHAR(id, p);
    GETSHORT(len, p);
    if (len < HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(ipcp_codenames) / sizeof(char *))
	printer(arg, " %s", ipcp_codenames[code-1]);
    else
	printer(arg, " code=0x%x", code);
    printer(arg, " id=0x%x", id);
    len -= HEADERLEN;
    switch (code) {
    case CONFREQ:
    case CONFACK:
    case CONFNAK:
    case CONFREJ:
	/* print option list */
	while (len >= 2) {
	    GETCHAR(code, p);
	    GETCHAR(olen, p);
	    p -= 2;
	    if (olen < 2 || olen > len) {
		break;
	    }
	    printer(arg, " <");
	    len -= olen;
	    optend = p + olen;
	    switch (code) {
	    case CI_ADDRS:
		if (olen == CILEN_ADDRS) {
		    p += 2;
		    GETLONG(cilong, p);
		    printer(arg, "addrs %s", ip_ntoa(htonl(cilong)));
		    GETLONG(cilong, p);
		    printer(arg, " %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    case CI_COMPRESSTYPE:
		if (olen >= CILEN_COMPRESS) {
		    p += 2;
		    GETSHORT(cishort, p);
		    printer(arg, "compress ");
		    switch (cishort) {
		    case IPCP_VJ_COMP:
			printer(arg, "VJ");
			break;
		    case IPCP_VJ_COMP_OLD:
			printer(arg, "old-VJ");
			break;
		    default:
			printer(arg, "0x%x", cishort);
		    }
		}
		break;
	    case CI_ADDR:
		if (olen == CILEN_ADDR) {
		    p += 2;
		    GETLONG(cilong, p);
		    printer(arg, "addr %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    case CI_MS_DNS1:
		if (olen == CILEN_ADDR) {
		    p+=2;
		    GETLONG(cilong,p);
		    printer(arg, "ms-dns1 %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    case CI_MS_DNS2:
		if (olen == CILEN_ADDR) {
		    p+=2;
		    GETLONG(cilong,p);
		    printer(arg, "ms-dns2 %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    case CI_MS_WINS1:
		if (olen == CILEN_ADDR) {
		    p+=2;
		    GETLONG(cilong,p);
		    printer(arg, "ms-wins1 %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    case CI_MS_WINS2:
		if (olen == CILEN_ADDR) {
		    p+=2;
		    GETLONG(cilong,p);
		    printer(arg, "ms-wins2 %s", ip_ntoa(htonl(cilong)));
		}
		break;
	    }
	    while (p < optend) {
		GETCHAR(code, p);
		printer(arg, " %.2x", code);
	    }
	    printer(arg, ">");
	}
	break;

    case TERMACK:
    case TERMREQ:
       if (len > 0 && *p >= ' ' && *p < 0x7f) {
           printer(arg, " ");
           print_string(p, len, printer, arg);
           p += len;
           len = 0;
       }
       break;
    }

    /* print the rest of the bytes in the packet */
    for (; len > 0; --len) {
	GETCHAR(code, p);
	printer(arg, " %.2x", code);
    }

    return p - pstart;
}


/*
 * create_resolv - create the replacement resolv.conf file
 */
static void
create_resolv(peerdns1, peerdns2)
    u_int32_t peerdns1, peerdns2;
{
    FILE *f;

    f = fopen(_PATH_RESOLV, "w");
    if (f == NULL) {
	syslog(LOG_ERR, "Failed to create %s: %m", _PATH_RESOLV);
        return;
    }

    if (peerdns1)
        fprintf(f, "nameserver %s\n", ip_ntoa(peerdns1));

    if (peerdns2)
        fprintf(f, "nameserver %s\n", ip_ntoa(peerdns2));

    if (ferror(f))
        syslog(LOG_ERR, "Write failed to %s: %m", _PATH_RESOLV);

    fclose(f);
}
