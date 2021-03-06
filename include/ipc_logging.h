/*
 *  This file is part of the gputrans package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  gputrans is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*HEADER---------------------------------------------------
* $Id$
*
* Copyright 2006 Gavin Hurlbut
* All rights reserved
*
* Comments :
*
*
*--------------------------------------------------------*/
#ifndef ipc_logging_h_
#define ipc_logging_h_

#include <sys/types.h>
#include <sys/time.h>
#include "environment.h"

/* CVS generated ID string (optional for h files) */
static char ipc_logging_h_ident[] _UNUSED_ = 
    "$Id$";

/* Define the log levels (lower number is higher priority) */
#include "logging_common.h"

typedef struct
{
    LogLevel_t          level;
    pid_t               pidId;
    char                file[128];
    int                 line;
    char                function[128];
    struct timeval      tv;
    char                message[LOGLINE_MAX];
} LoggingItem_t;


#ifdef __cplusplus
extern "C" {
#endif


#define LogPrint(level, format, ...) \
    LogIpcPrintLine(level, __FILE__, __LINE__, (char *)__FUNCTION__, format, \
                    ## __VA_ARGS__)

#define LogPrintNoArg(level, string) \
    LogIpcPrintLine(level, __FILE__, __LINE__, (char *)__FUNCTION__, string)


/* Define the external prototype */
void LogIpcPrintLine( LogLevel_t level, char *file, int line, char *function, 
                      char *format, ... );
void LogShowLine( LoggingItem_t *logItem );


#ifdef __cplusplus
}
#endif

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
