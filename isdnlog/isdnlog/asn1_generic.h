/* $Id$
 *
 * ISDN accounting for isdn4linux. (ASN.1 parser)
 *
 * Copyright 1995 .. 2000 by Andreas Kool (akool@isdn4linux.de)
 *
 * ASN.1 parser written by Kai Germaschewski <kai@thphy.uni-duesseldorf.de>
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
 *
 *
 */

// ======================================================================
// general ASN.1

int ParseBoolean(struct Aoc *chanp, u_char *p, u_char *end, int *i);
int ParseNull(struct Aoc *chanp, u_char *p, u_char *end, int dummy);
int ParseInteger(struct Aoc *chanp, u_char *p, u_char *end, int *i);
int ParseEnum(struct Aoc *chanp, u_char *p, u_char *end, int *i);
int ParseIA5String(struct Aoc *chanp, u_char *p, u_char *end, char *str);
int ParseNumericString(struct Aoc *chanp, u_char *p, u_char *end, char *str);
int ParseOctetString(struct Aoc *chanp, u_char *p, u_char *end, char *str);

