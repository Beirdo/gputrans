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
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "logging.h"
#include <syslog.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "queue.h"
#include "linked_list.h"
#include "ipc_logging.h"

/* INTERNAL CONSTANT DEFINITIONS */

/* INTERNAL TYPE DEFINITIONS */
typedef enum
{
    LT_CONSOLE,
    LT_FILE,
    LT_SYSLOG
} LogFileType_t;

/* Log File Descriptor Chain */
typedef struct
{
    LinkedListItem_t linkage;
    int fd;
    LogFileType_t type;
    bool aborted;
    union 
    {
        char *filename;
    } identifier;
} LogFileChain_t;

/* INTERNAL MACRO DEFINITIONS */
#define LOGLINE_MAX 256
#define DEBUG_FILE "/var/log/gputrans/debug.log"

/* INTERNAL FUNCTION PROTOTYPES */
void *LoggingThread( void *arg );
void LogWrite( LogFileChain_t *logfile, char *text, int length );
bool LogOutputRemove( LogFileChain_t *logfile );
void LogOutputAdd( int fd, LogFileType_t type, void *identifier );

/* CVS generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

QueueObject_t  *LoggingQ;
LinkedList_t   *LogList;
pthread_t       loggingThreadId;

extern bool     Debug;
extern bool     GlobalAbort;

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
void LogLocalPrintLine( LogLevel_t level, char *file, int line, char *function,
                        char *format, ... )
{
    LoggingItem_t *item;
    va_list arguments;

    if( level > LogLevel ) {
        return;
    }

    item = (LoggingItem_t *)malloc(sizeof(LoggingItem_t));
    if( !item ) {
        return;
    }

    item->level     = level;
    item->pidId     = getpid();
    strncpy( item->file, file, 128 );
    item->file[127] = '\0';

    item->line       = line;
    strncpy( item->function, function, 128 );
    item->function[127] = '\0';

    gettimeofday( &item->tv, NULL );

    va_start(arguments, format);
    vsnprintf(item->message, LOGLINE_MAX, format, arguments);
    va_end(arguments);

    QueueEnqueueItem( LoggingQ, item );
}

void logging_initialize( void )
{

    LoggingQ = QueueCreate(1024);
    LogList = LinkedListCreate();

    LogStdoutAdd();
    LogSyslogAdd( LOG_LOCAL7 );
    if( Debug ) {
        LogFileAdd( DEBUG_FILE );
    }

    pthread_create( &loggingThreadId, NULL, LoggingThread, NULL );
}

void logging_toggle_debug( int signum )
{
    if( Debug ) {
        /* We are turning OFF debug logging */
        LogPrintNoArg( LOG_CRIT, "Received SIGUSR1, disabling debug logging" );
        LogFileRemove( DEBUG_FILE );
        Debug = FALSE;
    } else {
        /* We are turning ON debug logging */
        LogPrintNoArg( LOG_CRIT, "Received SIGUSR1, enabling debug logging" );
        LogFileAdd( DEBUG_FILE );
        Debug = TRUE;
    }
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
void *LoggingThread( void *arg )
{
    LoggingItem_t      *item;
    struct tm           ts;
    char                line[MAX_STRING_LENGTH];
    char                usPart[9];
    char                timestamp[TIMESTAMP_MAX];
    int                 length;
    LinkedListItem_t   *listItem, *next;
    LogFileChain_t     *logFile;
    struct timespec     delay;

    /* 100ms delay */
    delay.tv_sec = 0;
    delay.tv_nsec = 100000L;

    LogPrintNoArg( LOG_NOTICE, "Started LoggingThread" );

    while( 1 ) {
        item = (LoggingItem_t *)QueueDequeueItem( LoggingQ, -1 );
        if( !item ) {
            nanosleep( &delay, NULL );
            continue;
        }

        localtime_r( (const time_t *)&(item->tv.tv_sec), &ts );
        strftime( timestamp, TIMESTAMP_MAX-8, "%Y-%b-%d %H:%M:%S",
                  (const struct tm *)&ts );
        snprintf( usPart, 9, ".%06d ", (int)(item->tv.tv_usec) );
        strcat( timestamp, usPart );
        length = strlen( timestamp );
        
        LinkedListLock( LogList );
        
        for( listItem = LogList->head; listItem; listItem = next ) {
            logFile = (LogFileChain_t *)listItem;
            next = listItem->next;

            switch( logFile->type ) {
            case LT_SYSLOG:
                syslog( item->level, "%s", item->message );
                break;
            case LT_CONSOLE:
                sprintf( line, "%s [%d] %s\n", timestamp, item->pidId,
                                               item->message );
                LogWrite( logFile, line, strlen(line) );
                break;
            case LT_FILE:
                sprintf( line, "%s [%d] %s:%d (%s) - %s\n", timestamp, 
                         item->pidId, item->file, item->line, item->function, 
                         item->message );
                LogWrite( logFile, line, strlen(line) );
                break;
            default:
                break;
            }

            if( logFile->aborted ) {
                LogOutputRemove( logFile );
            }
        }

        LinkedListUnlock( LogList );

        free( item );
    }

    return( NULL );
}

bool LogStdoutAdd( void )
{
    /* STDOUT corresponds to file descriptor 1 */
    LogOutputAdd( 1, LT_CONSOLE, NULL );
    LogPrintNoArg( LOG_INFO, "Added console logging" );
    return( TRUE );
}


bool LogSyslogAdd( int facility )
{
    openlog( "gputrans", LOG_NDELAY | LOG_PID, facility );
    LogOutputAdd( -1, LT_SYSLOG, NULL );
    LogPrintNoArg( LOG_INFO, "Added syslog logging" );
    return( TRUE );
}


void LogOutputAdd( int fd, LogFileType_t type, void *identifier )
{
    LogFileChain_t *item;

    item = (LogFileChain_t *)malloc(sizeof(LogFileChain_t));
    memset( item, 0, sizeof(LogFileChain_t) );

    item->type    = type;
    item->aborted = FALSE;
    switch( type )
    {
        case LT_SYSLOG:
            item->fd = -1;
            break;
        case LT_FILE:
            item->fd = fd;
            item->identifier.filename = strdup( (char *)identifier );
            break;
        case LT_CONSOLE:
            item->fd = fd;
            break;
        default:
            /* UNKNOWN! */
            free( item );
            return;
            break;
    }

    /* Add it to the Log File List (note, the function contains the mutex
     * handling
     */
    LinkedListAdd( LogList, (LinkedListItem_t *)item, UNLOCKED, AT_TAIL );
}


bool LogOutputRemove( LogFileChain_t *logfile )
{
    if( logfile == NULL )
    {
        return( FALSE );
    }

    /* logfile will be pointing at the offending member, close then 
     * remove it.  It is assumed that the caller already has the Mutex
     * locked.
     */
    switch( logfile->type )
    {
        case LT_FILE:
        case LT_CONSOLE:
            close( logfile->fd );
            if( logfile->identifier.filename != NULL )
            {
                free( logfile->identifier.filename );
            }
            break;
        case LT_SYSLOG:
            /* Nothing to do */
            break;
        default:
            break;
    }

    /* Remove the log file from the linked list */
    LinkedListRemove( LogList, (LinkedListItem_t *)logfile, LOCKED );

    free( logfile );
    return( TRUE );
}

void LogWrite( LogFileChain_t *logfile, char *text, int length )
{
    int result;

    if( logfile->aborted == FALSE )
    {
        result = write( logfile->fd, text, length );
        if( result == -1 )
        {
            LogPrint( LOG_UNKNOWN, "Closed Log output on fd %d due to errors", 
                      logfile->fd );
            logfile->aborted = TRUE;
        }
    }
}


bool LogFileAdd( char * filename )
{
    int fd;

    if( filename == NULL )
    {
        return( FALSE );
    }

    fd = open( filename, O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK,
               S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
    if( fd == -1 )
    {
        /* Couldn't open the log file.  Gak! */
        perror( "debug: " );
        return( FALSE );
    }

    LogOutputAdd( fd, LT_FILE, filename );
    LogPrint( LOG_INFO, "Added log file: %s", filename );

    return( TRUE );
}

bool LogFileRemove( char *filename )
{
    LogFileChain_t *logfile;
    LinkedListItem_t *listItem;
    bool found;

    if( filename == NULL )
    {
        return( FALSE );
    }

    LinkedListLock( LogList );

    for( listItem = LogList->head, found = FALSE; 
         listItem != NULL;
         listItem = listItem->next )
    {
        logfile = (LogFileChain_t *)listItem;
        if( logfile->type == LT_FILE && 
            strcmp( filename, logfile->identifier.filename ) == 0 )
        {
            LogOutputRemove( logfile );
            LogPrint( LOG_INFO, "Removed log file: %s", filename );
            found = TRUE;
            /* Take an early exit from the loop */
            break;
        }
    }

    if( found == FALSE )
    {
        LogPrint( LOG_UNKNOWN, "Can't find log file: %s", filename );
    }

    LinkedListUnlock( LogList );
    return( found );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
