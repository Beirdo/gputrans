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

#ifndef _video_h
#define _video_h

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "shared_mem.h"

/* SVN generated ID string */
static char video_h_ident[] _UNUSED_ = 
    "$Id$";

void openFile( char *input_filename, int *cols, int *rows );
void save_ppm( const unsigned char *rgb, size_t cols, size_t rows, int pixsize,
               const char *file );
void frameToUnsignedInt( unsigned char *frame, unsigned int *buffer, int cols,
                         int rows );
void unsignedIntToFrame( unsigned int *buffer, unsigned char *frame, int cols,
                         int rows );
void initFfmpeg( void );
void closeFfmpeg( AVPicture *pict );
void initVideoIn( sharedMem_t *sharedMem, int cols, int rows );
bool getFrame( AVPicture *pict, int pix_fmt );
void decode( int frames );

extern int      idFrame;

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

