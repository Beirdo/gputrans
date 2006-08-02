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
#include <sys/msg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "queue.h"


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

#define MAX_MSG_SIZE 1023

int                    idMsg;
static struct msgbuf  *msg;

void queueInit( void )
{
    idMsg = msgget( IPC_PRIVATE, IPC_CREAT | 0600 );
    msg = (struct msgbuf *)malloc(sizeof(struct msgbuf) + MAX_MSG_SIZE);
}

void queueDestroy( void )
{
    free( msg );
    msg = NULL;

    msgctl( idMsg, IPC_RMID, NULL );
}

void queueSendText( QueueMsg_t type, char *text )
{
    if( !msg ) {
        return;
    }

    msg->mtype = type;
    strncpy( msg->mtext, text, MAX_MSG_SIZE );
    msg->mtext[MAX_MSG_SIZE] = '\0';

    msgsnd( idMsg, msg, strlen(msg->mtext)+1, IPC_NOWAIT );
}

void queueSendBinary( QueueMsg_t type, void *data, int len )
{
    if( !msg ) {
        return;
    }

    msg->mtype = type;
    if( len > MAX_MSG_SIZE + 1 ) {
        len = MAX_MSG_SIZE + 1;
    }
    memcpy( msg->mtext, data, len );
    msgsnd( idMsg, msg, len, IPC_NOWAIT );
}

void queueReceive( QueueMsg_t *type, char **buf, int *len, int flags )
{
    long        msgType;

    /* For some reason, when casting, QueueMsg_t is treated as unsigned int,
     * since it can be negative, we need to play tricks to get the typing
     * right here
     */
    if( sizeof(QueueMsg_t) == 4 ) {
        msgType = (long)*(int *)type;
    } else {
        msgType = (long)*type;
    }

    *len = msgrcv( idMsg, msg, MAX_MSG_SIZE + 1, msgType, MSG_NOERROR | flags );

    *type = msg->mtype;
    *buf = msg->mtext;
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

