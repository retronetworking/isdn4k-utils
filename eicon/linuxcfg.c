/* $Id$
 *
 * Eicon-ISDN driver for Linux. (Config)
 *
 * Copyright 1999    by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
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
 */


#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/types.h>
#include <linux/isdn.h>
#include <eicon.h>



int Divas_ioctl(int fd, int req, void * p)
{
	int ret = 0;
	int newreq = 0;
	isdn_ioctl_struct iioctl_s;

	/* ret = ioctl(fd, req, p); */

	newreq = req + EICON_IOCTL_DIA_OFFSET + IIOCDRVCTL;
	iioctl_s.drvid[0] = '\0';
	iioctl_s.arg = (ulong)p;

	ret = ioctl(fd, newreq, &iioctl_s);

	return(ret);
}

