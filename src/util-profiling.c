/* Copyright (C) 2007-2020 Open Information Security Foundation
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
 * \author Endace Technology Limited.
 * \author Victor Julien <victor@inliniac.net>
 *
 * An API for profiling operations.
 *
 * Really just a wrapper around the existing perf counters.
 */

#include "suricata-common.h"
#include "decode.h"
#include "detect.h"
#include "detect-engine-prefilter.h"
#include "conf.h"
#include "flow-worker.h"

#include "tm-threads.h"

#include "util-unittest.h"
#include "util-byte.h"
#include "util-profiling.h"
#include "util-profiling-locks.h"

#ifdef PROFILING

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define DEFAULT_LOG_FILENAME "profile.log"
#define DEFAULT_LOG_MODE_APPEND "yes"

static pthread_mutex_t packet_profile_lock;
static FILE *packet_profile_csv_fp = NULL;

extern int profiling_locks_enabled;
extern int profiling_locks_output_to_file;
extern char *profiling_locks_file_name;
extern const char *profiling_locks_file_mode;

typedef struct SCProfilePacketData_ {
    uint64_t min;
    uint64_t max;
    uint64_t tot;
    uint64_t cnt;
#ifdef PROFILE_LOCKING
    uint64_t lock;
    uint64_t ticks;
    uint64_t contention;

    uint64_t slock;
    uint64_t sticks;
    uint64_t scontention;
#endif
} SCProfilePacketData;
SCProfilePacketData packet_profile_data4[257]; /**< all proto's + tunnel */
SCProfilePacketData packet_profile_data6[257]; /**< all proto's + tunnel */

/* each module, each proto */
SCProfilePacketData packet_profile_tmm_data4[TMM_SIZE][257];
SCProfilePacketData packet_profile_tmm_data6[TMM_SIZE][257];

SCProfilePacketData packet_profile_app_data4[TMM_SIZE][257];
SCProfilePacketData packet_profile_app_data6[TMM_SIZE][257];

SCProfilePacketData packet_profile_app_pd_data4[257];
SCProfilePacketData packet_profile_app_pd_data6[257];

SCProfilePacketData packet_profile_detect_data4[PROF_DETECT_SIZE][257];
SCProfilePacketData packet_profile_detect_data6[PROF_DETECT_SIZE][257];

SCProfilePacketData packet_profile_log_data4[LOGGER_SIZE][256];
SCProfilePacketData packet_profile_log_data6[LOGGER_SIZE][256];

struct ProfileProtoRecords {
    SCProfilePacketData records4[257];
    SCProfilePacketData records6[257];
};

struct ProfileProtoRecords packet_profile_flowworker_data[PROFILE_FLOWWORKER_SIZE];

int profiling_packets_enabled = 0;
int profiling_output_to_file = 0;

static int profiling_packets_csv_enabled = 0;
static int profiling_packets_output_to_file = 0;
static char *profiling_file_name;
static char profiling_packets_file_name[PATH_MAX];
static char *profiling_csv_file_name;
static const char *profiling_packets_file_mode = "a";

static int rate = 1;
static SC_ATOMIC_DECLARE(uint64_t, samples);

/**
 * Used as a check so we don't double enter a profiling run.
 */
thread_local int profiling_rules_entered = 0;

void SCProfilingDumpPacketStats(void);
const char * PacketProfileDetectIdToString(PacketProfileDetectId id);
const char * PacketProfileLoggertIdToString(LoggerId id);
static void PrintCSVHeader(void);

static void FormatNumber(uint64_t num, char *str, size_t size)
{
    if (num < 1000UL)
        snprintf(str, size, "%"PRIu64, num);
    else if (num < 1000000UL)
        snprintf(str, size, "%3.1fk", (float)num/1000UL);
    else if (num < 1000000000UL)
        snprintf(str, size, "%3.1fm", (float)num/1000000UL);
    else
        snprintf(str, size, "%3.1fb", (float)num/1000000000UL);
}

/**
 * \brief Initialize profiling.
 */
void
SCProfilingInit(void)
{
    ConfNode *conf;

    SC_ATOMIC_INIT(samples);

    intmax_t rate_v = 0;
    (void)ConfGetInt("profiling.sample-rate", &rate_v);
    if (rate_v > 0 && rate_v < INT_MAX) {
        rate = (int)rate_v;
        if (rate != 1)
            SCLogInfo("profiling runs for every %dth packet", rate);
        else
            SCLogInfo("profiling runs for every packet");
    }

    conf = ConfGetNode("profiling.packets");
    if (conf != NULL) {
        if (ConfNodeChildValueIsTrue(conf, "enabled")) {
            profiling_packets_enabled = 1;

            if (pthread_mutex_init(&packet_profile_lock, NULL) != 0) {
                        FatalError(SC_ERR_FATAL,
                                   "Failed to initialize packet profiling mutex.");
            }
            memset(&packet_profile_data4, 0, sizeof(packet_profile_data4));
            memset(&packet_profile_data6, 0, sizeof(packet_profile_data6));
            memset(&packet_profile_tmm_data4, 0, sizeof(packet_profile_tmm_data4));
            memset(&packet_profile_tmm_data6, 0, sizeof(packet_profile_tmm_data6));
            memset(&packet_profile_app_data4, 0, sizeof(packet_profile_app_data4));
            memset(&packet_profile_app_data6, 0, sizeof(packet_profile_app_data6));
            memset(&packet_profile_app_pd_data4, 0, sizeof(packet_profile_app_pd_data4));
            memset(&packet_profile_app_pd_data6, 0, sizeof(packet_profile_app_pd_data6));
            memset(&packet_profile_detect_data4, 0, sizeof(packet_profile_detect_data4));
            memset(&packet_profile_detect_data6, 0, sizeof(packet_profile_detect_data6));
            memset(&packet_profile_log_data4, 0, sizeof(packet_profile_log_data4));
            memset(&packet_profile_log_data6, 0, sizeof(packet_profile_log_data6));
            memset(&packet_profile_flowworker_data, 0, sizeof(packet_profile_flowworker_data));

            const char *filename = ConfNodeLookupChildValue(conf, "filename");
            if (filename != NULL) {
                const char *log_dir;
                log_dir = ConfigGetLogDirectory();

                snprintf(profiling_packets_file_name, sizeof(profiling_packets_file_name),
                        "%s/%s", log_dir, filename);

                const char *v = ConfNodeLookupChildValue(conf, "append");
                if (v == NULL || ConfValIsTrue(v)) {
                    profiling_packets_file_mode = "a";
                } else {
                    profiling_packets_file_mode = "w";
                }

                profiling_packets_output_to_file = 1;
            }
        }

        conf = ConfGetNode("profiling.packets.csv");
        if (conf != NULL) {
            if (ConfNodeChildValueIsTrue(conf, "enabled")) {
                const char *filename = ConfNodeLookupChildValue(conf, "filename");
                if (filename == NULL) {
                    filename = "packet_profile.csv";
                }

                const char *log_dir = ConfigGetLogDirectory();

                profiling_csv_file_name = SCMalloc(PATH_MAX);
                if (unlikely(profiling_csv_file_name == NULL)) {
                    FatalError(SC_ERR_FATAL, "out of memory");
                }
                snprintf(profiling_csv_file_name, PATH_MAX, "%s/%s", log_dir, filename);

                packet_profile_csv_fp = fopen(profiling_csv_file_name, "w");
                if (packet_profile_csv_fp == NULL) {
                    SCFree(profiling_csv_file_name);
                    profiling_csv_file_name = NULL;
                    return;
                }

                PrintCSVHeader();

                profiling_packets_csv_enabled = 1;
            }
        }
    }

    conf = ConfGetNode("profiling.locks");
    if (conf != NULL) {
        if (ConfNodeChildValueIsTrue(conf, "enabled")) {
#ifndef PROFILE_LOCKING
            SCLogWarning(SC_WARN_PROFILE, "lock profiling not compiled in. Add --enable-profiling-locks to configure.");
#else
            profiling_locks_enabled = 1;

            LockRecordInitHash();

            const char *filename = ConfNodeLookupChildValue(conf, "filename");
            if (filename != NULL) {
                const char *log_dir = ConfigGetLogDirectory();

                profiling_locks_file_name = SCMalloc(PATH_MAX);
                if (unlikely(profiling_locks_file_name == NULL)) {
                    FatalError(SC_ERR_FATAL, "can't duplicate file name");
                }

                snprintf(profiling_locks_file_name, PATH_MAX, "%s/%s", log_dir, filename);

                const char *v = ConfNodeLookupChildValue(conf, "append");
                if (v == NULL || ConfValIsTrue(v)) {
                    profiling_locks_file_mode = "a";
                } else {
                    profiling_locks_file_mode = "w";
                }

                profiling_locks_output_to_file = 1;
            }
#endif
        }
    }

}

/**
 * \brief Free resources used by profiling.
 */
void
SCProfilingDestroy(void)
{
    if (profiling_packets_enabled) {
        pthread_mutex_destroy(&packet_profile_lock);
    }

    if (profiling_packets_csv_enabled) {
        if (packet_profile_csv_fp != NULL)
            fclose(packet_profile_csv_fp);
        packet_profile_csv_fp = NULL;
    }

    if (profiling_csv_file_name != NULL)
        SCFree(profiling_csv_file_name);
    profiling_csv_file_name = NULL;

    if (profiling_file_name != NULL)
        SCFree(profiling_file_name);
    profiling_file_name = NULL;

#ifdef PROFILE_LOCKING
    LockRecordFreeHash();
#endif
}

void
SCProfilingDump(void)
{
    SCProfilingDumpPacketStats();
    SCLogPerf("Done dumping profiling data.");
}

static void DumpFlowWorkerIP(FILE *fp, int ipv, uint64_t total)
{
    char totalstr[256];

    enum ProfileFlowWorkerId fwi;
    for (fwi = 0; fwi < PROFILE_FLOWWORKER_SIZE; fwi++) {
        struct ProfileProtoRecords *r = &packet_profile_flowworker_data[fwi];
        for (int p = 0; p < 257; p++) {
            SCProfilePacketData *pd = ipv == 4 ? &r->records4[p] : &r->records6[p];
            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-20s    IPv%d     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    ProfileFlowWorkerIdToString(fwi), ipv, p, pd->cnt,
                    pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }
}

static void DumpFlowWorker(FILE *fp)
{
    uint64_t total = 0;

    enum ProfileFlowWorkerId fwi;
    for (fwi = 0; fwi < PROFILE_FLOWWORKER_SIZE; fwi++) {
        struct ProfileProtoRecords *r = &packet_profile_flowworker_data[fwi];
        for (int p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &r->records4[p];
            total += pd->tot;
            pd = &r->records6[p];
            total += pd->tot;
        }
    }

    fprintf(fp, "\n%-20s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s\n",
            "Flow Worker", "IP ver", "Proto", "cnt", "min", "max", "avg");
    fprintf(fp, "%-20s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s\n",
            "--------------------", "------", "-----", "----------", "------------", "------------", "-----------");
    DumpFlowWorkerIP(fp, 4, total);
    DumpFlowWorkerIP(fp, 6, total);
    fprintf(fp, "Note: %s includes app-layer for TCP\n",
            ProfileFlowWorkerIdToString(PROFILE_FLOWWORKER_STREAM));
}

void SCProfilingDumpPacketStats(void)
{
    int i;
    FILE *fp;
    char totalstr[256];
    uint64_t total;

    if (profiling_packets_enabled == 0)
        return;

    if (profiling_packets_output_to_file == 1) {
        fp = fopen(profiling_packets_file_name, profiling_packets_file_mode);

        if (fp == NULL) {
            SCLogError(SC_ERR_FOPEN, "failed to open %s: %s",
                    profiling_packets_file_name, strerror(errno));
            return;
        }
    } else {
       fp = stdout;
    }

    fprintf(fp, "\n\nPacket profile dump:\n");

    fprintf(fp, "\n%-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s\n",
            "IP ver", "Proto", "cnt", "min", "max", "avg", "tot", "%%");
    fprintf(fp, "%-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s\n",
            "------", "-----", "----------", "------------", "------------", "-----------", "-----------", "---");
    total = 0;
    for (i = 0; i < 257; i++) {
        SCProfilePacketData *pd = &packet_profile_data4[i];
        total += pd->tot;
        pd = &packet_profile_data6[i];
        total += pd->tot;
    }

    for (i = 0; i < 257; i++) {
        SCProfilePacketData *pd = &packet_profile_data4[i];

        if (pd->cnt == 0) {
            continue;
        }

        FormatNumber(pd->tot, totalstr, sizeof(totalstr));
        double percent = (long double)pd->tot /
            (long double)total * 100;

        fprintf(fp, " IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f\n", i, pd->cnt,
            pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
    }

    for (i = 0; i < 257; i++) {
        SCProfilePacketData *pd = &packet_profile_data6[i];

        if (pd->cnt == 0) {
            continue;
        }

        FormatNumber(pd->tot, totalstr, sizeof(totalstr));
        double percent = (long double)pd->tot /
            (long double)total * 100;

        fprintf(fp, " IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f\n", i, pd->cnt,
            pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
    }
    fprintf(fp, "Note: Protocol 256 tracks pseudo/tunnel packets.\n");

    fprintf(fp, "\nPer Thread module stats:\n");

    fprintf(fp, "\n%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s",
            "Thread Module", "IP ver", "Proto", "cnt", "min", "max", "avg", "tot", "%%");
#ifdef PROFILE_LOCKING
    fprintf(fp, "   %-10s   %-10s   %-12s   %-12s   %-10s   %-10s   %-12s   %-12s\n",
            "locks", "ticks", "cont.", "cont.avg", "slocks", "sticks", "scont.", "scont.avg");
#else
    fprintf(fp, "\n");
#endif
    fprintf(fp, "%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s",
            "------------------------", "------", "-----", "----------", "------------", "------------", "-----------", "-----------", "---");
#ifdef PROFILE_LOCKING
    fprintf(fp, "   %-10s   %-10s   %-12s   %-12s   %-10s   %-10s   %-12s   %-12s\n",
            "--------", "--------", "----------", "-----------", "--------", "--------", "------------", "-----------");
#else
    fprintf(fp, "\n");
#endif
    int m;
    total = 0;
    for (m = 0; m < TMM_SIZE; m++) {
        if (tmm_modules[m].flags & TM_FLAG_LOGAPI_TM)
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data4[m][p];
            total += pd->tot;

            pd = &packet_profile_tmm_data6[m][p];
            total += pd->tot;
        }
    }

    for (m = 0; m < TMM_SIZE; m++) {
        if (tmm_modules[m].flags & TM_FLAG_LOGAPI_TM)
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data4[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f",
                    TmModuleTmmIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
#ifdef PROFILE_LOCKING
            fprintf(fp, "  %10.2f  %12"PRIu64"  %12"PRIu64"  %10.2f  %10.2f  %12"PRIu64"  %12"PRIu64"  %10.2f\n",
                    (float)pd->lock/pd->cnt, (uint64_t)pd->ticks/pd->cnt, pd->contention, (float)pd->contention/pd->cnt, (float)pd->slock/pd->cnt, (uint64_t)pd->sticks/pd->cnt, pd->scontention, (float)pd->scontention/pd->cnt);
#else
            fprintf(fp, "\n");
#endif
        }
    }

    for (m = 0; m < TMM_SIZE; m++) {
        if (tmm_modules[m].flags & TM_FLAG_LOGAPI_TM)
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data6[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f\n",
                    TmModuleTmmIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }

    DumpFlowWorker(fp);

    fprintf(fp, "\nPer App layer parser stats:\n");

    fprintf(fp, "\n%-20s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s\n",
            "App Layer", "IP ver", "Proto", "cnt", "min", "max", "avg");
    fprintf(fp, "%-20s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s\n",
            "--------------------", "------", "-----", "----------", "------------", "------------", "-----------");

    total = 0;
    for (m = 0; m < ALPROTO_MAX; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_app_data4[m][p];
            total += pd->tot;

            pd = &packet_profile_app_data6[m][p];
            total += pd->tot;
        }
    }
    for (m = 0; m < ALPROTO_MAX; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_app_data4[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-20s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    AppProtoToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }

    for (m = 0; m < ALPROTO_MAX; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_app_data6[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-20s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    AppProtoToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }

    /* proto detect output */
    {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_app_pd_data4[p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            fprintf(fp, "%-20s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s\n",
                    "Proto detect", p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr);
        }

        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_app_pd_data6[p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            fprintf(fp, "%-20s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s\n",
                    "Proto detect", p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr);
        }
    }

    total = 0;
    for (m = 0; m < PROF_DETECT_SIZE; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_detect_data4[m][p];
            total += pd->tot;

            pd = &packet_profile_detect_data6[m][p];
            total += pd->tot;
        }
    }


    fprintf(fp, "\n%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s",
            "Log Thread Module", "IP ver", "Proto", "cnt", "min", "max", "avg", "tot", "%%");
#ifdef PROFILE_LOCKING
    fprintf(fp, "   %-10s   %-10s   %-12s   %-12s   %-10s   %-10s   %-12s   %-12s\n",
            "locks", "ticks", "cont.", "cont.avg", "slocks", "sticks", "scont.", "scont.avg");
#else
    fprintf(fp, "\n");
#endif
    fprintf(fp, "%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s  %-3s",
            "------------------------", "------", "-----", "----------", "------------", "------------", "-----------", "-----------", "---");
#ifdef PROFILE_LOCKING
    fprintf(fp, "   %-10s   %-10s   %-12s   %-12s   %-10s   %-10s   %-12s   %-12s\n",
            "--------", "--------", "----------", "-----------", "--------", "--------", "------------", "-----------");
#else
    fprintf(fp, "\n");
#endif
    total = 0;
    for (m = 0; m < TMM_SIZE; m++) {
        if (!(tmm_modules[m].flags & TM_FLAG_LOGAPI_TM))
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data4[m][p];
            total += pd->tot;

            pd = &packet_profile_tmm_data6[m][p];
            total += pd->tot;
        }
    }

    for (m = 0; m < TMM_SIZE; m++) {
        if (!(tmm_modules[m].flags & TM_FLAG_LOGAPI_TM))
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data4[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f",
                    TmModuleTmmIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
#ifdef PROFILE_LOCKING
            fprintf(fp, "  %10.2f  %12"PRIu64"  %12"PRIu64"  %10.2f  %10.2f  %12"PRIu64"  %12"PRIu64"  %10.2f\n",
                    (float)pd->lock/pd->cnt, (uint64_t)pd->ticks/pd->cnt, pd->contention, (float)pd->contention/pd->cnt, (float)pd->slock/pd->cnt, (uint64_t)pd->sticks/pd->cnt, pd->scontention, (float)pd->scontention/pd->cnt);
#else
            fprintf(fp, "\n");
#endif
        }
    }

    for (m = 0; m < TMM_SIZE; m++) {
        if (!(tmm_modules[m].flags & TM_FLAG_LOGAPI_TM))
            continue;

        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_tmm_data6[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %6.2f\n",
                    TmModuleTmmIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }

    fprintf(fp, "\nLogger/output stats:\n");

    total = 0;
    for (m = 0; m < LOGGER_SIZE; m++) {
        int p;
        for (p = 0; p < 256; p++) {
            SCProfilePacketData *pd = &packet_profile_log_data4[m][p];
            total += pd->tot;
            pd = &packet_profile_log_data6[m][p];
            total += pd->tot;
        }
    }

    fprintf(fp, "\n%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s\n",
            "Logger", "IP ver", "Proto", "cnt", "min", "max", "avg", "tot");
    fprintf(fp, "%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s\n",
            "------------------------", "------", "-----", "----------", "------------", "------------", "-----------", "-----------");
    for (m = 0; m < LOGGER_SIZE; m++) {
        int p;
        for (p = 0; p < 256; p++) {
            SCProfilePacketData *pd = &packet_profile_log_data4[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    PacketProfileLoggertIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }
    for (m = 0; m < LOGGER_SIZE; m++) {
        int p;
        for (p = 0; p < 256; p++) {
            SCProfilePacketData *pd = &packet_profile_log_data6[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    PacketProfileLoggertIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }

    fprintf(fp, "\nGeneral detection engine stats:\n");

    total = 0;
    for (m = 0; m < PROF_DETECT_SIZE; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_detect_data4[m][p];
            total += pd->tot;
            pd = &packet_profile_detect_data6[m][p];
            total += pd->tot;
        }
    }

    fprintf(fp, "\n%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s\n",
            "Detection phase", "IP ver", "Proto", "cnt", "min", "max", "avg", "tot");
    fprintf(fp, "%-24s   %-6s   %-5s   %-12s   %-12s   %-12s   %-12s   %-12s\n",
            "------------------------", "------", "-----", "----------", "------------", "------------", "-----------", "-----------");
    for (m = 0; m < PROF_DETECT_SIZE; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_detect_data4[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv4     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    PacketProfileDetectIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }
    for (m = 0; m < PROF_DETECT_SIZE; m++) {
        int p;
        for (p = 0; p < 257; p++) {
            SCProfilePacketData *pd = &packet_profile_detect_data6[m][p];

            if (pd->cnt == 0) {
                continue;
            }

            FormatNumber(pd->tot, totalstr, sizeof(totalstr));
            double percent = (long double)pd->tot /
                (long double)total * 100;

            fprintf(fp, "%-24s    IPv6     %3d  %12"PRIu64"     %12"PRIu64"   %12"PRIu64"  %12"PRIu64"  %12s  %-6.2f\n",
                    PacketProfileDetectIdToString(m), p, pd->cnt, pd->min, pd->max, (uint64_t)(pd->tot / pd->cnt), totalstr, percent);
        }
    }
    fclose(fp);
}

static void PrintCSVHeader(void)
{
    fprintf(packet_profile_csv_fp, "pcap_cnt,total,receive,decode,flowworker,");
    fprintf(packet_profile_csv_fp, "threading,");
    fprintf(packet_profile_csv_fp, "proto detect,");

    for (enum ProfileFlowWorkerId fwi = 0; fwi < PROFILE_FLOWWORKER_SIZE; fwi++) {
        fprintf(packet_profile_csv_fp, "%s,", ProfileFlowWorkerIdToString(fwi));
    }
    fprintf(packet_profile_csv_fp, "loggers,");

    /* detect stages */
    for (int i = 0; i < PROF_DETECT_SIZE; i++) {
        fprintf(packet_profile_csv_fp, "%s,", PacketProfileDetectIdToString(i));
    }

    /* individual loggers */
    for (LoggerId i = 0; i < LOGGER_SIZE; i++) {
        fprintf(packet_profile_csv_fp, "%s,", PacketProfileLoggertIdToString(i));
    }

    fprintf(packet_profile_csv_fp, "\n");
}

void SCProfilingPrintPacketProfile(Packet *p)
{
    if (profiling_packets_csv_enabled == 0 || p == NULL ||
        packet_profile_csv_fp == NULL || p->profile == NULL) {
        return;
    }

    uint64_t tmm_total = 0;
    uint64_t receive = 0;
    uint64_t decode = 0;

    /* total cost from acquisition to return to packetpool */
    uint64_t delta = p->profile->ticks_end - p->profile->ticks_start;
    fprintf(packet_profile_csv_fp, "%"PRIu64",%"PRIu64",",
            p->pcap_cnt, delta);

    for (int i = 0; i < TMM_SIZE; i++) {
        const PktProfilingTmmData *pdt = &p->profile->tmm[i];
        uint64_t tmm_delta = pdt->ticks_end - pdt->ticks_start;

        if (tmm_modules[i].flags & TM_FLAG_RECEIVE_TM) {
            if (tmm_delta) {
                receive = tmm_delta;
            }
            continue;

        } else if (tmm_modules[i].flags & TM_FLAG_DECODE_TM) {
            if (tmm_delta) {
                decode = tmm_delta;
            }
            continue;
        }

        tmm_total += tmm_delta;
    }
    fprintf(packet_profile_csv_fp, "%"PRIu64",", receive);
    fprintf(packet_profile_csv_fp, "%"PRIu64",", decode);
    PktProfilingTmmData *fw_pdt = &p->profile->tmm[TMM_FLOWWORKER];
    fprintf(packet_profile_csv_fp, "%"PRIu64",", fw_pdt->ticks_end - fw_pdt->ticks_start);
    fprintf(packet_profile_csv_fp, "%"PRIu64",", delta - tmm_total);

    /* count ticks for app layer */
    uint64_t app_total = 0;
    for (AppProto i = 1; i < ALPROTO_FAILED; i++) {
        const PktProfilingAppData *pdt = &p->profile->app[i];

        if (p->proto == IPPROTO_TCP) {
            app_total += pdt->ticks_spent;
        }
    }

    fprintf(packet_profile_csv_fp, "%"PRIu64",", p->profile->proto_detect);

    /* print flowworker steps */
    for (enum ProfileFlowWorkerId fwi = 0; fwi < PROFILE_FLOWWORKER_SIZE; fwi++) {
        const PktProfilingData *pd = &p->profile->flowworker[fwi];
        uint64_t ticks_spent = pd->ticks_end - pd->ticks_start;
        if (fwi == PROFILE_FLOWWORKER_STREAM) {
            ticks_spent -= app_total;
        } else if (fwi == PROFILE_FLOWWORKER_APPLAYERUDP && app_total) {
            ticks_spent = app_total;
        }

        fprintf(packet_profile_csv_fp, "%"PRIu64",", ticks_spent);
    }

    /* count loggers cost and print as a single cost */
    uint64_t loggers = 0;
    for (LoggerId i = 0; i < LOGGER_SIZE; i++) {
        const PktProfilingLoggerData *pd = &p->profile->logger[i];
        loggers += pd->ticks_spent;
    }
    fprintf(packet_profile_csv_fp, "%"PRIu64",", loggers);

    /* detect steps */
    for (int i = 0; i < PROF_DETECT_SIZE; i++) {
        const PktProfilingDetectData *pdt = &p->profile->detect[i];

        fprintf(packet_profile_csv_fp,"%"PRIu64",", pdt->ticks_spent);
    }

    /* print individual loggers */
    for (LoggerId i = 0; i < LOGGER_SIZE; i++) {
        const PktProfilingLoggerData *pd = &p->profile->logger[i];
        fprintf(packet_profile_csv_fp, "%"PRIu64",", pd->ticks_spent);
    }

    fprintf(packet_profile_csv_fp,"\n");
}

static void SCProfilingUpdatePacketDetectRecord(PacketProfileDetectId id, uint8_t ipproto, PktProfilingDetectData *pdt, int ipver)
{
    if (pdt == NULL) {
        return;
    }

    SCProfilePacketData *pd;
    if (ipver == 4)
        pd = &packet_profile_detect_data4[id][ipproto];
    else
        pd = &packet_profile_detect_data6[id][ipproto];

    if (pd->min == 0 || pdt->ticks_spent < pd->min) {
        pd->min = pdt->ticks_spent;
    }
    if (pd->max < pdt->ticks_spent) {
        pd->max = pdt->ticks_spent;
    }

    pd->tot += pdt->ticks_spent;
    pd->cnt ++;
}

static void SCProfilingUpdatePacketDetectRecords(Packet *p)
{
    PacketProfileDetectId i;
    for (i = 0; i < PROF_DETECT_SIZE; i++) {
        PktProfilingDetectData *pdt = &p->profile->detect[i];

        if (pdt->ticks_spent > 0) {
            if (PKT_IS_IPV4(p)) {
                SCProfilingUpdatePacketDetectRecord(i, p->proto, pdt, 4);
            } else {
                SCProfilingUpdatePacketDetectRecord(i, p->proto, pdt, 6);
            }
        }
    }
}

static void SCProfilingUpdatePacketAppPdRecord(uint8_t ipproto, uint32_t ticks_spent, int ipver)
{
    SCProfilePacketData *pd;
    if (ipver == 4)
        pd = &packet_profile_app_pd_data4[ipproto];
    else
        pd = &packet_profile_app_pd_data6[ipproto];

    if (pd->min == 0 || ticks_spent < pd->min) {
        pd->min = ticks_spent;
    }
    if (pd->max < ticks_spent) {
        pd->max = ticks_spent;
    }

    pd->tot += ticks_spent;
    pd->cnt ++;
}

static void SCProfilingUpdatePacketAppRecord(int alproto, uint8_t ipproto, PktProfilingAppData *pdt, int ipver)
{
    if (pdt == NULL) {
        return;
    }

    SCProfilePacketData *pd;
    if (ipver == 4)
        pd = &packet_profile_app_data4[alproto][ipproto];
    else
        pd = &packet_profile_app_data6[alproto][ipproto];

    if (pd->min == 0 || pdt->ticks_spent < pd->min) {
        pd->min = pdt->ticks_spent;
    }
    if (pd->max < pdt->ticks_spent) {
        pd->max = pdt->ticks_spent;
    }

    pd->tot += pdt->ticks_spent;
    pd->cnt ++;
}

static void SCProfilingUpdatePacketAppRecords(Packet *p)
{
    int i;
    for (i = 0; i < ALPROTO_MAX; i++) {
        PktProfilingAppData *pdt = &p->profile->app[i];

        if (pdt->ticks_spent > 0) {
            if (PKT_IS_IPV4(p)) {
                SCProfilingUpdatePacketAppRecord(i, p->proto, pdt, 4);
            } else {
                SCProfilingUpdatePacketAppRecord(i, p->proto, pdt, 6);
            }
        }
    }

    if (p->profile->proto_detect > 0) {
        if (PKT_IS_IPV4(p)) {
            SCProfilingUpdatePacketAppPdRecord(p->proto, p->profile->proto_detect, 4);
        } else {
            SCProfilingUpdatePacketAppPdRecord(p->proto, p->profile->proto_detect, 6);
        }
    }
}

static void SCProfilingUpdatePacketTmmRecord(int module, uint8_t proto, PktProfilingTmmData *pdt, int ipver)
{
    if (pdt == NULL) {
        return;
    }

    SCProfilePacketData *pd;
    if (ipver == 4)
        pd = &packet_profile_tmm_data4[module][proto];
    else
        pd = &packet_profile_tmm_data6[module][proto];

    uint32_t delta = (uint32_t)pdt->ticks_end - pdt->ticks_start;
    if (pd->min == 0 || delta < pd->min) {
        pd->min = delta;
    }
    if (pd->max < delta) {
        pd->max = delta;
    }

    pd->tot += (uint64_t)delta;
    pd->cnt ++;

#ifdef PROFILE_LOCKING
    pd->lock += pdt->mutex_lock_cnt;
    pd->ticks += pdt->mutex_lock_wait_ticks;
    pd->contention += pdt->mutex_lock_contention;
    pd->slock += pdt->spin_lock_cnt;
    pd->sticks += pdt->spin_lock_wait_ticks;
    pd->scontention += pdt->spin_lock_contention;
#endif
}

static void SCProfilingUpdatePacketTmmRecords(Packet *p)
{
    int i;
    for (i = 0; i < TMM_SIZE; i++) {
        PktProfilingTmmData *pdt = &p->profile->tmm[i];

        if (pdt->ticks_start == 0 || pdt->ticks_end == 0 || pdt->ticks_start > pdt->ticks_end) {
            continue;
        }

        if (PKT_IS_IPV4(p)) {
            SCProfilingUpdatePacketTmmRecord(i, p->proto, pdt, 4);
        } else {
            SCProfilingUpdatePacketTmmRecord(i, p->proto, pdt, 6);
        }
    }
}

static inline void SCProfilingUpdatePacketGenericRecord(PktProfilingData *pdt,
        SCProfilePacketData *pd)
{
    if (pdt == NULL || pd == NULL) {
        return;
    }

    uint64_t delta = pdt->ticks_end - pdt->ticks_start;
    if (pd->min == 0 || delta < pd->min) {
        pd->min = delta;
    }
    if (pd->max < delta) {
        pd->max = delta;
    }

    pd->tot += delta;
    pd->cnt ++;
}

static void SCProfilingUpdatePacketGenericRecords(Packet *p, PktProfilingData *pd,
        struct ProfileProtoRecords *records, int size)
{
    int i;
    for (i = 0; i < size; i++) {
        PktProfilingData *pdt = &pd[i];

        if (pdt->ticks_start == 0 || pdt->ticks_end == 0 || pdt->ticks_start > pdt->ticks_end) {
            continue;
        }

        struct ProfileProtoRecords *r = &records[i];
        SCProfilePacketData *store = NULL;

        if (PKT_IS_IPV4(p)) {
            store = &(r->records4[p->proto]);
        } else {
            store = &(r->records6[p->proto]);
        }

        SCProfilingUpdatePacketGenericRecord(pdt, store);
    }
}

static void SCProfilingUpdatePacketLogRecord(LoggerId id,
    uint8_t ipproto, PktProfilingLoggerData *pdt, int ipver)
{
    if (pdt == NULL) {
        return;
    }

    SCProfilePacketData *pd;
    if (ipver == 4)
        pd = &packet_profile_log_data4[id][ipproto];
    else
        pd = &packet_profile_log_data6[id][ipproto];

    if (pd->min == 0 || pdt->ticks_spent < pd->min) {
        pd->min = pdt->ticks_spent;
    }
    if (pd->max < pdt->ticks_spent) {
        pd->max = pdt->ticks_spent;
    }

    pd->tot += pdt->ticks_spent;
    pd->cnt++;
}

static void SCProfilingUpdatePacketLogRecords(Packet *p)
{
    for (LoggerId i = 0; i < LOGGER_SIZE; i++) {
        PktProfilingLoggerData *pdt = &p->profile->logger[i];

        if (pdt->ticks_spent > 0) {
            if (PKT_IS_IPV4(p)) {
                SCProfilingUpdatePacketLogRecord(i, p->proto, pdt, 4);
            } else {
                SCProfilingUpdatePacketLogRecord(i, p->proto, pdt, 6);
            }
        }
    }
}

void SCProfilingAddPacket(Packet *p)
{
    if (p == NULL || p->profile == NULL ||
        p->profile->ticks_start == 0 || p->profile->ticks_end == 0 ||
        p->profile->ticks_start > p->profile->ticks_end)
        return;

    pthread_mutex_lock(&packet_profile_lock);
    {

        if (PKT_IS_IPV4(p)) {
            SCProfilePacketData *pd = &packet_profile_data4[p->proto];

            uint64_t delta = p->profile->ticks_end - p->profile->ticks_start;
            if (pd->min == 0 || delta < pd->min) {
                pd->min = delta;
            }
            if (pd->max < delta) {
                pd->max = delta;
            }

            pd->tot += delta;
            pd->cnt ++;

            if (IS_TUNNEL_PKT(p)) {
                pd = &packet_profile_data4[256];

                if (pd->min == 0 || delta < pd->min) {
                    pd->min = delta;
                }
                if (pd->max < delta) {
                    pd->max = delta;
                }

                pd->tot += delta;
                pd->cnt ++;
            }

            SCProfilingUpdatePacketGenericRecords(p, p->profile->flowworker,
                packet_profile_flowworker_data, PROFILE_FLOWWORKER_SIZE);

            SCProfilingUpdatePacketTmmRecords(p);
            SCProfilingUpdatePacketAppRecords(p);
            SCProfilingUpdatePacketDetectRecords(p);
            SCProfilingUpdatePacketLogRecords(p);

        } else if (PKT_IS_IPV6(p)) {
            SCProfilePacketData *pd = &packet_profile_data6[p->proto];

            uint64_t delta = p->profile->ticks_end - p->profile->ticks_start;
            if (pd->min == 0 || delta < pd->min) {
                pd->min = delta;
            }
            if (pd->max < delta) {
                pd->max = delta;
            }

            pd->tot += delta;
            pd->cnt ++;

            if (IS_TUNNEL_PKT(p)) {
                pd = &packet_profile_data6[256];

                if (pd->min == 0 || delta < pd->min) {
                    pd->min = delta;
                }
                if (pd->max < delta) {
                    pd->max = delta;
                }

                pd->tot += delta;
                pd->cnt ++;
            }

            SCProfilingUpdatePacketGenericRecords(p, p->profile->flowworker,
                packet_profile_flowworker_data, PROFILE_FLOWWORKER_SIZE);

            SCProfilingUpdatePacketTmmRecords(p);
            SCProfilingUpdatePacketAppRecords(p);
            SCProfilingUpdatePacketDetectRecords(p);
            SCProfilingUpdatePacketLogRecords(p);
        }

        if (profiling_packets_csv_enabled)
            SCProfilingPrintPacketProfile(p);

    }
    pthread_mutex_unlock(&packet_profile_lock);
}

PktProfiling *SCProfilePacketStart(void)
{
    uint64_t sample = SC_ATOMIC_ADD(samples, 1);
    if (sample % rate == 0)
        return SCCalloc(1, sizeof(PktProfiling));
    else
        return NULL;
}

/* see if we want to profile rules for this packet */
int SCProfileRuleStart(Packet *p)
{
#ifdef PROFILE_LOCKING
    if (p->profile != NULL) {
        p->flags |= PKT_PROFILE;
        return 1;
    }
#else
    uint64_t sample = SC_ATOMIC_ADD(samples, 1);
    if (sample % rate == 0) {
        p->flags |= PKT_PROFILE;
        return 1;
    }
#endif
    if (p->flags & PKT_PROFILE)
        return 1;
    return 0;
}
#endif

#if defined(PROFILING_LITE)
thread_local uint64_t proflite_features = 0;
thread_local uint64_t proflite_tracepoints = 0;

static enum ProfliteTracepoint SettingToTp(const char *setting)
{
    if (strcmp(setting, "packetpool_get") == 0) {
        return PROFLITE_TP_PP_GET;
    } else if (strcmp(setting, "packetpool_return") == 0) {
        return PROFLITE_TP_PP_RETURN;
    } else if (strcmp(setting, "flowworker_enter") == 0) {
        return PROFLITE_TP_FLOWWORKER_ENTER;
    } else if (strcmp(setting, "flowworker_pre_inject") == 0) {
        return PROFLITE_TP_FLOWWORKER_PRE_INJECT;
    } else if (strcmp(setting, "flowworker_applayer_enter") == 0) {
        return PROFLITE_TP_FLOWWORKER_APPLAYER_START;
    } else if (strcmp(setting, "flowworker_applayer_end") == 0) {
        return PROFLITE_TP_FLOWWORKER_APPLAYER_END;
    } else if (strcmp(setting, "flowworker_detect_enter") == 0) {
        return PROFLITE_TP_FLOWWORKER_DETECT_START;
    } else if (strcmp(setting, "flowworker_detect_end") == 0) {
        return PROFLITE_TP_FLOWWORKER_DETECT_END;
    } else if (strcmp(setting, "flowworker_output_start") == 0) {
        return PROFLITE_TP_FLOWWORKER_OUTPUT_START;
    } else if (strcmp(setting, "flowworker_output_end") == 0) {
        return PROFLITE_TP_FLOWWORKER_OUTPUT_END;
    } else if (strcmp(setting, "flowworker_exit") == 0) {
        return PROFLITE_TP_FLOWWORKER_EXIT;
    }
    return PROFLITE_TP_DISABLED;
}

void ProfliteSetTpEntry(const char *setting)
{
    const enum ProfliteTracepoint tp = SettingToTp(setting);
    SC_ATOMIC_SET(proflite_tp_entry, tp);
    SCLogNotice("entry point set to \"%s\" Tp now %u", setting, SC_ATOMIC_GET(proflite_tp_entry));
}
void ProfliteSetTpExit(const char *setting)
{
    const enum ProfliteTracepoint tp = SettingToTp(setting);
    if (tp > SC_ATOMIC_GET(proflite_tp_entry)) {
        SC_ATOMIC_SET(proflite_tp_exit, tp);
        SCLogNotice("exit point set to \"%s\" Tp now %u", setting, SC_ATOMIC_GET(proflite_tp_exit));
    } else {
        SCLogNotice("tracepoint should be > entry (TODO error handle)");
    }
}

void ProfliteEnable(const char *setting)
{
    if (strcmp(setting, "all") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_ALL_BIT);
    } else if (strcmp(setting, "tcp") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_TCP_BIT);
    } else if (strcmp(setting, "app_http") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_ALPROTO_HTTP_BIT);
    } else if (strcmp(setting, "only_app_http") == 0) {
        SC_ATOMIC_SET(proflite_features, PROFLITE_ALPROTO_HTTP_BIT);
    } else if (strcmp(setting, "app_dns") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_ALPROTO_DNS_BIT);
    } else if (strcmp(setting, "only_app_dns") == 0) {
        SC_ATOMIC_SET(proflite_features, PROFLITE_ALPROTO_DNS_BIT);
    } else if (strcmp(setting, "app_ftp") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_ALPROTO_FTP_BIT);
    } else if (strcmp(setting, "only_app_ftp") == 0) {
        SC_ATOMIC_SET(proflite_features, PROFLITE_ALPROTO_FTP_BIT);
    } else if (strcmp(setting, "app_dcerpc") == 0) {
        SC_ATOMIC_OR(proflite_features, PROFLITE_ALPROTO_DCERPC_BIT);
    } else if (strcmp(setting, "only_app_dcerpc") == 0) {
        SC_ATOMIC_SET(proflite_features, PROFLITE_ALPROTO_DCERPC_BIT);
    } else if (strcmp(setting, "any") == 0) {
        SC_ATOMIC_SET(proflite_features, UINT64_MAX);
    } else {
        SCLogNotice("unknown setting %s", setting);
        return;
    }
    SCLogNotice("enabled \"%s\". Flags now %" PRIx64, setting, SC_ATOMIC_GET(proflite_features));
}

void ProfliteDisable(const char *setting)
{
#if 0
    if (strcmp(setting, "global") == 0) {
        SC_ATOMIC_AND(proflite_flags, ~PROFLITE_ENABLED_BIT);
    } else if (strcmp(setting, "app_http") == 0) {
        SC_ATOMIC_AND(proflite_flags, ~PROFLITE_ALPROTO_HTTP_BIT);
    } else {
        abort();
    }
    SCLogNotice("disabled \"%s\". Flags now %"PRIx64, setting, SC_ATOMIC_GET(proflite_flags));
#endif
}

enum ProfileLiteTracker {
    PLT_ALL,
    PLT_ALL_PSEUDO,
    PLT_ALL_PAYLOAD,
    PLT_ALL_NOPAYLOAD,
    PLT_ALL_ALERT,
    PLT_ALL_NOALERT,
    PLT_TCP_ALL,
    PLT_TCP_SYN,
    PLT_TCP_RST,
    PLT_TCP_FIN,
    PLT_TCP_OTHER,
    PLT_UDP,
    PLT_ICMP4,
    PLT_ICMP6,
    PLT_OTHERIP,
    PLT_OTHER,
    PLT_ALPROTO_HTTP,
    PLT_ALPROTO_HTTP2,
    PLT_ALPROTO_SMB,
    PLT_ALPROTO_NFS,
    PLT_ALPROTO_DNS,
    PLT_ALPROTO_DCERPC,
    PLT_ALPROTO_SMTP,
    PLT_ALPROTO_FTP,
    PLT_ALPROTO_FTPDATA,
    PLT_ALPROTO_TLS,
    PLT_ALPROTO_SSH,
    PLT_ALPROTO_DHCP,
    PLT_ALPROTO_SNMP,
    PLT_ALPROTO_TFTP,
    PLT_ALPROTO_OTHER,
    PLT_ALPROTO_NONE,
    PLT_SIZE,
};
static const char *PltToString(enum ProfileLiteTracker t)
{
    switch (t) {
        case PLT_ALL:
            return "all";
        case PLT_ALL_PSEUDO:
            return "all_pseudo";
        case PLT_ALL_PAYLOAD:
            return "all_payload";
        case PLT_ALL_NOPAYLOAD:
            return "all_nopayload";
        case PLT_ALL_ALERT:
            return "all_alert";
        case PLT_ALL_NOALERT:
            return "all_noalert";
        case PLT_TCP_ALL:
            return "tcp_all";
        case PLT_TCP_SYN:
            return "tcp_syn";
        case PLT_TCP_FIN:
            return "tcp_fin";
        case PLT_TCP_RST:
            return "tcp_rst";
        case PLT_TCP_OTHER:
            return "tcp_other";
        case PLT_UDP:
            return "udp";
        case PLT_ICMP4:
            return "icmp4";
        case PLT_ICMP6:
            return "icmp6";
        case PLT_OTHERIP:
            return "other_ip";
        case PLT_OTHER:
            return "other";
        case PLT_ALPROTO_HTTP:
            return "app_http";
        case PLT_ALPROTO_HTTP2:
            return "app_http2";
        case PLT_ALPROTO_SMTP:
            return "app_http2";
        case PLT_ALPROTO_SMB:
            return "app_smb";
        case PLT_ALPROTO_NFS:
            return "app_nfs";
        case PLT_ALPROTO_DNS:
            return "app_dns";
        case PLT_ALPROTO_DCERPC:
            return "app_dcerpc";
        case PLT_ALPROTO_FTP:
            return "app_ftp";
        case PLT_ALPROTO_FTPDATA:
            return "app_ftpdata";
        case PLT_ALPROTO_TLS:
            return "app_tls";
        case PLT_ALPROTO_SSH:
            return "app_ssh";
        case PLT_ALPROTO_SNMP:
            return "app_snmp";
        case PLT_ALPROTO_DHCP:
            return "app_dhcp";
        case PLT_ALPROTO_TFTP:
            return "app_tftp";
        case PLT_ALPROTO_OTHER:
            return "app_other";
        case PLT_ALPROTO_NONE:
            return "app_none";
        case PLT_SIZE:
            return "ERROR";
    }
}

struct ProfileLiteCounters {
    uint16_t cnt;
    uint16_t stdev;
    uint16_t avg;
    uint16_t max;
    uint16_t gt_1stdev;
    uint16_t gt_2stdev;
    uint16_t gt_3stdev;
};

struct ProfileLiteCounterNames {
    char cnt[256];
    char stdev[256];
    char avg[256];
    char max[256];
    char gt_1stdev[256];
    char gt_2stdev[256];
    char gt_3stdev[256];
};

thread_local struct ProfileLiteCounters profile_lite_counters[PLT_SIZE];
struct ProfileLiteCounterNames profile_lite_names[PLT_SIZE];

void ProfliteRegisterCounterNames(void)
{
    for (enum ProfileLiteTracker t = 0; t < PLT_SIZE; t++) {
        struct ProfileLiteCounterNames *n = &profile_lite_names[t];

        snprintf(n->cnt, sizeof(n->cnt), "profile.%s.cnt", PltToString(t));
        snprintf(n->max, sizeof(n->max), "profile.%s.max", PltToString(t));
        snprintf(n->stdev, sizeof(n->stdev), "profile.%s.stdev", PltToString(t));
        snprintf(n->avg, sizeof(n->avg), "profile.%s.avg", PltToString(t));
        snprintf(n->gt_1stdev, sizeof(n->gt_1stdev), "profile.%s.1_2_stdev", PltToString(t));
        snprintf(n->gt_2stdev, sizeof(n->gt_2stdev), "profile.%s.2_3_stdev", PltToString(t));
        snprintf(n->gt_3stdev, sizeof(n->gt_3stdev), "profile.%s.3_stdev", PltToString(t));
    }
}

void ProfliteRegisterCounters(ThreadVars *tv)
{
    for (enum ProfileLiteTracker t = 0; t < PLT_SIZE; t++) {
        struct ProfileLiteCounters *c = &profile_lite_counters[t];
        const struct ProfileLiteCounterNames *n = &profile_lite_names[t];

        c->cnt = StatsRegisterCounter(n->cnt, tv);
        c->max = StatsRegisterMaxCounter(n->max, tv);
        c->stdev = StatsRegisterCounter(n->stdev, tv);
        c->avg = StatsRegisterAvgCounter(n->avg, tv);

        c->gt_1stdev = StatsRegisterCounter(n->gt_1stdev, tv);
        c->gt_2stdev = StatsRegisterCounter(n->gt_2stdev, tv);
        c->gt_3stdev = StatsRegisterCounter(n->gt_3stdev, tv);
    }
}

struct ProfLiteStats {
    uint64_t tot;
    uint64_t cnt;
    uint64_t sd_cum; /**< stdev cumulative */
};

thread_local struct ProfLiteStats proflite_stats[PLT_SIZE] = { 0 };

static inline void Update(ThreadVars *tv, enum ProfileLiteTracker t, uint64_t usecs)
{
    struct ProfLiteStats *s = &proflite_stats[t];
    const struct ProfileLiteCounters *c = &profile_lite_counters[t];

    if (s->cnt > 1000) {
        const int64_t old_avg = s->tot / s->cnt;
        s->tot += usecs;
        s->cnt++;
        const int64_t new_avg = s->tot / s->cnt;
        const int64_t sd_sum_add = (usecs - old_avg) * (usecs - new_avg);
        s->sd_cum += sd_sum_add;
        double stdev = sqrt(s->sd_cum / (s->cnt - 1));
        if (usecs > new_avg + (stdev * 3)) {
            StatsIncr(tv, c->gt_3stdev);
        } else if (usecs > new_avg + (stdev * 2)) {
            StatsIncr(tv, c->gt_2stdev);
        } else if (usecs > new_avg + stdev) {
            StatsIncr(tv, c->gt_1stdev);
        }
        StatsSetUI64(tv, c->stdev, (uint64_t)stdev);
        StatsAddUI64(tv, c->avg, usecs);
        StatsIncr(tv, c->cnt);
        StatsSetUI64(tv, c->max, usecs);
    } else {
        s->tot += usecs;
        s->cnt++;
    }
}

void ProfliteAddPacket(ThreadVars *tv, Packet *p, const uint64_t flags)
{
    struct timeval endts;
    gettimeofday(&endts, NULL);

    if (unlikely(timercmp(&p->proflite_startts, &endts, >))) {
        memset(&p->proflite_startts, 0, sizeof(p->proflite_startts));
        return;
    }
    struct timeval e;
    timersub(&endts, &p->proflite_startts, &e);
    memset(&p->proflite_startts, 0, sizeof(p->proflite_startts));

    uint64_t usecs = (e.tv_sec * (uint64_t)1000000) + (e.tv_usec);
    if (flags & PROFLITE_ALL_BIT) {
        Update(tv, PLT_ALL, usecs);

        if (PKT_IS_PSEUDOPKT(p)) {
            Update(tv, PLT_ALL_PSEUDO, usecs);
        }

        if (p->payload_len) {
            Update(tv, PLT_ALL_PAYLOAD, usecs);
        } else {
            Update(tv, PLT_ALL_NOPAYLOAD, usecs);
        }
        if (flags & PROFLITE_ALERT_BIT) {
            if (p->alerts.cnt) {
                Update(tv, PLT_ALL_ALERT, usecs);
            } else {
                Update(tv, PLT_ALL_NOALERT, usecs);
            }
        }
    }
    if (p->ip4h || p->ip6h) {
        switch (p->proto) {
            case IPPROTO_TCP: {
                if (flags & PROFLITE_TCP_BIT) {
                    Update(tv, PLT_TCP_ALL, usecs);

                    if (p->tcph && p->tcph->th_flags & TH_SYN) {
                        Update(tv, PLT_TCP_SYN, usecs);
                    } else if (p->tcph && p->tcph->th_flags & TH_FIN) {
                        Update(tv, PLT_TCP_FIN, usecs);
                    } else if (p->tcph && p->tcph->th_flags & TH_RST) {
                        Update(tv, PLT_TCP_RST, usecs);
                    } else {
                        Update(tv, PLT_TCP_OTHER, usecs);
                    }
                }

                const AppProto alproto = p->flow ? p->flow->alproto : p->proflite_alproto;
                switch (alproto) {
                    case ALPROTO_HTTP1:
                        if (flags & PROFLITE_ALPROTO_HTTP_BIT) {
                            Update(tv, PLT_ALPROTO_HTTP, usecs);
                        }
                        break;
                    case ALPROTO_SMB:
                        if (flags & PROFLITE_ALPROTO_SMB_BIT) {
                            Update(tv, PLT_ALPROTO_SMB, usecs);
                        }
                        break;
                    case ALPROTO_NFS:
                        if (flags & PROFLITE_ALPROTO_NFS_BIT) {
                            Update(tv, PLT_ALPROTO_NFS, usecs);
                        }
                        break;
                    case ALPROTO_SMTP:
                        if (flags & PROFLITE_ALPROTO_SMTP_BIT) {
                            Update(tv, PLT_ALPROTO_SMTP, usecs);
                        }
                        break;
                    case ALPROTO_DNS:
                        if (flags & PROFLITE_ALPROTO_DNS_BIT) {
                            Update(tv, PLT_ALPROTO_DNS, usecs);
                        }
                        break;
                    case ALPROTO_SSH:
                        if (flags & PROFLITE_ALPROTO_SSH_BIT) {
                            Update(tv, PLT_ALPROTO_SSH, usecs);
                        }
                        break;
                    case ALPROTO_DCERPC:
                        if (flags & PROFLITE_ALPROTO_DCERPC_BIT) {
                            Update(tv, PLT_ALPROTO_DCERPC, usecs);
                        }
                        break;
                    case ALPROTO_FTP:
                        if (flags & PROFLITE_ALPROTO_FTP_BIT) {
                            Update(tv, PLT_ALPROTO_FTP, usecs);
                        }
                        break;
                    case ALPROTO_FTPDATA:
                        if (flags & PROFLITE_ALPROTO_FTPDATA_BIT) {
                            Update(tv, PLT_ALPROTO_FTPDATA, usecs);
                        }
                        break;
                    case ALPROTO_TLS:
                        if (flags & PROFLITE_ALPROTO_TLS_BIT) {
                            Update(tv, PLT_ALPROTO_TLS, usecs);
                        }
                        break;
                    case ALPROTO_HTTP2:
                        if (flags & PROFLITE_ALPROTO_HTTP2_BIT) {
                            Update(tv, PLT_ALPROTO_HTTP2, usecs);
                        }
                        break;
                    case ALPROTO_UNKNOWN:
                    case ALPROTO_FAILED:
                        if (flags & PROFLITE_ALPROTO_NONE_BIT) {
                            Update(tv, PLT_ALPROTO_NONE, usecs);
                        }
                        break;
                    default:
                        if (flags & PROFLITE_ALPROTO_OTHER_BIT) {
                            Update(tv, PLT_ALPROTO_OTHER, usecs);
                        }
                        break;
                }
                break;
            }
            case IPPROTO_UDP: {
                if (flags & PROFLITE_UDP_BIT) {
                    Update(tv, PLT_UDP, usecs);
                }

                const AppProto alproto = p->flow ? p->flow->alproto : p->proflite_alproto;
                if (alproto == ALPROTO_DNS) {
                    if (flags & PROFLITE_ALPROTO_DNS_BIT) {
                        Update(tv, PLT_ALPROTO_DNS, usecs);
                    }
                } else if (alproto == ALPROTO_SNMP) {
                    if (flags & PROFLITE_ALPROTO_SNMP_BIT) {
                        Update(tv, PLT_ALPROTO_SNMP, usecs);
                    }
                } else if (alproto == ALPROTO_DHCP) {
                    if (flags & PROFLITE_ALPROTO_DHCP_BIT) {
                        Update(tv, PLT_ALPROTO_DHCP, usecs);
                    }
                } else if (alproto == ALPROTO_TFTP) {
                    if (flags & PROFLITE_ALPROTO_TFTP_BIT) {
                        Update(tv, PLT_ALPROTO_TFTP, usecs);
                    }
                } else if (alproto == ALPROTO_UNKNOWN || alproto == ALPROTO_FAILED) {
                    if (flags & PROFLITE_ALPROTO_NONE_BIT) {
                        Update(tv, PLT_ALPROTO_NONE, usecs);
                    }
                } else {
                    if (flags & PROFLITE_ALPROTO_OTHER_BIT) {
                        Update(tv, PLT_ALPROTO_OTHER, usecs);
                    }
                }
                break;
            }
            case IPPROTO_ICMP:
                if (flags & PROFLITE_ICMP4_BIT) {
                    Update(tv, PLT_ICMP4, usecs);
                }
                break;
            case IPPROTO_ICMPV6:
                if (flags & PROFLITE_ICMP6_BIT) {
                    Update(tv, PLT_ICMP6, usecs);
                }
                break;
            default:
                if (flags & PROFLITE_OTHERIP_BIT) {
                    Update(tv, PLT_OTHERIP, usecs);
                }
                break;
        }
    } else {
        if (flags & PROFLITE_OTHER_BIT) {
            Update(tv, PLT_OTHER, usecs);
        }
    }
}

void ProfliteDump(void)
{
    for (int idx = 0; idx < PLT_SIZE; idx++) {
        const struct ProfLiteStats *s = &proflite_stats[idx];
        if (s->cnt > 1000) {
            int64_t avg = s->tot / s->cnt;
            double stdev = sqrt(s->sd_cum / (s->cnt - 1));

            const char *str = PltToString(idx);
            SCLogNotice(
                    "(%s): avg %" PRIi64 ", stdev %0.1f, cnt %" PRIu64, str, avg, stdev, s->cnt);
        }
    }
}
#endif /* PROFILING_LITE */

#if defined(PROFILING)
#define CASE_CODE(E)  case E: return #E

/**
 * \brief Maps the PacketProfileDetectId, to its string equivalent
 *
 * \param id PacketProfileDetectId id
 *
 * \retval string equivalent for the PacketProfileDetectId id
 */
const char * PacketProfileDetectIdToString(PacketProfileDetectId id)
{
    switch (id) {
        CASE_CODE (PROF_DETECT_SETUP);
        CASE_CODE (PROF_DETECT_GETSGH);
        CASE_CODE (PROF_DETECT_IPONLY);
        CASE_CODE (PROF_DETECT_RULES);
        CASE_CODE (PROF_DETECT_PF_PKT);
        CASE_CODE (PROF_DETECT_PF_PAYLOAD);
        CASE_CODE (PROF_DETECT_PF_TX);
        CASE_CODE (PROF_DETECT_PF_SORT1);
        CASE_CODE (PROF_DETECT_PF_SORT2);
        CASE_CODE (PROF_DETECT_NONMPMLIST);
        CASE_CODE (PROF_DETECT_TX);
        CASE_CODE (PROF_DETECT_ALERT);
        CASE_CODE (PROF_DETECT_TX_UPDATE);
        CASE_CODE (PROF_DETECT_CLEANUP);
        default:
            return "UNKNOWN";
    }
}

/**
 * \brief Maps the LoggerId's to its string equivalent for profiling output.
 *
 * \param id LoggerId id
 *
 * \retval string equivalent for the LoggerId id
 */
const char * PacketProfileLoggertIdToString(LoggerId id)
{
    switch (id) {
        CASE_CODE (LOGGER_UNDEFINED);
        CASE_CODE (LOGGER_ALERT_DEBUG);
        CASE_CODE (LOGGER_ALERT_FAST);
        CASE_CODE (LOGGER_ALERT_SYSLOG);
        CASE_CODE (LOGGER_JSON_ALERT);
        CASE_CODE (LOGGER_JSON_ANOMALY);
        CASE_CODE (LOGGER_JSON_DROP);
        CASE_CODE (LOGGER_JSON_SSH);
        CASE_CODE (LOGGER_JSON_SMB);
        CASE_CODE (LOGGER_JSON_NFS);
        CASE_CODE (LOGGER_HTTP);
        CASE_CODE(LOGGER_JSON_DNS);
        CASE_CODE (LOGGER_JSON_DNP3_TS);
        CASE_CODE (LOGGER_JSON_DNP3_TC);
        CASE_CODE (LOGGER_JSON_HTTP);
        CASE_CODE (LOGGER_JSON_DHCP);
        CASE_CODE (LOGGER_JSON_KRB5);
        CASE_CODE(LOGGER_JSON_IKE);
        CASE_CODE(LOGGER_JSON_MODBUS);
        CASE_CODE (LOGGER_JSON_FTP);
        CASE_CODE (LOGGER_JSON_TFTP);
        CASE_CODE (LOGGER_JSON_SMTP);
        CASE_CODE (LOGGER_JSON_SNMP);
        CASE_CODE (LOGGER_JSON_TLS);
        CASE_CODE (LOGGER_JSON_SIP);
        CASE_CODE (LOGGER_JSON_TEMPLATE_RUST);
        CASE_CODE (LOGGER_JSON_RFB);
        CASE_CODE (LOGGER_JSON_MQTT);
        CASE_CODE (LOGGER_JSON_TEMPLATE);
        CASE_CODE (LOGGER_JSON_RDP);
        CASE_CODE (LOGGER_JSON_DCERPC);
        CASE_CODE (LOGGER_JSON_HTTP2);
        CASE_CODE (LOGGER_TLS_STORE);
        CASE_CODE (LOGGER_TLS);
        CASE_CODE (LOGGER_FILE_STORE);
        CASE_CODE (LOGGER_JSON_FILE);
        CASE_CODE (LOGGER_TCP_DATA);
        CASE_CODE (LOGGER_JSON_FLOW);
        CASE_CODE (LOGGER_JSON_NETFLOW);
        CASE_CODE (LOGGER_STATS);
        CASE_CODE (LOGGER_JSON_STATS);
        CASE_CODE (LOGGER_PCAP);
        CASE_CODE (LOGGER_JSON_METADATA);
        case LOGGER_SIZE:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

#ifdef UNITTESTS

static int
ProfilingGenericTicksTest01(void)
{
#define TEST_RUNS 1024
    uint64_t ticks_start = 0;
    uint64_t ticks_end = 0;
    void *ptr[TEST_RUNS];
    unsigned int i;

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        ptr[i] = SCMalloc(1024);
    }
    ticks_end = UtilCpuGetTicks();
    printf("malloc(1024) %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCFree(ptr[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCFree(1024) %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    SCMutex m[TEST_RUNS];

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCMutexInit(&m[i], NULL);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCMutexInit() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCMutexLock(&m[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCMutexLock() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCMutexUnlock(&m[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCMutexUnlock() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCMutexDestroy(&m[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCMutexDestroy() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    SCSpinlock s[TEST_RUNS];

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCSpinInit(&s[i], 0);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCSpinInit() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCSpinLock(&s[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCSpinLock() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCSpinUnlock(&s[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCSpinUnlock() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SCSpinDestroy(&s[i]);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SCSpinDestroy() %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    SC_ATOMIC_DECL_AND_INIT(unsigned int, test);
    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        (void) SC_ATOMIC_ADD(test,1);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SC_ATOMIC_ADD %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);

    ticks_start = UtilCpuGetTicks();
    for (i = 0; i < TEST_RUNS; i++) {
        SC_ATOMIC_CAS(&test,i,i+1);
    }
    ticks_end = UtilCpuGetTicks();
    printf("SC_ATOMIC_CAS %"PRIu64"\n", (ticks_end - ticks_start)/TEST_RUNS);
    return 1;
}

#endif /* UNITTESTS */

void
SCProfilingRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("ProfilingGenericTicksTest01", ProfilingGenericTicksTest01);
#endif /* UNITTESTS */
}

#endif /* PROFILING */
