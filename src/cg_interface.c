/*
 *  This file is part of the gputrans package
 *  Copyright (C) 2006-2007 Gavin Hurlbut
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
* Copyright 2006-2007 Gavin Hurlbut
* All rights reserved
*
*/

#include "environment.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <time.h>
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

void setupCardInfo( cardInfo_t *cardInfo, int childNum );
void initFBO( int x, int y );
void checkGLErrors( const char *label );
void drawQuad( int wTex, int hTex, int wOut, int hOut );
bool checkFramebufferStatus( int index );
void swap( void );
void setupTexture(const GLuint texID, GLenum format, GLenum inFormat, int x, 
                  int y);
void createTextures(int x, int y);
void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y);
void unloadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y);
void attachPingPongFBOs( void );
void detachFBOs( void );
void handleCgError( CGcontext ctx, CGerror err, void *appdata );
CGprogram loadCgProgram( char *name, char *file, char *entry );
void unloadRaw(uint8 *yData, uint8 *uData, uint8 *vData, 
               uint8 *yDataOut, uint8 *uDataOut, uint8 *vDataOut,
               int x, int y);

static int              me;
GLuint                  glutWindowHandle;
GLuint                  fb;

/* ping pong management vars */
int                     writeTex = 0;
int                     readTex = 1;
GLenum                  attachmentpoints[] = { GL_COLOR_ATTACHMENT0_EXT, 
                                               GL_COLOR_ATTACHMENT1_EXT };
 
GLenum                  texTarget         = GL_TEXTURE_RECTANGLE_NV;
GLenum                  texIntFormatInout = GL_RGB8; /*GL_FLOAT_R16_NV; */
GLenum                  texIntFormat      = GL_RGB16; /*GL_FLOAT_RGB32_NV; GL_RGB16;*/
GLenum                  texFormatInout    = GL_RED; /*GL_LUMINANCE;*/
GLenum                  texFormat         = GL_RGB;

GLuint                  pingpongTexID[2];
GLuint                  frameTexID;
GLuint                  yTexID, uTexID, vTexID;

CGcontext               cgContext;
CGprofile               fragmentProfile = CG_PROFILE_FP30;
CGprofile               vertexProfile   = CG_PROFILE_VP30;
CGprogram               frProgYUV420pIn, frProgY420pOut, frProgU420pOut;
CGprogram               frProgV420pOut;

typedef struct {
    CGprogram  *program;
    char       *name;
    char       *file;
    char       *entry;
} cgProgram_t;

static cgProgram_t cgPrograms[] = {
    { &frProgYUV420pIn, "YUV420pIn", "yuv420p.cg", "yuv_input" },
    { &frProgY420pOut,  "Y420pOut",  "yuv420p.cg", "y_output" },
    { &frProgU420pOut,  "U420pOut",  "yuv420p.cg", "u_output" },
    { &frProgV420pOut,  "V420pOut",  "yuv420p.cg", "v_output" }
};
static int cgProgramCount = NELEMENTS(cgPrograms);

void cg_init( cardInfo_t *cardInfo, int childNum )
{
    int             argc = 3;
    char           *argv[] = { "client", "-display", NULL };

    me = childNum;

    argv[2] = cardInfo->display;

    glutInit( &argc, argv );
    glutWindowHandle = glutCreateWindow( "gputrans" );
    glewInit();
    cgContext = cgCreateContext();
    cgGLSetOptimalOptions(fragmentProfile);
    cgSetErrorHandler( handleCgError, NULL );
    LogPrint( LOG_NOTICE, "<%d> Using Cg version %s", childNum, 
                          cgGetString(CG_VERSION) );
}


void cg_enable( int width, int height ) 
{
    cgProgram_t    *prog;
    int             i;

    glEnable( GL_FRAGMENT_PROGRAM_NV );
    checkGLErrors("glEnable1");
    glEnable( GL_TEXTURE_RECTANGLE_NV );
    checkGLErrors("glEnable2");

    initFBO(width, height);
    createTextures(width, height);

    for( i = 0; i < cgProgramCount; i++ ) {
        prog = &cgPrograms[i];
        *prog->program = loadCgProgram( prog->name, prog->file, 
                                        prog->entry );
    }

    cgGLEnableProfile(fragmentProfile);
}


CGprogram loadCgProgram( char *name, char *file, char *entry )
{
    CGprogram       prog;
    const char     *listing;

    prog = cgCreateProgramFromFile( cgContext, CG_SOURCE, file, 
                                    fragmentProfile, entry, NULL );
    LogPrint( LOG_NOTICE, "<%d> fragment program %s loaded (%ld)", me, name, 
                          (long)prog );
    listing = cgGetLastListing( cgContext );
    if( listing ) {
        LogPrint( LOG_NOTICE, "<%d> listing: %s", me, listing );
    }

    return( prog );
}


void cg_destroy( void )
{
    glutDestroyWindow (glutWindowHandle);
}

void setupCardInfo( cardInfo_t *cardInfo, int childNum )
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
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &val[0]);
        cardInfo->max.DrawBuffers = val[0];
        LogPrint( LOG_NOTICE, "<%d> Max Draw Buffers: %d", childNum, 
                              cardInfo->max.DrawBuffers );
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
        LogPrint( LOG_CRIT, "<%d> OpenGL ERROR %04X: %s  (Label: %s)", me, 
                            errCode, errStr, label );
        exit( 1 );
    }
}

void handleCgError( CGcontext ctx, CGerror err, void *appdata )
{
    const char     *listing;

    LogPrint( LOG_NOTICE, "<%d> Cg error: %s", me, cgGetErrorString(err) );
    listing = cgGetLastListing(ctx);
    if( listing ) {
        LogPrint( LOG_NOTICE, "<%d> last listing: %s", me, listing );
    }
    exit(-1);
}

/*
 * Renders w x h quad in top left corner of the viewport.
 */
void drawQuad( int wTex, int hTex, int wOut, int hOut )
{
    glPolygonMode(GL_FRONT,GL_FILL);
    checkGLErrors("glPolygonMode");

    glBegin(GL_QUADS);

    glTexCoord2f(0.0, 0.0);
    glVertex2f(0.0, 0.0);

    glTexCoord2f(wTex, 0.0);
    glVertex2f(wOut, 0.0);

    glTexCoord2f(wTex, hTex);
    glVertex2f(wOut, hOut);

    glTexCoord2f(0.0, hTex);
    glVertex2f(0.0, hOut);

    glEnd();
    checkGLErrors("glEnd");
}


/*
 * Checks framebuffer status.
 * Copied directly out of the spec, modified to deliver a return value.
 */
bool checkFramebufferStatus( int index )
{
    GLenum          status;

    status = (GLenum)glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
        return( TRUE );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, incomplete "
                              "attachment", me, index );
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Unsupported framebuffer format", me, 
                              index );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, missing "
                              "attachment", me, index );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, attached "
                              "images must have same dimensions", me, index );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, attached "
                              "images must have same format", me, index );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, missing draw "
                              "buffer", me, index );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
        LogPrint( LOG_NOTICE, "<%d> (%d) Framebuffer incomplete, missing read "
                              "buffer", me, index );
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
    checkGLErrors("glBindTexture(setupTexture)");

    /* turn off filtering and wrap modes */
    glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP);

    /* define texture with floating point format */
    glTexImage2D( texTarget, 0, format, x, y, 0, inFormat, GL_UNSIGNED_BYTE, 
                  NULL );
    checkGLErrors("glTexImage2D()");
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
    glGenTextures(1, &yTexID);
    glGenTextures(1, &uTexID);
    glGenTextures(1, &vTexID);

    /* set up textures */
    setupTexture(pingpongTexID[readTex],  texIntFormat, texFormat, x, y);
    setupTexture(pingpongTexID[writeTex], texIntFormat, texFormat, x, y);
    setupTexture(frameTexID,              texIntFormat, texFormat, x, y);

    /* The Y, U & V input textures */
    setupTexture(yTexID, texIntFormatInout, texFormatInout, x,     y);
    setupTexture(uTexID, texIntFormatInout, texFormatInout, x / 2, y / 2);
    setupTexture(vTexID, texIntFormatInout, texFormatInout, x / 2, y / 2);
}

void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y)
{
    CGparameter yTexParam, uTexParam, vTexParam;

    detachFBOs();

    /* transfer data vector to input texture */
    glBindTexture(texTarget, yTexID);
    checkGLErrors("glBindTexture(Y)");
    glTexSubImage2D(texTarget, 0, 0, 0, x, y, texFormatInout, GL_UNSIGNED_BYTE,
                    yData);
    checkGLErrors("glTexSubImage2D(Y)");

    glBindTexture(texTarget, uTexID);
    checkGLErrors("glBindTexture(U)");
    glTexSubImage2D(texTarget, 0, 0, 0, x / 2, y / 2, texFormatInout, 
                    GL_UNSIGNED_BYTE, uData);
    checkGLErrors("glTexSubImage2D(U)");

    glBindTexture(texTarget, vTexID);
    checkGLErrors("glBindTexture(V)");
    glTexSubImage2D(texTarget, 0, 0, 0, x / 2, y / 2, texFormatInout, 
                    GL_UNSIGNED_BYTE, vData);
    checkGLErrors("glTexSubImage2D(V)");

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

    cgGLLoadProgram( frProgYUV420pIn );
    cgGLBindProgram( frProgYUV420pIn );

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, frameTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(in)");
    checkFramebufferStatus(0);
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(in)");
    drawQuad( x, y, x, y );

    detachFBOs();
}


void unloadFrame(uint8 *yData, uint8 *uData, uint8 *vData, int x, int y)
{
    CGparameter frameParam;

    detachFBOs();

    /* Since we want the output frame to be in YUV420P, we need to transfer
     * each plane separately, which means unpacking the pixels.
     */

    /* Y Plane */
    if( !cgIsProgramCompiled( frProgY420pOut ) ) {
        cgCompileProgram( frProgY420pOut );
    }

    glBindTexture(texTarget, frameTexID);
    checkGLErrors("glBindTexture(frameOut)");
    frameParam = cgGetNamedParameter( frProgY420pOut, "frame" );
    cgGLSetTextureParameter( frameParam, frameTexID );
    cgGLEnableTextureParameter( frameParam );
    cgGLLoadProgram( frProgY420pOut );
    cgGLBindProgram( frProgY420pOut );

    glBindTexture(texTarget, yTexID);
    checkGLErrors("glBindTexture(yOut)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, yTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(yOut)");
    checkFramebufferStatus(1);

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(yOut)");
    drawQuad( x, y, x, y );

    checkFramebufferStatus(2);

    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(yOut)");
    glReadPixels( 0, 0, x, y, texFormatInout, GL_UNSIGNED_BYTE, yData );
    checkGLErrors("glReadPixels(yOut)");

    /* U Plane */
    if( !cgIsProgramCompiled( frProgU420pOut ) ) {
        cgCompileProgram( frProgU420pOut );
    }

    frameParam = cgGetNamedParameter( frProgU420pOut, "frame" );
    cgGLSetTextureParameter( frameParam, frameTexID );
    cgGLEnableTextureParameter( frameParam );
    cgGLLoadProgram( frProgU420pOut );
    cgGLBindProgram( frProgU420pOut );
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, uTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(uOut)");
    checkFramebufferStatus(3);

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(uOut)");
    drawQuad( x, y, x / 2, y / 2 );
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(uOut)");
    glReadPixels( 0, 0, x / 2, y / 2, texFormatInout, GL_UNSIGNED_BYTE, uData );
    checkGLErrors("glReadPixels(uOut)");

    /* V Plane */
    if( !cgIsProgramCompiled( frProgV420pOut ) ) {
        cgCompileProgram( frProgV420pOut );
    }

    frameParam = cgGetNamedParameter( frProgV420pOut, "frame" );
    cgGLSetTextureParameter( frameParam, frameTexID );
    cgGLEnableTextureParameter( frameParam );
    cgGLLoadProgram( frProgV420pOut );
    cgGLBindProgram( frProgV420pOut );
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, vTexID, 0);
    checkGLErrors("glFrameBufferTexture2DEXT(vOut)");
    checkFramebufferStatus(4);

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(vOut)");
    drawQuad( x, y, x / 2, y / 2 );
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(vOut)");
    glReadPixels( 0, 0, x / 2, y / 2, texFormatInout, GL_UNSIGNED_BYTE, vData );
    checkGLErrors("glReadPixels(vOut)");

    detachFBOs();
}

void detachFBOs( void )
{
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, 0, 0);
    checkGLErrors("Detach 0");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, 
                              texTarget, 0, 0);
    checkGLErrors("Detach 1");
}

void attachPingPongFBOs( void )
{
    /* attach pingpong textures to FBO */
    glBindTexture(texTarget, pingpongTexID[writeTex]);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                              attachmentpoints[writeTex], texTarget,
                              pingpongTexID[writeTex], 0);
    /* check if that worked */
    if (!checkFramebufferStatus(5)) {
        LogPrint( LOG_CRIT, "<%d> glFramebufferTexture2DEXT(0): [FAIL]", me );
        exit(1);
    }

    glBindTexture(texTarget, pingpongTexID[readTex]);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
                              attachmentpoints[readTex], texTarget,
                              pingpongTexID[readTex], 0);
    /* check if that worked */
    if (!checkFramebufferStatus(6)) {
        LogPrint( LOG_CRIT, "<%d> glFramebufferTexture2DEXT(1): [FAIL]", me );
        exit(1);
    }
}

void unloadRaw(uint8 *yData, uint8 *uData, uint8 *vData, 
               uint8 *yDataOut, uint8 *uDataOut, uint8 *vDataOut,
               int x, int y)
{
    float          *data;
    FILE           *fp;
    int             i, j;
    int             size;
    int             index, index2, index3;

    size = x * y;
    data = (float *)malloc(sizeof(float) * size * 3);

    detachFBOs();

    glBindTexture(texTarget, frameTexID);
    checkGLErrors("glBindTexture(frameTexID)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, frameTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(frameTexID)");
    checkFramebufferStatus(7);

    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(frameTexID)");
    glReadPixels( 0, 0, x, y, GL_RGB, GL_FLOAT, data );
    checkGLErrors("glReadPixels(frameTexID)");

    detachFBOs();

    fp = fopen( "crap.out", "wt" );

    for( i = 0; i < y; i++ ) {
        for( j = 0; j < x; j++ ) {
            index = j + (i*x);
            index2 = (j / 2) + ((i /2) * (x / 2));
            index3 = index * 3;

            fprintf(fp, "Pixel (%d,%d):  I:%d:%d:%d  F:%f:%f:%f  O:%d:%d:%d\n",
                    j, i,
                    yData[index], uData[index2], vData[index2],
                    data[index3], data[index3+1], data[index3+2], 
                    yDataOut[index], uDataOut[index2], vDataOut[index2] );
        }
    }

    fclose(fp);
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

