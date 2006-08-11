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
#include <Cg/cgGL.h>
#include "ipc_queue.h"
#include "ipc_logging.h"
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
void setupTexture(const GLuint texID, GLenum format, GLenum inFormat, int x, 
                  int y);
void createTextures(int x, int y);
void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y, 
               GLuint texID);

extern int              idShm, idSem, idFrame;
extern char            *shmBlock;
extern unsigned char   *frameBlock;
static sharedMem_t     *sharedMem;
static cardInfo_t      *cardInfo;
static int              me;
static bool             initialized = FALSE;
static int              width, height;
static int              mode;
static unsigned int    *buffer = NULL;

GLuint                  glutWindowHandle;
GLuint                  fb;

/* ping pong management vars */
int                     writeTex = 0;
int                     readTex = 1;
GLenum                  attachmentpoints[] = { GL_COLOR_ATTACHMENT0_EXT, 
                                               GL_COLOR_ATTACHMENT1_EXT };
 
GLenum                  texTarget         = GL_TEXTURE_RECTANGLE_ARB;
GLenum                  texIntFormatInout = GL_FLOAT_R16_NV;
GLenum                  texIntFormat      = GL_FLOAT_RGB32_NV;
GLenum                  texFormatInout    = GL_LUMINANCE;
GLenum                  texFormat         = GL_RGB;

GLuint                  pingpongTexID[2];
GLuint                  frameTexID, prevFrameTexID;
GLuint                  yTexID, uTexID, vTexID;

CGcontext               cgContext;
CGprofile               fragmentProfile = CG_PROFILE_FP30;
CGprogram               frProgYUV420pIn;
                                               
char *frSrcYUV420pIn = "yuv420p-input.cg";


void frameToUnsignedInt( unsigned char *frame, unsigned int *buffer, int cols,
                         int rows );
void unsignedIntToFrame( unsigned int *buffer, unsigned char *frame, int cols,
                         int rows );

void do_child( int childNum )
{
    int             argc = 3;
    char           *argv[] = { "client", "-display", NULL };
    char           *msg;
    QueueMsg_t      type;
    int             len;
    ChildMsg_t     *message;
    bool            done;
    FrameDoneMsg_t  frameMsg;
    unsigned char  *frameInBase;
    unsigned char  *frameOutBase;
    unsigned char  *frameIn;
    unsigned char  *frameInPrev;
    unsigned char  *frameOut;
    int             frameSize;
    int             frameNum;
    int             indexIn;
    int             indexInPrev;

    shmBlock = NULL;
    frameBlock = NULL;
    atexit( SoftExitChild );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    sharedMem = (sharedMem_t *)shmBlock;
    cardInfo = &sharedMem->cardInfo[childNum];
    argv[2] = cardInfo->display;
    me = childNum;
    frameMsg.childNum = childNum;

    frameBlock = (unsigned char *)shmat( idFrame, NULL, 0 );

    frameInBase  = &frameBlock[sharedMem->offsets.frameIn];
    frameOutBase = &frameBlock[sharedMem->offsets.frameOut];
    frameSize    = sharedMem->frameSize;

    LogPrint( LOG_NOTICE, "<%d> inOffset = %d, outOffset = %d", childNum,
              sharedMem->offsets.frameIn, sharedMem->offsets.frameOut );

    glutInit( &argc, argv );
    glutWindowHandle = glutCreateWindow( "gputrans" );
    initialized = TRUE;
    glewInit();
    cgContext = cgCreateContext();
    cgGLSetOptimalOptions(fragmentProfile);

    /* must be done after OpenGL initialization */
    setupCardInfo( childNum );

    queueSendBinary( Q_MSG_READY, &childNum, sizeof(childNum) );

    done = FALSE;
    while( !done ) {
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
            done = TRUE;
            break;
        case CHILD_RENDER_MODE:
            mode   = message->payload.renderMode.mode;
            width  = message->payload.renderMode.cols;
            height = message->payload.renderMode.rows;
            LogPrint( LOG_NOTICE, "<%d> Enter rendering mode %d (%dx%d - %d)", 
                                  childNum, mode, width, height, 
                                  sharedMem->frameSize );

            frProgYUV420pIn = 
                cgCreateProgramFromFile( cgContext, CG_SOURCE, frSrcYUV420pIn, 
                                         fragmentProfile, "main", NULL );
            cgGLEnableProfile(fragmentProfile);

            LogPrint( LOG_NOTICE, "<%d> YUV420pIn: %s", 
                                  cgGetLastListing( cgContext ) );
    
            sleep(1);
            queueSendBinary( Q_MSG_RENDER_READY, &childNum, sizeof(childNum) );
            break;
        case CHILD_RENDER_FRAME:
            if( !buffer ) {
                LogPrint( LOG_CRIT, "<%d> Received frame before mode!!", 
                                    childNum );
                break;
            }

            frameNum    = message->payload.renderFrame.frameNum;
            indexIn     = message->payload.renderFrame.indexIn;
            indexInPrev = message->payload.renderFrame.indexInPrev;

            frameMsg.renderFrame.frameNum = frameNum;
            frameMsg.renderFrame.indexIn = indexIn;
            frameMsg.renderFrame.indexInPrev = indexInPrev;

            frameIn     = frameInBase  + (frameSize * indexIn);
            frameInPrev = frameInBase  + (frameSize * indexInPrev);
            frameOut    = frameOutBase + (frameSize * indexIn);

            LogPrint( LOG_NOTICE, "<%d> Received Frame #%d (index %d, prev %d)",
                                  childNum, frameNum, indexIn, indexInPrev );

#if 0
            /* Repack into 32bit RGBA structures */
            frameToUnsignedInt( frameIn, buffer, width, height );

            /* Pretend to have done some work */

            /* Repack into the YUV444P of the output frame */
            unsignedIntToFrame( buffer, frameOut, width, height );
#endif

            queueSendBinary( Q_MSG_FRAME_DONE, &frameMsg, sizeof( frameMsg ) );
            break;
        default:
            break;
        }
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
        shmBlock = NULL;
    }

    if( frameBlock ) {
        shmdt( frameBlock );
        frameBlock = NULL;
    }

    if( buffer ) {
        free( buffer );
        buffer = NULL;
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
    glPolygonMode(GL_FRONT,GL_FILL);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2f(0.0, 0.0);
    glTexCoord2f(w, 0.0);
    glVertex2f(w, 0.0);
    glVertex2f(w, h);
    glTexCoord2f(w, h);
    glVertex2f(0.0, h);
    glTexCoord2f(0.0, h);
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
void setupTexture(const GLuint texID, GLenum format, GLenum inFormat, int x, 
                  int y)
{
    /* make active and bind */
    glBindTexture(texTarget,texID);

    /* turn off filtering and wrap modes */
    glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* define texture with floating point format */
    glTexImage2D( texTarget, 0, format, x, y, 0, inFormat, GL_FLOAT, 0 );

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
    glGenTextures(1, &frameTexID);
    glGenTextures(1, &prevFrameTexID);
    glGenTextures(1, &yTexID);
    glGenTextures(1, &uTexID);
    glGenTextures(1, &vTexID);

    /* set up textures */
    setupTexture(pingpongTexID[readTex],  texIntFormat, texFormat, x, y);
    setupTexture(pingpongTexID[writeTex], texIntFormat, texFormat, x, y);
    setupTexture(frameTexID,              texIntFormat, texFormat, x, y);
    setupTexture(prevFrameTexID,          texIntFormat, texFormat, x, y);

    /* The Y, U & V input textures */
    setupTexture(yTexID, texIntFormatInout, texFormatInout, x,     y);
    setupTexture(uTexID, texIntFormatInout, texFormatInout, x / 2, y / 2);
    setupTexture(vTexID, texIntFormatInout, texFormatInout, x / 2, y / 2);

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

void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y, 
               GLuint texID)
{
    CGparameter yTexParam, uTexParam, vTexParam;

    /* transfer data vector to input texture */
    glBindTexture(texTarget, yTexID);
    glTexSubImage2D(texTarget, 0, 0, 0, x, y, texFormatInout, GL_UNSIGNED_BYTE,
                    yData);

    glBindTexture(texTarget, uTexID);
    glTexSubImage2D(texTarget, 0, 0, 0, x / 2, y / 2, texFormatInout, 
                    GL_UNSIGNED_BYTE, uData);

    glBindTexture(texTarget, uTexID);
    glTexSubImage2D(texTarget, 0, 0, 0, x / 2, y / 2, texFormatInout, 
                    GL_UNSIGNED_BYTE, vData);

    /* check if something went completely wrong */
    checkGLErrors("createTextures()");

    if( !cgIsProgramCompiled( frProgYUV420pIn ) ) {
        cgCompileProgram( frProgYUV420pIn );
    }

    yTexParam = cgGetNamedParameter( frProgYUV420pIn, "Ytex" );
    uTexParam = cgGetNamedParameter( frProgYUV420pIn, "Utex" );
    vTexParam = cgGetNamedParameter( frProgYUV420pIn, "Vtex" );

    cgGLSetTextureParameter( yTexParam, yTexID );
    cgGLSetTextureParameter( uTexParam, uTexID );
    cgGLSetTextureParameter( vTexParam, vTexID );

    cgGLEnableTextureParameter( yTexParam );
    cgGLEnableTextureParameter( uTexParam );
    cgGLEnableTextureParameter( vTexParam );

    cgGLBindProgram( frProgYUV420pIn );

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, 
                              texTarget, texID, 0);
    glDrawBuffer( GL_COLOR_ATTACHMENT2_EXT );
    drawQuad( x, y );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

