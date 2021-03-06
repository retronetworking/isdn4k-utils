/* -*- mode: c; c-basic-offset: 4 -*-
 * ccp.c - PPP Compression Control Protocol.
 *
 * Copyright (c) 1994 The Australian National University.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, provided that the above copyright
 * notice appears in all copies.  This software is provided without any
 * warranty, express or implied. The Australian National University
 * makes no representations about the suitability of this software for
 * any purpose.
 *
 * IN NO EVENT SHALL THE AUSTRALIAN NATIONAL UNIVERSITY BE LIABLE TO ANY
 * PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
 * THE AUSTRALIAN NATIONAL UNIVERSITY HAVE BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * THE AUSTRALIAN NATIONAL UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE AUSTRALIAN NATIONAL UNIVERSITY HAS NO
 * OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS,
 * OR MODIFICATIONS.
 */

char ccp_rcsid[] = "$Id$";

#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/types.h>
/* dummy decl for struct referenced but not defined in linux/ppp-comp.h */
struct compstat;
#include <linux/ppp-comp.h>

#include "fsm.h"
#include "ipppd.h"
#include "ccp.h"

#include "compressions.h"

#if 0
#include <linux/isdn_lzscomp.h>
#else
#include "../ipppcomp/isdn_lzscomp.h"
#endif

/*
 * Protocol entry points from main code.
 */
static void ccp_init __P((int unit));
static void ccp_open __P((int unit));
static void ccp_close __P((int unit, char *));
static void ccp_lowerup __P((int unit));
static void ccp_lowerdown __P((int));
static void ccp_input __P((int unit, u_char *pkt, int len));
static void ccp_l_input __P((int unit, u_char *pkt, int len));
static void ccp_protrej __P((int unit));
static void ccp_l_protrej __P((int unit));
static int  ccp_printpkt __P((u_char *pkt, int len,
                  void (*printer) __P((void *, char *, ...)),
                  void *arg));
static void ccp_datainput __P((int unit, u_char *pkt, int len));
static void ccp_l_datainput __P((int unit, u_char *pkt, int len));

struct protent ccp_link_protent = {
    PPP_LINK_CCP,
    ccp_init,
    ccp_l_input,
    ccp_l_protrej,
    ccp_lowerup,
    ccp_lowerdown,
    ccp_open,
    ccp_close,
    ccp_printpkt,
    ccp_l_datainput,
    0,	/* disabled */
    "linkCCP",
    NULL,
    NULL,
    NULL
};

struct protent ccp_protent = {
    PPP_CCP,
    ccp_init,
    ccp_input,
    ccp_protrej,
    ccp_lowerup,
    ccp_lowerdown,
    ccp_open,
    ccp_close,
    ccp_printpkt,
    ccp_datainput,
    1,
    "CCP",
    NULL,
    NULL,
    NULL
};

/*
 * Remember that CCP is negotiating the _decompression_ method the peer
 * asking for configuration is willing to do. More exactly spoken, if
 * peer A sends a Configure-Request to peer B this request enumerates
 * the _decompression_ methods A is willing to use on _receiving_ data
 * from B, aka in direction B->A. The RFC does not use clear enough words
 * to make this unmisunderstandable IMHO.
 */

fsm ccp_fsm[NUM_PPP];
ccp_options ccp_wantoptions[NUM_PPP];	/* what to request the peer to use */
ccp_options ccp_gotoptions[NUM_PPP];	/* what the peer agreed to do */
ccp_options ccp_allowoptions[NUM_PPP];	/* what we'll agree to do */
ccp_options ccp_hisoptions[NUM_PPP];	/* what we agreed to do */

/*
 * Callbacks for fsm code.
 */
static void ccp_resetci __P((fsm *));
static int  ccp_cilen __P((fsm *));
static void ccp_addci __P((fsm *, u_char *, int *));
static int  ccp_ackci __P((fsm *, u_char *, int));
static int  ccp_nakci __P((fsm *, u_char *, int));
static int  ccp_rejci __P((fsm *, u_char *, int));
static int  ccp_reqci __P((fsm *, u_char *, int *, int));
static void ccp_up __P((fsm *));
static void ccp_down __P((fsm *));
static int  ccp_extcode __P((fsm *, int, int, u_char *, int));
static void ccp_rack_timeout __P(());
static char *method_name __P((ccp_options *, ccp_options *));

static fsm_callbacks ccp_callbacks = {
    ccp_resetci,
    ccp_cilen,
    ccp_addci,
    ccp_ackci,
    ccp_nakci,
    ccp_rejci,
    ccp_reqci,
    ccp_up,
    ccp_down,
    NULL,
    NULL,
    NULL,
    NULL,
    ccp_extcode,
    "CCP"
};

/*
 * Do we want / did we get any compression?
 */
#define ANY_COMPRESS(opt)	((opt).deflate || (opt).bsd_compress \
				 || (opt).predictor_1 || (opt).predictor_2 \
				 || (opt).lzs)

/*
 * Local state (mainly for handling reset-reqs and reset-acks).
 */
static int ccp_localstate[NUM_PPP];
#define RACK_PENDING	1	/* waiting for reset-ack */
#define RREQ_REPEAT	2	/* send another reset-req if no reset-ack */

#define RACKTIMEOUT	1	/* second */

static int all_rejected[NUM_PPP];

/*
 * ccp_init - initialize CCP.
 */
static void ccp_init(int unit)
{
    fsm *f = &ccp_fsm[unit];

    memset(f,0,sizeof(fsm));

    f->unit = -1;
    f->protocol = PPP_CCP; /* we change this for link-CCP */
    f->callbacks = &ccp_callbacks;
    fsm_init(f);

    memset(&ccp_wantoptions[unit],  0, sizeof(ccp_options));
    memset(&ccp_gotoptions[unit],   0, sizeof(ccp_options));
    memset(&ccp_allowoptions[unit], 0, sizeof(ccp_options));
    memset(&ccp_hisoptions[unit],   0, sizeof(ccp_options));

    ccp_wantoptions[0].deflate = 1;
    ccp_wantoptions[0].deflate_size = DEFLATE_MAX_SIZE;
    ccp_allowoptions[0].deflate = 1;
    ccp_allowoptions[0].deflate_size = DEFLATE_MAX_SIZE;

    ccp_wantoptions[0].bsd_compress = 1;
    ccp_wantoptions[0].bsd_bits = BSD_MAX_BITS;

    ccp_allowoptions[unit].bsd_compress = 1;
    ccp_allowoptions[unit].bsd_bits = BSD_MAX_BITS;
    ccp_allowoptions[unit].predictor_1 = 1;

    /* Are these 0/unit mixups intended or just typos ? */

    /* What we want to decompress */
    ccp_wantoptions[unit].lzs = 1;
    ccp_wantoptions[unit].lzs_hists = LZS_DECOMP_DEF_HISTS;
    ccp_wantoptions[unit].lzs_cmode = LZS_CMODE_SEQNO;

    /* What we allow to compress */
    ccp_allowoptions[unit].lzs = 1;
    ccp_allowoptions[unit].lzs_hists = LZS_COMP_DEF_HISTS;
    ccp_allowoptions[unit].lzs_cmode = LZS_CMODE_SEQNO;
}

int ccp_getunit(int linkunit,int protocol)
{
  int i;
  char proto[64];

  if(protocol != PPP_CCP && protocol != PPP_LINK_CCP)
    return -1;

  for(i=0;i<NUM_PPP;i++) {
    if(!ccp_fsm[i].inuse) {
      ccp_fsm[i].inuse = 1;
      ccp_fsm[i].state = INITIAL;
      ccp_fsm[i].flags = 0;
      ccp_fsm[i].id = 0;
      ccp_fsm[i].unit = linkunit;
      ccp_fsm[i].protocol = protocol;
      switch (protocol) {
      case 0x80FD: strcpy(proto, "Compression Control Protocol"); break;
      case 0x80FB: strcpy(proto, "Link Compression Control Protocol"); break;
      default: sprintf(proto, "protocol: %#x", protocol);
      }
      syslog(LOG_NOTICE,"CCP: got ccp-unit %d for link %d (%s)",i,linkunit,proto);
      return i;
    }
  }
  syslog(LOG_ERR,"No more ccp_units available");
  return -1;
}

void ccp_freeunit(int unit)
{
  ccp_fsm[unit].inuse = 0;
  ccp_fsm[unit].unit = -1; 
}


/*
 * ccp_open - CCP is allowed to come up.
 */
static void ccp_open(int unit)
{
    fsm *f = &ccp_fsm[unit];

    if (f->state != OPENED)
      ccp_flags_set(unit, 1, 0);

    /*
     * Find out which compressors the kernel supports before
     * deciding whether to open in silent mode.
     */
	ccp_resetci(f);
	if (!ANY_COMPRESS(ccp_gotoptions[unit]))
		f->flags |= OPT_SILENT;

    fsm_open(f);
}

/*
 * ccp_close - Terminate CCP.
 */
static void ccp_close(int unit,char *reason)
{
    ccp_flags_set(unit, 0, 0);
    fsm_close(&ccp_fsm[unit],reason);
}

/*
 * ccp_lowerup - we may now transmit CCP packets.
 */
static void ccp_lowerup(int unit)
{
     fsm_lowerup(&ccp_fsm[unit]);
}

/*
 * ccp_lowerdown - we may not transmit CCP packets.
 */
static void ccp_lowerdown(int unit)
{
    fsm_lowerdown(&ccp_fsm[unit]);
}

/*
 * ccp_input - process a received CCP packet.
 */
static void ccp_u_input(int unit,u_char *p,int len)
{
  fsm *f = &ccp_fsm[unit];

    int oldstate;

    /*
     * Check for a terminate-request so we can print a message.
     */
    oldstate = f->state;
    fsm_input(f, p, len);
	if (oldstate == OPENED && p[0] == TERMREQ && f->state != OPENED)
		syslog(LOG_NOTICE, "Compression disabled by peer.");

    /*
     * If we get a terminate-ack and we're not asking for compression,
     * close CCP.
     */
	if (oldstate == REQSENT && p[0] == TERMACK && !ANY_COMPRESS(ccp_gotoptions[unit]))
		ccp_close(unit,"No compression negotiated");
}

static void ccp_input(int linkunit,u_char *p,int len)
{
  int unit = lns[linkunit].ccp_unit;
  ccp_u_input(unit,p,len);
}

static void ccp_l_input(int linkunit,u_char *p,int len)
{
  int unit = lns[linkunit].ccp_l_unit;
  ccp_u_input(unit,p,len);
}

/*
 * Handle a CCP-specific code.
 * With the new reset handling in the kernel, this code will become unused.
 */
static int ccp_extcode(fsm *f,int code,int id,u_char *p,int len)
{
    int unit;

    if(f->protocol == PPP_CCP)
       unit = lns[f->unit].ccp_unit;
    else
       unit = lns[f->unit].ccp_l_unit;

    switch (code) {
    case CCP_RESETREQ:
	if (f->state != OPENED)
	    break;
	/* send a reset-ack, which the transmitter will see and
	   reset its compression state. */
	fsm_sdata(f, CCP_RESETACK, id, NULL, 0);
	break;

    case CCP_RESETACK:
	if (ccp_localstate[unit] & RACK_PENDING && id == f->reqid) {
	    ccp_localstate[unit] &= ~(RACK_PENDING | RREQ_REPEAT);
	    UNTIMEOUT(ccp_rack_timeout, (caddr_t) f);
	}
	break;

    default:
	return 0;
    }

    return 1;
}

/*
 * ccp_protrej - peer doesn't talk CCP.
 */
static void ccp_l_protrej(int linkunit)
{
  int unit = lns[linkunit].ccp_l_unit;
  ccp_flags_set(unit, 0, 0);
  fsm_lowerdown(&ccp_fsm[unit]);
}

static void ccp_protrej(int linkunit)
{
  int unit = lns[linkunit].ccp_unit;
  ccp_flags_set(unit, 0, 0);
  fsm_lowerdown(&ccp_fsm[unit]);
}

/*
 * ccp_resetci - initialize at start of negotiation.
 */
static void ccp_resetci(fsm *f)
{
    ccp_options *go;
    u_char opt_buf[16];
    unsigned long protos[8] = {0,};
    int i,j,unit;

    if(f->protocol == PPP_CCP)
      unit = lns[f->unit].ccp_unit;
    else
      unit = lns[f->unit].ccp_l_unit;

    go = &ccp_gotoptions[unit];

    ccp_get_compressors(unit,protos);
    syslog(LOG_NOTICE,"ccp_resetci!\n");
    for(j=0;j<8;j++) 
      for(i=0;i<32;i++) {
        if( protos[j] & (1<<i) )
           syslog(LOG_NOTICE,"Compressor %s loaded!\n",
             compression_names[j*32+i]?compression_names[j*32+i]:"UNKNWOWN");
      }

	/* copy config */
    *go = ccp_wantoptions[unit];

    all_rejected[unit] = 0;

    /*
     * Check whether the kernel knows about the various
     * DEcompression methods we might request.
     */
    if (go->bsd_compress) {
	opt_buf[0] = CI_BSD_COMPRESS;
	opt_buf[1] = CILEN_BSD_COMPRESS;
	opt_buf[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION, BSD_MIN_BITS);
	if (ccp_test(unit, opt_buf, CILEN_BSD_COMPRESS, 0) <= 0)
	    go->bsd_compress = 0;
    }
    if (go->deflate) {
	opt_buf[0] = CI_DEFLATE;
	opt_buf[1] = CILEN_DEFLATE;
	opt_buf[2] = DEFLATE_MAKE_OPT(DEFLATE_MIN_SIZE);
	opt_buf[3] = DEFLATE_CHK_SEQUENCE;
	if (ccp_test(unit, opt_buf, CILEN_DEFLATE, 0) <= 0)
	    go->deflate = 0;
    }
    if (go->predictor_1) {
	opt_buf[0] = CI_PREDICTOR_1;
	opt_buf[1] = CILEN_PREDICTOR_1;
	if (ccp_test(unit, opt_buf, CILEN_PREDICTOR_1, 0) <= 0)
	    go->predictor_1 = 0;
    }
    if (go->predictor_2) {
	opt_buf[0] = CI_PREDICTOR_2;
	opt_buf[1] = CILEN_PREDICTOR_2;
	if (ccp_test(unit, opt_buf, CILEN_PREDICTOR_2, 0) <= 0)
	    go->predictor_2 = 0;
    }

    if(go->lzs) {
	opt_buf[0] = CI_LZS_COMPRESS;
	opt_buf[1] = CILEN_LZS_COMPRESS;
	opt_buf[2] = LZS_HIST_BYTE1(LZS_DECOMP_DEF_HISTS);
	opt_buf[3] = LZS_HIST_BYTE2(LZS_DECOMP_DEF_HISTS);
	opt_buf[4] = LZS_CMODE_SEQNO;
	if(ccp_test(unit, opt_buf, CILEN_LZS_COMPRESS, 0) <= 0) {
	    go->lzs = 0;
	}
    }
}

/*
 * ccp_cilen - Return total length of our configuration info.
 */
static int ccp_cilen(fsm *f)
{
    ccp_options *go;
   
    if(f->protocol == PPP_CCP)
      go = &ccp_gotoptions[lns[f->unit].ccp_unit];
    else
      go = &ccp_gotoptions[lns[f->unit].ccp_l_unit];

    return (go->bsd_compress? CILEN_BSD_COMPRESS: 0)
	+ (go->deflate? CILEN_DEFLATE: 0)
	+ (go->predictor_1? CILEN_PREDICTOR_1: 0)
	+ (go->predictor_2? CILEN_PREDICTOR_2: 0)
	+ (go->lzs? CILEN_LZS_COMPRESS: 0);
}

/*
 * ccp_addci - put our requests in a packet.
 */
static void ccp_addci(fsm *f,u_char *p,int *lenp)
{
    int res;
    ccp_options *go; 
    u_char *p0 = p;
    int unit;

    if(f->protocol == PPP_CCP) 
      unit = lns[f->unit].ccp_unit;
    else
      unit = lns[f->unit].ccp_l_unit;

    go = &ccp_gotoptions[unit];

    /*
     * Add the compression types that we can receive, in decreasing
     * preference order.  Get the kernel to allocate the first one
     * in case it gets Acked.
     */
    if (go->deflate) {
	p[0] = CI_DEFLATE;
	p[1] = CILEN_DEFLATE;
	p[2] = DEFLATE_MAKE_OPT(go->deflate_size);
	p[3] = DEFLATE_CHK_SEQUENCE;
	for (;;) {
	    res = ccp_test(unit, p, CILEN_DEFLATE, 0);
	    if (res > 0) {
		p += CILEN_DEFLATE;
		break;
	    }
	    if (res < 0 || go->deflate_size <= DEFLATE_MIN_SIZE) {
		go->deflate = 0;
		break;
	    }
	    --go->deflate_size;
	    p[2] = DEFLATE_MAKE_OPT(go->deflate_size);
	}
    }
    if (go->bsd_compress) {
	p[0] = CI_BSD_COMPRESS;
	p[1] = CILEN_BSD_COMPRESS;
	p[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION, go->bsd_bits);
	if (p != p0) {
	    p += CILEN_BSD_COMPRESS;	/* not the first option */
	} else {
	    for (;;) {
		res = ccp_test(unit, p, CILEN_BSD_COMPRESS, 0);
		if (res > 0) {
		    p += CILEN_BSD_COMPRESS;
		    break;
		}
		if (res < 0 || go->bsd_bits <= BSD_MIN_BITS) {
		    go->bsd_compress = 0;
		    break;
		}
		--go->bsd_bits;
		p[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION, go->bsd_bits);
	    }
	}
    }
    /* XXX Should Predictor 2 be preferable to Predictor 1? */
    if (go->predictor_1) {
	p[0] = CI_PREDICTOR_1;
	p[1] = CILEN_PREDICTOR_1;
	if (p == p0 && ccp_test(unit, p, CILEN_PREDICTOR_1, 0) <= 0) {
	    go->predictor_1 = 0;
	} else {
	    p += CILEN_PREDICTOR_1;
	}
    }
    if (go->predictor_2) {
	p[0] = CI_PREDICTOR_2;
	p[1] = CILEN_PREDICTOR_2;
	if (p == p0 && ccp_test(unit, p, CILEN_PREDICTOR_2, 0) <= 0) {
	    go->predictor_2 = 0;
	} else {
	    p += CILEN_PREDICTOR_2;
	}
    }
    if(go->lzs) {
	p[0] = CI_LZS_COMPRESS;
	p[1] = CILEN_LZS_COMPRESS;
	p[2] = LZS_HIST_BYTE1(go->lzs_hists);
	p[3] = LZS_HIST_BYTE2(go->lzs_hists);
	p[4] = go->lzs_cmode;
	if(p == p0 && ccp_test(unit, p, CILEN_LZS_COMPRESS, 0) <= 0) {
	    /* TODO: Try less histories and finally try cmode 4 until the
	       kernel accepts a method before really wiping LZS */
	    go->lzs = 0;
	} else {
	    p += CILEN_LZS_COMPRESS;
	}
    }

    go->method = (p > p0)? p0[0]: -1;

    *lenp = p - p0;
}

/*
 * ccp_ackci - process a received configure-ack, and return
 * 1 iff the packet was OK.
 */
static int ccp_ackci(fsm *f,u_char *p,int len)
{
    ccp_options *go;
    u_char *p0 = p;

    if(f->protocol == PPP_CCP)
      go = &ccp_gotoptions[lns[f->unit].ccp_unit];
    else
      go = &ccp_gotoptions[lns[f->unit].ccp_l_unit];

    if (go->deflate) {
	if (len < CILEN_DEFLATE
	    || p[0] != CI_DEFLATE || p[1] != CILEN_DEFLATE
	    || p[2] != DEFLATE_MAKE_OPT(go->deflate_size)
	    || p[3] != DEFLATE_CHK_SEQUENCE)
	    return 0;
	p += CILEN_DEFLATE;
	len -= CILEN_DEFLATE;
	/* XXX Cope with first/fast ack */
	if (len == 0)
	    return 1;
    }
    if (go->bsd_compress) {
	if (len < CILEN_BSD_COMPRESS
	    || p[0] != CI_BSD_COMPRESS || p[1] != CILEN_BSD_COMPRESS
	    || p[2] != BSD_MAKE_OPT(BSD_CURRENT_VERSION, go->bsd_bits))
	    return 0;
	p += CILEN_BSD_COMPRESS;
	len -= CILEN_BSD_COMPRESS;
	/* XXX Cope with first/fast ack */
	if (p == p0 && len == 0)
	    return 1;
    }
    if (go->predictor_1) {
	if (len < CILEN_PREDICTOR_1
	    || p[0] != CI_PREDICTOR_1 || p[1] != CILEN_PREDICTOR_1)
	    return 0;
	p += CILEN_PREDICTOR_1;
	len -= CILEN_PREDICTOR_1;
	/* XXX Cope with first/fast ack */
	if (p == p0 && len == 0)
	    return 1;
    }
    if (go->predictor_2) {
	if (len < CILEN_PREDICTOR_2
	    || p[0] != CI_PREDICTOR_2 || p[1] != CILEN_PREDICTOR_2)
	    return 0;
	p += CILEN_PREDICTOR_2;
	len -= CILEN_PREDICTOR_2;
	/* XXX Cope with first/fast ack */
	if (p == p0 && len == 0)
	    return 1;
    }
    if(go->lzs) {
	if(len < CILEN_LZS_COMPRESS
	   || p[0] != CI_LZS_COMPRESS || p[1] != CILEN_LZS_COMPRESS
	   || LZS_HIST_WORD(p[2], p[3]) != go->lzs_hists
	   || p[4] != go->lzs_cmode)
	    return 0;	/* Brocken ack - line noise ? */
	p += CILEN_LZS_COMPRESS;
	len -= CILEN_LZS_COMPRESS;
	if(p == p0 && len == 0)
	    return 1;
    }
    if (len != 0)
	return 0;
    return 1;
}

/*
 * ccp_nakci - process received configure-nak.
 * Returns 1 iff the nak was OK.
 */
static int ccp_nakci(fsm *f,u_char *p,int len)
{
    ccp_options *go;
    ccp_options no;		/* options we've seen already */
    ccp_options try;		/* options to ask for next time */

    int nb;

    if(f->protocol == PPP_CCP)
      go = &ccp_gotoptions[lns[f->unit].ccp_unit];
    else
      go = &ccp_gotoptions[lns[f->unit].ccp_l_unit];


    memset(&no, 0, sizeof(no));
    try = *go;

    if (go->deflate && len >= CILEN_DEFLATE
	&& p[0] == CI_DEFLATE && p[1] == CILEN_DEFLATE) {
	no.deflate = 1;
	/*
	 * Peer wants us to use a different code size or something.
	 * Stop asking for Deflate if we don't understand his suggestion.
	 */
	if (DEFLATE_METHOD(p[2]) != DEFLATE_METHOD_VAL
	    || DEFLATE_SIZE(p[2]) < DEFLATE_MIN_SIZE
	    || p[3] != DEFLATE_CHK_SEQUENCE)
	    try.deflate = 0;
	else if (DEFLATE_SIZE(p[2]) < go->deflate_size)
	    go->deflate_size = DEFLATE_SIZE(p[2]);
	p += CILEN_DEFLATE;
	len -= CILEN_DEFLATE;
    }

    if (go->bsd_compress && len >= CILEN_BSD_COMPRESS
	&& p[0] == CI_BSD_COMPRESS && p[1] == CILEN_BSD_COMPRESS) {
	no.bsd_compress = 1;
	/*
	 * Peer wants us to use a different number of bits
	 * or a different version.
	 */
	if (BSD_VERSION(p[2]) != BSD_CURRENT_VERSION)
	    try.bsd_compress = 0;
	else if (BSD_NBITS(p[2]) < go->bsd_bits)
	    try.bsd_bits = BSD_NBITS(p[2]);
	p += CILEN_BSD_COMPRESS;
	len -= CILEN_BSD_COMPRESS;
    }

    if(go->lzs && len >= CILEN_LZS_COMPRESS && p[0] == CI_LZS_COMPRESS
       && p[1] == CILEN_LZS_COMPRESS) {
	no.lzs = 1;
	/*
	 * Peer wants us to use a different number of histories or
	 * a different check mode.
	 */
	nb = LZS_HIST_WORD(p[2], p[3]);
	if(nb != go->lzs_hists) {
	    if(nb >= 0 && nb <= LZS_DECOMP_MAX_HISTS)
		try.lzs_hists = nb;
	    else
		try.lzs = 0;
	}
	if(p[4] != go->lzs_cmode)
	    try.lzs_cmode = p[4];
	p += CILEN_LZS_COMPRESS;
	len -= CILEN_LZS_COMPRESS;
    }

    /*
     * Predictor-1 and 2 have no options, so they can't be Naked.
     *
     * XXX What should we do with any remaining options?
     */

    if (len != 0)
	return 0;

    if (f->state != OPENED)
	*go = try;
    return 1;
}

/*
 * ccp_rejci - reject some of our suggested compression methods.
 */
static int ccp_rejci(fsm *f,u_char *p,int len)
{
    ccp_options *go;
    ccp_options try;		/* options to request next time */
    int unit;

    if(f->protocol == PPP_CCP)
      unit = lns[f->unit].ccp_unit;
    else
      unit = lns[f->unit].ccp_l_unit;

    go = &ccp_gotoptions[unit];

    try = *go;

    /*
     * Cope with empty configure-rejects by ceasing to send
     * configure-requests.
     */
    if (len == 0 && all_rejected[unit])
	return -1;

    if (go->deflate && len >= CILEN_DEFLATE
	&& p[0] == CI_DEFLATE && p[1] == CILEN_DEFLATE) {
	if (p[2] != DEFLATE_MAKE_OPT(go->deflate_size)
	    || p[3] != DEFLATE_CHK_SEQUENCE)
	    return 0;		/* Rej is bad */
	try.deflate = 0;
	p += CILEN_DEFLATE;
	len -= CILEN_DEFLATE;
    }
    if (go->bsd_compress && len >= CILEN_BSD_COMPRESS
	&& p[0] == CI_BSD_COMPRESS && p[1] == CILEN_BSD_COMPRESS) {
	if (p[2] != BSD_MAKE_OPT(BSD_CURRENT_VERSION, go->bsd_bits))
	    return 0;
	try.bsd_compress = 0;
	p += CILEN_BSD_COMPRESS;
	len -= CILEN_BSD_COMPRESS;
    }
    if (go->predictor_1 && len >= CILEN_PREDICTOR_1
	&& p[0] == CI_PREDICTOR_1 && p[1] == CILEN_PREDICTOR_1) {
	try.predictor_1 = 0;
	p += CILEN_PREDICTOR_1;
	len -= CILEN_PREDICTOR_1;
    }
    if (go->predictor_2 && len >= CILEN_PREDICTOR_2
	&& p[0] == CI_PREDICTOR_2 && p[1] == CILEN_PREDICTOR_2) {
	try.predictor_2 = 0;
	p += CILEN_PREDICTOR_2;
	len -= CILEN_PREDICTOR_2;
    }
    if(go->lzs && len >= CILEN_LZS_COMPRESS
       && p[0] == CI_LZS_COMPRESS && p[1] == CILEN_LZS_COMPRESS) {
	if(LZS_HIST_WORD(p[2], p[3]) != go->lzs_hists || p[4] != go->lzs_cmode)
	    return 0;
	try.lzs = 0;
	p += CILEN_LZS_COMPRESS;
	len -= CILEN_LZS_COMPRESS;
    }

    if (len != 0)
	return 0;

    if (f->state != OPENED)
	*go = try;

    return 1;
}

/*
 * ccp_reqci - processed a received configure-request.
 * Returns CONFACK, CONFNAK or CONFREJ and the packet modified
 * appropriately.
 */
static int ccp_reqci(fsm *f,u_char *p,int *lenp,int dont_nak)
{
    int ret, newret, res;
    u_char *p0, *retp;
    int len, clen, type, nb;
    int unit;
    ccp_options *ho,*ao;

    if(f->protocol == PPP_CCP)
      unit = lns[f->unit].ccp_unit;
    else
      unit = lns[f->unit].ccp_l_unit;

    ho = &ccp_hisoptions[unit];
    ao = &ccp_allowoptions[unit];

    ret = CONFACK;
    retp = p0 = p;
    len = *lenp;

    memset(ho, 0, sizeof(ccp_options));
    ho->method = (len > 0)? p[0]: -1;

    while (len > 0) {
	newret = CONFACK;
	if (len < 2 || p[1] < 2 || p[1] > len) {
	    /* length is bad */
	    clen = len;
	    newret = CONFREJ;

	} else {
	    type = p[0];
	    clen = p[1];

	    switch (type) {
	    case CI_DEFLATE:
		if (!ao->deflate || clen != CILEN_DEFLATE) {
		    newret = CONFREJ;
		    break;
		}

		ho->deflate = 1;
		ho->deflate_size = nb = DEFLATE_SIZE(p[2]);
		if (DEFLATE_METHOD(p[2]) != DEFLATE_METHOD_VAL
		    || p[3] != DEFLATE_CHK_SEQUENCE
			|| nb > ao->deflate_size || nb < DEFLATE_MIN_SIZE) {
		    newret = CONFNAK;
			if (!dont_nak) {
				p[2] = DEFLATE_MAKE_OPT(ao->deflate_size);
				p[3] = DEFLATE_CHK_SEQUENCE;
			}
			break;
		}

		/*
		 * Check whether we can do Deflate with the window
		 * size they want.  If the window is too big, reduce
		 * it until the kernel can cope and nak with that.
		 * We only check this for the first option.
		 */
		if (p == p0) {
			for (;;) {
				res = ccp_test(f->unit, p, CILEN_DEFLATE, 1);
				if (res > 0)
					break;              /* it's OK now */
				if (res < 0 || nb == DEFLATE_MIN_SIZE || dont_nak) {
					newret = CONFREJ;
					p[2] = DEFLATE_MAKE_OPT(ho->deflate_size);
					break;
				}
				newret = CONFNAK;
				--nb;
				p[2] = DEFLATE_MAKE_OPT(nb);
			}
		}
		break;

	    case CI_BSD_COMPRESS:
		if (!ao->bsd_compress || clen != CILEN_BSD_COMPRESS) {
		    newret = CONFREJ;
		    break;
		}

		ho->bsd_compress = 1;
		ho->bsd_bits = nb = BSD_NBITS(p[2]);
		if (BSD_VERSION(p[2]) != BSD_CURRENT_VERSION
			|| nb > ao->bsd_bits || nb < BSD_MIN_BITS) {
			newret = CONFNAK;
			if (!dont_nak)
				p[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION, ao->bsd_bits);
			break;
		}

		/*
		 * Check whether we can do BSD-Compress with the code
		 * size they want.  If the code size is too big, reduce
		 * it until the kernel can cope and nak with that.
		 * We only check this for the first option.
		 */
		if (p == p0) {
		    for (;;) {
				res = ccp_test(f->unit, p, CILEN_BSD_COMPRESS, 1);
				if (res > 0)
					break;
				if (res < 0 || nb == BSD_MIN_BITS || dont_nak) {
					newret = CONFREJ;
					p[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION,
							ho->bsd_bits);
					break;
				}
				newret = CONFNAK;
				--nb;
				p[2] = BSD_MAKE_OPT(BSD_CURRENT_VERSION, nb);
			}
		}
		break;

	    case CI_PREDICTOR_1:
		if (!ao->predictor_1 || clen != CILEN_PREDICTOR_1) {
		    newret = CONFREJ;
		    break;
 		}
 
		ho->predictor_1 = 1;
		if (p == p0 && ccp_test(unit, p, CILEN_PREDICTOR_1, 1) <= 0) {
		    newret = CONFREJ;
		}
		break;

	    case CI_PREDICTOR_2:
		if (!ao->predictor_2 || clen != CILEN_PREDICTOR_2) {
		    newret = CONFREJ;
		    break;
		}

		ho->predictor_2 = 1;
		if (p == p0 && ccp_test(unit, p, CILEN_PREDICTOR_2, 1) <= 0) {
		    newret = CONFREJ;
		}
		break;

	    case CI_LZS_COMPRESS:
		if(!ao->lzs || clen != CILEN_LZS_COMPRESS) {
		    newret = CONFREJ;
		    break;
		}
		ho->lzs = 1;
		ho->lzs_hists = nb = LZS_HIST_WORD(p[2], p[3]);
		ho->lzs_cmode = p[4];
		if(nb < 0 || nb > ao->lzs_hists) {
		    newret = CONFNAK;
		    if(!dont_nak) {
			p[2] = LZS_HIST_BYTE1(ao->lzs_hists);
			p[3] = LZS_HIST_BYTE2(ao->lzs_hists);
			break;
		    }
		}
		if(p[4] != ao->lzs_cmode) {
		    /* Peer wants another checkmode - accept only EXT for now */
		    if(p[4] != LZS_CMODE_EXT) {
			newret = CONFNAK;
			if(!dont_nak)
			    p[4] = ao->lzs_cmode;
			break;
		    } else {
			/* EXT is Ok but requires hists to be 1 */
			if(nb != 1) {
			    newret = CONFNAK;
			    if(!dont_nak) {
				p[2] = LZS_HIST_BYTE1(1);
				p[3] = LZS_HIST_BYTE2(1);
			    }
			    break;
			}
		    }
		}
		/* Finally verify the kernel is thinking the same way */
		if(p == p0 && ccp_test(unit, p, CILEN_LZS_COMPRESS, 1) <= 0) {
		    newret = CONFREJ;
		}
		break;

	    default:
		newret = CONFREJ;
	    }
	}

	if (newret == CONFNAK && dont_nak)
	    newret = CONFREJ;
	if (!(newret == CONFACK || (newret == CONFNAK && ret == CONFREJ) )) {
	    /* we're returning this option */
	    if (newret == CONFREJ && ret == CONFNAK)
		retp = p0;
	    ret = newret;
	    if (p != retp)
		BCOPY(p, retp, clen);
	    retp += clen;
	}

	p += clen;
	len -= clen;
    }

    if (ret != CONFACK) {
	if (ret == CONFREJ && *lenp == retp - p0)
	    all_rejected[unit] = 1;
	else
	    *lenp = retp - p0;
    }
    return ret;
}

/*
 * Make a string name for a compression method (or 2).
 */
static char *method_name(ccp_options *opt,ccp_options *opt2)
{
    static char result[64];

    if (!ANY_COMPRESS(*opt))
       return "(none)";
    switch (opt->method) {
    case CI_DEFLATE:
       if (opt2 != NULL && opt2->deflate_size != opt->deflate_size)
           sprintf(result, "Deflate (%d/%d)", opt->deflate_size,
                   opt2->deflate_size);
       else
           sprintf(result, "Deflate (%d)", opt->deflate_size);
       break;
    case CI_BSD_COMPRESS:
       if (opt2 != NULL && opt2->bsd_bits != opt->bsd_bits)
           sprintf(result, "BSD-Compress (%d/%d)", opt->bsd_bits,
                   opt2->bsd_bits);
       else
           sprintf(result, "BSD-Compress (%d)", opt->bsd_bits);
       break;
    case CI_PREDICTOR_1:
       return "Predictor 1";
    case CI_PREDICTOR_2:
       return "Predictor 2";
    case CI_LZS_COMPRESS:
	if (opt2 != NULL && (opt2->lzs_hists != opt->lzs_hists ||
	    opt2->lzs_cmode != opt->lzs_cmode))
	    sprintf(result, "LZS (hists %d check %d/hists %d check %d)",
		    opt->lzs_hists, opt->lzs_cmode,
		    opt2->lzs_hists, opt2->lzs_cmode);
	else
	    sprintf(result, "LZS (hists %d check %d)",
		    opt->lzs_hists, opt->lzs_cmode);
	break;
    default:
       sprintf(result, "Method %d", opt->method);
    }
    return result;
}

/*
 * CCP has come up - inform the kernel driver and log a message.
 */
static void ccp_up(fsm *f)
{
    int unit;
    ccp_options *go;
    ccp_options *ho;
    char method1[64];
   
    if(f->protocol == PPP_CCP)
       unit = lns[f->unit].ccp_unit;
    else
       unit = lns[f->unit].ccp_l_unit;
 
    go = &ccp_gotoptions[unit];
    ho = &ccp_hisoptions[unit];

    ccp_flags_set(unit, 1, 1);
    if (ANY_COMPRESS(*go)) {
       if (ANY_COMPRESS(*ho)) {
           if (go->method == ho->method) {
               syslog(LOG_NOTICE, "[%d] %s compression enabled",f->unit,
                      method_name(go, ho));
           } else {
               strcpy(method1, method_name(go, NULL));
               syslog(LOG_NOTICE, "[%d] %s / %s compression enabled",f->unit,
                      method1, method_name(ho, NULL));
           }
       } else
           syslog(LOG_NOTICE, "[%d] %s receive compression enabled",f->unit,
                  method_name(go, NULL));
    } else if (ANY_COMPRESS(*ho))
       syslog(LOG_NOTICE, "[%d] %s transmit compression enabled",f->unit,
	 method_name(ho, NULL));
}

/*
 * CCP has gone down - inform the kernel driver.
 */
static void ccp_down(fsm *f)
{
    int unit;

    if(f->protocol == PPP_CCP)
       unit = lns[f->unit].ccp_unit;
    else
       unit = lns[f->unit].ccp_l_unit;

    if (ccp_localstate[unit] & RACK_PENDING)
	UNTIMEOUT(ccp_rack_timeout, (caddr_t) f);
    ccp_localstate[unit] = 0;
    ccp_flags_set(unit, 1, 0);
}

/*
 * Print the contents of a CCP packet.
 */
static char *ccp_codenames[] = {
    "ConfReq", "ConfAck", "ConfNak", "ConfRej",
    "TermReq", "TermAck", "CodeRej",
    NULL, NULL, NULL, NULL, NULL, NULL,
    "ResetReq", "ResetAck",
};

static int ccp_printpkt(u_char *p,int plen,void (*printer)(void*,char*,...),void *arg)
{
    u_char *p0, *optend;
    int code, id, len;
    int optlen;

    p0 = p;
    if (plen < HEADERLEN)
	return 0;
    code = p[0];
    id = p[1];
    len = (p[2] << 8) + p[3];
    if (len < HEADERLEN || len > plen)
	return 0;

    if (code >= 1 && code <= sizeof(ccp_codenames) / sizeof(char *)
	&& ccp_codenames[code-1] != NULL)
	printer(arg, " %s", ccp_codenames[code-1]);
    else
	printer(arg, " code=0x%x", code);
    printer(arg, " id=0x%x", id);
    len -= HEADERLEN;
    p += HEADERLEN;

    switch (code) {
    case CONFREQ:
    case CONFACK:
    case CONFNAK:
    case CONFREJ:
	/* print list of possible compression methods */
	while (len >= 2) {
	    code = p[0];
	    optlen = p[1];
	    if (optlen < 2 || optlen > len)
		break;
	    printer(arg, " <");
	    len -= optlen;
	    optend = p + optlen;
	    switch (code) {
	    case CI_DEFLATE:
		if (optlen >= CILEN_DEFLATE) {
		    printer(arg, "deflate %d", DEFLATE_SIZE(p[2]));
		    if (DEFLATE_METHOD(p[2]) != DEFLATE_METHOD_VAL)
			printer(arg, " method %d", DEFLATE_METHOD(p[2]));
		    if (p[3] != DEFLATE_CHK_SEQUENCE)
			printer(arg, " check %d", p[3]);
		    p += CILEN_DEFLATE;
		}
		break;
	    case CI_BSD_COMPRESS:
		if (optlen >= CILEN_BSD_COMPRESS) {
		    printer(arg, "bsd v%d %d", BSD_VERSION(p[2]),
			    BSD_NBITS(p[2]));
		    p += CILEN_BSD_COMPRESS;
		}
		break;
	    case CI_PREDICTOR_1:
		if (optlen >= CILEN_PREDICTOR_1) {
		    printer(arg, "predictor 1");
		    p += CILEN_PREDICTOR_1;
		}
		break;
	    case CI_PREDICTOR_2:
		if (optlen >= CILEN_PREDICTOR_2) {
		    printer(arg, "predictor 2");
		    p += CILEN_PREDICTOR_2;
		}
		break;
	    case CI_LZS_COMPRESS:
		/* Make sure we differ real (RFC1974) from old pre-RFC
		   implementations like Ascends. ISPs who never set up
		   LZS on an Ascend Max will end up announcing the
		   mode "Stac" which claims to be 0x11 - the same
		   value used for RFC1974 conforming LZS - but has
		   another format, primarily a length of 6 of the
		   config element. While I know a bit about that mode
		   I refrain from implementing it. Users who see that
		   stuff announced should contact their ISPs and ask
		   for RFC compliant compression. Usually, it is just
		   an oversight at the ISP, no bad taste */
		if(optlen == CILEN_LZS_COMPRESS) {
		    printer(arg, "LZS (RFC) hists %d check %d",
			    (p[2] << 16) | p[3], p[4]);
		    p += CILEN_LZS_COMPRESS;
		} else if(optlen == 6) {
		    printer(arg, "LZS (Ascend pre-RFC)");
		    p += optlen;
		} else {
		    printer(arg, "LZS (non-RFC)");
		    p += optlen;
		}
		break;
		/* Looks like the following default was missing. I added
		   it, hopefully it is correct. ---abp */
	    default:
		while (p < optend)
		    printer(arg, " %.2x", *p++);
		printer(arg, ">");
	    }
	}
	break;

    case TERMACK:
    case TERMREQ:
       if (len > 0 && *p >= ' ' && *p < 0x7f) {
           print_string(p, len, printer, arg);
           p += len;
           len = 0;
       }
       break;
    }

    /* dump out the rest of the packet in hex */
    while (--len >= 0)
	printer(arg, " %.2x", *p++);

    return p - p0;
}

/*
 */
static void ccp_u_datainput(int unit,u_char *pkt,int len)
{
    fsm *f = &ccp_fsm[unit];

    if (f->state == OPENED) {
	if (ccp_fatal_error(unit)) {
	    /*
	     * Disable compression by taking CCP down.
	     */
	    syslog(LOG_ERR, "Lost compression sync: disabling compression");
	    ccp_close(unit,"Lost compression sync");
	} else {
	    /*
	     * Send a reset-request to reset the peer's compressor.
	     * We don't do that if we are still waiting for an
	     * acknowledgement to a previous reset-request.
	     */
	    if (!(ccp_localstate[unit] & RACK_PENDING)) {
		fsm_sdata(f, CCP_RESETREQ, f->reqid = ++f->id, NULL, 0);
		TIMEOUT(ccp_rack_timeout, (caddr_t) f, RACKTIMEOUT);
		ccp_localstate[unit] |= RACK_PENDING;
	    } else
		ccp_localstate[unit] |= RREQ_REPEAT;
	}
    }
}

static void ccp_l_datainput(int linkunit,u_char *pkt,int len)
{
  int unit = lns[linkunit].ccp_l_unit;
  ccp_u_datainput(unit,pkt,len);
}

static void ccp_datainput(int linkunit,u_char *pkt,int len)
{
  int unit = lns[linkunit].ccp_unit;
  ccp_u_datainput(unit,pkt,len);
}

/*
 * Timeout waiting for reset-ack.
 */
static void ccp_rack_timeout(caddr_t arg)
{
    fsm *f = (fsm *) arg;
    int unit;

    if(f->protocol == PPP_CCP)
       unit = lns[f->unit].ccp_unit;
    else
       unit = lns[f->unit].ccp_l_unit;

    if (f->state == OPENED && ccp_localstate[unit] & RREQ_REPEAT) {
	fsm_sdata(f, CCP_RESETREQ, f->reqid, NULL, 0);
	TIMEOUT(ccp_rack_timeout, (caddr_t) f, RACKTIMEOUT);
	ccp_localstate[unit] &= ~RREQ_REPEAT;
    } else
	ccp_localstate[unit] &= ~RACK_PENDING;
}

