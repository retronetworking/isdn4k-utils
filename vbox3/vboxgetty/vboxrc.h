/*
** $Id$
**
** Copyright 1996-1998 Michael 'Ghandi' Herold <michael@abadonna.mayn.de>
*/

#ifndef _VBOXRC_H
#define _VBOXRC_H 1

/** Defines **************************************************************/

#define VBOXRC_MAX_RCLINE		255

/** Structures ***********************************************************/

struct vboxcall
{
	unsigned char	section[VBOXRC_MAX_RCLINE + 1];
	unsigned char	name[VBOXRC_MAX_RCLINE + 1];
	unsigned char	script[VBOXRC_MAX_RCLINE + 1];
	int				ringring;
	int				tollring;
	int				savetime;
};

/** Prototypes ***********************************************************/

extern int vboxrc_parse(struct vboxcall *, unsigned char *, unsigned char *);

#endif /* _VBOXRC_H */
