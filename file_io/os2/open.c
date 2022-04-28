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
#include "apr_portable.h"
#include "apr_strings.h"
#include "apr_arch_inherit.h"
#include <string.h>
#include <unistd.h>

apr_status_t apr_file_cleanup(void *thefile)
{
    apr_file_t *file = thefile;
    return apr_file_close(file);
}

#ifdef __INNOTEK_LIBC__
static apr_status_t file_cleanup(apr_file_t *file)
{
    apr_status_t rv = APR_SUCCESS;

    if (close(file->filedes) == 0) {
        file->filedes = -1;
        if (file->flags & APR_DELONCLOSE) {
            unlink(file->fname);
        }
#if APR_HAS_THREADS
        if (file->mutex) {
            rv = apr_thread_mutex_destroy(file->mutex);
        }
#endif
    }
    else {
        /* Are there any error conditions other than EINTR or EBADF? */
        rv = errno;
    }
#ifndef WAITIO_USES_POLL
    if (file->pollset != NULL) {
        int pollset_rv = apr_pollset_destroy(file->pollset);
        /* If the file close failed, return its error value,
         * not apr_pollset_destroy()'s.
         */
        if (rv == APR_SUCCESS) {
            rv = pollset_rv;
        }
    }
#endif /* !WAITIO_USES_POLL */
    return rv;
}

apr_status_t apr_unix_file_cleanup(void *thefile)
{
    apr_file_t *file = thefile;
    apr_status_t flush_rv = APR_SUCCESS, rv = APR_SUCCESS;

    if (file->buffered) {
        flush_rv = apr_file_flush(file);
    }

    rv = file_cleanup(file);

    return rv != APR_SUCCESS ? rv : flush_rv;
}

apr_status_t apr_unix_child_file_cleanup(void *thefile)
{
    return file_cleanup(thefile);
}
#endif


APR_DECLARE(apr_status_t) apr_file_open(apr_file_t **new, const char *fname,
                                   apr_int32_t flag, apr_fileperms_t perm,
                                   apr_pool_t *pool)
{
    int oflags = 0;
    int mflags = OPEN_FLAGS_FAIL_ON_ERROR|OPEN_SHARE_DENYNONE|OPEN_FLAGS_NOINHERIT;
    int rv;
    ULONG action;
    ULONG    CurMaxFH      = 0;          /* Current count of handles         */
    LONG     ReqCount      = 0;          /* Number to adjust file handles    */

    // 2019-06-03 SHL ensure initialized to zeros
    // apr_file_t *dafile = (apr_file_t *)apr_palloc(pool, sizeof(apr_file_t));
    apr_file_t *dafile = (apr_file_t *)apr_pcalloc(pool, sizeof(apr_file_t));


    if (flag & APR_FOPEN_NONBLOCK) {
        return APR_ENOTIMPL;
    }

    dafile->pool = pool;
    dafile->isopen = FALSE;
    dafile->eof_hit = FALSE;
    dafile->buffer = NULL;
    dafile->flags = flag;
    dafile->blocking = BLK_ON;
    
    if ((flag & APR_FOPEN_READ) && (flag & APR_FOPEN_WRITE)) {
        mflags |= OPEN_ACCESS_READWRITE;
    } else if (flag & APR_FOPEN_READ) {
        mflags |= OPEN_ACCESS_READONLY;
    } else if (flag & APR_FOPEN_WRITE) {
        mflags |= OPEN_ACCESS_WRITEONLY;
    } else {
        dafile->filedes = -1;
        return APR_EACCES;
    }

    dafile->buffered = (flag & APR_FOPEN_BUFFERED) > 0;

    if (dafile->buffered) {
        dafile->buffer = apr_palloc(pool, APR_FILE_DEFAULT_BUFSIZE);
        dafile->bufsize = APR_FILE_DEFAULT_BUFSIZE;
        rv = apr_thread_mutex_create(&dafile->mutex, 0, pool);

        if (rv)
            return rv;
    }

    if (flag & APR_FOPEN_CREATE) {
        oflags |= OPEN_ACTION_CREATE_IF_NEW;

        if (!(flag & APR_FOPEN_EXCL) && !(flag & APR_FOPEN_TRUNCATE)) {
            oflags |= OPEN_ACTION_OPEN_IF_EXISTS;
        }
    }
    
    if ((flag & APR_FOPEN_EXCL) && !(flag & APR_FOPEN_CREATE))
        return APR_EACCES;

    if (flag & APR_FOPEN_TRUNCATE) {
        oflags |= OPEN_ACTION_REPLACE_IF_EXISTS;
    } else if ((oflags & 0xFF) == 0) {
        oflags |= OPEN_ACTION_OPEN_IF_EXISTS;
    }
    
    rv = DosOpen(fname, &(dafile->filedes), &action, 0, 0, oflags, mflags, NULL);

    if (rv == ERROR_TOO_MANY_OPEN_FILES) {
	rv = DosSetRelMaxFH(&ReqCount,     /* Using 0 here will return the       */
                      &CurMaxFH);    /* current number of file handles     */
	ReqCount      = 10L;         /* Want 10 more file handles         */
	rv = DosSetRelMaxFH(&ReqCount,&CurMaxFH);     /* Change handle maximum */

	rv = DosOpen(fname, &(dafile->filedes), &action, 0, 0, oflags, mflags, NULL);
    }    

    if (rv == 0 && (flag & APR_FOPEN_APPEND)) {
        ULONG newptr;
        rv = DosSetFilePtr(dafile->filedes, 0, FILE_END, &newptr );
        
        if (rv)
            DosClose(dafile->filedes);
    }
    
    if (rv != 0)
        return APR_FROM_OS_ERROR(rv);
   
    dafile->isopen = TRUE;
    dafile->fname = apr_pstrdup(pool, fname);
    dafile->filePtr = 0;
    dafile->bufpos = 0;
    dafile->dataRead = 0;
    dafile->direction = 0;
    dafile->pipe = FALSE;
    dafile->ungetchar = -1;


    if (!(flag & APR_FOPEN_NOCLEANUP)) {
        apr_pool_cleanup_register(dafile->pool, dafile, apr_file_cleanup, apr_file_cleanup);
    }

    *new = dafile;
    return APR_SUCCESS;
}



APR_DECLARE(apr_status_t) apr_file_close(apr_file_t *file)
{
    ULONG rc,rv;
    apr_status_t status;
    
    if (file && file->isopen) {
        /* XXX: flush here is not mutex protected */
        status = apr_file_flush(file);

	/* 2014-11-18 SHL ensure client sees ERROR_BROKEN_PIPE */
        if (file->pipe == 1) {
            DosCloseEventSem(file->pipeSem);
            DosDisConnectNPipe(file->filedes);
        }
        rc = DosClose(file->filedes);
    
        if (rc == 0) {
            file->isopen = FALSE;

            if (file->flags & APR_FOPEN_DELONCLOSE) {
                rv = DosDelete(file->fname);
                if (rv != 32)
                    status = APR_FROM_OS_ERROR(rv);
            }
            /* else we return the status of the flush attempt 
             * when all else succeeds
             */
        } else {
            return APR_FROM_OS_ERROR(rc);
        }
    }

    if (file->buffered)
        apr_thread_mutex_destroy(file->mutex);

    return status;
}



APR_DECLARE(apr_status_t) apr_file_remove(const char *path, apr_pool_t *pool)
{
#if 0
    ULONG rc = DosDelete(path);
    return APR_FROM_OS_ERROR(rc);
#else
    if (unlink(path) == 0) {
        return APR_SUCCESS;
    }
    else {
        return errno;
    }
#endif
}



APR_DECLARE(apr_status_t) apr_file_rename(const char *from_path, const char *to_path,
                                   apr_pool_t *p)
{
    ULONG rc = DosMove(from_path, to_path);

    if (rc == ERROR_ACCESS_DENIED || rc == ERROR_ALREADY_EXISTS) {
        rc = DosDelete(to_path);

        if (rc == 0 || rc == ERROR_FILE_NOT_FOUND) {
            rc = DosMove(from_path, to_path);
        }
    }

    return APR_FROM_OS_ERROR(rc);
}



APR_DECLARE(apr_status_t) apr_os_file_get(apr_os_file_t *thefile, apr_file_t *file)
{
    *thefile = file->filedes;
    return APR_SUCCESS;
}



APR_DECLARE(apr_status_t) apr_os_file_put(apr_file_t **file, apr_os_file_t *thefile, apr_int32_t flags, apr_pool_t *pool)
{
    apr_os_file_t *dafile = thefile;

    // 2019-06-03 SHL ensure initialized to zeros
    // (*file) = apr_palloc(pool, sizeof(apr_file_t));
    (*file) = apr_pcalloc(pool, sizeof(apr_file_t));
    (*file)->pool = pool;
    (*file)->filedes = *dafile;
    (*file)->isopen = TRUE;
    (*file)->eof_hit = FALSE;
    (*file)->flags = flags;
    (*file)->pipe = FALSE;
    (*file)->ungetchar = -1;

    if (flags & APR_FOPEN_BUFFERED) {
        (*file)->buffered = 1;
        (*file)->buffer = apr_palloc(pool, APR_FILE_DEFAULT_BUFSIZE);
        (*file)->bufsize = APR_FILE_DEFAULT_BUFSIZE;
    }

    if ((*file)->buffered) {
        apr_status_t rv;
        rv = apr_thread_mutex_create(&(*file)->mutex, 0, pool);

        if (rv)
            return rv;
    }

    return APR_SUCCESS;
}    


APR_DECLARE(apr_status_t) apr_file_eof(apr_file_t *fptr)
{
    if (!fptr->isopen || fptr->eof_hit == 1) {
        return APR_EOF;
    }
    return APR_SUCCESS;
}   


APR_DECLARE(apr_status_t) apr_file_open_flags_stderr(apr_file_t **thefile, 
                                                     apr_int32_t flags,
                                                     apr_pool_t *pool)
{
    apr_os_file_t fd = 2;

    return apr_os_file_put(thefile, &fd, flags | APR_FOPEN_WRITE, pool);
}


APR_DECLARE(apr_status_t) apr_file_open_flags_stdout(apr_file_t **thefile, 
                                                     apr_int32_t flags,
                                                     apr_pool_t *pool)
{
    apr_os_file_t fd = 1;

    return apr_os_file_put(thefile, &fd, flags | APR_FOPEN_WRITE, pool);
}


APR_DECLARE(apr_status_t) apr_file_open_flags_stdin(apr_file_t **thefile, 
                                                    apr_int32_t flags,
                                                    apr_pool_t *pool)
{
    apr_os_file_t fd = 0;

    return apr_os_file_put(thefile, &fd, flags | APR_FOPEN_READ, pool);
}


APR_DECLARE(apr_status_t) apr_file_open_stderr(apr_file_t **thefile, apr_pool_t *pool)
{
    return apr_file_open_flags_stderr(thefile, 0, pool);
}


APR_DECLARE(apr_status_t) apr_file_open_stdout(apr_file_t **thefile, apr_pool_t *pool)
{
    return apr_file_open_flags_stdout(thefile, 0, pool);
}


APR_DECLARE(apr_status_t) apr_file_open_stdin(apr_file_t **thefile, apr_pool_t *pool)
{
    return apr_file_open_flags_stdin(thefile, 0, pool);
}

APR_POOL_IMPLEMENT_ACCESSOR(file);



APR_DECLARE(apr_status_t) apr_file_inherit_set(apr_file_t *thefile)
{
    int rv;
    ULONG state;

    rv = DosQueryFHState(thefile->filedes, &state);

    if (rv == 0 && (state & OPEN_FLAGS_NOINHERIT) != 0) {
        rv = DosSetFHState(thefile->filedes, state & ~OPEN_FLAGS_NOINHERIT);
    }

    return APR_FROM_OS_ERROR(rv);
}



APR_DECLARE(apr_status_t) apr_file_inherit_unset(apr_file_t *thefile)
{
    int rv;
    ULONG state;

    rv = DosQueryFHState(thefile->filedes, &state);

    if (rv == 0 && (state & OPEN_FLAGS_NOINHERIT) == 0) {
/*        rv = DosSetFHState(thefile->filedes, state | OPEN_FLAGS_NOINHERIT);*/
    }

    return APR_FROM_OS_ERROR(rv);
}

APR_DECLARE(apr_status_t) apr_file_link(const char *from_path, 
                                          const char *to_path)
{
    return APR_ENOTIMPL;;
}
