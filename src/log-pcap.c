/* Copyright (C) 2007-2014 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */


/**
 * \file
 *
 * \author William Metcalf <William.Metcalf@gmail.com>
 * \author Victor Julien <victor@inliniac.net>
 *
 * Pcap packet logging module.
 */

#include "suricata-common.h"
#include "debug.h"
#include "detect.h"
#include "flow.h"
#include "conf.h"

#include "threads.h"
#include "threadvars.h"
#include "tm-threads.h"

#include "util-unittest.h"
#include "log-pcap.h"
#include "decode-ipv4.h"

#include "util-error.h"
#include "util-debug.h"
#include "util-time.h"
#include "util-byte.h"
#include "util-misc.h"
#include "util-cpu.h"

#include "source-pcap.h"

#include "output.h"

#include "queue.h"

#define DEFAULT_LOG_FILENAME            "pcaplog"
#define MODULE_NAME                     "PcapLog"
#define MIN_LIMIT                       1 * 1024 * 1024
#define DEFAULT_LIMIT                   100 * 1024 * 1024
#define DEFAULT_FILE_LIMIT              0

#define LOGMODE_NORMAL                  0
#define LOGMODE_SGUIL                   1

#define RING_BUFFER_MODE_DISABLED       0
#define RING_BUFFER_MODE_ENABLED        1

#define TS_FORMAT_SEC                   0
#define TS_FORMAT_USEC                  1

#define USE_STREAM_DEPTH_DISABLED       0
#define USE_STREAM_DEPTH_ENABLED        1

typedef struct PcapFileName_ {
    char *filename;
    char *dirname;
    TAILQ_ENTRY(PcapFileName_) next; /**< Pointer to next Pcap File for tailq. */
} PcapFileName;

typedef struct PcapLogProfileData_ {
    uint64_t total;
    uint64_t cnt;
} PcapLogProfileData;

/**
 * PcapLog thread vars
 *
 * Used for storing file options.
 */
typedef struct PcapLogData_ {
    uint64_t size_current;      /**< file current size */
    uint64_t size_limit;        /**< file size limit */
    struct pcap_pkthdr *h;      /**< pcap header struct */
    char *filename;             /**< current filename */
    uint32_t file_cnt;          /**< count of pcap files we currently have */
    uint32_t max_files;         /**< maximum files to use in ring buffer mode */
    uint64_t pkt_cnt;		    /**< total number of packets */
    int prev_day;               /**< last day, for finding out when */
    pcap_t *pcap_dead_handle;   /**< pcap_dumper_t needs a handle */
    pcap_dumper_t *pcap_dumper; /**< actually writes the packets */
    char *prefix;               /**< filename prefix */
    int mode;                   /**< normal or sguil */
    int use_ringbuffer;         /**< ring buffer mode enabled or disabled */
    int timestamp_format;       /**< timestamp format sec or usec */
    int use_stream_depth;       /**< use stream depth i.e. ignore packets that reach limit */
    char dir[PATH_MAX];         /**< pcap log directory */

    int reported;
    PcapLogProfileData profile_close;
    PcapLogProfileData profile_open;
    PcapLogProfileData profile_write;
    PcapLogProfileData profile_rotate;
    PcapLogProfileData profile_handles; // open handles
    uint64_t profile_data_size; /**< track in bytes how many bytes we wrote */

    SCMutex plog_lock;
    TAILQ_HEAD(, PcapFileName_) pcap_file_list;
} PcapLogData;

static int PcapLogOpenFileCtx(PcapLogData *);
static TmEcode PcapLog(ThreadVars *, Packet *, void *, PacketQueue *, PacketQueue *);
static TmEcode PcapLogDataInit(ThreadVars *, void *, void **);
static TmEcode PcapLogDataDeinit(ThreadVars *, void *);
static void PcapLogFileDeInitCtx(OutputCtx *);
static OutputCtx *PcapLogInitCtx(ConfNode *);
static void PcapLogProfilingDump(PcapLogData *);

void TmModulePcapLogRegister(void)
{
    tmm_modules[TMM_PCAPLOG].name = MODULE_NAME;
    tmm_modules[TMM_PCAPLOG].ThreadInit = PcapLogDataInit;
    tmm_modules[TMM_PCAPLOG].Func = PcapLog;
    tmm_modules[TMM_PCAPLOG].ThreadDeinit = PcapLogDataDeinit;
    tmm_modules[TMM_PCAPLOG].RegisterTests = NULL;

    OutputRegisterModule(MODULE_NAME, "pcap-log", PcapLogInitCtx);

    return;
}

#define PCAPLOG_PROFILE_START \
    uint64_t pcaplog_profile_ticks = UtilCpuGetTicks()

#define PCAPLOG_PROFILE_END(prof) \
    (prof).total += (UtilCpuGetTicks() - pcaplog_profile_ticks); \
    (prof).cnt++

/**
 * \brief Function to close pcaplog file
 *
 * \param t Thread Variable containing  input/output queue, cpu affinity etc.
 * \param pl PcapLog thread variable.
 */
static int PcapLogCloseFile(ThreadVars *t, PcapLogData *pl)
{
    if (pl != NULL) {
        PCAPLOG_PROFILE_START;

        if (pl->pcap_dumper != NULL)
            pcap_dump_close(pl->pcap_dumper);
        pl->size_current = 0;
        pl->pcap_dumper = NULL;

        if (pl->pcap_dead_handle != NULL)
            pcap_close(pl->pcap_dead_handle);
        pl->pcap_dead_handle = NULL;

        PCAPLOG_PROFILE_END(pl->profile_close);
    }

    return 0;
}

static void PcapFileNameFree(PcapFileName *pf)
{
    if (pf != NULL) {
        if (pf->filename != NULL) {
            SCFree(pf->filename);
        }
        if (pf->dirname != NULL) {
            SCFree(pf->dirname);
        }
        SCFree(pf);
    }

    return;
}

/**
 * \brief Function to rotate pcaplog file
 *
 * \param t Thread Variable containing  input/output queue, cpu affinity etc.
 * \param pl PcapLog thread variable.
 *
 * \retval 0 on succces
 * \retval -1 on failure
 */
static int PcapLogRotateFile(ThreadVars *t, PcapLogData *pl)
{
    PcapFileName *pf;
    PcapFileName *pfnext;

    PCAPLOG_PROFILE_START;

    if (PcapLogCloseFile(t,pl) < 0) {
        SCLogDebug("PcapLogCloseFile failed");
        return -1;
    }

    if (pl->use_ringbuffer == RING_BUFFER_MODE_ENABLED && pl->file_cnt >= pl->max_files) {
        pf = TAILQ_FIRST(&pl->pcap_file_list);
        SCLogDebug("Removing pcap file %s", pf->filename);

        if (remove(pf->filename) != 0) {
            // VJ remove can fail because file is already gone
            //LogWarning(SC_ERR_PCAP_FILE_DELETE_FAILED,
            //           "failed to remove log file %s: %s",
            //           pf->filename, strerror( errno ));
        }

        /* Remove directory if Sguil mode and no files left in sguil dir */
        if (pl->mode == LOGMODE_SGUIL) {
            pfnext = TAILQ_NEXT(pf,next);

            if (strcmp(pf->dirname, pfnext->dirname) == 0) {
                SCLogDebug("Current entry dir %s and next entry %s "
                        "are equal: not removing dir",
                        pf->dirname, pfnext->dirname);
            } else {
                SCLogDebug("current entry %s and %s are "
                        "not equal: removing dir",
                        pf->dirname, pfnext->dirname);

                if (remove(pf->dirname) != 0) {
                    SCLogWarning(SC_ERR_PCAP_FILE_DELETE_FAILED,
                            "failed to remove sguil log %s: %s",
                            pf->dirname, strerror( errno ));
                }
            }
        }

        TAILQ_REMOVE(&pl->pcap_file_list, pf, next);
        PcapFileNameFree(pf);
        pl->file_cnt--;
    }

    if (PcapLogOpenFileCtx(pl) < 0) {
        SCLogError(SC_ERR_FOPEN, "opening new pcap log file failed");
        return -1;
    }
    pl->file_cnt++;
    SCLogDebug("file_cnt %u", pl->file_cnt);

    PCAPLOG_PROFILE_END(pl->profile_rotate);
    return 0;
}

static int PcapLogOpenHandles(PcapLogData *pl, Packet *p) {
    PCAPLOG_PROFILE_START;

    SCLogDebug("Setting pcap-log link type to %u", p->datalink);

    if (pl->pcap_dead_handle == NULL) {
        if ((pl->pcap_dead_handle = pcap_open_dead(p->datalink,
                        -1)) == NULL) {
            SCLogDebug("Error opening dead pcap handle");
            return TM_ECODE_FAILED;
        }
    }

    if (pl->pcap_dumper == NULL) {
        if ((pl->pcap_dumper = pcap_dump_open(pl->pcap_dead_handle,
                        pl->filename)) == NULL) {
            SCLogInfo("Error opening dump file %s", pcap_geterr(pl->pcap_dead_handle));
            return TM_ECODE_FAILED;
        }
    }

    PCAPLOG_PROFILE_END(pl->profile_handles);
    return TM_ECODE_OK;
}

/**
 * \brief Pcap logging main function
 *
 * \param t threadvar
 * \param p packet
 * \param data thread module specific data
 * \param pq pre-packet-queue
 * \param postpq post-packet-queue
 *
 * \retval TM_ECODE_OK on succes
 * \retval TM_ECODE_FAILED on serious error
 */
static TmEcode PcapLog (ThreadVars *t, Packet *p, void *data, PacketQueue *pq,
                 PacketQueue *postpq)
{
    size_t len;
    int rotate = 0;
    int ret = 0;

    PcapLogData *pl = (PcapLogData *)data;

    if ((p->flags & PKT_PSEUDO_STREAM_END) ||
        ((p->flags & PKT_STREAM_NOPCAPLOG) &&
         (pl->use_stream_depth == USE_STREAM_DEPTH_ENABLED)) ||
        (IS_TUNNEL_PKT(p) && !IS_TUNNEL_ROOT_PKT(p)))
    {
        return TM_ECODE_OK;
    }

    SCMutexLock(&pl->plog_lock);

    pl->pkt_cnt++;
    pl->h->ts.tv_sec = p->ts.tv_sec;
    pl->h->ts.tv_usec = p->ts.tv_usec;
    pl->h->caplen = GET_PKT_LEN(p);
    pl->h->len = GET_PKT_LEN(p);
    len = sizeof(*pl->h) + GET_PKT_LEN(p);

    if (pl->filename == NULL) {
        ret = PcapLogOpenFileCtx(pl);
        if (ret < 0) {
            SCMutexUnlock(&pl->plog_lock);
            return TM_ECODE_FAILED;
        }
        SCLogDebug("Opening PCAP log file %s", pl->filename);
    }

    if (pl->mode == LOGMODE_SGUIL) {
        struct tm local_tm;
        struct tm *tms = SCLocalTime(p->ts.tv_sec, &local_tm);
        if (tms->tm_mday != pl->prev_day) {
            rotate = 1;
            pl->prev_day = tms->tm_mday;
        }
    }

    if ((pl->size_current + len) > pl->size_limit || rotate) {
        if (PcapLogRotateFile(t,pl) < 0) {
            SCMutexUnlock(&pl->plog_lock);
            SCLogDebug("rotation of pcap failed");
            return TM_ECODE_FAILED;
        }
    }

    /* XXX pcap handles, nfq, pfring, can only have one link type ipfw? we do
     * this here as we don't know the link type until we get our first packet */
    if (pl->pcap_dead_handle == NULL || pl->pcap_dumper == NULL) {
        if (PcapLogOpenHandles(pl, p) != TM_ECODE_OK) {
            SCMutexUnlock(&pl->plog_lock);
            return TM_ECODE_FAILED;
        }
    }

    PCAPLOG_PROFILE_START;
    pcap_dump((u_char *)pl->pcap_dumper, pl->h, GET_PKT_DATA(p));
    pl->size_current += len;
    PCAPLOG_PROFILE_END(pl->profile_write);
    pl->profile_data_size += len;

    SCLogDebug("pl->size_current %"PRIu64",  pl->size_limit %"PRIu64,
               pl->size_current, pl->size_limit);

    SCMutexUnlock(&pl->plog_lock);
    return TM_ECODE_OK;
}

static TmEcode PcapLogDataInit(ThreadVars *t, void *initdata, void **data)
{
    if (initdata == NULL) {
        SCLogDebug("Error getting context for PcapLog. \"initdata\" argument NULL");
        return TM_ECODE_FAILED;
    }

    PcapLogData *pl = ((OutputCtx *)initdata)->data;

    SCMutexLock(&pl->plog_lock);

    /** Use the Ouptut Context (file pointer and mutex) */
    pl->pkt_cnt = 0;
    pl->pcap_dead_handle = NULL;
    pl->pcap_dumper = NULL;
    pl->file_cnt = 1;

    struct timeval ts;
    memset(&ts, 0x00, sizeof(struct timeval));
    TimeGet(&ts);
    struct tm local_tm;
    struct tm *tms = SCLocalTime(ts.tv_sec, &local_tm);
    pl->prev_day = tms->tm_mday;

    *data = (void *)pl;

    SCMutexUnlock(&pl->plog_lock);
    return TM_ECODE_OK;
}

/**
 *  \brief Thread deinit function.
 *
 *  \param t Thread Variable containing  input/output queue, cpu affinity etc.
 *  \param data PcapLog thread data.
 *  \retval TM_ECODE_OK on succces
 *  \retval TM_ECODE_FAILED on failure
 */

static TmEcode PcapLogDataDeinit(ThreadVars *t, void *data)
{
    PcapLogData *pl = data;

    if (pl->pcap_dumper != NULL) {
        if (PcapLogCloseFile(t,pl) < 0) {
            SCLogDebug("PcapLogCloseFile failed");
        }
    }

    if (pl->reported == 0) {
        PcapLogProfilingDump(pl);
        pl->reported = 1;
    }
    return TM_ECODE_OK;
}

/** \brief Fill in pcap logging struct from the provided ConfNode.
 *  \param conf The configuration node for this output.
 *  \retval output_ctx
 * */
static OutputCtx *PcapLogInitCtx(ConfNode *conf)
{
    PcapLogData *pl = SCMalloc(sizeof(PcapLogData));
    if (unlikely(pl == NULL)) {
        SCLogError(SC_ERR_MEM_ALLOC, "Failed to allocate Memory for PcapLogData");
        exit(EXIT_FAILURE);
    }
    memset(pl, 0, sizeof(PcapLogData));

    pl->h = SCMalloc(sizeof(*pl->h));
    if (pl->h == NULL) {
        SCLogError(SC_ERR_MEM_ALLOC,
            "Failed to allocate Memory for pcap header struct");
        exit(EXIT_FAILURE);
    }

    /* Set the defaults */
    pl->mode = LOGMODE_NORMAL;
    pl->max_files = DEFAULT_FILE_LIMIT;
    pl->use_ringbuffer = RING_BUFFER_MODE_DISABLED;
    pl->timestamp_format = TS_FORMAT_SEC;
    pl->use_stream_depth = USE_STREAM_DEPTH_DISABLED;

    TAILQ_INIT(&pl->pcap_file_list);

    SCMutexInit(&pl->plog_lock, NULL);

    /* conf params */

    const char *filename = NULL;

    if (conf != NULL) { /* To faciliate unit tests. */
        filename = ConfNodeLookupChildValue(conf, "filename");
    }

    if (filename == NULL)
        filename = DEFAULT_LOG_FILENAME;

    if ((pl->prefix = SCStrdup(filename)) == NULL) {
        exit(EXIT_FAILURE);
    }

    pl->size_limit = DEFAULT_LIMIT;
    if (conf != NULL) {
        const char *s_limit = NULL;
        s_limit = ConfNodeLookupChildValue(conf, "limit");
        if (s_limit != NULL) {
            if (ParseSizeStringU64(s_limit, &pl->size_limit) < 0) {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                    "Failed to initialize unified2 output, invalid limit: %s",
                    s_limit);
                exit(EXIT_FAILURE);
            }
            if (pl->size_limit < 4096) {
                SCLogInfo("pcap-log \"limit\" value of %"PRIu64" assumed to be pre-1.2 "
                        "style: setting limit to %"PRIu64"mb", pl->size_limit, pl->size_limit);
                uint64_t size = pl->size_limit * 1024 * 1024;
                pl->size_limit = size;
            } else if (pl->size_limit < MIN_LIMIT) {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                    "Fail to initialize pcap-log output, limit less than "
                    "allowed minimum.");
                exit(EXIT_FAILURE);
            }
        }
    }

    if (conf != NULL) {
        const char *s_mode = NULL;
        s_mode = ConfNodeLookupChildValue(conf, "mode");
        if (s_mode != NULL) {
            if (strcasecmp(s_mode, "sguil") == 0) {
                pl->mode = LOGMODE_SGUIL;
            } else if (strcasecmp(s_mode, "normal") != 0) {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                    "log-pcap you must specify \"sguil\" or \"normal\" mode "
                    "option to be set.");
                exit(EXIT_FAILURE);
            }
        }

        const char *s_dir = NULL;
        s_dir = ConfNodeLookupChildValue(conf, "dir");
        if (s_dir == NULL) {
            s_dir = ConfNodeLookupChildValue(conf, "sguil-base-dir");
        }
        if (s_dir == NULL) {
            if (pl->mode == LOGMODE_SGUIL) {
                SCLogError(SC_ERR_LOGPCAP_SGUIL_BASE_DIR_MISSING,
                    "log-pcap \"sguil\" mode requires \"sguil-base-dir\" "
                    "option to be set.");
                exit(EXIT_FAILURE);
            } else {
                char *log_dir = NULL;
                log_dir = ConfigGetLogDirectory();

                strlcpy(pl->dir,
                    log_dir, sizeof(pl->dir));
                    SCLogInfo("Using log dir %s", pl->dir);
            }
        } else {
            if (PathIsAbsolute(s_dir)) {
                strlcpy(pl->dir,
                        s_dir, sizeof(pl->dir));
            } else {
                char *log_dir = NULL;
                log_dir = ConfigGetLogDirectory();

                snprintf(pl->dir, sizeof(pl->dir), "%s/%s",
                    log_dir, s_dir);
            }

            struct stat stat_buf;
            if (stat(pl->dir, &stat_buf) != 0) {
                SCLogError(SC_ERR_LOGDIR_CONFIG, "The sguil-base-dir directory \"%s\" "
                        "supplied doesn't exist. Shutting down the engine",
                        pl->dir);
                exit(EXIT_FAILURE);
            }
            SCLogInfo("Using log dir %s", pl->dir);
        }
    }

    SCLogInfo("using %s logging", pl->mode == LOGMODE_SGUIL ?
              "Sguil compatible" : "normal");

    uint32_t max_file_limit = DEFAULT_FILE_LIMIT;
    if (conf != NULL) {
        const char *max_number_of_files_s = NULL;
        max_number_of_files_s = ConfNodeLookupChildValue(conf, "max-files");
        if (max_number_of_files_s != NULL) {
            if (ByteExtractStringUint32(&max_file_limit, 10, 0,
                                        max_number_of_files_s) == -1) {
                SCLogError(SC_ERR_INVALID_ARGUMENT, "Failed to initialize "
                           "pcap-log output, invalid number of files limit: %s",
                           max_number_of_files_s);
                exit(EXIT_FAILURE);
            } else if (max_file_limit < 1) {
                SCLogError(SC_ERR_INVALID_ARGUMENT,
                    "Failed to initialize pcap-log output, limit less than "
                    "allowed minimum.");
                exit(EXIT_FAILURE);
            } else {
                pl->max_files = max_file_limit;
                pl->use_ringbuffer = RING_BUFFER_MODE_ENABLED;
            }
        }
    }

    const char *ts_format = NULL;
    if (conf != NULL) { /* To faciliate unit tests. */
        ts_format = ConfNodeLookupChildValue(conf, "ts-format");
    }
    if (ts_format != NULL) {
        if (strcasecmp(ts_format, "usec") == 0) {
            pl->timestamp_format = TS_FORMAT_USEC;
        } else if (strcasecmp(ts_format, "sec") != 0) {
            SCLogError(SC_ERR_INVALID_ARGUMENT,
                "log-pcap ts_format specified %s is invalid must be"
                " \"sec\" or \"usec\"", ts_format);
            exit(EXIT_FAILURE);
        }
    }

    const char *use_stream_depth = NULL;
    if (conf != NULL) { /* To faciliate unit tests. */
        use_stream_depth = ConfNodeLookupChildValue(conf, "use-stream-depth");
    }
    if (use_stream_depth != NULL) {
        if (ConfValIsFalse(use_stream_depth)) {
            pl->use_stream_depth = USE_STREAM_DEPTH_DISABLED;
        } else if (ConfValIsTrue(use_stream_depth)) {
            pl->use_stream_depth = USE_STREAM_DEPTH_ENABLED;
        } else {
            SCLogError(SC_ERR_INVALID_ARGUMENT,
                "log-pcap use_stream_depth specified is invalid must be");
            exit(EXIT_FAILURE);
        }
    }

    /* create the output ctx and send it back */

    OutputCtx *output_ctx = SCCalloc(1, sizeof(OutputCtx));
    if (unlikely(output_ctx == NULL)) {
        SCLogError(SC_ERR_MEM_ALLOC, "Failed to allocate memory for OutputCtx.");
        exit(EXIT_FAILURE);
    }
    output_ctx->data = pl;
    output_ctx->DeInit = PcapLogFileDeInitCtx;

    return output_ctx;
}

static void PcapLogFileDeInitCtx(OutputCtx *output_ctx)
{
    if (output_ctx == NULL)
        return;

    PcapLogData *pl = output_ctx->data;

    PcapFileName *pf = NULL;
    TAILQ_FOREACH(pf, &pl->pcap_file_list, next) {
        SCLogDebug("PCAP files left at exit: %s\n", pf->filename);
    }

    return;
}

/**
 *  \brief Read the config set the file pointer, open the file
 *
 *  \param PcapLogData.
 *
 *  \retval -1 if failure
 *  \retval 0 if succesful
 */
static int PcapLogOpenFileCtx(PcapLogData *pl)
{
    char *filename = NULL;

    PCAPLOG_PROFILE_START;

    if (pl->filename != NULL)
        filename = pl->filename;
    else {
        filename = SCMalloc(PATH_MAX);
        if (unlikely(filename == NULL)) {
            return -1;
        }
        pl->filename = filename;
    }

    /** get the time so we can have a filename with seconds since epoch */
    struct timeval ts;
    memset(&ts, 0x00, sizeof(struct timeval));
    TimeGet(&ts);

    /* Place to store the name of our PCAP file */
    PcapFileName *pf = SCMalloc(sizeof(PcapFileName));
    if (unlikely(pf == NULL)) {
        return -1;
    }
    memset(pf, 0, sizeof(PcapFileName));

    if (pl->mode == LOGMODE_SGUIL) {
        struct tm local_tm;
        struct tm *tms = SCLocalTime(ts.tv_sec, &local_tm);

        char dirname[32], dirfull[PATH_MAX] = "";

        snprintf(dirname, sizeof(dirname), "%04d-%02d-%02d",
                tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday);

        /* create the filename to use */
        snprintf(dirfull, PATH_MAX, "%s/%s", pl->dir, dirname);

        /* if mkdir fails file open will fail, so deal with errors there */
#ifndef OS_WIN32
        (void)mkdir(dirfull, 0700);
#else
        (void)mkdir(dirfull);
#endif
        if ((pf->dirname = SCStrdup(dirfull)) == NULL) {
            SCLogError(SC_ERR_MEM_ALLOC, "Error allocating memory for "
                       "directory name");
            goto error;
        }

        if (pl->timestamp_format == TS_FORMAT_SEC) {
            snprintf(filename, PATH_MAX, "%s/%s.%" PRIu32, dirfull,
                     pl->prefix, (uint32_t)ts.tv_sec);
        } else {
            snprintf(filename, PATH_MAX, "%s/%s.%" PRIu32 ".%" PRIu32,
                     dirfull, pl->prefix, (uint32_t)ts.tv_sec, (uint32_t)ts.tv_usec);
        }

    } else {
        /* create the filename to use */
        if (pl->timestamp_format == TS_FORMAT_SEC) {
            snprintf(filename, PATH_MAX, "%s/%s.%" PRIu32, pl->dir,
                     pl->prefix, (uint32_t)ts.tv_sec);
        } else {
            snprintf(filename, PATH_MAX, "%s/%s.%" PRIu32 ".%" PRIu32, pl->dir,
                     pl->prefix, (uint32_t)ts.tv_sec, (uint32_t)ts.tv_usec);
        }
    }

    if ((pf->filename = SCStrdup(pl->filename)) == NULL) {
        SCLogError(SC_ERR_MEM_ALLOC, "Error allocating memory. For filename");
        goto error;
    }
    SCLogDebug("Opening pcap file log %s", pf->filename);
    TAILQ_INSERT_TAIL(&pl->pcap_file_list, pf, next);

    PCAPLOG_PROFILE_END(pl->profile_open);
    return 0;

error:
    PcapFileNameFree(pf);
    return -1;
}

static int profiling_pcaplog_enabled = 0;
static int profiling_pcaplog_output_to_file = 0;
static char *profiling_pcaplog_file_name = NULL;
static char *profiling_pcaplog_file_mode = "a";

static void FormatNumber(uint64_t num, char *str, size_t size) {
    if (num < 1000UL)
        snprintf(str, size, "%"PRIu64, num);
    else if (num < 1000000UL)
        snprintf(str, size, "%3.1fk", (float)num/1000UL);
    else if (num < 1000000000UL)
        snprintf(str, size, "%3.1fm", (float)num/1000000UL);
    else
        snprintf(str, size, "%3.1fb", (float)num/1000000000UL);
}

static void ProfileReportPair(FILE *fp, const char *name, PcapLogProfileData *p) {
    char ticks_str[32] = "n/a";
    char cnt_str[32] = "n/a";
    char avg_str[32] = "n/a";

    FormatNumber((uint64_t)p->cnt, cnt_str, sizeof(cnt_str));
    FormatNumber((uint64_t)p->total, ticks_str, sizeof(ticks_str));
    if (p->cnt && p->total)
        FormatNumber((uint64_t)(p->total/p->cnt), avg_str, sizeof(avg_str));

    fprintf(fp, "%-28s %-10s %-10s %-10s\n", name, cnt_str, avg_str, ticks_str);
}

static void ProfileReport(FILE *fp, PcapLogData *pl) {
    ProfileReportPair(fp, "open", &pl->profile_open);
    ProfileReportPair(fp, "close", &pl->profile_close);
    ProfileReportPair(fp, "write", &pl->profile_write);
    ProfileReportPair(fp, "rotate (incl open/close)", &pl->profile_rotate);
    ProfileReportPair(fp, "handles", &pl->profile_handles);
}

static void FormatBytes(uint64_t num, char *str, size_t size) {
    if (num < 1000UL)
        snprintf(str, size, "%"PRIu64, num);
    else if (num < 1048576UL)
        snprintf(str, size, "%3.1fKiB", (float)num/1000UL);
    else if (num < 1073741824UL)
        snprintf(str, size, "%3.1fMiB", (float)num/1000000UL);
    else
        snprintf(str, size, "%3.1fGiB", (float)num/1000000000UL);
}

static void PcapLogProfilingDump(PcapLogData *pl) {
    FILE *fp = NULL;

    if (profiling_pcaplog_enabled == 0)
        return;

    if (profiling_pcaplog_output_to_file == 1) {
        fp = fopen(profiling_pcaplog_file_name, profiling_pcaplog_file_mode);
        if (fp == NULL) {
            SCLogError(SC_ERR_FOPEN, "failed to open %s: %s",
                    profiling_pcaplog_file_name, strerror(errno));
            return;
        }
    } else {
       fp = stdout;
    }

    /* counters */
    fprintf(fp, "\n\nOperation                    Cnt        Avg ticks  Total ticks\n");
    fprintf(fp,     "---------------------------- ---------- ---------- -----------\n");

    ProfileReport(fp, pl);
    uint64_t total = pl->profile_write.total + pl->profile_rotate.total + pl->profile_handles.total;

    /* overall stats */
    fprintf(fp, "\nOverall: %"PRIu64" bytes written, average %d bytes per write.\n",
        pl->profile_data_size, (int)(pl->profile_data_size / pl->profile_write.cnt));
    fprintf(fp, "         PCAP data structure overhead: %"PRIuMAX" per write.\n",
        sizeof(struct pcap_pkthdr));

    /* print total bytes written */
    char bytes_str[32];
    FormatBytes(pl->profile_data_size, bytes_str, sizeof(bytes_str));
    fprintf(fp, "         Size written: %s\n", bytes_str);

    /* ticks per MiB and GiB */
    uint64_t ticks_per_mib = 0, ticks_per_gib = 0;
    uint64_t mib = pl->profile_data_size/(1024*1024);
    if (mib)
        ticks_per_mib = total/mib;
    char ticks_per_mib_str[32] = "n/a";
    if (ticks_per_mib > 0)
        FormatNumber(ticks_per_mib, ticks_per_mib_str, sizeof(ticks_per_mib_str));
    fprintf(fp, "         Ticks per MiB: %s\n", ticks_per_mib_str);

    uint64_t gib = pl->profile_data_size/(1024*1024*1024);
    if (gib)
        ticks_per_gib = total/gib;
    char ticks_per_gib_str[32] = "n/a";
    if (ticks_per_gib > 0)
        FormatNumber(ticks_per_gib, ticks_per_gib_str, sizeof(ticks_per_gib_str));
    fprintf(fp, "         Ticks per GiB: %s\n", ticks_per_gib_str);

    if (fp != stdout)
        fclose(fp);
}

void PcapLogProfileSetup(void) {
    ConfNode *conf = ConfGetNode("profiling.pcap-log");
    if (conf != NULL && ConfNodeChildValueIsTrue(conf, "enabled")) {
        profiling_pcaplog_enabled = 1;
        SCLogInfo("pcap-log profiling enabled");

        const char *filename = ConfNodeLookupChildValue(conf, "filename");
        if (filename != NULL) {
            char *log_dir;
            log_dir = ConfigGetLogDirectory();

            profiling_pcaplog_file_name = SCMalloc(PATH_MAX);
            if (unlikely(profiling_pcaplog_file_name == NULL)) {
                SCLogError(SC_ERR_MEM_ALLOC, "can't duplicate file name");
                exit(EXIT_FAILURE);
            }

            snprintf(profiling_pcaplog_file_name, PATH_MAX, "%s/%s", log_dir, filename);

            const char *v = ConfNodeLookupChildValue(conf, "append");
            if (v == NULL || ConfValIsTrue(v)) {
                profiling_pcaplog_file_mode = "a";
            } else {
                profiling_pcaplog_file_mode = "w";
            }

            profiling_pcaplog_output_to_file = 1;
            SCLogInfo("pcap-log profiling output goes to %s (mode %s)",
                    profiling_pcaplog_file_name, profiling_pcaplog_file_mode);
        }
    }
}
