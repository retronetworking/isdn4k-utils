/*
** $Id$
**
** Copyright 1996-1998 Michael 'Ghandi' Herold <michael@abadonna.mayn.de>
**
** $Log$
** Revision 1.2  1998/06/17 17:01:23  michael
** - First part of the automake/autoconf implementation. Currently vbox will
**   *not* compile!
**
*/

#include "../config.h"

#include <string.h>

#include "stringutils.h"

/*************************************************************************/
/** xstrncpy():																			**/
/*************************************************************************/

void xstrncpy(char *dest, char *source, int max)
{
	strncpy(dest, source, max);
   
	dest[max] = '\0';
}

/*************************************************************************/
/** xstrtol():																				**/
/*************************************************************************/
      
long xstrtol(char *string, long number)
{
	long  back;
	char *stop;

	if (string)
	{
		back = strtol(string, &stop, 10);
		
		if (*stop == '\0') return(back);
	}

	return(number);
}

/*************************************************************************/
/** xstrtoo():																				**/
/*************************************************************************/

long xstrtoo(char *string, long number)
{
	long  back;
	char *stop;

	if (string)
	{
		back = strtol(string, &stop, 8);
		
		if (*stop == '\0') return(back);
	}

	return(number);
}
