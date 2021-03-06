/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_arch_file_io.h"
#include "apr_file_io.h"
#include "apr_lib.h"
#include <string.h>
#include <io.h>


static apr_status_t setptr(apr_file_t *thefile, unsigned long pos )
{
    long newbufpos;
    ULONG rc;

    if (thefile->direction == 1) {
        /* XXX: flush here is not mutex protected */
        apr_status_t rv = apr_file_flush(thefile);

        if (rv != APR_SUCCESS) {
            return rv;
        }

        thefile->bufpos = thefile->direction = thefile->dataRead = 0;
    }

    newbufpos = pos - (thefile->filePtr - thefile->dataRead);
    if (newbufpos >= 0 && newbufpos <= thefile->dataRead) {
        thefile->bufpos = newbufpos;
        rc = 0;
    } else {
        rc = DosSetFilePtr(thefile->filedes, pos, FILE_BEGIN, (unsigned long *) &thefile->filePtr );

        if ( !rc )
            thefile->bufpos = thefile->dataRead = 0;
    }

    return APR_FROM_OS_ERROR(rc);
}



APR_DECLARE(apr_status_t) apr_file_seek(apr_file_t *thefile, apr_seek_where_t where, apr_off_t *offset)
{
#ifndef __KLIBC__
    if (!thefile->isopen) {
        return APR_EBADF;
    }

    thefile->eof_hit = 0;

    if (thefile->buffered) {
        int rc = EINVAL;
        apr_finfo_t finfo;

        switch (where) {
        case APR_SET:
            rc = setptr(thefile, *offset);
            break;

        case APR_CUR:
            rc = setptr(thefile, thefile->filePtr - thefile->dataRead + thefile->bufpos + *offset);
            break;

        case APR_END:
            rc = apr_file_info_get(&finfo, APR_FINFO_NORM, thefile);
            if (rc == APR_SUCCESS)
                rc = setptr(thefile, finfo.size + *offset);
            break;
        }

        *offset = thefile->filePtr - thefile->dataRead + thefile->bufpos;
        return rc;
    } else {
        switch (where) {
        case APR_SET:
            where = FILE_BEGIN;
            break;

        case APR_CUR:
            where = FILE_CURRENT;
            break;

        case APR_END:
            where = FILE_END;
            break;
        }

        return APR_FROM_OS_ERROR(DosSetFilePtr(thefile->filedes, *offset, where, (ULONG *)offset));
    }
#else
    apr_off_t rv;

    thefile->eof_hit = 0;

    if (thefile->buffered) {
        int rc = EINVAL;
        apr_finfo_t finfo;

        file_lock(thefile);

        switch (where) {
        case APR_SET:
            rc = setptr(thefile, *offset);
            break;

        case APR_CUR:
            rc = setptr(thefile, thefile->filePtr - thefile->dataRead + thefile->bufpos + *offset);
            break;

        case APR_END:
            rc = apr_file_info_get_locked(&finfo, APR_FINFO_SIZE, thefile);
            if (rc == APR_SUCCESS)
                rc = setptr(thefile, finfo.size + *offset);
            break;
        }

        *offset = thefile->filePtr - thefile->dataRead + thefile->bufpos;

        file_unlock(thefile);

        return rc;
    }
    else {
        rv = lseek(thefile->filedes, *offset, where);
        if (rv == -1) {
            *offset = -1;
            return errno;
        }
        else {
            *offset = rv;
            return APR_SUCCESS;
        }
    }
#endif
}



APR_DECLARE(apr_status_t) apr_file_trunc(apr_file_t *thefile, apr_off_t offset)
{
    apr_status_t rv;

    if (thefile->buffered) {
        if (thefile->direction == 1) {
            /* Figure out what needs to be flushed.  Don't flush the part
             * of the write buffer that will get truncated anyway.
             */
            if (offset < thefile->filePtr) {
                thefile->bufpos = 0;
            }
            else if (offset < thefile->filePtr + (apr_off_t)thefile->bufpos) {
                thefile->bufpos = offset - thefile->filePtr;
            }

            if (thefile->bufpos != 0) {
                rv = apr_file_flush(thefile);
                if (rv != APR_SUCCESS)
                    return rv;
            }
        }
        else if (thefile->direction == 0) {
            /* Discard the read buffer, as we are about to reposition
             * ourselves to the end of file.
             */
            thefile->bufpos = 0;
            thefile->dataRead = 0;
        }
    }

    int rc = DosSetFileSize(thefile->filedes, offset);

    if (rc != 0) {
        return APR_FROM_OS_ERROR(rc);
    }

    if (thefile->buffered) {
        return setptr(thefile, offset);
    }

    return APR_SUCCESS;
}
