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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <pthread.h>
#include "ipc_queue.h"
#include "ipc_logging.h"
#include "shared_mem.h"
#include "video.h"
#include "queue.h"
#include "logging.h"


/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

typedef struct {
    LinkedListItem_t    link;
    ChildMsg_t          msg;
} ChildMsgList_t;

int                 idFrame;
extern int          numChildren;
unsigned char      *frameBlock;
AVPicture          *avFrameIn;
AVPicture          *avFrameOut;
AVFrame            *frame;
int                 video_index = -1;
AVFormatContext    *fcx = NULL;
AVCodecContext     *ccx = NULL;
AVCodec            *codec = NULL;
QueueObject_t      *ChildMsgQ;
pthread_t           videoInThreadId;
static sharedMem_t *sharedMem;
static int          headIn  = -1;
static int          tailIn  = 0;
extern unsigned long shmmax;
extern bool         GlobalAbort;


void *VideoInThread( void *arg );

void SoftExitParent( void );
    
void openFile( char *input_filename, int *cols, int *rows )
{
    int             err;
    int             i;

    /*
     * Open the input file. 
     */
    LogPrint( LOG_NOTICE, "Opening file: %s for reading", input_filename );
    err = av_open_input_file(&fcx, input_filename, NULL, 0, NULL);
    if (err < 0) {
        LogPrint(LOG_CRIT, "Can't open file: %s (%d)", input_filename, err);
        exit(-1);
    }

    /*
     * Find the stream info. 
     */
    err = av_find_stream_info(fcx);

    /*
     * Find the first video stream. 
     */
    for (i = 0; i < fcx->nb_streams; i++) {
        ccx = fcx->streams[i]->codec;
        if (ccx->codec_type == CODEC_TYPE_VIDEO) {
            break;
        }
    }
    video_index = i;

    /*
     * Open stream. FIXME: proper closing of av before exit. 
     */
    if (video_index >= 0) {
        codec = avcodec_find_decoder(ccx->codec_id);
        if (codec) {
            err = avcodec_open(ccx, codec);
        }

        if (err < 0) {
            LogPrintNoArg( LOG_CRIT, "Can't open codec." );
            exit(-1);
        }
    } else {
        LogPrintNoArg( LOG_CRIT, "Video stream not found." );
        exit(-1);
    }

     dump_format(fcx, 0, input_filename, 0);

     *cols = ccx->width;
     *rows = ccx->height;
}


bool getFrame( AVPicture *pict, int pix_fmt )
{
    bool            got_picture = FALSE;

    AVPacket        pkt;

    while ( !got_picture ) {
        /*
         * Read a frame/packet. 
         */
        if (av_read_frame(fcx, &pkt) < 0) {
            break;
        }

        /*
         * If it's a video packet from our video stream...  
         */
        if (pkt.stream_index == video_index) {
            /*
             * Decode the packet 
             */
            avcodec_decode_video(ccx, frame, &got_picture, pkt.data, pkt.size);

            if (got_picture) {
                /* Got a frame, extract the video, converting to the desired
                 * output format (normally PIX_FMT_YUV444P, but uses
                 * PIX_FMT_RGB24 for test_decode)
                 */
                if( GlobalAbort ) {
                    return( FALSE );
                }
                img_convert(pict, pix_fmt, (AVPicture *)frame,
                            ccx->pix_fmt, ccx->width, ccx->height);
            }

        }
        av_free_packet(&pkt);
    }

    return( got_picture );
}


void closeFfmpeg( AVPicture *pict )
{
    if( pict ) {
        avpicture_free(pict);
    }
    av_free(frame);
    av_close_input_file(fcx);
}

/*
 * Save the raw rgb data to a stream (either cout, or to a file)
 * (pgm/ppm). 
 */
void save_ppm( const unsigned char *rgb, size_t cols, size_t rows, int pixsize,
               const char *file )
{
    int         fd;
    char        string[64];

    if( !file ) {
        return;
    }

    fd = open( file, O_CREAT | O_WRONLY, 0644 );
    switch (pixsize) {
    case 1:
        write( fd, "P5\n", 3 );
        break;
    case 3:
        write( fd, "P6\n", 3 );
        break;
    default:
        LogPrint( LOG_CRIT, "Can't handle pixel size: %d", pixsize );
        exit(-1);
    }

    sprintf( string, "%ld %ld\n%d\n", (long)cols, (long)rows, 255 );
    write( fd, string, strlen(string) );
    write( fd, (const char *) rgb, rows * cols * pixsize );

    close( fd );
}

void initFfmpeg( void )
{
    av_register_all();
    frame = avcodec_alloc_frame();
}


void initVideoIn( sharedMem_t *sharedMem, int cols, int rows )
{
    int             i;
    int             offset;
    int             maxFrames;

    sharedMem->cols = cols;
    sharedMem->rows = rows;
    sharedMem->frameSize  = avpicture_get_size( PIX_FMT_YUV444P, cols, rows );

    LogPrint( LOG_NOTICE, "Frame Size = %d", sharedMem->frameSize );

    maxFrames = (int)(shmmax / sharedMem->frameSize);
    if( maxFrames < 8 ) {
        LogLocalPrint( LOG_CRIT, "In order to work with this video file, you will "
                            "need to increase /proc/sys/kernel/shmmax to at "
                            "least %ld", ((long)sharedMem->frameSize * 8) );
        atexit(SoftExitParent);
        exit( -1 );
    }
    LogPrint( LOG_NOTICE, "Maximum frames in SHM segment: %d", maxFrames );

    sharedMem->frameCountIn = numChildren + 4;
    if( sharedMem->frameCountIn > maxFrames / 2 ) {
        sharedMem->frameCountIn = maxFrames / 2;
        LogPrint( LOG_NOTICE, "Limiting in frames to %d", maxFrames / 2 );
    }
    sharedMem->frameCountOut = numChildren + 4;
    if( sharedMem->frameCountOut > maxFrames / 2 ) {
        sharedMem->frameCountOut = maxFrames / 2;
        LogPrint( LOG_NOTICE, "Limiting out frames to %d", maxFrames / 2 );
    }
    sharedMem->frameCount = sharedMem->frameCountIn + sharedMem->frameCountOut;

    sharedMem->offsets.frameIn = 0;
    sharedMem->offsets.frameOut = sharedMem->frameSize * 
                                  sharedMem->frameCountIn;
    
    idFrame = shmget( IPC_PRIVATE, 
                      sharedMem->frameSize * sharedMem->frameCount,
                      IPC_CREAT | 0600 );
    frameBlock = (unsigned char *)shmat( idFrame, NULL, 0 );

    avFrameIn  = (AVPicture *)malloc(sizeof(AVPicture) * 
                                     sharedMem->frameCountIn);
    avFrameOut = (AVPicture *)malloc(sizeof(AVPicture) * 
                                     sharedMem->frameCountOut);

    for( i = 0; i < sharedMem->frameCountIn; i++ ) {
        offset = sharedMem->frameSize * i;
        avpicture_fill(&avFrameIn[i], &frameBlock[offset], 
                       PIX_FMT_YUV444P, cols, rows);
    }

    for( i = 0; i < sharedMem->frameCountOut; i++ ) {
        offset = sharedMem->frameSize * (i + sharedMem->frameCountIn);
        avpicture_fill(&avFrameOut[i], &frameBlock[offset], 
                       PIX_FMT_YUV444P, cols, rows);
    }
}

void frameToUnsignedInt( unsigned char *frame, unsigned int *buffer, int cols,
                         int rows )
{
    int             i;
    unsigned char  *y, *u, *v;
    int             pixCount;

    pixCount = cols * rows;
    y = frame;
    u = y + pixCount;
    v = u + pixCount;

    for( i = 0; i < pixCount; i++ ) {
        buffer[i] = (y[i] << 24) + (u[i] << 16) + (v[i] << 8) + (0 << 0);
    }
}

void unsignedIntToFrame( unsigned int *buffer, unsigned char *frame, int cols,
                         int rows )
{
    int             i;
    unsigned char  *y, *u, *v;
    int             pixCount;
    unsigned int    val;

    pixCount = cols * rows;
    y = frame;
    u = y + pixCount;
    v = u + pixCount;

    for( i = 0; i < pixCount; i++ ) {
        val = buffer[i];
        y[i] = (val >> 24) & 0xFF;
        u[i] = (val >> 16) & 0xFF;
        v[i] = (val >>  8) & 0xFF;
    }
}

void video_in_initialize( sharedMem_t *shared, char *filename )
{
    sharedMem = shared;
    pthread_create( &videoInThreadId, NULL, VideoInThread, filename );
}

void *VideoInThread( void *arg )
{
    char           *filename = (char *)arg;
    int             cols;
    int             rows;
    AVPicture      *pict;
    int             frameCount;
    int             frameNum = 0;
    int             curr;
    int             prev;
    bool            done = FALSE;
    ChildMsg_t     *msg;

    LogPrintNoArg( LOG_NOTICE, "Starting video input thread" );

    ChildMsgQ = QueueCreate( numChildren * 2 );
    initFfmpeg();
    openFile( filename, &cols, &rows );
    initVideoIn( sharedMem, cols, rows );

    frameCount = sharedMem->frameCountIn;

    pict = &avFrameIn[0];

    queueSendBinary( Q_MSG_VIDEO_READY, NULL, 0 );

    while( !done && !GlobalAbort ) {
        while( (tailIn + 1) % frameCount == headIn ) {
            usleep( 100000L );
        }

        if( GlobalAbort ) {
            continue;
        }

        if( !getFrame( pict, PIX_FMT_YUV444P ) ) {
            done = TRUE;
            msg = (ChildMsg_t *)malloc(sizeof(ChildMsg_t));
            msg->type = CHILD_RENDER_FRAME;
            msg->payload.renderFrame.frameNum    = -1;
            msg->payload.renderFrame.indexIn     = -1;
            msg->payload.renderFrame.indexInPrev = -1;
            QueueEnqueueItem( ChildMsgQ, (QueueItem_t)msg );
            continue;
        }

        frameNum++;
        curr = tailIn;

        if( headIn == -1 ) {
            /* This is the first frame, no previous frame */
            headIn = tailIn;
            prev = -1;
        } else {
            prev = (tailIn + frameCount - 1) % frameCount;
        }

        msg = (ChildMsg_t *)malloc(sizeof(ChildMsg_t));
        msg->type = CHILD_RENDER_FRAME;
        msg->payload.renderFrame.frameNum    = frameNum;
        msg->payload.renderFrame.indexIn     = curr;
        msg->payload.renderFrame.indexInPrev = prev;

#if 0
        LogPrint( LOG_NOTICE, "enqueued frame #%d, head=%d, tail=%d",
                              frameNum, headIn, tailIn );
#endif
        QueueEnqueueItem( ChildMsgQ, (QueueItem_t)msg );

        tailIn = (tailIn + 1) % frameCount;

        pict = &avFrameIn[tailIn];
    }

    closeFfmpeg( NULL );
    return( NULL );
}

void videoFinished( int index )
{
    int         frameCount;

    frameCount = sharedMem->frameCountIn;

    headIn = (index + 1) % frameCount;
}

void videoOut( int frameNum, int index )
{
    static char         filename[64];
    static AVPicture    pict;
    static bool         init = FALSE;

    sprintf( filename, "out/%05d.ppm", frameNum );

    if( !init ) {
        avpicture_alloc( &pict, PIX_FMT_RGB24, sharedMem->cols, 
                         sharedMem->rows );
        init = TRUE;
    }
    img_convert(&pict, PIX_FMT_RGB24, &avFrameOut[index], PIX_FMT_YUV444P, 
                sharedMem->cols, sharedMem->rows);
    save_ppm( pict.data[0], sharedMem->cols, sharedMem->rows, 3, filename );
}

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

