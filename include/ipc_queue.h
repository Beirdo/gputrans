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

#ifndef ipc_queue_h
#define ipc_queue_h

#include "environment.h"

/* SVN generated ID string */
static char queue_h_ident[] _UNUSED_ = 
    "$Id$";


typedef enum {
    Q_MSG_UNUSED = 0,
    Q_MSG_READY,
    Q_MSG_FRAME_DONE,
    Q_MSG_DYING_GASP,
    Q_MSG_LOG,
    Q_MSG_CLIENT_START
} QueueMsg_t;

#define Q_MSG_ALL_SERVER    (-1 * Q_MSG_LOG)

typedef enum {
    CHILD_EXIT,
    CHILD_RENDER_MODE,
    CHILD_RENDER_FRAME
} ChildMsgType_t;

typedef struct {
    int             mode;
    int             cols;
    int             rows;
} RenderMode_t;

typedef struct {
    int             frameNum;
    int             indexIn;
    int             indexOut;
} RenderFrame_t;

typedef union {
    RenderMode_t        renderMode;
    RenderFrame_t       renderFrame;
} ChildMsgPayload_t;

typedef struct {
    ChildMsgType_t      type;
    ChildMsgPayload_t   payload;
} ChildMsg_t;

void queueInit( void );
void queueDestroy( void );
void queueSendText( QueueMsg_t type, char *text );
void queueSendBinary( QueueMsg_t type, void *data, int len );
void queueReceive( QueueMsg_t *type, char **buf, int *len, int flags );


#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

