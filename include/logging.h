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
#ifndef logging_h_
#define logging_h_

/* CVS generated ID string (optional for h files) */
static char logging_h_ident[] _UNUSED_ = 
    "$Id$";

/* Define the log levels (lower number is higher priority) */
#include "logging_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LogLocalPrint(level, format, ...) \
    LogPrintLine(level, __FILE__, __LINE__, (char *)__FUNCTION__, format, \
                 ## __VA_ARGS__)

#define LogLocalPrintNoArg(level, string) \
    LogPrintLine(level, __FILE__, __LINE__, (char *)__FUNCTION__, string)

/* Define the external prototype */
void LogPrintLine( LogLevel_t level, char *file, int line, char *function, 
                   char *format, ... );
bool LogFileAdd( char * filename );
bool LogStdoutAdd( void );
bool LogSyslogAdd( int facility );
bool LogFileRemove( char *filename );
bool LogTcpAdd( int fd );
bool LogTcpRemove( int fd );

void logging_initialize( void );

#ifdef __cplusplus
}
#endif

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
