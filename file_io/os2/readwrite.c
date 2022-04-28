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

#define INCL_DOS
#define INCL_DOSERRORS

#include "apr_arch_file_io.h"
#include "apr_file_io.h"
#include "apr_lib.h"
#include "apr_strings.h"

#include <malloc.h>
#include <umalloc.h>
#include <stdlib.h>
#include <unistd.h>

APR_DECLARE(apr_status_t) apr_file_read(apr_file_t *thefile, void *buf, apr_size_t *len)
{
    ULONG rc = 0;
    ULONG bytes_read = 0;

    if (*len <= 0) {
        *len = 0;
        return APR_SUCCESS;
    }
#if 1
    /* Handle the ungetchar if there is one */
    /* copied from win32/readwrite.c */
    if (thefile->ungetchar != -1) {
        bytes_read = 1;
        *(char *)buf = (char)thefile->ungetchar;
        buf = (char *)buf + 1;
        (*len)--;
        thefile->ungetchar = -1;
        if (*len == 0) {
            *len = bytes_read;
            return APR_SUCCESS;
        }
    }
#endif
    if (thefile->buffered) {
        char *pos = (char *)buf;
        ULONG blocksize;
        ULONG size = *len;

        apr_thread_mutex_lock(thefile->mutex);

        if (thefile->direction == 1) {
            int rv = apr_file_flush(thefile);

            if (rv != APR_SUCCESS) {
                apr_thread_mutex_unlock(thefile->mutex);
                return rv;
            }

            thefile->bufpos = 0;
            thefile->direction = 0;
            thefile->dataRead = 0;
        }

        while (rc == 0 && size > 0) {
            if (thefile->bufpos >= thefile->dataRead) {
                ULONG bytes_read;
                rc = DosRead(thefile->filedes, thefile->buffer,
                             thefile->bufsize, &bytes_read);
                if (bytes_read == 0) {
                    if (rc == 0)
                        thefile->eof_hit = TRUE;
                    break;
                }
		else {
	                thefile->dataRead = bytes_read;
                thefile->filePtr += thefile->dataRead;
                thefile->bufpos = 0;
            }
            }

            blocksize = size > thefile->dataRead - thefile->bufpos ? thefile->dataRead - thefile->bufpos : size;
            memcpy(pos, thefile->buffer + thefile->bufpos, blocksize);
            thefile->bufpos += blocksize;
            pos += blocksize;
            size -= blocksize;
        }

        *len = rc == 0 ? pos - (char *)buf : 0;
        apr_thread_mutex_unlock(thefile->mutex);

        if (*len == 0 && rc == 0 && thefile->eof_hit) {
            thefile->eof_hit = TRUE;
            return APR_EOF;
        }

        return APR_FROM_OS_ERROR(rc);
    } else {
        /* Unbuffered i/o */
        unsigned long nbytes;

        // 2014-11-11 SHL reworked to support ERROR_MORE_DATA etc.
	for (;;) {
	    unsigned long post_count;
        if (thefile->pipe)
		DosResetEventSem(thefile->pipeSem, &post_count);
	    rc = DosRead(thefile->filedes, buf, *len, &nbytes);
	    if (!thefile->pipe)
		break;			// Blocking i/o - we are done
	    if (rc == ERROR_MORE_DATA) {
		// Buffer is full - caller will read more eventually
		rc = NO_ERROR;
		break;
            }
	    if (thefile->timeout == 0)
		break;
	    if (rc != ERROR_NO_DATA)
		break;
	    rc = DosWaitEventSem(thefile->pipeSem, thefile->timeout >= 0 ? thefile->timeout / 1000 : SEM_INDEFINITE_WAIT);
	    if (rc == ERROR_TIMEOUT) {
		*len = 0;
                return APR_TIMEUP;
            }
	} // for

        if (rc) {
	    *len = 0;
            return APR_FROM_OS_ERROR(rc);
        }

	*len = nbytes;
        
	if (nbytes == 0) {
            thefile->eof_hit = TRUE;
            return APR_EOF;
        }

        return APR_SUCCESS;
    }
}



APR_DECLARE(apr_status_t) apr_file_write(apr_file_t *thefile, const void *buf, apr_size_t *nbytes)
{
    ULONG rc = 0;
    ULONG byteswritten;

    if (!thefile->isopen) {
        *nbytes = 0;
        return APR_EBADF;
    }

    if (thefile->buffered) {
        char *pos = (char *)buf;
        int blocksize;
        int size = *nbytes;
 
       apr_thread_mutex_lock(thefile->mutex);

        if ( thefile->direction == 0 ) {
            /* Position file pointer for writing at the offset we are logically reading from */
            ULONG offset = thefile->filePtr - thefile->dataRead + thefile->bufpos;
            if (offset != thefile->filePtr)
                DosSetFilePtr(thefile->filedes, offset, FILE_BEGIN, (unsigned long *) &thefile->filePtr );
            thefile->bufpos = thefile->dataRead = 0;
            thefile->direction = 1;
        }

        while (rc == 0 && size > 0) {
	    if (thefile->bufpos == thefile->bufsize){ /* write buffer is full */
                /* XXX bug; - rc is double-transformed os->apr below */
                rc = apr_file_flush(thefile);
		}
	    blocksize = size > thefile->bufsize - thefile->bufpos ? 
	                       thefile->bufsize - thefile->bufpos : size;
            memcpy(thefile->buffer + thefile->bufpos, pos, blocksize);
            thefile->bufpos += blocksize;
            pos += blocksize;
            size -= blocksize;
        }

        apr_thread_mutex_unlock(thefile->mutex);
        return APR_FROM_OS_ERROR(rc);
    } else {
        void *pvBuf_safe = NULL;

        /*
         * Devices doesn't like getting high addresses.
         */
        if (isatty(thefile->filedes)
            &&  (unsigned)buf >= 512*1024*1024)
        {
            if (*nbytes > 256)
            {   /* use heap for large buffers */
                pvBuf_safe = _tmalloc(*nbytes);
                if (!pvBuf_safe)
                {
                    errno = ENOMEM;
	            return APR_FROM_OS_ERROR(errno);
                }
                memcpy(pvBuf_safe, buf, *nbytes);
                buf = pvBuf_safe;
            }
            else
            {   /* use stack for small buffers */
                pvBuf_safe = alloca(*nbytes);
                memcpy(pvBuf_safe, buf, *nbytes);
                buf = pvBuf_safe;
                pvBuf_safe = NULL;
            }
        }

        if (thefile->flags & APR_FOPEN_APPEND) {
            FILELOCK all = { 0, 0x7fffffff };
            ULONG newpos;

            rc = DosSetFileLocks(thefile->filedes, NULL, &all, -1, 0);
 
            if (rc == 0) {
                rc = DosSetFilePtr(thefile->filedes, 0, FILE_END, &newpos);
                if (rc == 0) {
	            rc = DosWrite(thefile->filedes, (PVOID)buf, *nbytes, &byteswritten);
                }
                DosSetFileLocks(thefile->filedes, &all, NULL, -1, 0);
            }
        } else {
            rc = DosWrite(thefile->filedes, buf, *nbytes, &byteswritten);
        }

        if (pvBuf_safe)
            free(pvBuf_safe);

        if (rc) {
            *nbytes = 0;
            return APR_FROM_OS_ERROR(rc);
        }

        *nbytes = byteswritten;
        return APR_SUCCESS;
    }
}



APR_DECLARE(apr_status_t) apr_file_writev(apr_file_t *thefile, const struct iovec *vec, apr_size_t nvec, apr_size_t *nbytes)
{
    apr_status_t rv = APR_SUCCESS;
    apr_size_t i;
    apr_size_t bwrote = 0;
    char *buf;

    *nbytes = 0;
    for (i = 0; i < nvec; i++) {
	buf = vec[i].iov_base;
	bwrote = vec[i].iov_len;
	rv = apr_file_write(thefile, buf, &bwrote);
	*nbytes += bwrote;
        if (rv != APR_SUCCESS) {
	    break;
        }
    }
    return rv;
    }



APR_DECLARE(apr_status_t) apr_file_putc(char ch, apr_file_t *thefile)
{
    apr_size_t nbytes = 1;

    return apr_file_write(thefile, &ch, &nbytes);
    }


    
APR_DECLARE(apr_status_t) apr_file_ungetc(char ch, apr_file_t *thefile)
{
    thefile->ungetchar = (unsigned char)ch;
    return APR_SUCCESS; 
}


APR_DECLARE(apr_status_t) apr_file_getc(char *ch, apr_file_t *thefile)
{
    apr_size_t nbytes = 1;

    return apr_file_read(thefile, ch, &nbytes);

    }


    
APR_DECLARE(apr_status_t) apr_file_puts(const char *str, apr_file_t *thefile)
{
    return apr_file_write_full(thefile, str, strlen(str), NULL);
    }
    


apr_status_t apr_file_flush_locked(apr_file_t *thefile)
{
    apr_status_t rv = APR_SUCCESS;

    if (thefile->direction == 1 && thefile->bufpos) {
	apr_ssize_t written;

	do {
	    written = write(thefile->filedes, thefile->buffer, thefile->bufpos);
	} while (written == -1 && errno == EINTR);
	if (written == -1) {
	    rv = errno;
	} else {
	    thefile->filePtr += written;
	    thefile->bufpos = 0;
}
    }

    return rv;
}



APR_DECLARE(apr_status_t) apr_file_flush(apr_file_t *thefile)
{
    if (thefile->buffered) {
        ULONG written = 0;
        int rc = 0;

        if (thefile->direction == 1 && thefile->bufpos) {
            rc = DosWrite(thefile->filedes, thefile->buffer, thefile->bufpos, &written);
            thefile->filePtr += written;

            if (rc == 0)
                thefile->bufpos = 0;
        }

        return APR_FROM_OS_ERROR(rc);
    } else {
        /* There isn't anything to do if we aren't buffering the output
         * so just return success.
         */
        return APR_SUCCESS;
    }
}

APR_DECLARE(apr_status_t) apr_file_sync(apr_file_t *thefile)
{
    return APR_ENOTIMPL;
}

APR_DECLARE(apr_status_t) apr_file_datasync(apr_file_t *thefile)
{
    return APR_ENOTIMPL;
}

APR_DECLARE(apr_status_t) apr_file_gets(char *str, int len, apr_file_t *thefile)
{
    apr_size_t readlen;
    apr_status_t rv = APR_SUCCESS;
    int i;    

    for (i = 0; i < len-1; i++) {
        readlen = 1;
        rv = apr_file_read(thefile, str+i, &readlen);

        if (rv != APR_SUCCESS) {
            break;
        }

        if (readlen != 1) {
            rv = APR_EOF;
            break;
        }
        
        if (str[i] == '\n') {
            i++;
            break;
        }
    }
    str[i] = 0;
    if (i > 0) {
        /* we stored chars; don't report EOF or any other errors;
         * the app will find out about that on the next call
         */
        return APR_SUCCESS;
    }
    return rv;
}


/* Pull from unix code to start a sync up */
#if (defined(__INNOTEK_LIBC__) || defined(__WATCOMC__) )

struct apr_file_printf_data {
    apr_vformatter_buff_t vbuff;
    apr_file_t *fptr;
    char *buf;
};

static int file_printf_flush(apr_vformatter_buff_t *buff)
{
    struct apr_file_printf_data *data = (struct apr_file_printf_data *)buff;

    if (apr_file_write_full(data->fptr, data->buf,
	                    data->vbuff.curpos - data->buf, NULL)) {
	return -1;
    }

    data->vbuff.curpos = data->buf;
    return 0;
}

APR_DECLARE_NONSTD(int) apr_file_printf(apr_file_t *fptr, 
                                        const char *format, ...)
{
    struct apr_file_printf_data data;
    va_list ap;
    int count;

    /* don't really need a HUGE_STRING_LEN anymore */
    data.buf = malloc(HUGE_STRING_LEN);
    if (data.buf == NULL) {
	return -1;
    }
    data.vbuff.curpos = data.buf;
    data.vbuff.endpos = data.buf + HUGE_STRING_LEN;
    data.fptr = fptr;
    va_start(ap, format);
    count = apr_vformatter(file_printf_flush,
	                   (apr_vformatter_buff_t *)&data, format, ap);
    /* apr_vformatter does not call flush for the last bits */
    if (count >= 0) file_printf_flush((apr_vformatter_buff_t *)&data);

    va_end(ap);

    free(data.buf);

    return count;
}

#else

APR_DECLARE_NONSTD(int) apr_file_printf(apr_file_t *fptr, 
	                                const char *format, ...)
{
    int cc;
    va_list ap;
    char *buf;
    int len;

    buf = malloc(HUGE_STRING_LEN);
    if (buf == NULL) {
        return 0;
    }
    va_start(ap, format);
    len = apr_vsnprintf(buf, HUGE_STRING_LEN, format, ap);
    cc = apr_file_puts(buf, fptr);
    va_end(ap);
    free(buf);
    return (cc == APR_SUCCESS) ? len : -1;
}
#endif


apr_status_t apr_file_check_read(apr_file_t *fd)
{
    int rc;

    if (!fd->pipe)
        return APR_SUCCESS; /* Not a pipe, assume no waiting */

    rc = DosWaitEventSem(fd->pipeSem, SEM_IMMEDIATE_RETURN);

    if (rc == ERROR_TIMEOUT) {
        return APR_TIMEUP;
    }

    return APR_FROM_OS_ERROR(rc);
}
