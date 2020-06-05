/* Copyright (C) 2020 Open Information Security Foundation
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
 * Implement JSON/eve logging app-layer BitTorrent DHT.
 */

#include "suricata-common.h"
#include "debug.h"
#include "detect.h"
#include "pkt-var.h"
#include "conf.h"

#include "threads.h"
#include "threadvars.h"
#include "tm-threads.h"

#include "util-unittest.h"
#include "util-buffer.h"
#include "util-debug.h"
#include "util-byte.h"

#include "output.h"
#include "output-json.h"

#include "app-layer.h"
#include "app-layer-parser.h"

#include "output-json-bittorrent-dht.h"
#include "rust.h"

typedef struct LogBitTorrentDHTFileCtx_ {
    LogFileCtx *file_ctx;
    uint32_t flags;
} LogBitTorrentDHTFileCtx;

typedef struct LogBitTorrentDHTLogThread_ {
    LogBitTorrentDHTFileCtx *bittorrent_dht_log_ctx;
    LogFileCtx *file_ctx;
    MemBuffer *buffer;
} LogBitTorrentDHTLogThread;

static int JsonBitTorrentDHTLogger(ThreadVars *tv, void *thread_data, const Packet *p, Flow *f,
        void *state, void *tx, uint64_t tx_id)
{
    LogBitTorrentDHTLogThread *thread = thread_data;

    JsonBuilder *js = CreateEveHeader(p, LOG_DIR_PACKET, "bittorrent-dht", NULL);
    if (unlikely(js == NULL)) {
        return TM_ECODE_FAILED;
    }

    jb_open_object(js, "bittorrent-dht");
    if (!rs_bittorrent_dht_logger_log(tx, js)) {
        goto error;
    }
    jb_close(js);

    MemBufferReset(thread->buffer);
    OutputJsonBuilderBuffer(js, thread->file_ctx, &thread->buffer);
    jb_free(js);

    return TM_ECODE_OK;

error:
    jb_free(js);
    return TM_ECODE_FAILED;
}

static void OutputBitTorrentDHTLogDeInitCtxSub(OutputCtx *output_ctx)
{
    LogBitTorrentDHTFileCtx *bittorrent_dht_log_ctx = (LogBitTorrentDHTFileCtx *)output_ctx->data;
    SCFree(bittorrent_dht_log_ctx);
    SCFree(output_ctx);
}

static OutputInitResult OutputBitTorrentDHTLogInitSub(ConfNode *conf, OutputCtx *parent_ctx)
{
    OutputInitResult result = { NULL, false };
    OutputJsonCtx *ajt = parent_ctx->data;

    LogBitTorrentDHTFileCtx *bittorrent_dht_log_ctx = SCCalloc(1, sizeof(*bittorrent_dht_log_ctx));
    if (unlikely(bittorrent_dht_log_ctx == NULL)) {
        return result;
    }
    bittorrent_dht_log_ctx->file_ctx = ajt->file_ctx;

    OutputCtx *output_ctx = SCCalloc(1, sizeof(*output_ctx));
    if (unlikely(output_ctx == NULL)) {
        SCFree(bittorrent_dht_log_ctx);
        return result;
    }
    output_ctx->data = bittorrent_dht_log_ctx;
    output_ctx->DeInit = OutputBitTorrentDHTLogDeInitCtxSub;

    AppLayerParserRegisterLogger(IPPROTO_UDP, ALPROTO_BITTORRENT_DHT);

    result.ctx = output_ctx;
    result.ok = true;
    return result;
}

static TmEcode JsonBitTorrentDHTLogThreadInit(ThreadVars *t, const void *initdata, void **data)
{
    LogBitTorrentDHTLogThread *thread = SCCalloc(1, sizeof(*thread));
    if (unlikely(thread == NULL)) {
        return TM_ECODE_FAILED;
    }

    if (initdata == NULL) {
        SCLogDebug("Error getting context for EveLogBitTorrentDHT.  \"initdata\" is NULL.");
        goto error_exit;
    }

    thread->buffer = MemBufferCreateNew(JSON_OUTPUT_BUFFER_SIZE);
    if (unlikely(thread->buffer == NULL)) {
        goto error_exit;
    }

    thread->bittorrent_dht_log_ctx = ((OutputCtx *)initdata)->data;
    thread->file_ctx = LogFileEnsureExists(thread->bittorrent_dht_log_ctx->file_ctx, t->id);
    if (!thread->file_ctx) {
        goto error_exit;
    }
    *data = (void *)thread;

    return TM_ECODE_OK;

error_exit:
    if (thread->buffer != NULL) {
        MemBufferFree(thread->buffer);
    }
    SCFree(thread);
    return TM_ECODE_FAILED;
}

static TmEcode JsonBitTorrentDHTLogThreadDeinit(ThreadVars *t, void *data)
{
    LogBitTorrentDHTLogThread *thread = (LogBitTorrentDHTLogThread *)data;
    if (thread == NULL) {
        return TM_ECODE_OK;
    }
    if (thread->buffer != NULL) {
        MemBufferFree(thread->buffer);
    }
    SCFree(thread);
    return TM_ECODE_OK;
}

void JsonBitTorrentDHTLogRegister(void)
{
    if (ConfGetNode("app-layer.protocols.bittorrent-dht") == NULL) {
        return;
    }

    /* Register as an eve sub-module. */
    OutputRegisterTxSubModule(LOGGER_JSON_BITTORRENT_DHT, "eve-log", "JsonBitTorrentDHTLog",
            "eve-log.bittorrent-dht", OutputBitTorrentDHTLogInitSub, ALPROTO_BITTORRENT_DHT,
            JsonBitTorrentDHTLogger, JsonBitTorrentDHTLogThreadInit,
            JsonBitTorrentDHTLogThreadDeinit, NULL);
}
