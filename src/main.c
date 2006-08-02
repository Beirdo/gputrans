/*
 *  This file is part of the gputrans package
 *  Copyright (C) 2006 Gavin Hurlbut
 *
 *  beirdobot is free software; you can redistribute it and/or modify
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


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

int main( int argc, char **argv );
void signal_handler( int signum );
void signal_child( int signum );
void SoftExitParent( void );
void do_child( int childNum );

int                 idShm, idSem;
int                 childCount = 0;
char               *shmBlock;
int                 numChildren = 0;
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

    queueInit();

    signal( SIGINT, signal_handler );
    signal( SIGCHLD, signal_child );

    LogPrint( LOG_NOTICE, "Starting, %d", argc );

    idShm = shmget( IPC_PRIVATE, sizeof(sharedMem_t), IPC_CREAT | 0600 );
    shmBlock = (char *)shmat( idShm, NULL, 0 );
    memset( shmBlock, 0, sizeof(sharedMem_t) );
    sharedMem = (sharedMem_t *)shmBlock;

    child = -1;
    for( i = 0; i < 5 && child; i++ ) {
        child = fork();
        if( !child ) {
            do_child( i );
        } else {
#if 0
            LogPrint( LOG_NOTICE, "In parent - child = %d", child );
#endif
            childCount++;
            numChildren++;
        }
    }

    if( child ) {
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
                LogPrint( LOG_NOTICE, "Child %d ready", childNum );
                queueSendBinary( Q_MSG_CLIENT_START + childNum, "", 0 );
                break;
            default:
                break;
            }
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
        if( !pid ) {
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

        if( childCount == 0 ) {
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


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

