##
## $Id$
##

## Other directories #####################################################

SUBDIRS			= scripts

## Configuration to install ##############################################

EXTRA_DIST		= vboxgetty.conf vboxcallerid.conf

## Clean & Maintenance ###################################################

CLEANFILES		= *~
MAINTAINERCLEANFILES	= Makefile.in

## Local install routines ################################################

install-data-local:
			  $(mkinstalldirs) $(sysconfdir)
			  $(INSTALL) -m 664 vboxgetty.conf $(sysconfdir)
			  $(INSTALL) -m 664 callerid.conf  $(sysconfdir)
