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

#define INCL_DOSERRORS
#include "apr_arch_file_io.h"
#include "apr_file_io.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include <string.h>
#include <process.h>

static apr_status_t file_pipe_create(apr_file_t **in, apr_file_t **out,
        apr_pool_t *pool_in, apr_pool_t *pool_out)
{
    ULONG filedes[2];
    ULONG rc, action;
    static int id = 0;
    char pipename[50];
    ULONG    CurMaxFH      = 0;          /* Current count of handles         */
    LONG     ReqCount      = 0;          /* Number to adjust file handles    */

    sprintf(pipename, "/pipe/%d.%d", getpid(), id++);
    rc = DosCreateNPipe(pipename, filedes, NP_ACCESS_INBOUND, NP_NOWAIT|0x01, 4096, 4096, 0);

    if (rc)
        return APR_FROM_OS_ERROR(rc);

    rc = DosConnectNPipe(filedes[0]);

    if (rc && rc != ERROR_PIPE_NOT_CONNECTED) {
        DosClose(filedes[0]);
        return APR_FROM_OS_ERROR(rc);
    }

    rc = DosOpen (pipename, filedes+1, &action, 0, FILE_NORMAL,
                  OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
                  OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE,
                  NULL);
    if (rc == ERROR_TOO_MANY_OPEN_FILES) {
	rc = DosSetRelMaxFH(&ReqCount,     /* Using 0 here will return the       */
                      &CurMaxFH);    /* current number of file handles     */
	ReqCount      = 10L;         /* Want 10 more file handles         */
	rc = DosSetRelMaxFH(&ReqCount,&CurMaxFH);     /* Change handle maximum */

	rc = DosOpen (pipename, filedes+1, &action, 0, FILE_NORMAL,
                  OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
                  OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYREADWRITE,
                  NULL);

    }    

    if (rc) {
        DosClose(filedes[0]);
        return APR_FROM_OS_ERROR(rc);
    }

    // 2019-06-03 SHL Ensure apr_file_t initialized to zeros
    // (*in) = (apr_file_t *)apr_palloc(pool_in, sizeof(apr_file_t));
    (*in) = (apr_file_t *)apr_pcalloc(pool_in, sizeof(apr_file_t));
    rc = DosCreateEventSem(NULL, &(*in)->pipeSem, DC_SEM_SHARED, FALSE);

    if (rc) {
        DosClose(filedes[0]);
        DosClose(filedes[1]);
        return APR_FROM_OS_ERROR(rc);
    }

    rc = DosSetNPipeSem(filedes[0], (HSEM)(*in)->pipeSem, 1);

    if (!rc) {
        rc = DosSetNPHState(filedes[0], NP_WAIT);
    }

    if (rc) {
        DosClose(filedes[0]);
        DosClose(filedes[1]);
        DosCloseEventSem((*in)->pipeSem);
        return APR_FROM_OS_ERROR(rc);
    }

    (*in)->pool = pool_in;
    (*in)->filedes = filedes[0];
    (*in)->fname = apr_pstrdup(pool_in, pipename);
    (*in)->isopen = TRUE;
    (*in)->buffered = FALSE;
    (*in)->pipe = 1;
    (*in)->timeout = -1;
    (*in)->ungetchar = -1;
    (*in)->blocking = BLK_ON;
    apr_pool_cleanup_register(pool_in, *in, apr_file_cleanup,
            apr_pool_cleanup_null);

    // 2019-06-03 SHL Ensure apr_file_t initialized to zeros
    // (*out) = (apr_file_t *)apr_palloc(pool_out, sizeof(apr_file_t));
    (*out) = (apr_file_t *)apr_pcalloc(pool_out, sizeof(apr_file_t));
    (*out)->pool = pool_out;
    (*out)->filedes = filedes[1];
    (*out)->fname = apr_pstrdup(pool_out, pipename);
    (*out)->isopen = TRUE;
    (*out)->buffered = FALSE;
    (*out)->pipe = 2;			// 2014-11-17 SHL mark as client pipe
    (*out)->ungetchar = -1;
    (*out)->timeout = -1;
    (*out)->blocking = BLK_ON;
    apr_pool_cleanup_register(pool_out, *out, apr_file_cleanup,
            apr_pool_cleanup_null);

    return APR_SUCCESS;
}

static void file_pipe_block(apr_file_t **in, apr_file_t **out,
        apr_int32_t blocking)
{
    switch (blocking) {
    case APR_FULL_BLOCK:
        break;
    case APR_READ_BLOCK:
        apr_file_pipe_timeout_set(*out, 0);
        break;
    case APR_WRITE_BLOCK:
        apr_file_pipe_timeout_set(*in, 0);
        break;
    default:
        apr_file_pipe_timeout_set(*out, 0);
        apr_file_pipe_timeout_set(*in, 0);
        break;
    }
}

APR_DECLARE(apr_status_t) apr_file_pipe_create(apr_file_t **in,
                                               apr_file_t **out,
                                               apr_pool_t *pool)
{
    return file_pipe_create(in, out, pool, pool);
}

APR_DECLARE(apr_status_t) apr_file_pipe_create_ex(apr_file_t **in, 
                                                  apr_file_t **out, 
                                                  apr_int32_t blocking,
                                                  apr_pool_t *pool)
{
    apr_status_t status;

    if ((status = file_pipe_create(in, out, pool, pool)) != APR_SUCCESS)
        return status;

    file_pipe_block(in, out, blocking);

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_pipe_create_pools(apr_file_t **in,
                                                     apr_file_t **out,
                                                     apr_int32_t blocking,
                                                     apr_pool_t *pool_in,
                                                     apr_pool_t *pool_out)
{
    apr_status_t status;

    if ((status = file_pipe_create(in, out, pool_in, pool_out)) != APR_SUCCESS)
        return status;

    file_pipe_block(in, out, blocking);

    return APR_SUCCESS;
}
    
    
APR_DECLARE(apr_status_t) apr_file_namedpipe_create(const char *filename, apr_fileperms_t perm, apr_pool_t *pool)
{
    /* Not yet implemented, interface not suitable */
    return APR_ENOTIMPL;
} 

 

APR_DECLARE(apr_status_t) apr_file_pipe_timeout_set(apr_file_t *thepipe, apr_interval_time_t timeout)
{
    int rc = 0;

    if (thepipe->pipe == 1) {
        thepipe->timeout = timeout;

        if ((thepipe->timeout >= 0) && (thepipe->blocking != BLK_OFF) ) {
                thepipe->blocking = BLK_OFF;
            rc = DosSetNPHState(thepipe->filedes, NP_NOWAIT);
        }
        else if ( (thepipe->timeout == -1) && (thepipe->blocking != BLK_ON) ) {
                thepipe->blocking = BLK_ON;
            rc = DosSetNPHState(thepipe->filedes, NP_WAIT);
    }

        return APR_FROM_OS_ERROR(rc);

    } else return APR_EINVAL;

}



APR_DECLARE(apr_status_t) apr_file_pipe_timeout_get(apr_file_t *thepipe, apr_interval_time_t *timeout)
{
    if (thepipe->pipe == 1) {
        *timeout = thepipe->timeout;
        return APR_SUCCESS;
    }
    return APR_EINVAL;
}



APR_DECLARE(apr_status_t) apr_os_pipe_put_ex(apr_file_t **file,
                                             apr_os_file_t *thefile,
                                             int register_cleanup,
                                             apr_pool_t *pool)
{
    (*file) = apr_pcalloc(pool, sizeof(apr_file_t));
    (*file)->pool = pool;
    (*file)->isopen = TRUE;
    (*file)->pipe = 1;
    (*file)->blocking = BLK_UNKNOWN; /* app needs to make a timeout call */
    (*file)->timeout = -1;
    (*file)->ungetchar = -1;
    (*file)->filedes = *thefile;

    if (register_cleanup) {
        apr_pool_cleanup_register(pool, *file, apr_file_cleanup,
                                  apr_pool_cleanup_null);
    }

    return APR_SUCCESS;
}



APR_DECLARE(apr_status_t) apr_os_pipe_put(apr_file_t **file,
                                          apr_os_file_t *thefile,
                                          apr_pool_t *pool)
{
    return apr_os_pipe_put_ex(file, thefile, 0, pool);
}
