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
void initFBO( int x, int y );
void checkGLErrors( const char *label );
void drawQuad( int w, int h );
bool checkFramebufferStatus( void );
void swap( void );
void setupTexture(const GLuint texID, int x, int y);
void createTextures(int x, int y);
void loadFrame(uint32 *data, int x, int y);

extern int          idShm, idSem;
extern char        *shmBlock;
static sharedMem_t *sharedMem;
static cardInfo_t  *cardInfo;
static int          me;
static bool         initialized = FALSE;
static int          width, height;

GLuint              glutWindowHandle;
GLuint              fb;

/* ping pong management vars */
int writeTex = 0;
int readTex = 1;
GLenum attachmentpoints[] = { GL_COLOR_ATTACHMENT0_EXT, 
                              GL_COLOR_ATTACHMENT1_EXT };
 
GLenum texTarget = GL_TEXTURE_RECTANGLE_ARB;
GLenum texInternalFormat = GL_FLOAT_RGBA32_NV;
GLenum texFormat = GL_RGBA;

GLuint pingpongTexID[2];
GLuint inputTexID;


void do_child( int childNum )
{
    int             argc = 3;
    char           *argv[] = { "client", "-display", NULL };
    char           *msg;
    QueueMsg_t      type;
    int             len;
    ChildMsg_t     *message;

    (void)width;
    (void)height;

    shmBlock = NULL;
    atexit( SoftExitChild );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    sharedMem = (sharedMem_t *)shmBlock;
    cardInfo = &sharedMem->cardInfo[childNum];
    argv[2] = cardInfo->display;
    me = childNum;

    glutInit( &argc, argv );
    glutWindowHandle = glutCreateWindow( "gputrans" );
    initialized = TRUE;
    glewInit();
    
    /* must be done after OpenGL initialization */
    setupCardInfo( childNum );

    queueSendBinary( Q_MSG_READY, &childNum, sizeof(childNum) );

    len = -1;

    while( len < 0 ) {
        type = Q_MSG_CLIENT_START + childNum;
        queueReceive( &type, &msg, &len, 0 );
        if( len < 0 ) {
            continue;
        }
    }

    message = (ChildMsg_t *)msg;
    switch( message->type ) {
    case CHILD_EXIT:
        LogPrint( LOG_NOTICE, "<%d> Got message, exiting", childNum );
        break;
    case CHILD_RENDER_MODE:
        LogPrint( LOG_NOTICE, "<%d> Entering rendering mode %d", childNum,
                              message->payload.renderMode.mode );
        sleep(10);
        break;
    default:
        break;
    }

    sleep(1);
    exit(0);
}


void SoftExitChild( void )
{
    queueSendBinary( Q_MSG_DYING_GASP, &me, sizeof(me) );
    if( initialized ) {
        glutDestroyWindow (glutWindowHandle);
    }

    if( shmBlock ) {
        shmdt( shmBlock );
    }

    sleep( 1 );
    _exit( 0 );
}

void setupCardInfo( int childNum )
{
    GLint           val[4];
    const GLubyte  *string;

    cardInfo->childNum = childNum;
    cardInfo->pid = getpid();

    glGetIntegerv(GL_MAX_TEXTURE_SIZE,  &val[0]);
    cardInfo->max.TexSize = val[0];

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
    cardInfo->max.ViewportDim[0] = val[0];
    cardInfo->max.ViewportDim[1] = val[1];

    LogPrint( LOG_NOTICE, "<%d> Display: %s", childNum, cardInfo->display );
    LogPrint( LOG_NOTICE, "<%d> Vendor: %s", childNum, cardInfo->vendor );
    LogPrint( LOG_NOTICE, "<%d> Renderer: %s", childNum, 
                          cardInfo->renderer );
    LogPrint( LOG_NOTICE, "<%d> Version: %s", childNum, cardInfo->version );
    LogPrint( LOG_NOTICE, "<%d> Max Texture Size: %d", childNum, 
                          cardInfo->max.TexSize );
    LogPrint( LOG_NOTICE, "<%d> Max Viewport Dimensions: %dx%d", childNum, 
                          cardInfo->max.ViewportDim[0], 
                          cardInfo->max.ViewportDim[1]);

    /* GLEW_EXT_framebuffer_object doesn't seem to work */
    if( glGenFramebuffersEXT && glBindFramebufferEXT ) {
        LogPrint( LOG_NOTICE, "<%d> Supports framebuffer objects", 
                              childNum );
        cardInfo->have.FBO = TRUE;
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &val[0]);
        cardInfo->max.ColorAttach = val[0];
        LogPrint( LOG_NOTICE, "<%d> Max Color Attachments: %d", childNum, 
                              cardInfo->max.ColorAttach );
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support framebuffer objects", 
                              childNum );
        cardInfo->have.FBO = FALSE;
    }

    if( GLEW_ARB_texture_rectangle ) {
        LogPrint( LOG_NOTICE, "<%d> Supports texture rectangles", 
                              childNum );
        cardInfo->have.TextRect = TRUE;
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support texture rectangles",
                              childNum );
        cardInfo->have.TextRect = FALSE;
    }

    if( GLEW_NV_float_buffer ) {
        LogPrint( LOG_NOTICE, "<%d> Supports NV float buffers", childNum );
        cardInfo->have.NvFloat = TRUE;
    } else {
        LogPrint( LOG_NOTICE, "<%d> Does not support NV float buffers",
                              childNum );
        cardInfo->have.NvFloat = FALSE;
    }
}

void initFBO( int x, int y )
{
    /* Create the framebuffer object for off-screen rendering */
    glGenFramebuffersEXT(1, &fb);

    /* Redirect the output to said buffer rather than the screen */
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, x, 0, y );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, x, y );
}

void checkGLErrors( const char *label )
{
    GLenum          errCode;
    const GLubyte  *errStr;

    if ((errCode = glGetError()) != GL_NO_ERROR) {
        errStr = gluErrorString(errCode);
        LogPrint( LOG_CRIT, "<%d> OpenGL ERROR: %s  (Label: %s)", errStr,
                            label );
        exit( 1 );
    }
}

/*
 * Renders w x h quad in top left corner of the viewport.
 */
void drawQuad( int w, int h )
{
    glBegin(GL_QUADS);
    glVertex2f(0.0, 0.0);
    glVertex2f(w, 0.0);
    glVertex2f(w, h);
    glVertex2f(0.0, h);
    glEnd();
}


/*
 * Checks framebuffer status.
 * Copied directly out of the spec, modified to deliver a return value.
 */
bool checkFramebufferStatus( void )
{
    GLenum          status;

    status = (GLenum)glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
        return( TRUE );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, incomplete "
                              "attachment", me );
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
        LogPrint( LOG_NOTICE, "<%d> Unsupported framebuffer format", me );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, missing "
                              "attachment", me );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, attached images "
                              "must have same dimensions", me );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, attached images "
                              "must have same format", me );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, missing draw "
                              "buffer", me );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
        LogPrint( LOG_NOTICE, "<%d> Framebuffer incomplete, missing read "
                              "buffer", me );
        break;
    }

    return( FALSE );
}

/*
 * swaps the role of the two y-textures (read-only and write-only)
 */
void swap( void )
{
    writeTex = 1 - writeTex;
    readTex  = 1 - writeTex;
}

/*
 * Sets up a floating point texture with NEAREST filtering.
 * (mipmaps etc. are unsupported for floating point textures)
 */
void setupTexture(const GLuint texID, int x, int y)
{
    /* make active and bind */
    glBindTexture(texTarget,texID);

    /* turn off filtering and wrap modes */
    glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP);

    /* define texture with floating point format */
    glTexImage2D( texTarget, 0, texInternalFormat, x, y, 0, texFormat, 
                  GL_FLOAT, 0 );

    /* check if that worked */
    if (glGetError() != GL_NO_ERROR) {
        LogPrint( LOG_CRIT, "<%d> glTexImage2D():\t\t\t [FAIL]", me );
        exit(1);
    } else {
        LogPrint( LOG_NOTICE, "<%d> glTexImage2D():\t\t\t [PASS]", me );
    }
}

/*
 * creates textures and populates the input texture
 */
void createTextures(int x, int y)
{
    /*
     * pingpong needs two textures, alternatingly read-only and
     * write-only, input is just read-only 
     */
    glGenTextures(2, pingpongTexID);
    glGenTextures(1, &inputTexID);

    /* set up textures */
    setupTexture(pingpongTexID[readTex], x, y);
    setupTexture(pingpongTexID[writeTex], x, y);
    setupTexture(inputTexID, x, y);

    /* attach pingpong textures to FBO */
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                              attachmentpoints[writeTex], texTarget,
                              pingpongTexID[writeTex], 0);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                              attachmentpoints[readTex], texTarget,
                              pingpongTexID[readTex], 0);

    /* check if that worked */
    if (!checkFramebufferStatus()) {
        LogPrint( LOG_CRIT, "<%d> glFramebufferTexture2DEXT():\t [FAIL]", me );
        exit(1);
    } else {
        LogPrint( LOG_NOTICE, "<%d> glFramebufferTexture2DEXT():\t [PASS]", 
                  me );
    }
}

void loadFrame(uint32 *data, int x, int y)
{
    /* transfer data vector to input texture */
    glBindTexture(texTarget, inputTexID);
    glTexSubImage2D(texTarget, 0, 0, 0, x, y, texFormat, 
                    GL_UNSIGNED_INT_8_8_8_8, data);

    /* check if something went completely wrong */
    checkGLErrors("createTextures()");
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

