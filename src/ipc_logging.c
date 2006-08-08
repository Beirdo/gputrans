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
 */


/**
 * @file
 * @brief Logs messages to the console and/or logfiles
 */

/* INCLUDE FILES */
#include "environment.h"
#define _LogLevelNames_
#include "ipc_logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ipc_queue.h"


/* INTERNAL MACRO DEFINITIONS */

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

LogLevel_t LogLevel = LOG_UNKNOWN;  /**< The log level mask to apply, messages
                                         must be at at least this priority to
                                         be output */
LoggingItem_t item;

/**
 * @brief Formats and enqueues a log message for the Logging thread
 * @param level the logging level to log at
 * @param file the sourcefile the message is from
 * @param line the line in the sourcefile the message is from
 * @param function the function the message is from
 * @param format the printf-style format string
 *
 * Creates a log message (up to LOGLINE_MAX) in length using vsnprintf, and
 * enqueues it with a timestamp, thread and sourcefile info.  These messages 
 * go onto the LoggingQ which is then read by the Logging thread.  When this
 * function returns, all strings passed in can be reused or freed.
 */
void LogPrintLine( LogLevel_t level, char *file, int line, char *function,
                   char *format, ... )
{
    va_list arguments;

    if( level > LogLevel ) {
        return;
    }

    item.level      = level;
    item.pidId      = getpid();
    strncpy( item.file, file, 128 );
    item.file[127] = '\0';

    item.line       = line;
    strncpy( item.function, function, 128 );
    item.function[127] = '\0';

    gettimeofday( &item.tv, NULL );

    va_start(arguments, format);
    vsnprintf(item.message, LOGLINE_MAX, format, arguments);
    va_end(arguments);

    queueSendBinary( Q_MSG_LOG, &item, sizeof(item) - sizeof(item.message) +
                                       strlen(item.message) + 1 );
}


/**
 * @brief Prints the log messages to the console (and logfile)
 * @param arg unused
 * @return never returns until shutdown
 * @todo Add support for a logfile as well as console output.
 *
 * Dequeues log messages from the LoggingQ and outputs them to the console.
 * If the message's log level is lower (higher numerically) than the current
 * system log level, the message will be dumped and not displayed.
 * In the future, it will also log to a logfile.
 */
void LogShowLine( LoggingItem_t *logItem )
{
    struct tm           ts;
    char                usPart[9];
    char                timestamp[TIMESTAMP_MAX];

    if( !logItem ) {
        return;
    }

    localtime_r( (const time_t *)&(logItem->tv.tv_sec), &ts );
    strftime( timestamp, TIMESTAMP_MAX-8, "%Y-%b-%d %H:%M:%S",
              (const struct tm *)&ts );
    snprintf( usPart, 9, ".%06d ", (int)(logItem->tv.tv_usec) );
    strcat( timestamp, usPart );

    printf( "%s [%d] %s\n", timestamp, logItem->pidId, logItem->message );
}

void *LogThread( void *arg )
{
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
