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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glew.h>
#include <GL/glut.h>
#include "queue.h"
#include "logging.h"
#include "shared_mem.h"


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void do_child( int childNum );
void SoftExitChild( void );
void setupCardInfo( int childNum );

extern int          idShm, idSem;
extern char        *shmBlock;
static sharedMem_t *sharedMem;
static cardInfo_t  *cardInfo;


void do_child( int childNum )
{
    int             argc = 3;
    char           *argv[] = { "client", "-display", NULL };
    GLuint          fb;
    char           *msg;
    QueueMsg_t      type;
    int             len;


    (void)fb;

    shmBlock = NULL;
    atexit( SoftExitChild );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    sharedMem = (sharedMem_t *)shmBlock;
    cardInfo = &sharedMem->cardInfo[childNum];
    argv[2] = cardInfo->display;

    glutInit( &argc, argv );
    glutCreateWindow( "gputrans" );
    glewInit();
    
#if 0
    LogPrint( LOG_NOTICE, "Using GLEW %s", glewGetString(GLEW_VERSION) );
#endif

#if 0
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 0, texSize, texSize );
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, texSize, texSize );
#endif

    /* must be done after OpenGL initialization */
    setupCardInfo( childNum );

    queueSendBinary( Q_MSG_READY, &childNum, sizeof(childNum) );

#if 0
    glGenFramebuffersEXT(1, &fb);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
#endif

    len = -1;

    while( len < 0 ) {
        type = Q_MSG_CLIENT_START + childNum;
        queueReceive( &type, &msg, &len, 0 );
        if( len < 0 ) {
            continue;
        }
    }

    LogPrint( LOG_NOTICE, "<%d> Got message, exiting", childNum );
    sleep(1);
    exit(0);
}


void SoftExitChild( void )
{
    if( shmBlock ) {
        shmdt( shmBlock );
    }
    _exit( 0 );
}

void setupCardInfo( int childNum )
{
    GLint           val[4];
    const GLubyte  *string;

    cardInfo->childNum = childNum;
    cardInfo->pid = getpid();

    glGetIntegerv(GL_MAX_TEXTURE_SIZE,  &val[0]);
    cardInfo->maxTexSize = val[0];

    string = glGetString(GL_VENDOR);
    strncpy( cardInfo->vendor, (char *)string, 
             ELEMSIZE(vendor, cardInfo_t) );

    string = glGetString(GL_RENDERER);
    strncpy( cardInfo->renderer, (char *)string, 
             ELEMSIZE(renderer, cardInfo_t) );

    string = glGetString(GL_VERSION);
    strncpy( cardInfo->version, (char *)string, 
             ELEMSIZE(version, cardInfo_t) );

    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, &val[0]);
    cardInfo->maxViewportDim[0] = val[0];
    cardInfo->maxViewportDim[1] = val[1];

    LogPrint( LOG_NOTICE, "<%d> Display: %s", childNum, cardInfo->display );
    LogPrint( LOG_NOTICE, "<%d> Vendor: %s", childNum, cardInfo->vendor );
    LogPrint( LOG_NOTICE, "<%d> Renderer: %s", childNum, 
                          cardInfo->renderer );
    LogPrint( LOG_NOTICE, "<%d> Version: %s", childNum, cardInfo->version );
    LogPrint( LOG_NOTICE, "<%d> Max Texture Size: %d", childNum, 
                          cardInfo->maxTexSize );
    LogPrint( LOG_NOTICE, "<%d> Max Viewport Dimensions: %dx%d", childNum, 
                          cardInfo->maxViewportDim[0], 
                          cardInfo->maxViewportDim[1]);

    /* GLEW_EXT_framebuffer_object doesn't seem to work */
    if( glGenFramebuffersEXT && glBindFramebufferEXT ) {
        LogPrint( LOG_NOTICE, "<%d> Supports framebuffer objects", 
                              childNum );
        cardInfo->haveFBO = TRUE;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &val[0]);
        cardInfo->maxColorAttach = val[0];
        LogPrint( LOG_NOTICE, "<%d> Max Color Attachments: %d", childNum, 
                              cardInfo->maxColorAttach );
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support framebuffer objects", 
                              childNum );
        cardInfo->haveFBO = FALSE;
    }

    if( GLEW_ARB_texture_rectangle ) {
        LogPrint( LOG_NOTICE, "<%d> Supports texture rectangles", 
                              childNum );
        cardInfo->haveTextRect = TRUE;
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support texture rectangles",
                              childNum );
        cardInfo->haveTextRect = FALSE;
    }

    if( GLEW_NV_float_buffer ) {
        LogPrint( LOG_NOTICE, "<%d> Supports NV float buffers", childNum );
        cardInfo->haveNvFloat = TRUE;
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support NV float buffers",
                              childNum );
        cardInfo->haveNvFloat = FALSE;
    }
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

