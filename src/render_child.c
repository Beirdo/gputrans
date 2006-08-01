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


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void do_child( int childNum );
void SoftExitChild( void );

extern int         idShm, idSem;
extern char       *shmBlock;


void do_child( int childNum )
{
    int         argc = 3;
    char        display[256];
    char       *argv[] = { "client", "-display", display };
    GLuint      fb;
    int         maxTexSize;
    const char *glRenderer;

    (void)fb;

    atexit( SoftExitChild );

    sprintf( display, ":0.%d", childNum );

    shmBlock = (char *)shmat( idShm, NULL, 0 );

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

    if( glGenFramebuffersEXT && glBindFramebufferEXT ) {
        LogPrint( LOG_NOTICE, "<%d> Supports framebuffer objects", childNum );
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support framebuffer objects", 
                              childNum );
    }
#if 0
    glGenFramebuffersEXT(1, &fb);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
#endif

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    glRenderer = (const char *) glGetString(GL_RENDERER);

    LogPrint( LOG_NOTICE, "<%d> (%s) Renderer: %s", childNum, display,
                          glRenderer );
    LogPrint( LOG_NOTICE, "<%d> Max Texture Size: %d", childNum, maxTexSize );

    sleep(10);
    exit(0);
}


void SoftExitChild( void )
{
    shmdt( shmBlock );
    _exit( 0 );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

