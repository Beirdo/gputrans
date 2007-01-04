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

void do_child( int childNum );
void SoftExitChild( void );
void DisplayFPS( void );

extern CGprogram loadCgProgram( char *name, char *file, char *entry );
extern void cg_init( cardInfo_t *cardInfo, int childNum );
extern void cg_enable( int width, int height );
extern void cg_destroy( void );
extern void setupCardInfo( cardInfo_t *cardInfo, int childNum );
extern void loadFrame(uint8 *yData, uint8 *uData, uint8 *vData);
extern void unloadFrame(uint8 *yData, uint8 *uData, uint8 *vData);
extern void unloadRaw(uint8 *yData, uint8 *uData, uint8 *vData, 
                      uint8 *yDataOut, uint8 *uDataOut, uint8 *vDataOut);
extern void denoiseFrame( void );
extern void initFBO( void );
extern void checkGLErrors( const char *label );
extern void drawQuad( int wTex, int hTex, int wOut, int hOut );
extern bool checkFramebufferStatus( int index );
extern void swap( void );
extern void setupTexture( const GLuint texID, GLenum format, GLenum inFormat, 
                          int x, int y);
extern void createTextures(int x, int y);
extern void detachFBOs( void );
extern void attachPingPongFBOs( void );

extern int              idShm, idSem, idFrame;
extern char            *shmBlock;
extern unsigned char   *frameBlock;
static sharedMem_t     *sharedMem;
static cardInfo_t      *cardInfo;
static int              me;
static bool             initialized = FALSE;
static int              width, height;
static int              mode;
static int              framesDone = 0;
static struct timeval   renderStart;
static bool             rendered = FALSE;

void do_child( int childNum )
{
    char           *msg;
    QueueMsg_t      type;
    int             len;
    ChildMsg_t     *message;
    bool            done;
    FrameDoneMsg_t  frameMsg;
    unsigned char  *frameInBase;
    unsigned char  *frameOutBase;
    unsigned char  *frameIn;
    unsigned char  *frameOut;
    unsigned char  *yIn, *uIn, *vIn, *yOut, *uOut, *vOut;
    int             frameSize;
    int             frameNum;
    int             indexIn;
    int             oldCurr;
    int             stride;

    oldCurr = -1;

    shmBlock = NULL;
    frameBlock = NULL;
    atexit( SoftExitChild );

    shmBlock = (char *)shmat( idShm, NULL, 0 );
    sharedMem = (sharedMem_t *)shmBlock;
    cardInfo = &sharedMem->cardInfo[childNum];
    me = childNum;
    frameMsg.childNum = childNum;

    frameBlock = (unsigned char *)shmat( idFrame, NULL, 0 );

    frameInBase  = &frameBlock[sharedMem->offsets.frameIn];
    frameOutBase = &frameBlock[sharedMem->offsets.frameOut];
    frameSize    = sharedMem->frameSize;

    LogPrint( LOG_NOTICE, "<%d> inOffset = %d, outOffset = %d", childNum,
              sharedMem->offsets.frameIn, sharedMem->offsets.frameOut );

    /* Initialize the Cg/OpenGL setup */
    cg_init( cardInfo, childNum );
    initialized = TRUE;

    /* must be done after OpenGL initialization */
    setupCardInfo( cardInfo, childNum );

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
            cg_enable( width, height );

            queueSendBinary( Q_MSG_RENDER_READY, &childNum, sizeof(childNum) );
            rendered = TRUE;
            gettimeofday( &renderStart, NULL );
            break;
        case CHILD_RENDER_FRAME:
            frameNum    = message->payload.renderFrame.frameNum;
            indexIn     = message->payload.renderFrame.indexIn;

            stride      = width * height;
            frameIn     = frameInBase  + (frameSize * indexIn);

            /* Setup the current frame in the GPU */
            yIn = frameIn;
            uIn = yIn + stride;
            vIn = uIn + (stride / 4);
            loadFrame( yIn, uIn, vIn );

#if 0
            LogPrint( LOG_NOTICE, "<%d> Received Frame #%d (index %d)",
                                  childNum, frameNum, indexIn );
#endif


            /* Pretend to have done some work */
            denoiseFrame();
#if 0
            usleep( 1000L );
#endif

            /* Pull the output frame out of the GPU */
            frameOut    = frameOutBase + (frameSize * indexIn);

            yOut = frameOut;
            uOut = yOut + stride;
            vOut = uOut + (stride / 4);
            unloadFrame( yOut, uOut, vOut );
#if 0
            unloadRaw( yIn, uIn, vIn, yOut, uOut, vOut ); 
#endif

            framesDone++;

            if( framesDone % 50 == 0 ) {
                DisplayFPS();
            }

            frameMsg.renderFrame.frameNum = frameNum;
            frameMsg.renderFrame.indexIn = indexIn;
            queueSendBinary( Q_MSG_FRAME_DONE, &frameMsg, sizeof( frameMsg ) );
            break;
        default:
            break;
        }
    }

    exit(0);
}


void DisplayFPS( void )
{
    struct timeval      renderFinish;
    float               sec;
    float               fps;

    gettimeofday( &renderFinish, NULL );
    renderFinish.tv_sec  -= renderStart.tv_sec;
    renderFinish.tv_usec -= renderStart.tv_usec;
    while( renderFinish.tv_usec < 0L ) {
        renderFinish.tv_sec--;
        renderFinish.tv_usec += 1000000L;
    }
    sec = (float)renderFinish.tv_sec + 
          ((float)renderFinish.tv_usec / 1000000.0);
    fps = (float)framesDone / sec;

    LogPrint( LOG_NOTICE, "<%d> %d frames in %.6f (%.2f FPS)", me,
              framesDone, sec, fps );
}

void SoftExitChild( void )
{
    if( rendered ) {
        DisplayFPS();
    }

    queueSendBinary( Q_MSG_DYING_GASP, &me, sizeof(me) );
    if( initialized ) {
        cg_destroy();
    }

    if( shmBlock ) {
        shmdt( shmBlock );
        shmBlock = NULL;
    }

    if( frameBlock ) {
        shmdt( frameBlock );
        frameBlock = NULL;
    }

    sleep( 1 );
    _exit( 0 );
}


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

