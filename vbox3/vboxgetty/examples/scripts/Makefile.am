##
## $Id$
##

## The tcl scripts to install ############################################

EXTRA_DIST		= initmodem.tcl answercall.tcl

## Clean & Maintenance ###################################################

CLEANFILES              = *~
MAINTAINERCLEANFILES    = Makefile.in

## Local install routines ################################################

install-data-local:
			  $(mkinstalldirs) $(pkgdatadir)/tcl
			  $(INSTALL) -m 644 initmodem.tcl $(pkgdatadir)/tcl/
			  $(INSTALL) -m 644 answercall.tcl $(pkgdatadir)/tcl/
