/*
 * $Id$
 *
 * rate database (Tarifdatenbank) -- skipping of providers
 *
 * Copyright 2005 Tobias Becker <tobiasb@isdn4linux.de>
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
 */

#ifndef _RATE_SKIP_H_
#define _RATE_SKIP_H_

int add_skipped_provider(char *line, char **msg);
int is_skipped_provider(int prov, int var, int booked);
int clear_skipped_provider(void);
int dump_skipped_provider(char *str, int len);

#endif /* _RATE_SKIP_H_ */

/*
 * $Log$
 */

/* vim:set ts=2: */
