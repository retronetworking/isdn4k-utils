/*
 * ipxcp.c - PPP IPX Control Protocol.
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

char ipxcp_rcsid[] = "$Id$";

/*
 * TODO:
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

#include "ipppd.h"
#include "fsm.h"
#include "ipxcp.h"
#include "pathnames.h"

/* global vars */
ipxcp_options ipxcp_wantoptions[NUM_PPP];	/* Options that we want to request */
ipxcp_options ipxcp_gotoptions[NUM_PPP];	/* Options that peer ack'd */
ipxcp_options ipxcp_allowoptions[NUM_PPP];	/* Options we allow peer to request */
ipxcp_options ipxcp_hisoptions[NUM_PPP];	/* Options that we ack'd */

/*
 * Callbacks for fsm code.  (CI = Configuration Information)
 */
static void ipxcp_resetci __P((fsm *));	/* Reset our CI */
static int  ipxcp_cilen __P((fsm *));		/* Return length of our CI */
static void ipxcp_addci __P((fsm *, u_char *, int *)); /* Add our CI */
static int  ipxcp_ackci __P((fsm *, u_char *, int));	/* Peer ack'd our CI */
static int  ipxcp_nakci __P((fsm *, u_char *, int));	/* Peer nak'd our CI */
static int  ipxcp_rejci __P((fsm *, u_char *, int));	/* Peer rej'd our CI */
static int  ipxcp_reqci __P((fsm *, u_char *, int *, int)); /* Rcv CI */
static void ipxcp_up __P((fsm *));		/* We're UP */
static void ipxcp_down __P((fsm *));		/* We're DOWN */
static void ipxcp_script __P((fsm *, char *)); /* Run an up/down script */

fsm ipxcp_fsm[NUM_PPP];		/* IPXCP fsm structure */

static fsm_callbacks ipxcp_callbacks = { /* IPXCP callback routines */
    ipxcp_resetci,		/* Reset our Configuration Information */
    ipxcp_cilen,		/* Length of our Configuration Information */
    ipxcp_addci,		/* Add our Configuration Information */
    ipxcp_ackci,		/* ACK our Configuration Information */
    ipxcp_nakci,		/* NAK our Configuration Information */
    ipxcp_rejci,		/* Reject our Configuration Information */
    ipxcp_reqci,		/* Request peer's Configuration Information */
    ipxcp_up,			/* Called when fsm reaches OPENED state */
    ipxcp_down,			/* Called when fsm leaves OPENED state */
    NULL,			/* Called when we want the lower layer up */
    NULL,			/* Called when we want the lower layer down */
    NULL,			/* Called when Protocol-Reject received */
    NULL,			/* Retransmission is necessary */
    NULL,			/* Called to handle protocol-specific codes */
    "IPXCP"			/* String name of protocol */
};

/*
 * Protocol entry points.
 */

static void ipxcp_init __P((int));
static void ipxcp_open __P((int));
static void ipxcp_close __P((int, char *));
static void ipxcp_lowerup __P((int));
static void ipxcp_lowerdown __P((int));
static void ipxcp_input __P((int, u_char *, int));
static void ipxcp_protrej __P((int));
static int  ipxcp_printpkt __P((u_char *, int,
                void (*) __P((void *, char *, ...)), void *));

struct protent ipxcp_protent = {
    PPP_IPXCP,
    ipxcp_init,
    ipxcp_input,
    ipxcp_protrej,
    ipxcp_lowerup,
    ipxcp_lowerdown,
    ipxcp_open,
    ipxcp_close,
    ipxcp_printpkt,
    NULL,
    0,
    "IPXCP",
    NULL,
    NULL,
    NULL
};

/*
 * Lengths of configuration options.
 */

/*
IPX-ROUTING-PROTOCOL OPTIONS

     Value           Protocol                          Reference
     -----           --------                          ---------
        0             No routing protocol required     [RFC1552]
        1             RESERVED                         [RFC1552]
        2             Novell RIP/SAP required          [RFC1552]
        4             Novell NLSP required             [RFC1552]
        5             Novell Demand RIP required       [RFC1582]
        6             Novell Demand SAP required       [RFC1582]
        7             Novell Triggered RIP required [Edmonstone]
        8             Novell Triggered SAP required [Edmonstone]
*/


#define CILEN_VOID	2
#define CILEN_COMPLETE	2	/* length of complete option */
#define CILEN_NETN	6	/* network number length option */
#define CILEN_NODEN	8	/* node number length option */
#define CILEN_PROTOCOL	4	/* Minimum length of routing protocol */
#define CILEN_NAME	3	/* Minimum length of router name */
#define CILEN_COMPRESS	4	/* Minimum length of compression protocol */

#define CODENAME(x)	((x) == CONFACK ? "ACK" : \
			 (x) == CONFNAK ? "NAK" : "REJ")

/* Used in printing the node number */
#define NODE(base) base[0], base[1], base[2], base[3], base[4], base[5]

/* Used to generate the proper bit mask */
/* the whole BIT stuff in the code was wrong .. seldom saw such a shit */
#define BIT(num)   (1 << (num))

/*
 * Make a string representation of a network IP address.
 */

char *
ipx_ntoa(ipxaddr)
u_int32_t ipxaddr;
{
    static char b[64];
    sprintf(b, "%lx",(long) ipxaddr);
    return b;
}


/*
 * ipxcp_init - Initialize IPXCP.
 */
static void ipxcp_init(int unit)
{
    fsm *f = &ipxcp_fsm[unit];
    ipxcp_options *wo = &ipxcp_wantoptions[unit];
    ipxcp_options *ao = &ipxcp_allowoptions[unit];

	memset(f,0,sizeof(fsm));

    f->unit	 = -1;
    f->protocol	 = PPP_IPXCP;
    f->callbacks = &ipxcp_callbacks;
    f->inuse = 0;
    fsm_init(f);

    memset (wo->name,	  0, sizeof (wo->name));
    memset (wo->our_node, 0, sizeof (wo->our_node));
    memset (wo->his_node, 0, sizeof (wo->his_node));

    wo->neg_nn	       = 1;
    wo->neg_complete   = 1;
    wo->network	       = 0;

    ao->neg_node       = 1;
    ao->neg_nn	       = 1;
    ao->neg_name       = 1;
    ao->neg_complete   = 1;
    ao->neg_router     = 1;

    ao->accept_local   = 0;
    ao->accept_remote  = 0;
    ao->accept_network = 0;
}

int ipxcp_getunit(int linkunit)
{
  int i;
  for(i=0;i<NUM_PPP;i++)
    if(!ipxcp_fsm[i].inuse)
    {
      ipxcp_fsm[i].inuse = 1;
      ipxcp_fsm[i].state = INITIAL;
      ipxcp_fsm[i].flags = 0;
      ipxcp_fsm[i].id = 0;
      ipxcp_fsm[i].unit = linkunit;
      return i;
    }
  syslog(LOG_ERR,"No more ipxcp_units available");
  return -1;
}

void ipxcp_freeunit(int unit)
{
  ipxcp_fsm[unit].inuse = 0;
  ipxcp_fsm[unit].unit = -1;
}


/*
 * Copy the node number
 */

static void copy_node (int unit,u_char *src,u_char *dst)
{
    memcpy (dst, src, sizeof (ipxcp_wantoptions[unit].our_node));
}

/*
 * Compare node numbers
 */

static int compare_node (int unit,u_char *src,u_char *dst)
{
    return memcmp (dst, src, sizeof (ipxcp_wantoptions[unit].our_node)) == 0;
}

/*
 * Is the node number zero?
 */

static int zero_node (int unit,u_char *node)
{
    int indx;
    for (indx = 0; indx < sizeof (ipxcp_wantoptions[unit].our_node); ++indx)
	if (node [indx] != 0)
	    return 0;
    return 1;
}

/*
 * Increment the node number
 */

static void inc_node (u_char *node)
{
    u_char   *outp;
    u_int32_t magic_num;

    outp      = node;
    magic_num = magic();
    *outp++   = '\0';
    *outp++   = '\0';
    PUTLONG (magic_num, outp);
}

/*
 * ipxcp_open - IPXCP is allowed to come up.
 */
static void ipxcp_open(int unit)
{
    fsm_open(&ipxcp_fsm[unit]);
}

/*
 * ipxcp_close - Take IPXCP down.
 */
static void ipxcp_close(int unit,char *reason)
{
    fsm_close(&ipxcp_fsm[unit],reason);
}


/*
 * ipxcp_lowerup - The lower layer is up.
 */
static void ipxcp_lowerup(int unit)
{
    fsm_lowerup(&ipxcp_fsm[unit]);
}


/*
 * ipxcp_lowerdown - The lower layer is down.
 */
static void ipxcp_lowerdown(int unit)
{
    fsm_lowerdown(&ipxcp_fsm[unit]);
}


/*
 * ipxcp_input - Input IPXCP packet.
 */
static void ipxcp_input(int unit,u_char *p,int len)
{
    fsm_input(&ipxcp_fsm[unit], p, len);
}


/*
 * ipxcp_protrej - A Protocol-Reject was received for IPXCP.
 *
 * Pretend the lower layer went down, so we shut up.
 */
static void ipxcp_protrej(int unit)
{
    fsm_lowerdown(&ipxcp_fsm[unit]);
}


/*
 * ipxcp_resetci - Reset our CI.
 */
static void ipxcp_resetci(fsm *f)
{
/*    u_int32_t network; */
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *wo = &ipxcp_wantoptions[unit];
    ipxcp_options *ao = &ipxcp_allowoptions[unit];
    ipxcp_options *go = &ipxcp_gotoptions[unit];


    wo->req_node = wo->neg_node && ao->neg_node;
    wo->req_nn	 = wo->neg_nn	&& ao->neg_nn;

    if (wo->our_network == 0) {
	wo->neg_node	   = 1;
	ao->accept_network = 1;
    }
/*
 * If our node number is zero then change it.
 */
    if (zero_node (unit,wo->our_node)) {
	inc_node (wo->our_node);
	ao->accept_local = 1;
	wo->neg_node	 = 1;
    }
/*
 * If his node number is zero then change it.
 */
    if (zero_node (unit,wo->his_node)) {
	inc_node (wo->his_node);
	ao->accept_remote = 1;
    }

	if (wo->num_router)
		wo->neg_router = 1;
    *go = *wo;
}

/*
 * ipxcp_cilen - Return length of our CI.
 */
static int ipxcp_cilen(fsm *f)
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *go = &ipxcp_gotoptions[unit];
    int len;

    len	 = go->neg_nn	    ? CILEN_NETN     : 0;
    len += go->neg_node	    ? CILEN_NODEN    : 0;
    len += go->neg_name	    ? CILEN_NAME + strlen (go->name) - 1 : 0;
    len += go->neg_complete ? CILEN_COMPLETE : 0;

/*
 * Router protocol 0 is mutually exclusive with the others.
 */
	if (go->neg_router) {
		int i,nonnull=0;
		for(i=0;i<go->num_router;i++) {
			if (go->router[i] == 0) {
				if(nonnull || go->num_router > 1) {
					IPXCPDEBUG((LOG_INFO, "ipxcp_cilen ignored router proto 0"));
					continue;
				}
				len += CILEN_PROTOCOL;
			}
			else {
				nonnull = 1;
				len += CILEN_PROTOCOL;
			}
		}
    }

    return (len);
}


/*
 * ipxcp_addci - Add our desired CIs to a packet.
 */
static void ipxcp_addci(fsm *f,u_char *ucp,int *lenp)
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *go = &ipxcp_gotoptions[unit];
	u_char *ucp1 = ucp;

/*
 * Add the options to the record.
 */

	if (go->neg_nn) {
		PUTCHAR (IPX_NETWORK_NUMBER, ucp);
		PUTCHAR (CILEN_NETN, ucp);
		PUTLONG (go->our_network, ucp);
	}

	if (go->neg_node) {
		int indx;
		PUTCHAR (IPX_NODE_NUMBER, ucp);
		PUTCHAR (CILEN_NODEN, ucp);
		for (indx = 0; indx < sizeof (go->our_node); ++indx)
			PUTCHAR (go->our_node[indx], ucp);
	}

	if (go->neg_name) {
		int cilen = strlen (go->name);
		int indx;
		PUTCHAR (IPX_ROUTER_NAME, ucp);
		PUTCHAR (CILEN_NAME + cilen - 1, ucp);
		for (indx = 0; indx < cilen; ++indx)
			PUTCHAR (go->name [indx], ucp);
	}

	if (go->neg_router) {
		int i,nonnull=0;
		for(i=0;i<go->num_router;i++) {
			if(go->router[i] == 0) {
				if(nonnull || go->num_router > 1)
					continue;
			}
			else
				nonnull = 1;
			PUTCHAR  (IPX_ROUTER_PROTOCOL, ucp);
			PUTCHAR  (CILEN_PROTOCOL,      ucp);
			PUTSHORT (go->router[i],       ucp);
		}
    }

	if (go->neg_complete) {
		PUTCHAR (IPX_COMPLETE,	 ucp);
		PUTCHAR (CILEN_COMPLETE, ucp);
	}

    if (ucp - ucp1 != *lenp) {
        syslog(LOG_ERR, "Bug in ipxcp_addci: wrong length");
    }
}

/*
 * ipxcp_ackci - Ack our CIs.
 *
 * Returns:
 *	0 - Ack was bad.
 *	1 - Ack was good.
 */
static int
ipxcp_ackci(f, p, len)
    fsm *f;
    u_char *p;
    int len;
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *go = &ipxcp_gotoptions[unit];
    u_short cilen, citype, cishort;
    u_char cichar;
    u_int32_t cilong;

#define ACKCIVOID(opt, neg) \
    if (neg) { \
	if ((len -= CILEN_VOID) < 0) \
	    break; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != CILEN_VOID || \
	    citype != opt) \
	    break; \
    }

#define ACKCICOMPLETE(opt,neg)	ACKCIVOID(opt, neg)

#define ACKCICHARS(opt, neg, val, cnt) \
    if (neg) { \
	int indx, count = cnt; \
	len -= (count + 2); \
	if (len < 0) \
	    break; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != (count + 2) || \
	    citype != opt) \
	    break; \
	for (indx = 0; indx < count; ++indx) {\
	    GETCHAR(cichar, p); \
	    if (cichar != ((u_char *) &val)[indx]) \
	       break; \
	}\
	if (indx != count) \
	    break; \
    }

#define ACKCINODE(opt,neg,val) ACKCICHARS(opt,neg,val,sizeof(val))
#define ACKCINAME(opt,neg,val) ACKCICHARS(opt,neg,val,strlen(val))

#define ACKCINETWORK(opt, neg, val) \
    if (neg) { \
	if ((len -= CILEN_NETN) < 0) \
	    break; \
	GETCHAR(citype, p); \
	GETCHAR(cilen, p); \
	if (cilen != CILEN_NETN || \
	    citype != opt) \
	    break; \
	GETLONG(cilong, p); \
	if (cilong != val) \
	    break; \
    }

/*
 * Process the ACK frame in the order in which the frame was assembled
 */
	do {
		ACKCINETWORK  (IPX_NETWORK_NUMBER,  go->neg_nn,	    go->our_network);
		ACKCINODE     (IPX_NODE_NUMBER,	    go->neg_node,   go->our_node);
		ACKCINAME     (IPX_ROUTER_NAME,	    go->neg_name,   go->name);
		if(p[0] == IPX_ROUTER_PROTOCOL && go->neg_router && p[1] == CILEN_PROTOCOL && len >= p[1]) {
			int i;
			INCPTR(2, p);
			len -= CILEN_PROTOCOL;
			GETSHORT(cishort, p);
			for(i=0;i<go->num_router;i++) {
				if(cishort == go->router[i]) {
					go->router[0] = go->router[i];
					go->num_router = 1;
					i = 0;
					break;
				}
			}
			if(i == go->num_router)
				break;
		}
		ACKCICOMPLETE (IPX_COMPLETE,	    go->neg_complete);
		if (len == 0)
			return (1);
	} while (0);
/*
 * The frame is invalid
 */
    IPXCPDEBUG((LOG_INFO, "ipxcp_ackci: received bad Ack!"));
    return (0);
}

/*
 * ipxcp_nakci - Peer has sent a NAK for some of our CIs.
 * This should not modify any state if the Nak is bad
 * or if IPXCP is in the OPENED state.
 *
 * Returns:
 *	0 - Nak was bad.
 *	1 - Nak was good.
 */

static int
ipxcp_nakci(f, p, len)
    fsm *f;
    u_char *p;
    int len;
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
	int i;
    ipxcp_options *go = &ipxcp_gotoptions[unit];
    ipxcp_options *ao = &ipxcp_allowoptions[unit];
    ipxcp_options *ho = &ipxcp_hisoptions[unit];

    u_char citype, cilen, *next;
    u_short s;
    u_int32_t l;
    ipxcp_options no;		/* options we've seen Naks for */
    ipxcp_options try;		/* options to request next time */

    BZERO(&no, sizeof(no));
    try = *go;

    while (len > CILEN_VOID) {
	GETCHAR (citype, p);
	GETCHAR (cilen,	 p);
	len -= cilen;
	if (len < 0)
	    goto bad;
	next = &p [cilen - CILEN_VOID];

	switch (citype) {
	case IPX_NETWORK_NUMBER:
	    if (!go->neg_nn || no.neg_nn || (cilen != CILEN_NETN))
		goto bad;
	    no.neg_nn = 1;

	    GETLONG(l, p);
	    IPXCPDEBUG((LOG_INFO, "local IP address %d", l));
	    if (l && ao->accept_network)
		try.our_network = l;
	    break;

	case IPX_NODE_NUMBER:
	    if (!go->neg_node || no.neg_node || (cilen != CILEN_NODEN))
		goto bad;
	    no.neg_node = 1;

	    IPXCPDEBUG((LOG_INFO,
			"local node number %02X%02X%02X%02X%02X%02X",
			NODE(p)));

	    if (!zero_node (unit,p) && ao->accept_local &&
		! compare_node (unit,p, ho->his_node))
		copy_node (unit,p, try.our_node);
	    break;

	    /* These have never been sent. Ignore the NAK frame */
	case IPX_COMPRESSION_PROTOCOL:
	    goto bad;

	case IPX_ROUTER_PROTOCOL:
		if (!go->neg_router || (cilen < CILEN_PROTOCOL))
			goto bad;
		GETSHORT (s, p);
		for(i=0;i<go->num_router;i++) {
			if(go->router[i] == s) {
				go->router[0] = s;
				go->num_router = 1;
				i = 0;
				IPXCPDEBUG((LOG_INFO, "Router protocol number %d", s));
				break;
			}
		}
		if(i == go->num_router) {
			go->router[0] = 0;
			go->num_router = 1;
			IPXCPDEBUG((LOG_INFO, "try Router protocol number %d", s));
		}
		break;

	    /* These, according to the RFC, must never be NAKed. */
	case IPX_ROUTER_NAME:
	case IPX_COMPLETE:
	    goto bad;

	    /* These are for options which we have not seen. */
	default:
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
    IPXCPDEBUG((LOG_INFO, "ipxcp_nakci: received bad Nak!"));
    return 0;
}

/*
 * ipxcp_rejci - Reject some of our CIs.
 */
static int ipxcp_rejci(fsm *f,u_char *p,int len)
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *go = &ipxcp_gotoptions[unit];

    u_short cishort;
    u_char cichar;
    u_int32_t cilong;
    ipxcp_options try;		/* options to request next time */

#define REJCINETWORK(opt, neg, val) \
	if (neg && p[1] == CILEN_NETN && len >= p[1] && p[0] == opt) { \
		neg = 0; \
		INCPTR(2, p); \
		len -= CILEN_NETN; \
		GETLONG(cilong, p); \
		if (cilong != val) \
			break; \
		IPXCPDEBUG((LOG_INFO,"ipxcp_rejci rejected network 0x%08x", val)); \
    }

#define REJCICHARS(opt, neg, val, cnt) \
	if (neg && p[1] == cnt + 2 && p[1] >= len && p[0] == opt) { \
		int indx, count = cnt; \
		neg = 0; \
		INCPTR(2, p); \
		len -= (cnt + 2); \
		for (indx = 0; indx < count; ++indx) {\
			GETCHAR(cichar, p); \
			if (cichar != ((u_char *) &val)[indx]) \
				break; \
		}\
		if (indx != count) \
			break; \
		IPXCPDEBUG((LOG_INFO,"ipxcp_rejci rejected opt %d", opt)); \
	}

#define REJCINODE(opt,neg,val) REJCICHARS(opt,neg,val,sizeof(val))
#define REJCINAME(opt,neg,val) REJCICHARS(opt,neg,val,strlen(val))

#define REJCIVOID(opt, neg) \
	if (neg && p[1] == CILEN_VOID && len >= p[1] && p[0] == opt) { \
		neg = 0; \
		INCPTR(2, p); \
		len -= CILEN_VOID; \
		IPXCPDEBUG((LOG_INFO, "ipxcp_rejci rejected void opt %d", opt)); \
	}

/*
 * Any Rejected CIs must be in exactly the same order that we sent.
 * Check packet length and CI length at each step.
 * If we find any deviations, then this packet is bad.
 */
    try = *go;

	do {
		REJCINETWORK (IPX_NETWORK_NUMBER,  try.neg_nn,	   try.our_network);
		REJCINODE    (IPX_NODE_NUMBER,	   try.neg_node,   try.our_node);
		if(p[0] == IPX_ROUTER_PROTOCOL && try.neg_router &&  p[1] == CILEN_PROTOCOL && len >= p[1]) {
			int i;
			INCPTR(2, p);
			len -= CILEN_PROTOCOL;
			GETSHORT(cishort, p);
			IPXCPDEBUG((LOG_INFO, "ipxcp_rejci rejected router proto 0x%04x", cishort));
			for(i=0;i<try.num_router;i++) {
				if(try.router[i] == cishort)
					break;
			}
			if(i == try.num_router)
				break;
			try.num_router--;
			try.router[i] = try.router[try.num_router];
		}
		REJCINAME    (IPX_ROUTER_NAME,	   try.neg_name,   try.name);
		REJCIVOID    (IPX_COMPLETE,	   try.neg_complete);

		if (len == 0) {
		    if (f->state != OPENED)
			*go = try;
		    return (1);
		}
    } while (0);
/*
 * The frame is invalid at this point.
 */
    IPXCPDEBUG((LOG_INFO, "ipxcp_rejci: received bad Reject!"));
    return 0;
}

/*
 * ipxcp_reqci - Check the peer's requested CIs and send appropriate response.
 *
 * Returns: CONFACK, CONFNAK or CONFREJ and input packet modified
 * appropriately.  If reject_if_disagree is non-zero, doesn't return
 * CONFNAK; returns CONFREJ if it can't return CONFACK.
 */
static int
ipxcp_reqci(f, inp, len, reject_if_disagree)
    fsm *f;
    u_char *inp;		/* Requested CIs */
    int *len;			/* Length of requested CIs */
    int reject_if_disagree;
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *go = &ipxcp_gotoptions[unit];
    ipxcp_options *ao = &ipxcp_allowoptions[unit];
    ipxcp_options *ho = &ipxcp_hisoptions[unit];
    ipxcp_options *wo = &ipxcp_wantoptions[unit];

    u_char *cip, *next;		/* Pointer to current and next CIs */
    u_short cilen, citype,cishort;	/* Parsed len, type */
/*    u_short ts; */	/* Parsed short value */
    u_int32_t cinetwork;/* Parsed address values */
/*    u_int32_t tl,outnet; */ /* Parsed address values */
    int rc = CONFACK;		/* Final packet return code */
    int orc;			/* Individual option return code */
    u_char *p;			/* Pointer to next char to parse */
    u_char *ucp = inp;		/* Pointer to current output char */
    int l = *len;		/* Length left */
/*    u_char maxslotindex, cflag; */

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
	    IPXCPDEBUG((LOG_INFO, "ipxcp_reqci: bad CI length!"));
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
/*
 * The network number must match. Choose the larger of the two.
 */
	case IPX_NETWORK_NUMBER:
	    IPXCPDEBUG((LOG_INFO, "ipxcp: received Network Number request"));
	    
	    /* if we wont negotiate the network number or the length is wrong
	       then reject the option */
	    if ( !ao->neg_nn || cilen != CILEN_NETN ) {
		orc = CONFREJ;
		break;		
	    }
	    GETLONG(cinetwork, p);
	    IPXCPDEBUG((LOG_INFO,"Remote proposed IPX network number is %8Lx",cinetwork));

	    /* If the network numbers match then acknowledge them. */
	    if (cinetwork != 0) {
		ho->his_network = cinetwork;
		ho->neg_nn	= 1;
		if (wo->our_network == cinetwork)
		    break;
/*
 * If the network number is not given or we don't accept their change or
 * the network number is too small then NAK it.
 */
		if (! ao->accept_network || cinetwork < wo->our_network) {
		    DECPTR (sizeof (u_int32_t), p);
		    PUTLONG (wo->our_network, p);
		    orc = CONFNAK;
		}
		break;
	    }
/*
 * The peer sent '0' for the network. Give it ours if we have one.
 */
	    if (go->our_network != 0) {
		DECPTR (sizeof (u_int32_t), p);
		PUTLONG (wo->our_network, p);
		orc = CONFNAK;
/*
 * We don't have one. Reject the value.
 */
	    } else
		orc = CONFREJ;

	    break;
/*
 * The node number is required
 */
	case IPX_NODE_NUMBER:
	    IPXCPDEBUG((LOG_INFO, "ipxcp: received Node Number request"));

	    /* if we wont negotiate the node number or the length is wrong
	       then reject the option */
	    if ( cilen != CILEN_NODEN ) {
		orc = CONFREJ;
		break;
	    }

	    copy_node (unit ,p, ho->his_node);
	    ho->neg_node = 1;
/*
 * If the remote does not have a number and we do then NAK it with the value
 * which we have for it. (We never have a default value of zero.)
 */
	    if (zero_node (unit,ho->his_node)) {
		orc = CONFNAK;
		copy_node (unit,wo->his_node, p);
		INCPTR (sizeof (wo->his_node), p);
		break;
	    }
/*
 * If you have given me the expected network node number then I'll accept
 * it now.
 */
	    if (compare_node (unit,wo->his_node, ho->his_node)) {
		orc = CONFACK;
		ho->neg_node = 1;
		INCPTR (sizeof (wo->his_node), p);
		break;
	    }
/*
 * If his node number is the same as ours then ask him to try the next
 * value.
 */
	    if (compare_node (unit,ho->his_node, go->our_node)) {
		inc_node (ho->his_node);
		orc = CONFNAK;
		copy_node (unit,ho->his_node, p);
		INCPTR (sizeof (wo->his_node), p);
		break;
	    }
/*
 * If we don't accept a new value then NAK it.
 */
	    if (! ao->accept_remote) {
		copy_node (unit,wo->his_node, p);
		INCPTR (sizeof (wo->his_node), p);
		orc = CONFNAK;
		break;
	    }
	    orc = CONFACK;
	    ho->neg_node = 1;
	    INCPTR (sizeof (wo->his_node), p);
	    break;
/*
 * Compression is not desired at this time. It is always rejected.
 */
	case IPX_COMPRESSION_PROTOCOL:
	    IPXCPDEBUG((LOG_INFO, "ipxcp: received Compression Protocol request "));
	    orc = CONFREJ;
	    break;
	case IPX_ROUTER_PROTOCOL:
		if ( !ao->neg_router || cilen < CILEN_PROTOCOL ) {
			orc = CONFREJ;
			break;		
		}
	    GETSHORT (cishort, p);
		IPXCPDEBUG((LOG_INFO, "Remote router protocol number %d", cishort));

		if (cishort != 0 && cishort != 2 && cishort != 4) {
			orc = CONFREJ;
			break;
		}

		ho->router[ho->num_router] = cishort;
		ho->num_router++;
		if(ho->num_router > 31)
			ho->num_router = 31;
		ho->neg_router = 1;
		break;

	case IPX_ROUTER_NAME:
		IPXCPDEBUG((LOG_INFO, "ipxcp: received Router Name request"));
		if (cilen >= CILEN_NAME) {
			int name_size = cilen - CILEN_NAME;
			if (name_size > sizeof (ho->name))
				name_size = sizeof (ho->name) - 1;
			memset (ho->name, 0, sizeof (ho->name));
			memcpy (ho->name, p, name_size);
			ho->name [name_size] = '\0';
			ho->neg_name = 1;
			orc = CONFACK;
			break;
		}
		orc = CONFREJ;
		break;

	case IPX_COMPLETE:
		IPXCPDEBUG((LOG_INFO, "ipxcp: received Complete request"));
		if (cilen != CILEN_COMPLETE)
			orc = CONFREJ;
		else {
			ho->neg_complete = 1;
			orc = CONFACK;
		}
		break;

	default:
		IPXCPDEBUG((LOG_INFO, "ipxcp: received Complete request"));
		orc = CONFREJ;
		break;
	}

endswitch:
	IPXCPDEBUG((LOG_INFO, " (%s)\n", CODENAME(orc)));

	if (orc == CONFACK &&		/* Good CI */
	    rc != CONFACK)		/*  but prior CI wasnt? */
	    continue;			/* Don't send this one */

	if (orc == CONFNAK) {		/* Nak this CI? */
	    if (reject_if_disagree)	/* Getting fed up with sending NAKs? */
		orc = CONFREJ;		/* Get tough if so */
	    if (rc == CONFREJ)		/* Rejecting prior CI? */
		continue;		/* Don't send this one */
	    if (rc == CONFACK) {	/* Ack'd all prior CIs? */
		rc  = CONFNAK;		/* Not anymore... */
		ucp = inp;		/* Backup */
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
     * send a NAK with a IPX_NODE_NUMBER option appended. We assume the
     * input buffer is long enough that we can append the extra
     * option safely.
     */

    if (rc != CONFREJ && !ho->neg_node &&
	wo->req_nn && !reject_if_disagree) {
/*	u_char *ps; */
	if (rc == CONFACK) {
	    rc = CONFNAK;
	    wo->req_nn = 0;		/* don't ask again */
	    ucp = inp;			/* reset pointer */
	}

	if (zero_node (unit,wo->his_node))
	    inc_node (wo->his_node);

	PUTCHAR (IPX_NODE_NUMBER, ucp);
	PUTCHAR (CILEN_NODEN, ucp);
	copy_node (unit,wo->his_node, ucp);
	INCPTR (sizeof (wo->his_node), ucp);
    }

    *len = ucp - inp;			/* Compute output length */
    IPXCPDEBUG((LOG_INFO, "ipxcp: returning Configure-%s", CODENAME(rc)));
    return (rc);			/* Return final code */
}

/*
 * ipxcp_up - IPXCP has come UP.
 *
 * Configure the IP network interface appropriately and bring it up.
 */

static void
ipxcp_up(f)
    fsm *f;
{
    int linkunit = f->unit;
    int unit = lns[linkunit].ipxcp_unit;
    ipxcp_options *ho = &ipxcp_hisoptions[unit];
    ipxcp_options *wo = &ipxcp_wantoptions[unit];
    ipxcp_options *go = &ipxcp_gotoptions[unit];

    IPXCPDEBUG((LOG_INFO, "ipxcp: up"));

    if (!ho->neg_nn)
	ho->his_network = wo->his_network;

    if (!ho->neg_node)
	copy_node (unit,wo->his_node, ho->his_node);

    if (!wo->neg_node && !go->neg_node)
	copy_node (unit,wo->our_node, go->our_node);

    if (zero_node (unit,go->our_node)) {
	IPXCPDEBUG((LOG_ERR, "Could not determine local IPX node address"));
	ipxcp_close(unit,"Could not determine local IPX node address");
	return;
    }

    go->network = go->our_network;
    if (ho->his_network != 0 && ho->his_network > go->network)
	go->network = ho->his_network;

    if (go->network == 0) {
	IPXCPDEBUG((LOG_ERR, "Could not determine network number"));
	ipxcp_close (unit,"Could not determine network number");
	return;
    }

    /* bring the interface up */
    if (!sifup(unit)) {
	IPXCPDEBUG((LOG_WARNING, "sifup failed"));
	ipxcp_close(unit,"Interface configuration failed");
	return;
    }

    /* set the network number for IPX */
    if (!sipxfaddr(unit, go->network, go->our_node)) {
	IPXCPDEBUG((LOG_WARNING, "sipxfaddr failed"));
	ipxcp_close(unit,"Interface configuration failed");
	return;
    }

    /*
     * Execute the ipx-up script, like this:
     *	/etc/ppp/ipx-up interface tty speed local-IPX remote-IPX
     */
	ipxcp_script (f, _PATH_IPXUP);
}

/*
 * ipxcp_down - IPXCP has gone DOWN.
 *
 * Take the IP network interface down, clear its addresses
 * and delete routes through it.
 */

static void ipxcp_down(fsm *f)
{
/*    u_int32_t ournn, network; */

    IPXCPDEBUG((LOG_INFO, "ipxcp: down"));

    cipxfaddr (f->unit);
#if 0
    sifdown(f->unit);
#endif

	ipxcp_script (f, _PATH_IPXDOWN);
}


/*
 * ipxcp_script - Execute a script with arguments
 * interface-name tty-name speed local-IPX remote-IPX networks.
 */
static void ipxcp_script(fsm *f,char *script)
{
	int	 linkunit = f->unit;
	int unit = lns[linkunit].ipxcp_unit;
	ipxcp_options *ho = &ipxcp_hisoptions[unit];
	ipxcp_options *go = &ipxcp_gotoptions[unit];

	char strspeed[32],	 strlocal[32],	   strremote[32];
	char strnetwork[32], strpid[32];
	char *argv[14];
	char strproto_lcl[32], strproto_rmt[32];

	sprintf (strpid,   "%d", getpid());
	sprintf (strspeed, "%d", baud_rate);

	strproto_lcl[0] = '\0';
	if (go->neg_router) {
		if (go->router[0] == 2 )
			strcpy (strproto_lcl, "RIP ");
		if (go->router[0] == 4 )
			strcpy (strproto_lcl, "NLSP ");
    }

	if (strproto_lcl[0] == '\0')
		strcpy (strproto_lcl, "NONE ");

	strproto_lcl[strlen (strproto_lcl)-1] = '\0';

	strproto_rmt[0] = '\0';
	if (ho->neg_router) {
		if (ho->router[0] == 2 )
			strcpy (strproto_rmt, "RIP ");
		if (ho->router[0] == 4 )
			strcpy (strproto_rmt, "NLSP ");
	}

	if (strproto_rmt[0] == '\0')
		strcpy (strproto_rmt, "NONE ");

	strproto_rmt[strlen (strproto_rmt)-1] = '\0';

	strcpy (strnetwork, ipx_ntoa (go->network));

	sprintf (strlocal, "%02X%02X%02X%02X%02X%02X", NODE(go->our_node));
	sprintf (strremote, "%02X%02X%02X%02X%02X%02X", NODE(ho->his_node));

	argv[0]  = script;
	argv[1]  = lns[linkunit].ifname;
	argv[2]  = lns[linkunit].devnam;
	argv[3]  = strspeed;
	argv[4]  = strnetwork;
	argv[5]  = strlocal;
	argv[6]  = strremote;
	argv[7]  = strproto_lcl;
	argv[8]  = strproto_rmt;
	argv[9]  = go->name;
	argv[10] = ho->name;
	argv[11] = ipparam;
	argv[12] = strpid;
	argv[13] = NULL;
	run_program(script, argv, debug , linkunit);
}

/*
 * ipxcp_printpkt - print the contents of an IPXCP packet.
 */
static char *ipxcp_codenames[] = {
    "ConfReq", "ConfAck", "ConfNak", "ConfRej",
    "TermReq", "TermAck", "CodeRej"
};

static int ipxcp_printpkt(u_char *p,int plen,void (*printer) __P((void *, char *, ...)),void *arg)
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

	if (code >= 1 && code <= sizeof(ipxcp_codenames) / sizeof(char *))
		printer(arg, " %s", ipxcp_codenames[code-1]);
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
			if (olen < CILEN_VOID || olen > len)
				break;
			printer(arg, " <");
			len -= olen;
			optend = p + olen;
			switch (code) {
				case IPX_NETWORK_NUMBER:
					if (olen == CILEN_NETN) {
						p += CILEN_VOID;
						GETLONG(cilong, p);
						printer (arg, "network %s", ipx_ntoa (cilong));
					}
					break;
				case IPX_NODE_NUMBER:
					if (olen == CILEN_NODEN) {
						p += CILEN_VOID;
						printer (arg, "node ");
						while (p < optend) {
							GETCHAR(code, p);
							printer(arg, "%.2x", code);
						}
					}
					break;
				case IPX_COMPRESSION_PROTOCOL:
					if (olen >= CILEN_COMPRESS) {
						int i;
						p += CILEN_VOID;
						GETSHORT (cishort, p);
						printer (arg, "compression %d", cishort);
						for(i=0;i<(olen-CILEN_COMPRESS);i++) {
							unsigned char ccode;
							GETCHAR(ccode, p);
							printer (arg, "%#02x ",(int)ccode);
						}
					}
					break;
				case IPX_ROUTER_PROTOCOL:
					if (olen >= CILEN_PROTOCOL) {
						int i;
						p += CILEN_VOID;
						GETSHORT (cishort, p);
						printer (arg, "router proto %d", cishort);
						for(i=0;i<(olen-CILEN_PROTOCOL);i++) {
							unsigned char ccode;
							GETCHAR(ccode, p);
							printer (arg, "%#02x ",(int)ccode);
						}
					}
					break;
				case IPX_ROUTER_NAME:
					if (olen >= CILEN_NAME) {
						p += CILEN_VOID;
						printer (arg, "router name \"");
						while (p < optend) {
							GETCHAR(code, p);
							if (code >= 0x20 && code < 0x7E)
								printer (arg, "%c", code);
							else
								printer (arg, " \\%.2x", code);
						}
						printer (arg, "\"");
					}
					break;
				case IPX_COMPLETE:
					if (olen == CILEN_COMPLETE) {
						p += CILEN_VOID;
						printer (arg, "complete");
					}
					break;
				default:
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

	/*
	 * print the rest of the bytes in the packet 
	 */
	for (; len > 0; --len) {
		GETCHAR(code, p);
		printer(arg, " %.2x", code);
	}

    return p - pstart;
}




