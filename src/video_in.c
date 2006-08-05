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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include "queue.h"
#include "logging.h"
#include "shared_mem.h"


void save_ppm(const uint8_t * rgb, size_t cols, size_t rows, int pixsize,
              const char *file);

/* SVN generated ID string */
static char ident[] _UNUSED_ = 
    "$Id$";

void decode(char *input_filename, int frames)
{
    int             err = 0;
    int             i = 0;
    int             counter = 0;
    int             len1 = 0;
    int             got_picture;
    int             video_index = -1;
    char            name[16];

    AVFormatContext *fcx = NULL;
    AVCodecContext *ccx = NULL;
    AVCodec        *codec = NULL;

    AVPicture       pict;
    AVPacket        pkt;
    AVFrame        *frame = avcodec_alloc_frame();

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

    /*
     * Decode proper 
     */
    while (frames == 0 || counter < frames ) {
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
            len1 = avcodec_decode_video(ccx, frame, &got_picture,
                                        pkt.data, pkt.size);

            if (got_picture) {
                /*
                 * Allocate AVPicture the first time through. 
                 */
                if (counter == 0) {
                    avpicture_alloc(&pict, PIX_FMT_RGB24,
                                    ccx->width, ccx->height);
                }

                img_convert(&pict, PIX_FMT_RGB24, (AVPicture *) frame,
                            ccx->pix_fmt, ccx->width, ccx->height);

                /*
                 * Visual effects: display image (save to disk, for now). 
                 */
                sprintf( name, "%05d.ppm", counter );
                save_ppm(pict.data[0], ccx->width, ccx->height, 3, name);

                counter++;
            }

            av_free_packet(&pkt);
        }
    }

    /*
     * Clean up 
     */
    avpicture_free(&pict);
    av_free(frame);
    av_close_input_file(fcx);
}


/*
 * Save the raw rgb data to a stream (either cout, or to a file)
 * (pgm/ppm). 
 */
void save_ppm(const uint8_t * rgb, size_t cols, size_t rows, int pixsize,
              const char *file)
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


/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

