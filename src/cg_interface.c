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
void initFBO( void );
void checkGLErrors( const char *label );
void drawQuad( int wTex, int hTex, int wOut, int hOut );
void drawQuadMulti( int wTex, int hTex, int wOut, int hOut );
bool checkFramebufferStatus( int index );
void swap( void );
void setupTexture(const GLuint texID, GLenum format, GLenum inFormat, int x, 
                  int y);
void createTextures( void );
void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData);
void unloadFrame(uint8 *yData, uint8 *uData, uint8 *vData);
void unloadRaw(uint8 *yData, uint8 *uData, uint8 *vData, 
               uint8 *yDataOut, uint8 *uDataOut, uint8 *vDataOut);
void attachPingPongFBOs( void );
void detachFBOs( void );
void handleCgError( CGcontext ctx, CGerror err, void *appdata );
CGprogram loadCgProgram( char *name, char *file, char *entry, 
                         CGprofile profile );

void copy_frame( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight, 
                 int destWidth, int destHeight );
void contrast_frame( void );
void decimate_frame( GLuint destTex, GLuint srcTex, int srcWidth, 
                     int srcHeight );
void mark_low_contrast_blocks( void );
void mb_search_44( void );
void mb_search_22( void );
void mb_search_11( void );
void mb_search_00( void );
void move_frame( void );
void average_frame( void );
void correct_frame2( void );
void denoise_frame_pass2( void );
void sharpen_frame( void );

void diff_frame( GLuint destTex, GLuint srcATex, GLuint srcBTex, int srcWidth,
                 int srcHeight );
void thresh_diff( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight,
                  float threshold );
void vector_low_contrast( GLuint destTex, GLuint srcTex, int srcWidth, 
                          int srcHeight, float threshold );
void decimate_add( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight );
void SAD( GLuint vectorTex, GLuint refTex, GLuint avgTex, int srcWidth,
          int srcHeight, int xoffset, int yoffset, int factor );
void SAD_pass2( GLuint vectorTex, GLuint refTex, GLuint avgTex, 
                GLuint vectorInTex, int srcWidth, int srcHeight, int xoffset, 
                int yoffset, int factor );
void SAD_halfpel( GLuint vectorTex, GLuint refTex, GLuint avgTex, 
                  GLuint vectorInTex, int srcWidth, int srcHeight, int xoffset, 
                  int yoffset, int factor );
void vector_update( void );
void vector_scale( GLuint destTex, GLuint srcTex, int x, int y, int scale );
void vector_badcheck( void );
void vector_range( void );
void scale_buffer( GLuint destTex, GLuint srcTex, int x, int y, int scale );
void dump_data( GLuint tex, int x, int y );
void dump_data3d( GLuint tex, int x, int y, char *tag, char *filename );
void dump_ppm( GLuint tex, int x, int y, int yOff, char *filepatt );
void dump_pgm( GLuint tex, int x, int y, int yOff, char *filepatt );

void denoiseFrame( void );

extern void save_ppm( const unsigned char *rgb, size_t cols, size_t rows, 
                      int pixsize, const char *file );

static int              me;
static bool             new_scene;
static int              bad_vector;
static int              width;
static int              height;
static int              vecWidth;
static int              vecHeight;
static int              padHeight;
static int              sub2Height;
static int              sub2Width;
static int              sub4Height;
static int              sub4Width;
static int              frameNum;

float                   luma_contrast = 1.0;
float                   chroma_contrast = 1.0;
int                     scene_thresh = 50;
float                   contrast_thresh = 0.019607843; /* 5/255 */
int                     block_contrast_thresh = 0.125; /* 8/64 */
int                     radius = 8;
float                   block_bad_thresh = 4.015686275; /* 1024/255 */
int                     delay = 3;
float                   correct_thresh = 0.019607843; /* 5/255 */
float                   pp_thresh = 0.015686275; /* 4/255 */
float                   sharpen = 1.25;

GLuint                  glutWindowHandle;
GLuint                  fb;

/* ping pong management vars */
int                     writeTex = 0;
int                     readTex = 1;
GLenum                  attachmentpoints[] = { GL_COLOR_ATTACHMENT0_EXT, 
                                               GL_COLOR_ATTACHMENT1_EXT };
 
GLenum                  texTarget         = GL_TEXTURE_RECTANGLE_NV;
GLenum                  texIntFormatInout = GL_RGB8;
GLenum                  texIntFormat      = GL_RGB16; /* GL_FLOAT_RGB32_NV; GL_RGB16;*/
GLenum                  texIntFormatVect  = GL_FLOAT_RGBA16_NV;
GLenum                  texFormatInout    = GL_RED;
GLenum                  texFormat         = GL_RGB;
GLenum                  texFormatVect     = GL_RGBA;

GLuint                  pingpongTexID[2];
GLuint                  frameTexID;
GLuint                  yTexID, uTexID, vTexID;
GLuint                  refTexID, avgTexID, avg2TexID, tmpTexID, tmp2TexID;
GLuint                  sub2refTexID, sub2avgTexID, sub4refTexID, sub4avgTexID;
GLuint                  diffTexID, vectorTexID, vector2TexID;
GLuint                  vector3TexID, badTexID, bad2TexID;

CGcontext               cgContext;
CGprofile               fragmentProfile = CG_PROFILE_FP30;
CGprofile               vertexProfile   = CG_PROFILE_VP30;

CGprogram               frProgYUV420pIn, frProgY420pOut, frProgU420pOut;
CGprogram               frProgV420pOut;
CGprogram               frProgContrastFrame, frProgDiffFrame, frProgThreshDiff;
CGprogram               frProgDecimateAdd, frProgMoveFrame, frProgAverageFrame;
CGprogram               frProgCorrectFrame2, frProgDenoiseFramePass2;
CGprogram               frProgSharpenFrame, frProgSAD, frProgSADHalfpel;
CGprogram               frProgCopy, frProgVectorUpdate;
CGprogram               frProgVectorBadcheck, frProgVectorRange;
CGprogram               frProgVectorLowContrast, frProgSADPass2;
CGprogram               frProgScale, frProgVectorScale;

#define FP30 CG_PROFILE_FP30
#define VP30 CG_PROFILE_VP30

typedef struct {
    CGprogram  *program;
    char       *name;
    char       *file;
    char       *entry;
    CGprofile   profile;
} cgProgram_t;

static cgProgram_t cgPrograms[] = {
    { &frProgYUV420pIn, "YUV420pIn", "yuv420p.cg", "yuv_input", FP30 },
    { &frProgY420pOut,  "Y420pOut",  "yuv420p.cg", "y_output",  FP30 },
    { &frProgU420pOut,  "U420pOut",  "yuv420p.cg", "u_output",  FP30 },
    { &frProgV420pOut,  "V420pOut",  "yuv420p.cg", "v_output",  FP30 },
    { &frProgContrastFrame, "ContrastFrame", "yuvdenoise.cg", "contrast_frame", FP30 },
    { &frProgDiffFrame, "DiffFrame", "yuvdenoise.cg", "diff_frame", FP30 },
    { &frProgThreshDiff, "ThreshDiff", "yuvdenoise.cg", "thresh_diff", FP30 },
    { &frProgDecimateAdd, "DecimateAdd", "yuvdenoise.cg", "decimate_add", FP30 },
    { &frProgMoveFrame, "MoveFrame", "yuvdenoise.cg", "move_frame", FP30 },
    { &frProgAverageFrame, "AverageFrame", "yuvdenoise.cg", "average_frame", FP30 },
    { &frProgCorrectFrame2, "CorrectFrame2", "yuvdenoise.cg", "correct_frame2", FP30 },
    { &frProgDenoiseFramePass2, "DenoiseFramePass2", "yuvdenoise.cg", "denoise_frame_pass2", FP30 },
    { &frProgSharpenFrame, "SharpenFrame", "yuvdenoise.cg", "sharpen_frame", FP30 },
    { &frProgSAD, "SAD", "yuvdenoise.cg", "SAD", FP30 },
    { &frProgSADHalfpel, "SADHalfpel", "yuvdenoise.cg", "SAD_halfpel", FP30 },
    { &frProgCopy, "Copy", "yuvdenoise.cg", "copy", FP30 },
    { &frProgVectorUpdate, "VectorUpdate", "yuvdenoise.cg", "vector_update", FP30 },
    { &frProgVectorBadcheck, "VectorBadcheck", "yuvdenoise.cg", "vector_badcheck", FP30 },
    { &frProgVectorRange, "VectorRange", "yuvdenoise.cg", "vector_range", FP30 },
    { &frProgVectorLowContrast, "VectorLowContrast", "yuvdenoise.cg", "vector_low_contrast", FP30 },
    { &frProgSADPass2, "SADPass2", "yuvdenoise.cg", "SAD_pass2", FP30 },
    { &frProgScale, "Scale", "yuvdenoise.cg", "scale", FP30 },
    { &frProgVectorScale, "VectorScale", "yuvdenoise.cg", "vector_scale", FP30 }
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
    cgGLSetOptimalOptions(vertexProfile);
    cgSetErrorHandler( handleCgError, NULL );
    LogPrint( LOG_NOTICE, "<%d> Using Cg version %s", childNum, 
                          cgGetString(CG_VERSION) );

    new_scene = TRUE;
    frameNum = 0;
}


void cg_enable( int x, int y ) 
{
    cgProgram_t    *prog;
    int             i;

    width = x;
    height = y;
    padHeight = y + 64;

    /* the vectorWidths are based on decimation, and at each level, an odd
     * pixel will still decimate to a pixel (half-pixel rounded to full)
     */
    sub2Width  = (width + 1) /2;
    sub2Height = (padHeight + 1) / 2;
    sub4Width  = (sub2Width + 1) /2;
    sub4Height = (sub2Height + 1) / 2;
    vecWidth   = (sub4Width + 1) /2;
    vecHeight  = (sub4Height + 1) / 2;

    glEnable( GL_FRAGMENT_PROGRAM_NV );
    checkGLErrors("glEnable1");
    glEnable( GL_TEXTURE_RECTANGLE_NV );
    checkGLErrors("glEnable2");

    initFBO();
    createTextures();

    for( i = 0; i < cgProgramCount; i++ ) {
        prog = &cgPrograms[i];
        *prog->program = loadCgProgram( prog->name, prog->file, 
                                        prog->entry, prog->profile );
    }

    cgGLEnableProfile(fragmentProfile);
}


CGprogram loadCgProgram( char *name, char *file, char *entry, 
                         CGprofile profile )
{
    CGprogram       prog;
    const char     *listing;

    prog = cgCreateProgramFromFile( cgContext, CG_SOURCE, file, 
                                    profile, entry, NULL );
    LogPrint( LOG_NOTICE, "<%d> %s-%s (%ld) loaded", me, 
                          (profile == FP30 ? "F" : "V" ), name, 
                          (long)prog );
    listing = cgGetLastListing( cgContext );
    if( listing ) {
        LogPrint( LOG_NOTICE, "<%d> %s-%s listing: %s", me, 
                              (profile == FP30 ? "F" : "V"), name, listing );
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

void initFBO( void )
{
    /* Create the framebuffer object for off-screen rendering */
    glGenFramebuffersEXT(1, &fb);

    /* Redirect the output to said buffer rather than the screen */
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, width, 0, padHeight );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glViewport(0, 0, width, padHeight );
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
    exit(1);
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

void drawQuadMulti( int wTex, int hTex, int wOut, int hOut )
{
    glBegin(GL_QUADS);
    glMultiTexCoord2f(GL_TEXTURE0, -0.5, -0.5);
    glMultiTexCoord2f(GL_TEXTURE1, 0.5, -0.5);
    glMultiTexCoord2f(GL_TEXTURE2, -0.5, 0.5);
    glMultiTexCoord2f(GL_TEXTURE3, 0.5, 0.5);
    glVertex2f(0.0, 0.0);

    glMultiTexCoord2f(GL_TEXTURE0, wTex - 0.5, -0.5);
    glMultiTexCoord2f(GL_TEXTURE1, wTex + 0.5, -0.5);
    glMultiTexCoord2f(GL_TEXTURE2, wTex - 0.5, 0.5);
    glMultiTexCoord2f(GL_TEXTURE3, wTex + 0.5, 0.5);
    glVertex2f(wOut, 0.0);

    glMultiTexCoord2f(GL_TEXTURE0, wTex - 0.5, hTex - 0.5);
    glMultiTexCoord2f(GL_TEXTURE1, wTex + 0.5, hTex - 0.5);
    glMultiTexCoord2f(GL_TEXTURE2, wTex - 0.5, hTex + 0.5);
    glMultiTexCoord2f(GL_TEXTURE3, wTex + 0.5, hTex + 0.5);
    glVertex2f(wOut, hOut);

    glMultiTexCoord2f(GL_TEXTURE0, -0.5, hTex - 0.5);
    glMultiTexCoord2f(GL_TEXTURE1, 0.5,  hTex - 0.5);
    glMultiTexCoord2f(GL_TEXTURE2, -0.5, hTex + 0.5);
    glMultiTexCoord2f(GL_TEXTURE3, 0.5,  hTex + 0.5);
    glVertex2f(0.0, hOut);
    glEnd();
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
void createTextures( void )
{
    /*
     * pingpong needs two textures, alternatingly read-only and
     * write-only, input is just read-only 
     */
    glGenTextures(1, &frameTexID);
    glGenTextures(1, &yTexID);
    glGenTextures(1, &uTexID);
    glGenTextures(1, &vTexID);

    glGenTextures(1, &refTexID);
    glGenTextures(1, &avgTexID);
    glGenTextures(1, &avg2TexID);
    glGenTextures(1, &tmpTexID);
    glGenTextures(1, &tmp2TexID);
    glGenTextures(1, &sub2refTexID);
    glGenTextures(1, &sub2avgTexID);
    glGenTextures(1, &sub4refTexID);
    glGenTextures(1, &sub4avgTexID);
    glGenTextures(1, &diffTexID);
    glGenTextures(1, &vectorTexID);
    glGenTextures(1, &vector2TexID);
    glGenTextures(1, &vector3TexID);
    glGenTextures(1, &badTexID);
    glGenTextures(1, &bad2TexID);

    /* set up textures */
    setupTexture(frameTexID,   texIntFormat, texFormat, width, padHeight);

    setupTexture(refTexID,     texIntFormat, texFormat, width, padHeight);
    setupTexture(avgTexID,     texIntFormat, texFormat, width, padHeight);
    setupTexture(avg2TexID,    texIntFormat, texFormat, width, padHeight);
    setupTexture(tmpTexID,     texIntFormat, texFormat, width, padHeight);
    setupTexture(tmp2TexID,    texIntFormat, texFormat, width, padHeight);
    setupTexture(diffTexID,    texIntFormat, texFormat, width, padHeight);

    setupTexture(sub2refTexID, texIntFormat, texFormat, sub2Width, sub2Height);
    setupTexture(sub2avgTexID, texIntFormat, texFormat, sub2Width, sub2Height);

    setupTexture(sub4refTexID, texIntFormat, texFormat, sub4Width, sub4Height);
    setupTexture(sub4avgTexID, texIntFormat, texFormat, sub4Width, sub4Height);

    setupTexture(vectorTexID,  texIntFormatVect, texFormatVect, vecWidth, vecHeight);
    setupTexture(vector2TexID, texIntFormatVect, texFormatVect, vecWidth, vecHeight);
    setupTexture(vector3TexID, texIntFormatVect, texFormatVect, vecWidth, vecHeight);
    
    setupTexture(badTexID, texIntFormat, texFormatInout, vecWidth, vecHeight);
    setupTexture(bad2TexID, texIntFormat, texFormatInout, vecWidth, vecHeight);

    /* The Y, U & V input textures */
    setupTexture(yTexID, texIntFormatInout, texFormatInout, width, padHeight);
    setupTexture(uTexID, texIntFormatInout, texFormatInout, width / 2, 
                 padHeight / 2);
    setupTexture(vTexID, texIntFormatInout, texFormatInout, width / 2, 
                 padHeight / 2);
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

void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData)
{
    CGparameter yTexParam, uTexParam, vTexParam;

    detachFBOs();

    /* transfer data vector to input texture */
    glBindTexture(texTarget, yTexID);
    checkGLErrors("glBindTexture(Y)");
    glTexSubImage2D(texTarget, 0, 0, 32, width, height, texFormatInout, 
                    GL_UNSIGNED_BYTE, yData);
    checkGLErrors("glTexSubImage2D(Y)");

    glBindTexture(texTarget, uTexID);
    checkGLErrors("glBindTexture(U)");
    glTexSubImage2D(texTarget, 0, 0, 16, width / 2, height / 2, texFormatInout, 
                    GL_UNSIGNED_BYTE, uData);
    checkGLErrors("glTexSubImage2D(U)");

    glBindTexture(texTarget, vTexID);
    checkGLErrors("glBindTexture(V)");
    glTexSubImage2D(texTarget, 0, 0, 16, width / 2, height / 2, texFormatInout, 
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
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(in)");
    checkFramebufferStatus(0);
    drawQuad( width, padHeight, width, padHeight );

    detachFBOs();
}


void unloadFrame(uint8 *yData, uint8 *uData, uint8 *vData)
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

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(yOut)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );


    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(yOut)");
    checkFramebufferStatus(2);
    glReadPixels( 0, 32, width, height, texFormatInout, GL_UNSIGNED_BYTE, 
                  yData );
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

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(uOut)");
    checkFramebufferStatus(3);
    drawQuad( width, padHeight, width / 2, padHeight / 2 );
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(uOut)");
    glReadPixels( 0, 16, width / 2, height / 2, texFormatInout, GL_UNSIGNED_BYTE,
                  uData );
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

    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(vOut)");
    checkFramebufferStatus(4);
    drawQuad( width, padHeight, width / 2, padHeight / 2 );
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(vOut)");
    glReadPixels( 0, 16, width / 2, height / 2, texFormatInout, GL_UNSIGNED_BYTE,
                  vData );
    checkGLErrors("glReadPixels(vOut)");

    detachFBOs();

    /* Disable the read buffer so it won't cause issues elsewhere */
    glReadBuffer( GL_NONE );
}

void unloadRaw(uint8 *yData, uint8 *uData, uint8 *vData, 
               uint8 *yDataOut, uint8 *uDataOut, uint8 *vDataOut)
{
    float          *data;
    FILE           *fp;
    int             i, j;
    int             size;
    int             index, index2, index3;

    size = width * height;
    data = (float *)malloc(sizeof(float) * size * 3);

    detachFBOs();

    glBindTexture(texTarget, frameTexID);
    checkGLErrors("glBindTexture(frameTexID)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, frameTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(frameTexID)");

    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(frameTexID)");
    checkFramebufferStatus(7);
    glReadPixels( 0, 0, width, height, GL_RGB, GL_FLOAT, data );
    checkGLErrors("glReadPixels(frameTexID)");

    detachFBOs();

    fp = fopen( "crap.out", "wt" );

    for( i = 0; i < height; i++ ) {
        for( j = 0; j < width; j++ ) {
            index = j + (i*width);
            index2 = (j / 2) + ((i /2) * (width / 2));
            index3 = index * 3;

            fprintf(fp, "Pixel (%d,%d):  I:%d:%d:%d  F:%f:%f:%f  O:%d:%d:%d\n",
                    j, i,
                    yData[index], uData[index2], vData[index2],
                    data[index3], data[index3+1], data[index3+2], 
                    yDataOut[index], uDataOut[index2], vDataOut[index2] );
        }
    }

    fclose(fp);
    free(data);
}


/*
 * Denoise stuff
 */

void copy_frame( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight, 
                 int destWidth, int destHeight )
{
    CGparameter frameParam;

    if( srcTex == destTex ) {
        LogPrint( LOG_CRIT, "Copy %d to itself", srcTex );
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgCopy ) ) {
        cgCompileProgram( frProgCopy );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgCopy, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );

    cgGLLoadProgram( frProgCopy );
    cgGLBindProgram( frProgCopy );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, destWidth, destHeight );

    checkFramebufferStatus(2);
}

void contrast_frame( void )
{
    CGparameter frameParam;
    CGparameter contrastParam;

    if( luma_contrast == 1.0 && chroma_contrast == 1.0 ) {
        return;
    }

    detachFBOs();

    /* Takes input from ref, sends output to ref */
    pingpongTexID[readTex] = refTexID;
    pingpongTexID[writeTex] = tmpTexID;
    glDrawBuffer( attachmentpoints[writeTex] );
    checkGLErrors("glDrawBuffer(write)");
    attachPingPongFBOs();

    if( !cgIsProgramCompiled( frProgContrastFrame ) ) {
        cgCompileProgram( frProgContrastFrame );
    }

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgContrastFrame, "frame" );
    cgGLSetTextureParameter( frameParam, pingpongTexID[readTex] );
    cgGLEnableTextureParameter( frameParam );

    contrastParam = cgGetNamedParameter( frProgContrastFrame, "contrast" );
    cgGLSetParameter2f( contrastParam, luma_contrast, chroma_contrast );

    cgGLLoadProgram( frProgContrastFrame );
    cgGLBindProgram( frProgContrastFrame );

    /* Attach the output */
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    tmpTexID = pingpongTexID[readTex];
    refTexID = pingpongTexID[writeTex];

    detachFBOs();
}

void decimate_frame( GLuint destTex, GLuint srcTex, int srcWidth, 
                     int srcHeight )
{
    copy_frame( destTex, srcTex, srcWidth, srcHeight, (srcWidth + 1) / 2,
                (srcHeight + 1) / 2 );
}

void diff_frame( GLuint destTex, GLuint srcATex, GLuint srcBTex, int srcWidth,
                 int srcHeight )
{
    CGparameter frameAParam;
    CGparameter frameBParam;

    if( srcATex == destTex || srcBTex == destTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgDiffFrame ) ) {
        cgCompileProgram( frProgDiffFrame );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, srcATex);
    checkGLErrors("glBindTexture(srcA)");
    glBindTexture(texTarget, srcBTex);
    checkGLErrors("glBindTexture(srcB)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgDiffFrame, "frameA" );
    frameBParam = cgGetNamedParameter( frProgDiffFrame, "frameB" );
    cgGLSetTextureParameter( frameAParam, srcATex );
    cgGLEnableTextureParameter( frameAParam );
    cgGLSetTextureParameter( frameBParam, srcBTex );
    cgGLEnableTextureParameter( frameBParam );
    cgGLLoadProgram( frProgDiffFrame );
    cgGLBindProgram( frProgDiffFrame );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, srcWidth, srcHeight );

    checkFramebufferStatus(2);
}

void thresh_diff( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight,
                  float threshold )
{
    CGparameter frameParam;
    CGparameter threshParam;

    if( srcTex == destTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgThreshDiff ) ) {
        cgCompileProgram( frProgThreshDiff );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgThreshDiff, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );
    threshParam = cgGetNamedParameter( frProgThreshDiff, "threshold" );
    cgGLSetParameter1f( threshParam, threshold );
    cgGLLoadProgram( frProgThreshDiff );
    cgGLBindProgram( frProgThreshDiff );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, srcWidth, srcHeight );

    checkFramebufferStatus(2);
}

void decimate_add( GLuint destTex, GLuint srcTex, int srcWidth, int srcHeight )
{
    CGparameter frameParam;

    if( srcTex == destTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgDecimateAdd ) ) {
        cgCompileProgram( frProgDecimateAdd );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgDecimateAdd, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );
    cgGLLoadProgram( frProgDecimateAdd );
    cgGLBindProgram( frProgDecimateAdd );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuadMulti( srcWidth, srcHeight, (srcWidth+1) / 2, (srcHeight+1) / 2 );

    checkFramebufferStatus(2);
}

void vector_low_contrast( GLuint destTex, GLuint srcTex, int srcWidth, 
                          int srcHeight, float threshold )
{
    CGparameter frameParam;
    CGparameter threshParam;

    if( srcTex == destTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgVectorLowContrast ) ) {
        cgCompileProgram( frProgVectorLowContrast );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgVectorLowContrast, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );
    threshParam = cgGetNamedParameter( frProgVectorLowContrast, "threshold" );
    cgGLSetParameter1f( threshParam, threshold );
    cgGLLoadProgram( frProgVectorLowContrast );
    cgGLBindProgram( frProgVectorLowContrast );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, srcWidth, srcHeight );

    checkFramebufferStatus(2);
}

void vector_scale( GLuint destTex, GLuint srcTex, int x, int y, int scale )
{
    CGparameter frameParam;
    CGparameter scaleParam;

    if( srcTex == destTex ) {
        LogPrint( LOG_CRIT, "Scale %d to itself", srcTex );
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgVectorScale ) ) {
        cgCompileProgram( frProgVectorScale );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgVectorScale, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );
    
    scaleParam = cgGetNamedParameter( frProgVectorScale, "scale" );
    cgGLSetParameter1f( scaleParam, (float)scale );

    cgGLLoadProgram( frProgVectorScale );
    cgGLBindProgram( frProgVectorScale );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( x, y, x, y );

    checkFramebufferStatus(2);
}

void scale_buffer( GLuint destTex, GLuint srcTex, int x, int y, int scale )
{
    CGparameter frameParam;
    CGparameter scaleParam;

    if( srcTex == destTex ) {
        LogPrint( LOG_CRIT, "Scale %d to itself", srcTex );
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgScale ) ) {
        cgCompileProgram( frProgScale );
    }

    /* Setup the source texture */
    glBindTexture(texTarget, srcTex);
    checkGLErrors("glBindTexture(src)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgScale, "frame" );
    cgGLSetTextureParameter( frameParam, srcTex );
    cgGLEnableTextureParameter( frameParam );
    
    scaleParam = cgGetNamedParameter( frProgScale, "scale" );
    cgGLSetParameter1f( scaleParam, (float)scale );

    cgGLLoadProgram( frProgScale );
    cgGLBindProgram( frProgScale );

    /* Setup the destination */
    glBindTexture(texTarget, destTex);
    checkGLErrors("glBindTexture(dest)");

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, destTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(dest)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(dest)");
    checkFramebufferStatus(1);
    drawQuad( x, y, x, y );

    checkFramebufferStatus(2);
}

void mark_low_contrast_blocks( void )
{
    diff_frame( diffTexID, refTexID, avgTexID, width, padHeight );
#if 0
    dump_ppm( diffTexID, width, height, 32, "/tmp/diff%05d.ppm" );
#endif
    thresh_diff( tmpTexID, diffTexID, width, padHeight, contrast_thresh );
#if 0
    dump_pgm( tmpTexID, width, height, 32, "/tmp/thres1h%05d.pgm" );
#endif

    decimate_add( diffTexID, tmpTexID, width, padHeight );
#if 0
    dump_pgm( diffTexID, sub2Width, sub2Height - 32, 16, 
              "/tmp/thres2h%05d.pgm" );
#endif

    decimate_add( tmpTexID, diffTexID, sub2Width, sub2Height );
#if 0
    dump_pgm( tmpTexID, sub4Width, sub4Height - 16,  8, 
              "/tmp/thres4h%05d.pgm" );
#endif

    decimate_add( diffTexID, tmpTexID, sub4Width, sub4Height );
#if 0
    dump_pgm( diffTexID, vecWidth + 2, vecHeight - 8,  4, "/tmp/thres8h%05d.pgm" );
#endif

    /* diffTexID now holds a /8 size frame of thresholded differences */
    vector_low_contrast( vectorTexID, diffTexID, vecWidth, vecHeight, 
                         block_contrast_thresh );

    /* Now the vector texture will be initialized, with a SAD of 0 for the
     * macroblocks that are low-contrast, and uninit for those which aren't
     */
}

void SAD( GLuint vectorTex, GLuint refTex, GLuint avgTex, int srcWidth,
          int srcHeight, int xoffset, int yoffset, int factor )
{
    CGparameter frameAParam;
    CGparameter frameBParam;
    CGparameter factorParam;
    CGparameter offsetParam;

    if( vectorTex == refTex || vectorTex == avgTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgSAD ) ) {
        cgCompileProgram( frProgSAD );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, refTex);
    checkGLErrors("glBindTexture(ref)");
    glBindTexture(texTarget, avgTex);
    checkGLErrors("glBindTexture(avg)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgSAD, "frameA" );
    cgGLSetTextureParameter( frameAParam, refTex );
    cgGLEnableTextureParameter( frameAParam );

    frameBParam = cgGetNamedParameter( frProgSAD, "frameB" );
    cgGLSetTextureParameter( frameBParam, avgTex );
    cgGLEnableTextureParameter( frameBParam );

    factorParam = cgGetNamedParameter( frProgSAD, "factor" );
    cgGLSetParameter1f( factorParam, (float)factor );

    offsetParam = cgGetNamedParameter( frProgSAD, "offset" );
    cgGLSetParameter2f( offsetParam, (float)xoffset, (float)yoffset );

    cgGLLoadProgram( frProgSAD );
    cgGLBindProgram( frProgSAD );

    /* Setup the destination */
    glBindTexture(texTarget, vectorTex);
    checkGLErrors("glBindTexture(vector)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, vectorTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(vector)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(vector)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, vecWidth, vecHeight );

    checkFramebufferStatus(2);
}

void SAD_pass2( GLuint vectorTex, GLuint refTex, GLuint avgTex, 
                GLuint vectorInTex, int srcWidth, int srcHeight, int xoffset, 
                int yoffset, int factor )
{
    CGparameter frameAParam;
    CGparameter frameBParam;
    CGparameter vectorParam;
    CGparameter factorParam;
    CGparameter offsetParam;

    if( vectorTex == refTex || vectorTex == avgTex || 
        vectorTex == vectorInTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgSADPass2 ) ) {
        cgCompileProgram( frProgSADPass2 );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, refTex);
    checkGLErrors("glBindTexture(ref)");
    glBindTexture(texTarget, avgTex);
    checkGLErrors("glBindTexture(avg)");
    glBindTexture(texTarget, vectorInTex);
    checkGLErrors("glBindTexture(vectorIn)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgSADPass2, "frameA" );
    cgGLSetTextureParameter( frameAParam, refTex );
    cgGLEnableTextureParameter( frameAParam );

    frameBParam = cgGetNamedParameter( frProgSADPass2, "frameB" );
    cgGLSetTextureParameter( frameBParam, avgTex );
    cgGLEnableTextureParameter( frameBParam );

    vectorParam = cgGetNamedParameter( frProgSADPass2, "vector" );
    cgGLSetTextureParameter( vectorParam, vectorInTex );
    cgGLEnableTextureParameter( vectorParam );

    factorParam = cgGetNamedParameter( frProgSADPass2, "factor" );
    cgGLSetParameter1f( factorParam, (float)factor );

    offsetParam = cgGetNamedParameter( frProgSADPass2, "offset" );
    cgGLSetParameter2f( offsetParam, (float)xoffset, (float)yoffset );

    cgGLLoadProgram( frProgSADPass2 );
    cgGLBindProgram( frProgSADPass2 );

    /* Setup the destination */
    glBindTexture(texTarget, vectorTex);
    checkGLErrors("glBindTexture(vector)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, vectorTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(vector)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(vector)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, vecWidth, vecHeight );

    checkFramebufferStatus(2);
}

void SAD_halfpel( GLuint vectorTex, GLuint refTex, GLuint avgTex, 
                  GLuint vectorInTex, int srcWidth, int srcHeight, int xoffset, 
                  int yoffset, int factor )
{
    CGparameter frameAParam;
    CGparameter frameBParam;
    CGparameter vectorParam;
    CGparameter factorParam;
    CGparameter offsetParam;

    if( vectorTex == refTex || vectorTex == avgTex || 
        vectorTex == vectorInTex ) {
        return;
    }

    detachFBOs();

    if( !cgIsProgramCompiled( frProgSADHalfpel ) ) {
        cgCompileProgram( frProgSADHalfpel );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, refTex);
    checkGLErrors("glBindTexture(ref)");
    glBindTexture(texTarget, avgTex);
    checkGLErrors("glBindTexture(avg)");
    glBindTexture(texTarget, vectorInTex);
    checkGLErrors("glBindTexture(vectorIn)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgSADHalfpel, "frameA" );
    cgGLSetTextureParameter( frameAParam, refTex );
    cgGLEnableTextureParameter( frameAParam );

    frameBParam = cgGetNamedParameter( frProgSADHalfpel, "frameB" );
    cgGLSetTextureParameter( frameBParam, avgTex );
    cgGLEnableTextureParameter( frameBParam );

    vectorParam = cgGetNamedParameter( frProgSADHalfpel, "vector" );
    cgGLSetTextureParameter( vectorParam, vectorInTex );
    cgGLEnableTextureParameter( vectorParam );

    factorParam = cgGetNamedParameter( frProgSADHalfpel, "factor" );
    cgGLSetParameter1f( factorParam, (float)factor );

    offsetParam = cgGetNamedParameter( frProgSADHalfpel, "offset" );
    cgGLSetParameter2f( offsetParam, (float)xoffset, (float)yoffset );

    cgGLLoadProgram( frProgSADHalfpel );
    cgGLBindProgram( frProgSADHalfpel );

    /* Setup the destination */
    glBindTexture(texTarget, vectorTex);
    checkGLErrors("glBindTexture(vector)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, vectorTex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(vector)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(vector)");
    checkFramebufferStatus(1);
    drawQuad( srcWidth, srcHeight, vecWidth, vecHeight );

    checkFramebufferStatus(2);
}


void vector_update( void )
{
    CGparameter oldParam;
    CGparameter newParam;

    /* Inputs are vectorTexID, vector2TexID
     * Output is vectorTexID (by swapping IDs with vector3TexID)
     */

    detachFBOs();

    if( !cgIsProgramCompiled( frProgVectorUpdate ) ) {
        cgCompileProgram( frProgVectorUpdate );
    }

    pingpongTexID[readTex] = vectorTexID;
    pingpongTexID[writeTex] = vector3TexID;
    glDrawBuffer( attachmentpoints[writeTex] );
    checkGLErrors("glDrawBuffer(write)");
    attachPingPongFBOs();

    /* Setup the source textures */
    glBindTexture(texTarget, vector2TexID);
    checkGLErrors("glBindTexture(vector2)");

    /* Setup the Cg parameters, bind the program */
    oldParam = cgGetNamedParameter( frProgVectorUpdate, "oldvector" );
    cgGLSetTextureParameter( oldParam, pingpongTexID[readTex] );
    cgGLEnableTextureParameter( oldParam );

    newParam = cgGetNamedParameter( frProgVectorUpdate, "newvector" );
    cgGLSetTextureParameter( newParam, vector2TexID );
    cgGLEnableTextureParameter( newParam );

    cgGLLoadProgram( frProgVectorUpdate );
    cgGLBindProgram( frProgVectorUpdate );

    /* Attach the output */
    drawQuad( vecWidth, vecHeight, vecWidth, vecHeight );

    checkFramebufferStatus(2);

    vector3TexID = pingpongTexID[readTex];
    vectorTexID = pingpongTexID[writeTex];

    detachFBOs();
}

void dump_ppm( GLuint tex, int x, int y, int yOff, char *filepatt )
{
    static char         filename[64];
    unsigned char      *data;
    int                 size;

    size = x * y * 3;
    data = (unsigned char *)malloc(size * sizeof(unsigned char));

    detachFBOs();

    glDrawBuffer( GL_NONE );

    glBindTexture(texTarget, tex);
    checkGLErrors("glBindTexture(tex)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tex)");

    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(tex)");
    checkFramebufferStatus(7);
    glReadPixels( 0, yOff, x, y, GL_RGB, GL_UNSIGNED_BYTE, data );
    checkGLErrors("glReadPixels(tex)");

    detachFBOs();
    
    glReadBuffer( GL_NONE );

    sprintf( filename, filepatt, frameNum );

    save_ppm( data, x, y, 3, filename );
    free(data);
}

void dump_pgm( GLuint tex, int x, int y, int yOff, char *filepatt )
{
    static char         filename[128];
    unsigned char      *data;
    int                 size;

    size = x * y;
    data = (unsigned char *)malloc(size * sizeof(unsigned char));
#if 0
    LogPrint( LOG_CRIT, "x: %d, y: %d, size: %d, data: %p", x, y, size, data );
#endif

    detachFBOs();

    glDrawBuffer( GL_NONE );

    glBindTexture(texTarget, tex);
    checkGLErrors("glBindTexture(tex)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tex)");

    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(tex)");
    checkFramebufferStatus(7);
    glReadPixels( 0, yOff, x, y, GL_RED, GL_UNSIGNED_BYTE, data );
    checkGLErrors("glReadPixels(tex)");

    detachFBOs();
    
    glReadBuffer( GL_NONE );

    snprintf( filename, 127, filepatt, frameNum );

    save_ppm( data, x, y, 1, filename );
#if 0
    LogPrint( LOG_CRIT, "x: %d, y: %d, size: %d, data: %p", x, y, size, data );
#endif
    free(data);
}

void dump_data( GLuint tex, int x, int y )
{
    FILE           *fp;
    float          *data;
    int             i;
    int             j;

    data = (float *)malloc(x*y*sizeof(float));

    detachFBOs();
    glDrawBuffer( GL_NONE );
    glBindTexture(texTarget, tex);
    checkGLErrors("glBindTexture(dump)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tex)");
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(tex)");
    glReadPixels( 0, 0, x, y, GL_RED, GL_FLOAT, data );
    checkGLErrors("glReadPixels(tex)");

    glReadBuffer( GL_NONE );
    detachFBOs();

    fp = fopen( "/tmp/dump.out", "at" );

    for( i = 0; i < y; i++ ) {
        fprintf(fp, "Row %d:  ", i );
        for( j = 0; j < x; j++ ) {
            fprintf(fp, "%f ", data[i*x + j]);
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "\n\n");
    fclose(fp);

    free(data);
}

void dump_data3d( GLuint tex, int x, int y, char *tag, char *filename )
{
    FILE           *fp;
    float          *data;
    int             i;
    int             j;
    int             index;

    data = (float *)malloc(x*y*3*sizeof(float));

    detachFBOs();
    glDrawBuffer( GL_NONE );
    glBindTexture(texTarget, tex);
    checkGLErrors("glBindTexture(dump)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tex, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tex)");
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(tex)");
    glReadPixels( 0, 0, x, y, GL_RGB, GL_FLOAT, data );
    checkGLErrors("glReadPixels(tex)");

    glReadBuffer( GL_NONE );
    detachFBOs();

    fp = fopen( filename, "at" );

    fprintf(fp, "%s\n\n", tag );
    for( i = 0; i < y; i++ ) {
        for( j = 0; j < x; j++ ) {
            index = (i*x + j) * 3;
            fprintf(fp, "Row %d, Col %d:  ", i, j );
            fprintf(fp, "%f %f %f\n", data[index], data[index+1], 
                                      data[index+2]);
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "\n\n");
    fclose(fp);

    free(data);
}

void vector_badcheck( void )
{
    CGparameter     vectorParam;
    CGparameter     threshParam;
    int             x;
    int             y;
    int             xx;
    int             yy;
    float           bad_count;
    GLuint          temp;
    float           factor;

    x = vecWidth;
    y = vecHeight;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgVectorBadcheck ) ) {
        cgCompileProgram( frProgVectorBadcheck );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, vectorTexID);
    checkGLErrors("glBindTexture(vector)");

    /* Setup the Cg parameters, bind the program */
    vectorParam = cgGetNamedParameter( frProgVectorBadcheck, "vector" );
    cgGLSetTextureParameter( vectorParam, vectorTexID );
    cgGLEnableTextureParameter( vectorParam );

    threshParam = cgGetNamedParameter( frProgVectorBadcheck, "block_thresh" );
    cgGLSetParameter1f( threshParam, block_bad_thresh );

    cgGLLoadProgram( frProgVectorBadcheck );
    cgGLBindProgram( frProgVectorBadcheck );

    /* Setup the destination */
    glBindTexture(texTarget, badTexID);
    checkGLErrors("glBindTexture(bad)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, badTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(bad)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(bad)");
    checkFramebufferStatus(1);
    drawQuad( x, y, x, y );

    checkFramebufferStatus(2);
#if 0
    dump_pgm( badTexID, x+2, y, 0, "/tmp/bad1_%05d.pgm" );
#endif

    factor = 1.0;

    /* Decimate the bad_vector data down to a single number */
    while( x > 1 && y > 1 ) {
        xx = (x + 1) / 2;
        yy = (y + 1) / 2;

        decimate_add( bad2TexID, badTexID, x, y );
        factor *= 4.0;
        
        temp = bad2TexID;
        bad2TexID = badTexID;
        badTexID = temp;

        x = xx;
        y = yy;
    }

    /* now read back that one value
     */
    detachFBOs();
    glDrawBuffer( GL_NONE );
    glBindTexture(texTarget, badTexID);
    checkGLErrors("glBindTexture(bad)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, badTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(bad)");
    glReadBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glReadBuffer(bad)");
    glReadPixels( 0, 0, 1, 1, texFormatInout, GL_FLOAT, &bad_count );
    checkGLErrors("glReadPixels(bad)");

    bad_count *= factor;
#if 1
    LogPrint( LOG_CRIT, "bad_count: %f", bad_count );
#endif
    bad_vector += (int)bad_count;

    glReadBuffer( GL_NONE );
    glDrawBuffer( GL_NONE );

    detachFBOs();
}

void vector_range( void )
{
    CGparameter     frameParam;
    CGparameter     resParam;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgVectorRange ) ) {
        cgCompileProgram( frProgVectorRange );
    }

    /* Setup the source textures */
    pingpongTexID[readTex] = vectorTexID;
    pingpongTexID[writeTex] = vector3TexID;
    glDrawBuffer( attachmentpoints[writeTex] );
    checkGLErrors("glDrawBuffer(write)");
    attachPingPongFBOs();

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgVectorRange, "vector" );
    cgGLSetTextureParameter( frameParam, pingpongTexID[readTex] );
    cgGLEnableTextureParameter( frameParam );

    resParam = cgGetNamedParameter( frProgVectorRange, "resolution" );
    cgGLSetParameter2f( resParam, (float)width, (float)height );

    cgGLLoadProgram( frProgVectorRange );
    cgGLBindProgram( frProgVectorRange );

    /* Attach the output */
    drawQuad( vecWidth, vecHeight, vecWidth, vecHeight );

    checkFramebufferStatus(2);

    vector3TexID = pingpongTexID[readTex];
    vectorTexID = pingpongTexID[writeTex];

    detachFBOs();
}

void mb_search_44( void )
{
    int         xx;
    int         yy;

    for( yy = -radius; yy <= radius; yy++ ) {
        for( xx = -radius; xx <= radius; xx++ ) {
            SAD( vector2TexID, sub4refTexID, sub4avgTexID, sub4Width, 
                 sub4Height, xx, yy, 2 );
            vector_update();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_44:", "/tmp/44.out" );
            }
        }
    }
}

void mb_search_22( void )
{
    int         xx;
    int         yy;
    GLuint      temp;

    /* Unscale the subscaled vectors */
    vector_scale( vector2TexID, vectorTexID, vecWidth, vecHeight, 2 );
    temp = vector2TexID;
    vector2TexID = vectorTexID;
    vectorTexID = temp;
    if( frameNum == 3 ) {
        dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_22:", "/tmp/22.out" );
    }

    for( yy = -2; yy <= 2; yy++ ) {
        for( xx = -2; xx <= 2; xx++ ) {
            SAD_pass2( vector2TexID, sub2refTexID, sub2avgTexID, vectorTexID,
                       sub2Width, sub2Height, xx, yy, 4 );
            vector_update();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_22:", "/tmp/22.out" );
            }
        }
    }
}

void mb_search_11( void )
{
    int         xx;
    int         yy;
    GLuint      temp;

    /* Unscale the subscaled vectors */
    vector_scale( vector2TexID, vectorTexID, vecWidth, vecHeight, 2 );
    temp = vector2TexID;
    vector2TexID = vectorTexID;
    vectorTexID = temp;
    if( frameNum == 3 ) {
        dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_11:", "/tmp/11.out" );
    }

    for( yy = -2; yy <= 2; yy++ ) {
        for( xx = -2; xx <= 2; xx++ ) {
            SAD_pass2( vector2TexID, refTexID, avgTexID, vectorTexID,
                       width, padHeight, xx, yy, 8 );
            vector_update();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_11:", "/tmp/11.out" );
            }
        }
    }

    /* Idiot-check against no motion */
    SAD( vector2TexID, refTexID, avgTexID, width, padHeight, 0, 0, 8 );
    vector_update();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_11:", "/tmp/11.out" );
            }
}

void mb_search_00( void )
{
    int         xx;
    int         yy;

    for( yy = -1; yy <= 1; yy++ ) {
        for( xx = -1; xx <= 1; xx++ ) {
            SAD_halfpel( vector2TexID, refTexID, avgTexID, vectorTexID,
                         width, padHeight, xx, yy, 8 );
            vector_update();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_00:", "/tmp/00.out" );
            }
        }
    }

    vector_badcheck();
    vector_range();
            if( frameNum == 3 ) {
                dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_00:", "/tmp/00.out" );
            }
}

void move_frame( void )
{
    CGparameter frameParam;
    CGparameter vectorParam;
    GLuint      tmp;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgMoveFrame ) ) {
        cgCompileProgram( frProgMoveFrame );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, avgTexID);
    checkGLErrors("glBindTexture(avg)");
    glBindTexture(texTarget, vectorTexID);
    checkGLErrors("glBindTexture(vector)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgMoveFrame, "frame" );
    cgGLSetTextureParameter( frameParam, avgTexID );
    cgGLEnableTextureParameter( frameParam );

    vectorParam = cgGetNamedParameter( frProgMoveFrame, "vector" );
    cgGLSetTextureParameter( vectorParam, vectorTexID );
    cgGLEnableTextureParameter( vectorParam );

    cgGLLoadProgram( frProgMoveFrame );
    cgGLBindProgram( frProgMoveFrame );

    /* Setup the destination */
    glBindTexture(texTarget, tmpTexID);
    checkGLErrors("glBindTexture(tmp)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tmpTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tmp)");
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(tmp)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    /* present the output on avgTexID */
    tmp = tmpTexID;
    tmpTexID = avgTexID;
    avgTexID = tmp;
}

void average_frame( void )
{
    CGparameter frameParam;
    CGparameter avgParam;
    CGparameter delayParam;
    GLuint      tmp;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgAverageFrame ) ) {
        cgCompileProgram( frProgAverageFrame );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, refTexID);
    checkGLErrors("glBindTexture(ref)");
    glBindTexture(texTarget, avgTexID);
    checkGLErrors("glBindTexture(avg)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgAverageFrame, "frame" );
    cgGLSetTextureParameter( frameParam, refTexID );
    cgGLEnableTextureParameter( frameParam );

    avgParam = cgGetNamedParameter( frProgAverageFrame, "avg" );
    cgGLSetTextureParameter( avgParam, avgTexID );
    cgGLEnableTextureParameter( avgParam );

    delayParam = cgGetNamedParameter( frProgAverageFrame, "delay" );
    cgGLSetParameter1f( delayParam, (float)delay );

    cgGLLoadProgram( frProgAverageFrame );
    cgGLBindProgram( frProgAverageFrame );

    /* Setup the destination */
    glBindTexture(texTarget, tmpTexID);
    checkGLErrors("glBindTexture(tmp)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tmpTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tmp)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(tmp)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    /* present the output on avgTexID */
    tmp = tmpTexID;
    tmpTexID = avgTexID;
    avgTexID = tmp;
}

void correct_frame2( void )
{
    CGparameter frameAParam;
    CGparameter frameBParam;
    CGparameter threshParam;
    GLuint      tmp;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgCorrectFrame2 ) ) {
        cgCompileProgram( frProgCorrectFrame2 );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, refTexID);
    checkGLErrors("glBindTexture(ref)");
    glBindTexture(texTarget, avgTexID);
    checkGLErrors("glBindTexture(avg)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgCorrectFrame2, "frameA" );
    cgGLSetTextureParameter( frameAParam, refTexID );
    cgGLEnableTextureParameter( frameAParam );

    frameBParam = cgGetNamedParameter( frProgCorrectFrame2, "frameB" );
    cgGLSetTextureParameter( frameBParam, avgTexID );
    cgGLEnableTextureParameter( frameBParam );

    threshParam = cgGetNamedParameter( frProgCorrectFrame2, "threshold" );
    cgGLSetParameter1f( threshParam, correct_thresh );

    cgGLLoadProgram( frProgCorrectFrame2 );
    cgGLBindProgram( frProgCorrectFrame2 );

    /* Setup the destination */
    glBindTexture(texTarget, tmpTexID);
    checkGLErrors("glBindTexture(tmp)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tmpTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tmp)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(tmp)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    /* present the output on avgTexID */
    tmp = tmpTexID;
    tmpTexID = avgTexID;
    avgTexID = tmp;
}

void denoise_frame_pass2( void )
{
    CGparameter frameAParam;
    CGparameter frameBParam;
    CGparameter threshParam;
    GLuint      tmp;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgDenoiseFramePass2 ) ) {
        cgCompileProgram( frProgDenoiseFramePass2 );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, avgTexID);
    checkGLErrors("glBindTexture(avg)");
    glBindTexture(texTarget, avg2TexID);
    checkGLErrors("glBindTexture(avg2)");

    /* Setup the Cg parameters, bind the program */
    frameAParam = cgGetNamedParameter( frProgDenoiseFramePass2, "frameA" );
    cgGLSetTextureParameter( frameAParam, avgTexID );
    cgGLEnableTextureParameter( frameAParam );

    frameBParam = cgGetNamedParameter( frProgDenoiseFramePass2, "frameB" );
    cgGLSetTextureParameter( frameBParam, avg2TexID );
    cgGLEnableTextureParameter( frameBParam );

    threshParam = cgGetNamedParameter( frProgDenoiseFramePass2, 
                                       "pp_threshold" );
    cgGLSetParameter1f( threshParam, pp_thresh );

    cgGLLoadProgram( frProgDenoiseFramePass2 );
    cgGLBindProgram( frProgDenoiseFramePass2 );

    /* Setup the destination */
    glBindTexture(texTarget, tmpTexID);
    checkGLErrors("glBindTexture(tmp)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tmpTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tmp)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(tmp)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    /* present the output on avg2TexID */
    tmp = tmpTexID;
    tmpTexID = avg2TexID;
    avg2TexID = tmp;
}

void sharpen_frame( void )
{
    CGparameter frameParam;
    CGparameter sharpenParam;
    GLuint      tmp;

    detachFBOs();

    if( !cgIsProgramCompiled( frProgSharpenFrame ) ) {
        cgCompileProgram( frProgSharpenFrame );
    }

    /* Setup the source textures */
    glBindTexture(texTarget, avg2TexID);
    checkGLErrors("glBindTexture(avg2)");

    /* Setup the Cg parameters, bind the program */
    frameParam = cgGetNamedParameter( frProgSharpenFrame, "frame" );
    cgGLSetTextureParameter( frameParam, avg2TexID );
    cgGLEnableTextureParameter( frameParam );

    sharpenParam = cgGetNamedParameter( frProgSharpenFrame, "sharpen" );
    cgGLSetParameter1f( sharpenParam, sharpen );

    cgGLLoadProgram( frProgSharpenFrame );
    cgGLBindProgram( frProgSharpenFrame );

    /* Setup the destination */
    glBindTexture(texTarget, tmpTexID);
    checkGLErrors("glBindTexture(tmp)");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                              texTarget, tmpTexID, 0);
    checkGLErrors("glFramebufferTexture2DEXT(tmp)");

    /* Attach the output */
    glDrawBuffer( GL_COLOR_ATTACHMENT0_EXT );
    checkGLErrors("glDrawBuffer(tmp)");
    checkFramebufferStatus(1);
    drawQuad( width, padHeight, width, padHeight );

    checkFramebufferStatus(2);

    /* present the output on avg2TexID */
    tmp = tmpTexID;
    tmpTexID = avg2TexID;
    avg2TexID = tmp;
}


void denoiseFrame( void )
{
    bad_vector = 0;
    frameNum++;

    /* Copy input frame to ref */
    copy_frame(refTexID, frameTexID, width, padHeight, width, padHeight);
#if 0
    dump_ppm( refTexID, width, height, 32, "/tmp/ref%05d.ppm" );
#endif

    if( new_scene ) {
        new_scene = FALSE;

        LogPrint( LOG_NOTICE, "New Scene detected, frame %d", frameNum );

        /* Copy input frame to avg */
        copy_frame(avgTexID, frameTexID, width, padHeight, width, padHeight);

        /* Copy input frame to avg2 */
        copy_frame(avg2TexID, frameTexID, width, padHeight, width, padHeight);
    }

#if 0
    LogPrint( LOG_CRIT, "Frame %d", frameNum );
#endif
    contrast_frame();
    decimate_frame(sub2refTexID, refTexID, width, padHeight);
    decimate_frame(sub2avgTexID, avgTexID, width, padHeight);
    decimate_frame(sub4refTexID, sub2refTexID, sub2Width, sub2Height);
    decimate_frame(sub4avgTexID, sub2avgTexID, sub2Width, sub2Height);

#if 0
    dump_ppm( sub2refTexID, sub2Width, sub2Height - 32, 16, 
              "/tmp/sub2ref%05d.ppm" );
    dump_ppm( sub2avgTexID, sub2Width, sub2Height - 32, 16, 
              "/tmp/sub2avg%05d.ppm" );
    dump_ppm( sub4refTexID, sub4Width, sub4Height - 16,  8, 
              "/tmp/sub4ref%05d.ppm" );
    dump_ppm( sub4avgTexID, sub4Width, sub4Height - 16,  8, 
              "/tmp/sub4avg%05d.ppm" );
#endif

    mark_low_contrast_blocks();
#if 0
    dump_ppm( vectorTexID, vecWidth+2, vecHeight - 8, 4, "/tmp/vec1_%05d.ppm" );
#endif

    mb_search_44();
#if 0
    dump_ppm( vectorTexID, vecWidth+2, vecHeight - 8, 4, "/tmp/vec2_%05d.ppm" );
    dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_44:", "/tmp/44.out" );
#endif

    mb_search_22();
#if 0
    dump_ppm( vectorTexID, vecWidth+2, vecHeight - 8, 4, "/tmp/vec3_%05d.ppm" );
    dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_22:", "/tmp/22.out" );
#endif

    mb_search_11();
#if 0
    dump_ppm( vectorTexID, vecWidth+2, vecHeight - 8, 4, "/tmp/vec4_%05d.ppm" );
    dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_11:", "/tmp/11.out" );
#endif

    mb_search_00();
#if 0
    dump_ppm( vectorTexID, vecWidth+2, vecHeight - 8, 4, "/tmp/vec5_%05d.ppm" );
    dump_data3d( vectorTexID, vecWidth, vecHeight, "mb_search_00:", "/tmp/00.out" );
#endif

    move_frame();
#if 0
    dump_ppm( avgTexID, width, height, 32, "/tmp/avga_%05d.ppm" );
#endif

#if 0
    LogPrint( LOG_CRIT, "Frame %d - %d bad_vector", frameNum, bad_vector );
#endif
    if( ( width * height * scene_thresh / 6400 ) < bad_vector ) {
        /* Scene change */
        new_scene = TRUE;
    }

    bad_vector = 0;
    
    average_frame();
#if 0
    dump_ppm( avgTexID, width, height, 32, "/tmp/avgb_%05d.ppm" );
#endif

    correct_frame2();
#if 0
    dump_ppm( avgTexID, width, height, 32, "/tmp/avgc_%05d.ppm" );
#endif

    denoise_frame_pass2();
#if 0
    dump_ppm( avg2TexID, width, height, 32, "/tmp/avgd_%05d.ppm" );
#endif
    
    sharpen_frame();
#if 0
    dump_ppm( avg2TexID, width, height, 32, "/tmp/avge_%05d.ppm" );
#endif

    /* Copy the output back to be read */
    copy_frame(frameTexID, avg2TexID, width, padHeight, width, padHeight);

    /* Just to be sure */
    detachFBOs();
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

