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

#include "apr_arch_networkio.h"
#include "apr_errno.h"
#include "apr_general.h"
#include "apr_network_io.h"
#include "apr_lib.h"
#include <sys/time.h>
#include <stdlib.h>
int	 usleep(useconds_t);

APR_DECLARE(apr_status_t) apr_socket_send(apr_socket_t *sock, const char *buf,
                                          apr_size_t *len)
{
    apr_ssize_t rv;
    int fds, err = 0;

#ifndef __INNOTEK_LIBC__
    if (*len > 65536) {
        *len = 65536;
    }
#endif 

    do {
        if (!sock->nonblock || err == SOCEWOULDBLOCK) {
            fds = sock->socketdes;
            rv = select(&fds, 0, 1, 0, sock->timeout >= 0 ? sock->timeout/1000 : -1);

            if (rv != 1) {
                *len = 0;
                err = sock_errno();

                if (rv == 0) {
                    return APR_TIMEUP;
		}
                if (err == SOCEINTR)
                    continue;

                return APR_OS2_STATUS(err);
            }
        }

        rv = send(sock->socketdes, buf, (*len), 0);
        err = rv < 0 ? sock_errno() : 0;
    } while (err == SOCEINTR || err == SOCEWOULDBLOCK);

    if (err) {
        *len = 0;
        return APR_OS2_STATUS(err);
    }

    (*len) = rv;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_socket_recv(apr_socket_t *sock, char *buf,
                                          apr_size_t *len)
{
    apr_ssize_t rv;
    int fds, err = 0;
#if 1
    if (!sock->nonblock)
        apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
#endif

    do {
        if (!sock->nonblock || (err == SOCEWOULDBLOCK && sock->timeout != 0)) {
            fds = sock->socketdes;
            rv = select(&fds, 1, 0, 0, sock->timeout >= 0 ? sock->timeout/1000 : -1);

            if (rv != 1) {
                *len = 0;
                err = sock_errno();

                if (rv == 0) {
                        return APR_TIMEUP;
		}

                if (err == SOCEINTR)
                    continue;

                return APR_OS2_STATUS(err);
            }
        }

        rv = recv(sock->socketdes, buf, (*len), 0);
        err = rv < 0 ? sock_errno() : 0;
    } while (err == SOCEINTR || (err == SOCEWOULDBLOCK && sock->timeout != 0));

    if (err) {
        *len = 0;
        return APR_OS2_STATUS(err);
    }

    (*len) = rv;
    return rv == 0 ? APR_EOF : APR_SUCCESS;
}


APR_DECLARE(apr_status_t) apr_socket_sendv(apr_socket_t *sock, 
                                           const struct iovec *vec, 
                                           apr_int32_t nvec, apr_size_t *len)
{
    apr_status_t rv;
    struct iovec *tmpvec;
    int fds, err = 0;
    int nv_tosend, total = 0;
    int retries = 0;

#ifndef __INNOTEK_LIBC__
    /* Make sure writev() only gets fed 64k at a time */
    for ( nv_tosend = 0; nv_tosend < nvec && total + vec[nv_tosend].iov_len < 65536; nv_tosend++ ) {
        total += vec[nv_tosend].iov_len;
    }
#else
    /* workaround for writev() not required with libc */
    for ( nv_tosend = 0; nv_tosend < nvec; nv_tosend++ ) {
        total += vec[nv_tosend].iov_len;
    }
#endif
    tmpvec = alloca(sizeof(struct iovec) * nv_tosend);
    memcpy(tmpvec, vec, sizeof(struct iovec) * nv_tosend);

    do {
        if (!sock->nonblock || err == SOCEWOULDBLOCK) {
            fds = sock->socketdes;
            rv = select(&fds, 0, 1, 0, sock->timeout >= 0 ? sock->timeout/1000 : -1);

#if 1
// OS/2 select sometimes fails, retry  it
            if (rv == -1) {
                rv = select(&fds, 0, 1, 0, sock->timeout >= 0 ? sock->timeout/1000 : -1);                
             }
// OS/2 select sometimes fails, retry  it one last time, after a 500ms sleep
            if (rv == -1) {
                usleep(500000);
                rv = select(&fds, 0, 1, 0, sock->timeout >= 0 ? sock->timeout/1000 : -1);                
             }

#endif
            if (rv != 1) {
                *len = 0;
                err = sock_errno();

                if (rv == 0) {
		    if (err == SOCEWOULDBLOCK && ++retries < 10) {
		      apr_sleep(100 * 1000);        /* 100 mSec */
		      continue;			/* Retry select */
		    }
                    return APR_TIMEUP;
		}

                if (err == SOCEINTR)
                    continue;

                return APR_OS2_STATUS(err);
            }
        }

        rv = writev(sock->socketdes, tmpvec, nv_tosend);
        err = rv < 0 ? sock_errno() : 0;
        /* 2015-02-06 SHL Allow SOCENOBUFS and delay if out of resources */
        if (err == SOCEWOULDBLOCK || err == SOCENOBUFS)
          apr_sleep(100 * 1000);        /* 100 mSec */
    } while (err == SOCEINTR || err == SOCEWOULDBLOCK || err == SOCENOBUFS);

    if (err) {
        *len = 0;
        return APR_OS2_STATUS(err);
    }

    *len = rv;
    return APR_SUCCESS;
}
