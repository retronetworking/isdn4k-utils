/* $Id$
 *
 * ISDN accounting for isdn4linux.
 *
 * Copyright 1995 .. 2000 by Andreas Kool (akool@isdn4linux.de)
 *                     and Stefan Luethje (luethje@sl-gw.lake.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log$
 * Revision 1.24  2004/07/25 14:21:13  tobiasb
 * New isdnrep option -m [*|/]number.  It multiplies or divide the cost of
 * each call by the given number.  `-m/1.16' for example displays the costs
 * without the German `Umsatzsteuer'.
 *
 * Revision 1.23  2004/07/24 17:58:06  tobiasb
 * New isdnrep options: `-L:' controls the displayed call summaries in the
 * report footer.  `-x' displays only call selected or not deselected by
 * hour or type of day -- may be useful in conjunction with `-r'.
 *
 * Activated new configuration file entry `REPOPTIONS' for isdnrep default
 * options.  This options can be disabled by `-c' on the command line.
 *
 * Revision 1.22  2003/10/29 17:41:35  tobiasb
 * isdnlog-4.67:
 *  - Enhancements for isdnrep:
 *    - New option -r for recomputing the connection fees with the rates
 *      from the current (and for a different or the cheapest provider).
 *    - Revised output format of summaries at end of report.
 *    - New format parameters %j, %v, and %V.
 *    - 2 new input formats for -t option.
 *  - Fix for dualmode workaround 0x100 to ensure that incoming calls
 *    will not become outgoing calls if a CALL_PROCEEDING message with
 *    an B channel confirmation is sent by a terminal prior to CONNECT.
 *  - Fixed and enhanced t: Tag handling in pp_rate.
 *  - Fixed typo in interface description of tools/rate.c
 *  - Fixed typo in tools/isdnrate.man, found by Paul Slootman.
 *  - Minor update to sample isdn.conf files:
 *    - Default isdnrep format shows numbers with 16 chars (+ & 15 digits).
 *    - New isdnrep format (-FNIO) without display of transfered bytes.
 *    - EUR as currency in Austria, may clash with outdated rate-at.dat.
 *      The number left of the currency symbol is nowadays insignificant.
 *  - Changes checked in earlier but after step to isdnlog-4.66:
 *    - New option for isdnrate: `-rvNN' requires a vbn starting with NN.
 *    - Do not compute the zone with empty strings (areacodes) as input.
 *    - New ratefile tags r: und t: which need an enhanced pp_rate.
 *      For a tag description see rate-files(5).
 *    - Some new and a few updated international cellphone destinations.
 *
 * NOTE: If there any questions, problems, or problems regarding isdnlog,
 *    feel free to join the isdn4linux mailinglist, see
 *    https://www.isdn4linux.de/mailman/listinfo/isdn4linux for details,
 *    or send a mail in English or German to <tobiasb@isdn4linux.de>.
 *
 * Revision 1.21  2000/10/15 12:53:04  leo
 * Changed iobytes to double
 *
 * Revision 1.20  2000/08/17 21:34:44  akool
 * isdnlog-4.40
 *  - README: explain possibility to open the "outfile=" in Append-Mode with "+"
 *  - Fixed 2 typos in isdnlog/tools/zone/de - many thanks to
 *      Tobias Becker <tobias@talypso.de>
 *  - detect interface (via IIOCNETGPN) _before_ setting CHARGEINT/HUPTIMEOUT
 *  - isdnlog/isdnlog/processor.c ... fixed wrong init of IIOCNETGPNavailable
 *  - isdnlog/isdnrep/isdnrep.c ... new option -S summary
 *  - isdnlog/isdnrep/rep_main.c
 *  - isdnlog/isdnrep/isdnrep.1.in
 *  - isdnlog/tools/NEWS
 *  - isdnlog/tools/cdb/debian ... (NEW dir) copyright and such from orig
 *  - new "rate-de.dat" from sourceforge (hi and welcome: Who is "roro"?)
 *
 * Revision 1.19  2000/03/06 07:03:20  akool
 * isdnlog-4.15
 *   - isdnlog/tools/tools.h ... moved one_call, sum_calls to isdnrep.h
 *     ==> DO A 'make clean' PLEASE
 *   - isdnlog/tools/telnum.c ... fixed a small typo
 *   - isdnlog/isdnrep/rep_main.c ... incl. dest.h
 *   - isdnlog/isdnrep/isdnrep.c ... fixed %l, %L
 *   - isdnlog/isdnrep/isdnrep.h ... struct one_call, sum_calls are now here
 *
 *   Support for Norway added. Many thanks to Tore Ferner <torfer@pvv.org>
 *     - isdnlog/rate-no.dat  ... NEW
 *     - isdnlog/holiday-no.dat  ... NEW
 *     - isdnlog/samples/isdn.conf.no ... NEW
 *     - isdnlog/samples/rate.conf.no ... NEW
 *
 * Revision 1.18  1999/12/31 13:57:19  akool
 * isdnlog-4.00 (Millenium-Edition)
 *  - Oracle support added by Jan Bolt (Jan.Bolt@t-online.de)
 *  - resolved *any* warnings against rate-de.dat
 *  - Many new rates
 *  - CREDITS file added
 *
 * Revision 1.17  1999/12/17 22:51:54  akool
 * isdnlog-3.79
 *  - isdnlog/isdnrep/isdnrep.{c,h} ... error -handling, print_msg
 *  - isdnlog/isdnrep/rep_main.c
 *  - isdnlog/isdnrep/isdnrep.1.in
 *  - isdnlog/tools/rate.c  ... dupl entry in rate.conf
 *  - isdnlog/tools/NEWS
 *  - isdnlog/tools/isdnrate.c
 *  - isdnlog/tools/dest/configure{,.in}
 *  - isdnlog/tools/zone/configure{,.in}
 *
 * Revision 1.16  1999/07/18 08:40:37  akool
 * fix from Michael
 *
 * Revision 1.15  1999/07/12 11:37:37  calle
 * Bugfix: isdnrep defined print_msg as function pointer, the object files
 *         in tools directory, declare it as external function.
 * 	compiler and linker did not detect the problem.
 * 	Now print_msg is a function in rep_main.c and I copied
 * 	print_in_modules from isdnconf. Also set_print_fct_for_isdnrep
 * 	is removed from isdnrep.c. isdnrep didn?t crash now, but throw
 * 	out warning messages about rate.dat and did?t generate output.
 *
 * Revision 1.14  1999/05/04 19:33:19  akool
 * isdnlog Version 3.24
 *
 *  - fully removed "sondernummern.c"
 *  - removed "gcc -Wall" warnings in ASN.1 Parser
 *  - many new entries for "rate-de.dat"
 *  - better "isdnconf" utility
 *
 * Revision 1.13  1999/03/24 19:38:41  akool
 * - isdnlog Version 3.10
 * - moved "sondernnummern.c" from isdnlog/ to tools/
 * - "holiday.c" and "rate.c" integrated
 * - NetCologne rates from Oliver Flimm <flimm@ph-cip.uni-koeln.de>
 * - corrected UUnet and T-Online rates
 *
 * Revision 1.12  1999/01/24 19:02:25  akool
 *  - second version of the new chargeint database
 *  - isdnrep reanimated
 *
 * Revision 1.11  1998/11/24 20:52:46  akool
 *  - changed my email-adress
 *  - new Option "-R" to supply the preselected provider (-R24 -> Telepassport)
 *  - made Provider-Prefix 6 digits long
 *  - full support for internal S0-bus implemented (-A, -i Options)
 *  - isdnlog now ignores unknown frames
 *  - added 36 allocated, but up to now unused "Auskunft" Numbers
 *  - added _all_ 122 Providers
 *  - Patch from Jochen Erwied <mack@Joker.E.Ruhr.DE> for Quante-TK-Anlagen
 *    (first dialed digit comes with SETUP-Frame)
 *
 * Revision 1.10  1998/03/29 19:54:17  luethje
 * idnrep: added html feature (incoming/outgoing calls)
 *
 * Revision 1.9  1998/03/08 11:43:08  luethje
 * I4L-Meeting Wuerzburg final Edition, golden code - Service Pack number One
 *
 * Revision 1.8  1997/05/15 23:24:56  luethje
 * added new links on HTML
 *
 * Revision 1.7  1997/05/15 22:21:40  luethje
 * New feature: isdnrep can transmit via HTTP fax files and vbox files.
 *
 * Revision 1.6  1997/04/20 22:52:28  luethje
 * isdnrep has new features:
 *   -variable format string
 *   -can create html output (option -w1 or ln -s isdnrep isdnrep.cgi)
 *    idea and design from Dirk Staneker (dirk.staneker@student.uni-tuebingen.de)
 * bugfix of processor.c from akool
 *
 * Revision 1.5  1997/04/16 22:23:00  luethje
 * some bugfixes, README completed
 *
 * Revision 1.4  1997/04/03 22:30:03  luethje
 * improved performance
 *
 * Revision 1.3  1997/03/24 22:52:14  luethje
 * isdnrep completed.
 *
 */

#ifndef _ISDNREP_H_
#define _ISDNREP_H_

#define PUBLIC extern
#include <tools.h>
#include <holiday.h>
#include <rate.h>
#include <telnum.h>

/*****************************************************************************/

#ifdef  MAXUNKNOWN
#undef  MAXUNKNOWN
#endif
#define MAXUNKNOWN   500

#ifdef  MAXCONNECTS
#undef  MAXCONNECTS
#endif
#define MAXCONNECTS  500

/*****************************************************************************/

#define H_PRINT_HTML   1
#define H_PRINT_HEADER 2

/*****************************************************************************/

typedef struct {
  char	 mode;    /* \0 (none), - (logged provider), p (provider), v (vbn) */
  int    prefix;  /* internal provider number */
  int    count;   /* number of calls suitable for recomputing */
  int    unknown; /* thereof the calls which have not been recomputed */
  int    cheaper; /* number of calls where a cheaper provider was found */
  char  *input;   /* input from command line after -r[vp] */
} RECALC;

/*****************************************************************************/

typedef struct {
  int    mode;    /* 0 no action, 1 multiply, 2 divide */
  char  *numstr;  /* number as string, part of given option */
  double number;  /* number for multiplication or division */
} MODCOST;

/*****************************************************************************/

typedef struct {
  int    mode;    /* 0 no action, 1 use and show, 2 only use */
  char  *number;  /* replacement for missing source numbers */
} DEFSRC;

/*****************************************************************************/
/* isdnrep.c defines _REP_FUNC_C_, rep_main.c defines _ISDNREP_C_, ... */
#ifdef _REP_FUNC_C_
#define _EXTERN
#define _SET_NULL   = NULL
#define _SET_0      = 0
#define _SET_1      = 1
#define _SET_33	    = 33
#define _SET_EMPTY  = ""
#define _SET_ARRAY_0 = { 0 }
#else
#define _EXTERN extern
#define _SET_NULL
#define _SET_0
#define _SET_1
#define _SET_33
#define _SET_EMPTY
#define _SET_ARRAY_0
#define _SET_FILE
#endif

_EXTERN int read_logfile(char *myname);
_EXTERN int get_term (char *String, time_t *Begin, time_t *End,int delentries);
_EXTERN int set_msnlist(char *String);
_EXTERN int send_html_request(char *myname, char *option);
_EXTERN int new_args(int *nargc, char ***nargv);
_EXTERN int prep_recalc(void);
_EXTERN int select_summaries(int *list, char *input);
_EXTERN char *select_day_hour(bitfield *days, bitfield *hours, char *input);

_EXTERN int     print_msg(int Level, const char *, ...);
_EXTERN int     incomingonly    _SET_0;
_EXTERN int     outgoingonly    _SET_0;
_EXTERN int     verbose         _SET_0;
_EXTERN int     print_failed    _SET_0;
_EXTERN int	bill		_SET_0;
_EXTERN int     timearea        _SET_0;
_EXTERN int     phonenumberonly _SET_0;
_EXTERN int     delentries      _SET_0;
_EXTERN int     numbers         _SET_0;
_EXTERN int     html         		_SET_0;
_EXTERN int     seeunknowns  		_SET_0;
_EXTERN int     header          _SET_1;
_EXTERN char	  timestring[256] _SET_EMPTY;
_EXTERN char	  *lineformat     _SET_NULL;
_EXTERN time_t  begintime       _SET_0;
_EXTERN time_t  endtime         _SET_0;
#if 0 /* fixme remove */
_EXTERN int     preselect	_SET_33;
#endif
_EXTERN int     summary		_SET_0;
_EXTERN RECALC  recalc; /* initiation done in main */
_EXTERN MODCOST modcost;        /* initiation done in main */
_EXTERN DEFSRC  defsrc;         /* initiation done in main */
_EXTERN int     sel_sums[3]     _SET_ARRAY_0;
_EXTERN bitfield days[2]        _SET_ARRAY_0;
_EXTERN bitfield hours[2]       _SET_ARRAY_0;

#undef _SET_NULL
#undef _SET_0
#undef _SET_1
#undef _SET_EMPTY
#undef _SET_ARRAY_0
#undef _EXTERN

/*****************************************************************************/

#ifdef _OPT_TIME_C_
#define _EXTERN
#else
#define _EXTERN extern
#endif

_EXTERN int get_term (char *String, time_t *Begin, time_t *End,int delentries);

#undef _EXTERN

/*****************************************************************************/

#define LOG_VERSION_1 "1.0"
#define LOG_VERSION_2 "2.0"
#define LOG_VERSION_3 "3.0"
#define LOG_VERSION_4 "3.1"

/*****************************************************************************/

#define C_DELIM '|'

/*****************************************************************************/

typedef struct {
  char   num[NUMSIZE];
  char   mynum[NUMSIZE];
  int   si1;
  int	 called;
  int	 connects;
  time_t connect[MAXCONNECTS];
  int    cause;
} UNKNOWNS;

/****************************************************************************/
/* these were in tools.h, but are onyl used in isdnrep */
typedef struct {
  int    in;
  int    out;
  int    eh;
  int    err;
  double din;
  double dout;
  double pay;
  double ibytes;
  double obytes;
} sum_calls;

/****************************************************************************/

typedef struct {
  int    eh;
  int    cause;
  time_t t;
  int    dir;
  double duration;
  char   num[2][NUMSIZE];
  char   who[2][RETSIZE];
  char	 sarea[2][TN_MAX_SAREA_LEN]; /* lt */
  double ibytes;
  double obytes;
  char   version[10];
  int	 si;
  int	 si1;
  double currency_factor;
  char	 currency[32];
  double pay;
  int	 provider;
  int	 zone; /* fixme: zones may vary over time */
} one_call;
  
/*****************************************************************************/

#endif /* _ISDNREP_H_ */
