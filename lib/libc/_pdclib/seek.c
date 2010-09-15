/* $Id: seek.c 393 2010-03-12 11:08:14Z solar $ */

/* int64_t _PDCLIB_seek( FILE *, int64_t, int )

   This file is part of the Public Domain C Library (PDCLib).
   Permission is granted to use, modify, and / or redistribute at will.
*/

#include <stdio.h>

#ifndef _PDCLIB_GLUE_H
#define _PDCLIB_GLUE_H
#include <_PDCLIB/_PDCLIB_glue.h>
#endif

_PDCLIB_int64_t _PDCLIB_seek( struct _PDCLIB_file_t * stream, _PDCLIB_int64_t offset, int whence )
{
    switch ( whence )
    {
        case SEEK_SET:
        case SEEK_CUR:
        case SEEK_END:
            /* EMPTY - OK */
            break;
        default:
            _PDCLIB_errno = _PDCLIB_EINVAL;
            return EOF;
            break;
    }
    _PDCLIB_int64_t rc = lseek( stream->handle, offset, whence );
    if ( rc != EOF )
    {
        stream->ungetidx = 0;
        stream->bufidx = 0;
        stream->bufend = 0;
        stream->pos.offset = rc;
        return rc;
    }
#if 0
    switch ( errno )
    {
        case EBADF:
        case EFAULT:
            _PDCLIB_errno = _PDCLIB_EIO;
            break;
        default:
#endif
            _PDCLIB_errno = _PDCLIB_EUNKNOWN;
#if 0
            break;
    }
#endif
    return EOF;
}

#ifdef TEST
#include <_PDCLIB_test.h>

int main()
{
    /* Testing covered by ftell.c */
    return TEST_RESULTS;
}

#endif

