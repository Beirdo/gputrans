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
#include <stdlib.h>
#include <unistd.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/avcodec.h>
#include "ipc_queue.h"
#include "ipc_logging.h"
#include "video.h"


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

int main( int argc, char **argv );
void SoftExitParent( void );

int                 numChildren;
AVPicture           pict;
bool                Debug = FALSE;
bool                GlobalAbort = FALSE;
unsigned long       shmmax;

int main(int argc, char **argv)
{
    LoggingItem_t  *message;
    char           *msg;
    QueueMsg_t      type;
    int             len = 0;
    int             counter = 0;
    char            name[16];
    int             cols;
    int             rows;

    queueInit();
    atexit(SoftExitParent);

    initFfmpeg();

    openFile( argv[1], &cols, &rows );

    avpicture_alloc( &pict, PIX_FMT_RGB24, cols, rows );

    while( counter < 100 && getFrame( &pict, PIX_FMT_RGB24 ) ) {
        sprintf( name, "%05d.ppm", counter );
        save_ppm(pict.data[0], cols, rows, 3, name);
        counter++;
    }

    closeFfmpeg( &pict );

    while (len >= 0) {
        type = Q_MSG_LOG;
        queueReceive(&type, &msg, &len, IPC_NOWAIT);
        if (len < 0) {
            /*
             * No more messages waiting 
             */
            continue;
        }
        message = (LoggingItem_t *) msg;
        if (type == Q_MSG_LOG) {
            LogShowLine(message);
        }
    }

    return (0);
}

void SoftExitParent(void)
{
    LoggingItem_t  *message;
    char           *msg;
    QueueMsg_t      type;
    int             len = 0;

    while (len >= 0) {
        type = Q_MSG_LOG;
        queueReceive(&type, &msg, &len, IPC_NOWAIT);
        if (len < 0) {
            /*
             * No more messages waiting 
             */
            continue;
        }
        message = (LoggingItem_t *) msg;
        if (type == Q_MSG_LOG) {
            LogShowLine(message);
        }
    }

    queueDestroy();
    _exit(0);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */
