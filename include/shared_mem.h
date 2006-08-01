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

#ifndef _shared_mem_h
#define _shared_mem_h

/* SVN generated ID string */
static char shared_mem_h_ident[] _UNUSED_ = 
    "$Id$";

#define NUM_CARDS   5

typedef struct {
    int         childNum;
    char        display[16];
    char        renderer[256];
    int         maxTexSize;
    int         maxViewportDim;
    int         maxColorAttach;
    bool        haveFBO;
    bool        haveTextRect;
    bool        haveNvFloat;
} cardInfo_t;


typedef struct {
    cardInfo_t    cardInfo[NUM_CARDS];
} sharedMem_t;

#endif

/*
 * vim:ts=4:sw=4:ai:et:si:sts=4
 */

