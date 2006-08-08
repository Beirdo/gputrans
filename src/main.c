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
*/

#include "environment.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "queue.h"
#include "logging.h"
#include "shared_mem.h"
#include "video.h"


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#define CONF_FILE   "/etc/gputrans.conf"

int main( int argc, char **argv );
void signal_handler( int signum );
void signal_child( int signum );
void SoftExitParent( void );
void do_child( int childNum );
void readConfigFile( void );

int                 idShm, idSem;
extern int          idFrame;
int                 childCount = 0;
char               *shmBlock;
int                 numChildren = -1;
static sharedMem_t *sharedMem;


int main( int argc, char **argv )
{
    pid_t               child;
    int                 i;
    LoggingItem_t      *message;
    char               *msg;
    QueueMsg_t          type;
    int                 len;
    int                 childNum;
    ChildMsg_t          msgOut;
    cardInfo_t         *cardInfo;

    queueInit();
    initFfmpeg();

    signal( SIGINT, signal_handler );
    signal( SIGCHLD, signal_child );

    LogPrintNoArg( LOG_NOTICE, "Starting gputrans" );

    idShm = shmget( IPC_PRIVATE, sizeof(sharedMem_t), IPC_CREAT | 0600 );
    shmBlock = (char *)shmat( idShm, NULL, 0 );
    memset( shmBlock, 0, sizeof(sharedMem_t) );
    sharedMem = (sharedMem_t *)shmBlock;

    readConfigFile();
    if( numChildren == -1 ) {
        fprintf( stderr, "Couldn't find config file %s\n\n", CONF_FILE );
        exit( 1 );
    }
    numChildren++;  /* Adjust for starting at -1 */

    /* Fork the children that will each control an OpenGL context */
    child = -1;
    for( i = 0; i < numChildren && child; i++ ) {
        child = fork();
        if( !child ) {
            /* Never returns */
            do_child( i );
        } else {
            childCount++;
        }
    }

    if( !child ) {
        LogPrintNoArg( LOG_CRIT, "How the hell did I get here?" );
        exit( 0 );
    }

    /* This is in the parent */
    atexit( SoftExitParent );

    while( 1 ) {
        type = Q_MSG_ALL_SERVER;
        queueReceive( &type, &msg, &len, 0 );
        if( len < 0 ) {
            continue;
        }
        message = (LoggingItem_t *)msg;
        switch( type ) {
        case Q_MSG_LOG:
            LogShowLine( message );
            break;
        case Q_MSG_READY:
            childNum = *(int *)msg;
            cardInfo = &sharedMem->cardInfo[childNum];
            if( !cardInfo->have.NvFloat || !cardInfo->have.TextRect ) {
                LogPrint( LOG_NOTICE, "Child %d ready, but doesn't support "
                                      "required extensions, killing it", 
                                      childNum );
                msgOut.type = CHILD_EXIT;
                queueSendBinary( Q_MSG_CLIENT_START + childNum, 
                                 (char *)&msgOut, 
                                 ELEMSIZE(type, ChildMsg_t) );
            } else {
                LogPrint( LOG_NOTICE, "Child %d ready", childNum );
                msgOut.type = CHILD_RENDER_MODE;
                msgOut.payload.renderMode.mode = 0;
                queueSendBinary( Q_MSG_CLIENT_START + childNum, 
                                 (char *)&msgOut, 
                                 sizeof(ChildMsg_t) + 
                                 ELEMSIZE( renderMode, ChildMsgPayload_t ) - 
                                 ELEMSIZE( payload, ChildMsg_t ) );
            }
            break;
        default:
            break;
        }
    }
}

void signal_handler( int signum )
{
    extern const char *const sys_siglist[];

    LogPrint( LOG_CRIT, "Received signal: %s", sys_siglist[signum] );
    exit( 0 );
}

void signal_child( int signum )
{
    extern const char *const sys_siglist[];
    int             status;
    pid_t           pid;
    int             i;
    cardInfo_t     *cardInfo;
    int             childNum;

    (void)signum;

    while( 1 ) {
        pid = waitpid( -1, &status, WNOHANG );
        if( pid <= 0 ) {
            /* Ain't none left waiting to be de-zombified */
            return;
        }

        childNum = -1;
        for( i = 0; i < numChildren; i++ ) {
            cardInfo = &sharedMem->cardInfo[i];
            if( cardInfo->pid == pid ) {
                childNum = cardInfo->childNum;
            }
        }

        if( WIFEXITED(status) ) {
            childCount--;
            LogPrint( LOG_CRIT, "Child %d (pid %d) exited (ret=%d) - %d left", 
                                childNum, pid, WEXITSTATUS(status), 
                                childCount );
        } else if( WIFSIGNALED(status) ) {
            childCount--;
            LogPrint( LOG_CRIT, "Child %d (pid %d) exited (%s) - %d left", 
                                childNum, pid, sys_siglist[WTERMSIG(status)], 
                                childCount );
        }

        if( childCount <= 0 ) {
            LogPrintNoArg( LOG_CRIT, "All children exited, exiting" );
            exit( 0 );
        }
    }
}

void SoftExitParent( void )
{
    LoggingItem_t      *message;
    char               *msg;
    QueueMsg_t          type;
    int                 len = 0;

    while( len >= 0 ) {
        type = Q_MSG_LOG;
        queueReceive( &type, &msg, &len, IPC_NOWAIT );
        if( len < 0 ) {
            /* No more messages waiting */
            continue;
        }
        message = (LoggingItem_t *)msg;
        if( type == Q_MSG_LOG ) {
            LogShowLine( message );
        }
    }

    shmdt( shmBlock );
    shmctl( idShm, IPC_RMID, NULL );
    queueDestroy();
    _exit( 0 );
}

void readConfigFile( void )
{
    FILE           *fp;
    cardInfo_t     *cardInfo;
    char            line[MAX_STRING_LENGTH];
    char           *comment;
    char           *display;
    int             len;

    fp = fopen( CONF_FILE, "r" );
    if( !fp ) {
        return;
    }

    while( !feof( fp ) && numChildren < MAX_NUM_CARDS ) {
        fgets( line, MAX_STRING_LENGTH, fp );
        if( line[0] == '\0' || line[0] == '\n' || line[0] == '\r' ||
            line[0] == '#' ) {
            continue;
        }

        display = &line[0];
        comment = strchr( display, '#' );
        if( comment ) {
            *comment = '\0';
        }

        /* Eat leading spaces */
        while( *display == ' ' || *display == '\t' ) {
            display++;
        }

        /* Eat trailing spaces */
        len = strlen( display );
        while( len && (display[len-1] == ' ' || display[len-1] == '\t' ||
                       display[len-1] == '\n' || display[len-1] == '\r') ) {
            display[len-1] = '\0';
            len--;
        }

        if( !len ) {
            /* Empty string, eh? */
            continue;
        }

        /* The remainder is our display string */
        numChildren++;  /* initialized to -1, so preincrement */
        cardInfo = &sharedMem->cardInfo[numChildren];

        strncpy( cardInfo->display, display, ELEMSIZE( display, cardInfo_t ) );
        cardInfo->display[ELEMSIZE(display, cardInfo_t) - 1] = '\0';
    }

    fclose( fp );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

