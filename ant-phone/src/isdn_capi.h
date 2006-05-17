/*  $Id$
 *
 *  This file is part of ANT (Ant is Not a Telephone)
 *
 *  Authors:
 *    - Martin Bachem, m.bachem@gmx.de
 *    - heavy capitalized on: isdn4k-utils/[pppdcapiplugin,capiiinfo]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "session.h"

int  ant_capi_init(session_t *session);
void ant_capi_listen(session_t *session);
void ant_capi_free(session_t *session);
void ant_capi_messages(session_t *session);
