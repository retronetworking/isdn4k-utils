/* $Id$
 *
 * Feiertagsberechnung
 *
 * Copyright 1999 by Michael Reinelt (reinelt@eunet.at)
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
 */

#ifndef _HOLIDAY_H_
#define _HOLIDAY_H_

#define MONDAY    1
#define THUESDAY  2
#define WEDNESDAY 3
#define THURSDAY  4
#define FRIDAY    5
#define SATURDAY  6
#define SUNDAY    7
#define HOLIDAY   8

int  initHoliday(char *path);
void exitHoliday(void);
int  isHoliday(struct tm *tm, char **name);
int  getDay(struct tm *tm, char **name);

#endif
