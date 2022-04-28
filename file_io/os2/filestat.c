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
#include <sys/time.h>
#include "apr_strings.h"

static void FS3_to_finfo(apr_finfo_t *finfo, FILESTATUS3 *fstatus)
{
    finfo->protection = (fstatus->attrFile & FILE_READONLY) ? 0x555 : 0x777;

    if (fstatus->attrFile & FILE_DIRECTORY)
        finfo->filetype = APR_DIR;
    else
        finfo->filetype = APR_REG;
    /* XXX: No other possible types from FS3? */

    finfo->user = 0;
    finfo->group = 0;
    finfo->inode = 0;
    finfo->device = 0;
    finfo->size = fstatus->cbFile;
    finfo->csize = fstatus->cbFileAlloc;
    apr_os2_time_to_apr_time(&finfo->atime, fstatus->fdateLastAccess, 
                             fstatus->ftimeLastAccess );
    apr_os2_time_to_apr_time(&finfo->mtime, fstatus->fdateLastWrite,  
                             fstatus->ftimeLastWrite );
    apr_os2_time_to_apr_time(&finfo->ctime, fstatus->fdateCreation,   
                             fstatus->ftimeCreation );
    finfo->valid = APR_FINFO_TYPE | APR_FINFO_PROT | APR_FINFO_SIZE
                 | APR_FINFO_CSIZE | APR_FINFO_MTIME 
                 | APR_FINFO_CTIME | APR_FINFO_ATIME | APR_FINFO_LINK;
}



static apr_status_t handle_type(apr_filetype_e *ftype, HFILE file)
{
    ULONG filetype, fileattr, rc;

    rc = DosQueryHType(file, &filetype, &fileattr);

    if (rc == 0) {
        switch (filetype & 0xff) {
        case 0:
            *ftype = APR_REG;
            break;

        case 1:
            *ftype = APR_CHR;
            break;

        case 2:
            *ftype = APR_PIPE;
            break;

        default:
            /* Brian, is this correct???
             */
            *ftype = APR_UNKFILE;
            break;
        }

        return APR_SUCCESS;
    }
    return APR_FROM_OS_ERROR(rc);
}

#ifdef __KLIBC__
static apr_filetype_e filetype_from_mode(mode_t mode)
{
    apr_filetype_e type;

    switch (mode & S_IFMT) {
    case S_IFREG:
        type = APR_REG;  break;
    case S_IFDIR:
        type = APR_DIR;  break;
    case S_IFLNK:
        type = APR_LNK;  break;
    case S_IFCHR:
        type = APR_CHR;  break;
    case S_IFBLK:
        type = APR_BLK;  break;
#if defined(S_IFFIFO)
    case S_IFFIFO:
        type = APR_PIPE; break;
#endif
#if !defined(BEOS) && defined(S_IFSOCK)
    case S_IFSOCK:
        type = APR_SOCK; break;
#endif

    default:
	/* Work around missing S_IFxxx values above
         * for Linux et al.
         */
#if !defined(S_IFFIFO) && defined(S_ISFIFO)
    	if (S_ISFIFO(mode)) {
            type = APR_PIPE;
	} else
#endif
#if !defined(BEOS) && !defined(S_IFSOCK) && defined(S_ISSOCK)
    	if (S_ISSOCK(mode)) {
            type = APR_SOCK;
	} else
#endif
        type = APR_UNKFILE;
    }
    return type;
}

static void fill_out_finfo(apr_finfo_t *finfo, struct_stat *info,
                           apr_int32_t wanted)
{ 
    finfo->valid = APR_FINFO_MIN | APR_FINFO_IDENT | APR_FINFO_NLINK
                 | APR_FINFO_OWNER | APR_FINFO_PROT;
    finfo->protection = apr_unix_mode2perms(info->st_mode);
    finfo->filetype = filetype_from_mode(info->st_mode);
    finfo->user = info->st_uid;
    finfo->group = info->st_gid;
    finfo->size = info->st_size;
    finfo->inode = info->st_ino;
    finfo->device = info->st_dev;
    finfo->nlink = info->st_nlink;
    apr_time_ansi_put(&finfo->atime, info->st_atime);
#ifdef HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC
    finfo->atime += info->st_atim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
    finfo->atime += info->st_atimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_ATIME_N)
    finfo->ctime += info->st_atime_n / APR_TIME_C(1000);
#endif

    apr_time_ansi_put(&finfo->mtime, info->st_mtime);
#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
    finfo->mtime += info->st_mtim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
    finfo->mtime += info->st_mtimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
    finfo->ctime += info->st_mtime_n / APR_TIME_C(1000);
#endif

    apr_time_ansi_put(&finfo->ctime, info->st_ctime);
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
    finfo->ctime += info->st_ctim.tv_nsec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
    finfo->ctime += info->st_ctimensec / APR_TIME_C(1000);
#elif defined(HAVE_STRUCT_STAT_ST_CTIME_N)
    finfo->ctime += info->st_ctime_n / APR_TIME_C(1000);
#endif

#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
#ifdef DEV_BSIZE
    finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)DEV_BSIZE;
#else
    finfo->csize = (apr_off_t)info->st_blocks * (apr_off_t)512;
#endif
    finfo->valid |= APR_FINFO_CSIZE;
#endif
}

apr_status_t apr_file_info_get_locked(apr_finfo_t *finfo, apr_int32_t wanted,
                                      apr_file_t *thefile)
{

    struct_stat info;

    if (thefile->buffered) {
        apr_status_t rv = apr_file_flush_locked(thefile);
        if (rv != APR_SUCCESS)
            return rv;
    }

    if (fstat(thefile->filedes, &info) == 0) {
        finfo->pool = thefile->pool;
        finfo->fname = thefile->fname;
        fill_out_finfo(finfo, &info, wanted);
        return (wanted & ~finfo->valid) ? APR_INCOMPLETE : APR_SUCCESS;
    }
    else {
        return errno;
    }
}
#endif

APR_DECLARE(apr_status_t) apr_file_info_get(apr_finfo_t *finfo, apr_int32_t wanted, 
                                   apr_file_t *thefile)
{
    ULONG rc;
    FILESTATUS3 fstatus;

    if (thefile->isopen) {
        if (thefile->buffered) {
            /* XXX: flush here is not mutex protected */
            apr_status_t rv = apr_file_flush(thefile);

            if (rv != APR_SUCCESS) {
                return rv;
            }
        }

        rc = DosQueryFileInfo(thefile->filedes, FIL_STANDARD, &fstatus, sizeof(fstatus));
    }
    else
        rc = DosQueryPathInfo(thefile->fname, FIL_STANDARD, &fstatus, sizeof(fstatus));

    if (rc == 0) {
        FS3_to_finfo(finfo, &fstatus);
        finfo->fname = thefile->fname;

        if (finfo->filetype == APR_REG) {
            if (thefile->isopen) {
                return handle_type(&finfo->filetype, thefile->filedes);
            }
        } else {
            return APR_SUCCESS;
        }
    }

    finfo->protection = 0;
    finfo->filetype = APR_NOFILE;
    return APR_FROM_OS_ERROR(rc);
}

APR_DECLARE(apr_status_t) apr_file_perms_set(const char *fname, apr_fileperms_t perms)
{
    return APR_ENOTIMPL;
}

/* duplciated from dir_make_recurse.c */

#define IS_SEP(c) (c == '/' || c == '\\')

/* Remove trailing separators that don't affect the meaning of PATH. */
static const char *path_canonicalize(const char *path, apr_pool_t *pool)
{
    /* At some point this could eliminate redundant components.  For
     * now, it just makes sure there is no trailing slash. */
    apr_size_t len = strlen(path);
    apr_size_t orig_len = len;

    while ((len > 0) && IS_SEP(path[len - 1])) {
        len--;
    }

    if (len != orig_len) {
        return apr_pstrndup(pool, path, len);
    }
    else {
        return path;
    }
}

APR_DECLARE(apr_status_t) apr_stat(apr_finfo_t *finfo, const char *fname,
                              apr_int32_t wanted, apr_pool_t *cont)
{
    ULONG rc;
    FILESTATUS3 fstatus;
    
    finfo->protection = 0;
    finfo->filetype = APR_NOFILE;
    finfo->name = NULL;

    /* remove trailing / from any paths, otherwise DosQueryPathInfo will fail */
    const char *newfname = path_canonicalize(fname, cont);
    rc = DosQueryPathInfo(newfname, FIL_STANDARD, &fstatus, sizeof(fstatus));
    if (rc == 0) {
        FS3_to_finfo(finfo, &fstatus);
        finfo->fname = fname;

        if (wanted & APR_FINFO_NAME) {
            ULONG count = 1;
            HDIR hDir = HDIR_SYSTEM;
            FILEFINDBUF3 ffb;
            rc = DosFindFirst(fname, &hDir,
                              FILE_DIRECTORY|FILE_HIDDEN|FILE_SYSTEM|FILE_ARCHIVED,
                              &ffb, sizeof(ffb), &count, FIL_STANDARD);
            if (rc == 0 && count == 1) {
                finfo->name = apr_pstrdup(cont, ffb.achName);
                finfo->valid |= APR_FINFO_NAME;
            }
        }
    } else if (rc == ERROR_INVALID_ACCESS) {
        memset(finfo, 0, sizeof(apr_finfo_t));
        finfo->valid = APR_FINFO_TYPE | APR_FINFO_PROT;
        finfo->protection = 0666;
        finfo->filetype = APR_CHR;

        if (wanted & APR_FINFO_NAME) {
            finfo->name = apr_pstrdup(cont, fname);
            finfo->valid |= APR_FINFO_NAME;
        }
    } else {
        return APR_FROM_OS_ERROR(rc);
    }

#ifndef __INNOTEK_LIBC__
    return (wanted & ~finfo->valid) ? APR_INCOMPLETE : APR_SUCCESS;
#else
    return APR_SUCCESS;
#endif

}



APR_DECLARE(apr_status_t) apr_file_attrs_set(const char *fname,
                                             apr_fileattrs_t attributes,
                                             apr_fileattrs_t attr_mask,
                                             apr_pool_t *cont)
{
    FILESTATUS3 fs3;
    ULONG rc;

    /* Don't do anything if we can't handle the requested attributes */
    if (!(attr_mask & (APR_FILE_ATTR_READONLY
                       | APR_FILE_ATTR_HIDDEN)))
        return APR_SUCCESS;

    rc = DosQueryPathInfo(fname, FIL_STANDARD, &fs3, sizeof(fs3));
    if (rc == 0) {
        ULONG old_attr = fs3.attrFile;

        if (attr_mask & APR_FILE_ATTR_READONLY)
        {
            if (attributes & APR_FILE_ATTR_READONLY) {
                fs3.attrFile |= FILE_READONLY;
            } else {
                fs3.attrFile &= ~FILE_READONLY;
            }
        }

        if (attr_mask & APR_FILE_ATTR_HIDDEN)
        {
            if (attributes & APR_FILE_ATTR_HIDDEN) {
                fs3.attrFile |= FILE_HIDDEN;
            } else {
                fs3.attrFile &= ~FILE_HIDDEN;
            }
        }

        if (fs3.attrFile != old_attr) {
            rc = DosSetPathInfo(fname, FIL_STANDARD, &fs3, sizeof(fs3), 0);
        }
    }

    return APR_FROM_OS_ERROR(rc);
}


/* ### Somebody please write this! */
APR_DECLARE(apr_status_t) apr_file_mtime_set(const char *fname,
                                              apr_time_t mtime,
                                              apr_pool_t *pool)
{
    FILESTATUS3 fs3;
    ULONG rc;
    rc = DosQueryPathInfo(fname, FIL_STANDARD, &fs3, sizeof(fs3));

    if (rc) {
        return APR_FROM_OS_ERROR(rc);
    }

    apr_apr_time_to_os2_time(&fs3.fdateLastWrite, &fs3.ftimeLastWrite, mtime);

    rc = DosSetPathInfo(fname, FIL_STANDARD, &fs3, sizeof(fs3), 0);
    return APR_FROM_OS_ERROR(rc);
}
