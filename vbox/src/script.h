/*
** $Id$
**
** Copyright (C) 1996, 1997 Michael 'Ghandi' Herold
*/

#ifndef _VBOX_SCRIPT_H
#define _VBOX_SCRIPT_H 1

#include <tcl.h>

#include "lists.h"

/** Structures ************************************************************/

struct vbox_tcl_function
{
	char        *name;
	Tcl_CmdProc *proc;
};

/** Prototypes ***********************************************************/

extern int script_run(char *);
extern int script_run_call(char *, char *, int);
extern int script_check_interpreter(void);

#endif /* _VBOX_SCRIPT_H */
