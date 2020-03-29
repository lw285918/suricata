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
 * \author Anoop Saldanha <anoopsaldanha@gmail.com>
 */

#include "suricata-common.h"
#include "threads.h"
#include "decode.h"

#include "app-layer.h"
#include "app-layer-protos.h"
#include "app-layer-parser.h"
#include "app-layer-smtp.h"
#include "detect.h"
#include "detect-parse.h"
#include "detect/keywords/engine.h"
#include "detect-engine-state.h"
#include "detect/keywords/app-layer-event.h"

#include "flow.h"
#include "flow-var.h"
#include "flow-util.h"

#include "decoders/events.h"
#include "util/byte.h"
#include "util/debug.h"
#include "util/unittest.h"
#include "util/unittest-helper.h"
#include "stream-tcp-util.h"

#define MAX_ALPROTO_NAME 50

static int DetectAppLayerEventPktMatch(DetectEngineThreadCtx *det_ctx,
                                       Packet *p, const Signature *s, const SigMatchCtx *ctx);
static int DetectAppLayerEventSetupP1(DetectEngineCtx *, Signature *, const char *);
static void DetectAppLayerEventRegisterTests(void);
static void DetectAppLayerEventFree(void *);
static int DetectEngineAptEventInspect(ThreadVars *tv,
        DetectEngineCtx *de_ctx, DetectEngineThreadCtx *det_ctx,
        const Signature *s, const SigMatchData *smd,
        Flow *f, uint8_t flags, void *alstate,
        void *tx, uint64_t tx_id);
static int g_applayer_events_list_id = 0;

/**
 * \brief Registers the keyword handlers for the "app-layer-event" keyword.
 */
void DetectAppLayerEventRegister(void)
{
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].name = "app-layer-event";
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].desc = "match on events generated by the App Layer Parsers and the protocol detection engine";
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].url = DOC_URL DOC_VERSION "/rules/app-layer.html#app-layer-event";
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].Match =
        DetectAppLayerEventPktMatch;
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].Setup = DetectAppLayerEventSetupP1;
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].Free = DetectAppLayerEventFree;
    sigmatch_table[DETECT_AL_APP_LAYER_EVENT].RegisterTests =
        DetectAppLayerEventRegisterTests;

    DetectAppLayerInspectEngineRegister("app-layer-events",
            ALPROTO_UNKNOWN, SIG_FLAG_TOSERVER, 0,
            DetectEngineAptEventInspect);
    DetectAppLayerInspectEngineRegister("app-layer-events",
            ALPROTO_UNKNOWN, SIG_FLAG_TOCLIENT, 0,
            DetectEngineAptEventInspect);

    g_applayer_events_list_id = DetectBufferTypeGetByName("app-layer-events");
}

static int DetectEngineAptEventInspect(ThreadVars *tv,
        DetectEngineCtx *de_ctx, DetectEngineThreadCtx *det_ctx,
        const Signature *s, const SigMatchData *smd,
        Flow *f, uint8_t flags, void *alstate,
        void *tx, uint64_t tx_id)
{
    int r = 0;
    const AppProto alproto = f->alproto;
    AppLayerDecoderEvents *decoder_events =
        AppLayerParserGetEventsByTx(f->proto, alproto, tx);
    if (decoder_events == NULL)
        goto end;

    while (1) {
        DetectAppLayerEventData *aled = (DetectAppLayerEventData *)smd->ctx;
        KEYWORD_PROFILING_START;

        if (AppLayerDecoderEventsIsEventSet(decoder_events, aled->event_id)) {
            KEYWORD_PROFILING_END(det_ctx, smd->type, 1);

            if (smd->is_last)
                break;
            smd++;
            continue;
        }

        KEYWORD_PROFILING_END(det_ctx, smd->type, 0);
        goto end;
    }

    r = 1;

 end:
    if (r == 1) {
        return DETECT_ENGINE_INSPECT_SIG_MATCH;
    } else {
        if (AppLayerParserGetStateProgress(f->proto, alproto, tx, flags) ==
            AppLayerParserGetStateProgressCompletionStatus(alproto, flags))
        {
            return DETECT_ENGINE_INSPECT_SIG_CANT_MATCH;
        } else {
            return DETECT_ENGINE_INSPECT_SIG_NO_MATCH;
        }
    }
}


static int DetectAppLayerEventPktMatch(DetectEngineThreadCtx *det_ctx,
                                Packet *p, const Signature *s, const SigMatchCtx *ctx)
{
    const DetectAppLayerEventData *aled = (const DetectAppLayerEventData *)ctx;

    return AppLayerDecoderEventsIsEventSet(p->app_layer_events,
                                           aled->event_id);
}

static DetectAppLayerEventData *DetectAppLayerEventParsePkt(const char *arg,
                                                            AppLayerEventType *event_type)
{
    int event_id = 0;
    int r = AppLayerGetPktEventInfo(arg, &event_id);
    if (r < 0) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword "
                   "supplied with packet based event - \"%s\" that isn't "
                   "supported yet.", arg);
        return NULL;
    }

    DetectAppLayerEventData *aled = SCCalloc(1, sizeof(DetectAppLayerEventData));
    if (unlikely(aled == NULL))
        return NULL;
    aled->event_id = event_id;
    *event_type = APP_LAYER_EVENT_TYPE_PACKET;

    return aled;
}

/** \retval int 0 ok
  * \retval int -1 error
  * \retval int -3 non-fatal error: sig will be rejected w/o raising error
  */
static int DetectAppLayerEventParseAppP2(DetectAppLayerEventData *data,
                                         uint8_t *ipproto_bitarray,
                                         AppLayerEventType *event_type)
{
    int event_id = 0;
    uint8_t ipproto;
    char alproto_name[MAX_ALPROTO_NAME];
    int r = 0;

    const char *p_idx = strchr(data->arg, '.');
    if (strlen(data->arg) > MAX_ALPROTO_NAME) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword is too long or malformed");
        return -1;
    }
    strlcpy(alproto_name, data->arg, p_idx - data->arg + 1);

    if (ipproto_bitarray[IPPROTO_TCP / 8] & 1 << (IPPROTO_TCP % 8)) {
        ipproto = IPPROTO_TCP;
    } else if (ipproto_bitarray[IPPROTO_UDP / 8] & 1 << (IPPROTO_UDP % 8)) {
        ipproto = IPPROTO_UDP;
    } else {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "protocol %s is disabled", alproto_name);
        return -1;
    }

    if (!data->needs_detctx) {
        r = AppLayerParserGetEventInfo(ipproto, data->alproto,
                            p_idx + 1, &event_id, event_type);
    } else {
        r = DetectEngineGetEventInfo(p_idx + 1, &event_id, event_type);
    }
    if (r < 0) {
        if (SigMatchStrictEnabled(DETECT_AL_APP_LAYER_EVENT)) {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword's "
                    "protocol \"%s\" doesn't have event \"%s\" registered",
                    alproto_name, p_idx + 1);
            return -1;
        } else {
            SCLogWarning(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword's "
                    "protocol \"%s\" doesn't have event \"%s\" registered",
                    alproto_name, p_idx + 1);
            return -3;
        }
    }
    data->event_id = event_id;

    return 0;
}

static DetectAppLayerEventData *DetectAppLayerEventParseAppP1(const char *arg)
{
    /* period index */
    char alproto_name[MAX_ALPROTO_NAME];
    bool needs_detctx = false;

    const char *p_idx = strchr(arg, '.');
    if (strlen(arg) > MAX_ALPROTO_NAME) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword is too long or malformed");
        return NULL;
    }
    /* + 1 for trailing \0 */
    strlcpy(alproto_name, arg, p_idx - arg + 1);

    const AppProto alproto = AppLayerGetProtoByName(alproto_name);
    if (alproto == ALPROTO_UNKNOWN) {
        if (!strcmp(alproto_name, "file")) {
            needs_detctx = true;
        } else {
            SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword "
                       "supplied with unknown protocol \"%s\"",
                       alproto_name);
            return NULL;
        }
    }

    DetectAppLayerEventData *aled = SCCalloc(1, sizeof(*aled));
    if (unlikely(aled == NULL))
        return NULL;
    aled->alproto = alproto;
    aled->arg = SCStrdup(arg);
    if (aled->arg == NULL) {
        SCFree(aled);
        return NULL;
    }
    aled->needs_detctx = needs_detctx;

    return aled;
}

static DetectAppLayerEventData *DetectAppLayerEventParse(const char *arg,
                                                         AppLayerEventType *event_type)
{
    *event_type = 0;

    if (arg == NULL) {
        SCLogError(SC_ERR_INVALID_SIGNATURE, "app-layer-event keyword supplied "
                   "with no arguments.  This keyword needs an argument.");
        return NULL;
    }

    while (*arg != '\0' && isspace((unsigned char)*arg))
        arg++;

    if (strchr(arg, '.') == NULL) {
        return DetectAppLayerEventParsePkt(arg, event_type);
    } else {
        return DetectAppLayerEventParseAppP1(arg);
    }
}

static int DetectAppLayerEventSetupP2(Signature *s,
                                      SigMatch *sm)
{
    AppLayerEventType event_type = 0;

    int ret = DetectAppLayerEventParseAppP2((DetectAppLayerEventData *)sm->ctx,
            s->proto.proto, &event_type);
    if (ret < 0) {
        /* DetectAppLayerEventParseAppP2 prints errors */

        /* sm has been removed from lists by DetectAppLayerEventPrepare */
        SigMatchFree(sm);
        return ret;
    }
    SigMatchAppendSMToList(s, sm, g_applayer_events_list_id);
    /* We should have set this flag already in SetupP1 */
    s->flags |= SIG_FLAG_APPLAYER;

    return 0;
}

static int DetectAppLayerEventSetupP1(DetectEngineCtx *de_ctx, Signature *s, const char *arg)
{
    AppLayerEventType event_type;

    DetectAppLayerEventData *data = DetectAppLayerEventParse(arg, &event_type);
    if (data == NULL)
        SCReturnInt(-1);

    SigMatch *sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_AL_APP_LAYER_EVENT;
    sm->ctx = (SigMatchCtx *)data;

    if (event_type == APP_LAYER_EVENT_TYPE_PACKET) {
        SigMatchAppendSMToList(s, sm, DETECT_SM_LIST_MATCH);
    } else {
        if (DetectSignatureSetAppProto(s, data->alproto) != 0)
            goto error;

        SigMatchAppendSMToList(s, sm, g_applayer_events_list_id);
    }

    return 0;

error:
    if (data) {
        DetectAppLayerEventFree(data);
    }
    if (sm) {
        sm->ctx = NULL;
        SigMatchFree(sm);
    }
    return -1;
}

static void DetectAppLayerEventFree(void *ptr)
{
    DetectAppLayerEventData *data = (DetectAppLayerEventData *)ptr;
    if (data->arg != NULL)
        SCFree(data->arg);

    SCFree(ptr);

    return;
}

int DetectAppLayerEventPrepare(Signature *s)
{
    SigMatch *sm = s->init_data->smlists[g_applayer_events_list_id];
    SigMatch *smn;
    s->init_data->smlists[g_applayer_events_list_id] = NULL;
    s->init_data->smlists_tail[g_applayer_events_list_id] = NULL;

    while (sm != NULL) {
        // save it for later use in loop
        smn = sm->next;
        /* these will be overwritten in SigMatchAppendSMToList
         * called by DetectAppLayerEventSetupP2
         */
        sm->next = sm->prev = NULL;
        int ret = DetectAppLayerEventSetupP2(s, sm);
        if (ret < 0) {
            // current one was freed, let's free the next ones
            sm = smn;
            while(sm) {
                smn = sm->next;
                SigMatchFree(sm);
                sm = smn;
            }
            return ret;
        }
        sm = smn;
    }

    return 0;
}

/**********************************Unittests***********************************/

#ifdef UNITTESTS /* UNITTESTS */
#include "stream-tcp-private.h"
#include "stream-tcp-reassemble.h"
#include "stream-tcp.h"

#define APP_LAYER_EVENT_TEST_MAP_EVENT1 0
#define APP_LAYER_EVENT_TEST_MAP_EVENT2 1
#define APP_LAYER_EVENT_TEST_MAP_EVENT3 2
#define APP_LAYER_EVENT_TEST_MAP_EVENT4 3
#define APP_LAYER_EVENT_TEST_MAP_EVENT5 4
#define APP_LAYER_EVENT_TEST_MAP_EVENT6 5

SCEnumCharMap app_layer_event_test_map[ ] = {
    { "event1", APP_LAYER_EVENT_TEST_MAP_EVENT1 },
    { "event2", APP_LAYER_EVENT_TEST_MAP_EVENT2 },
    { "event3", APP_LAYER_EVENT_TEST_MAP_EVENT3 },
    { "event4", APP_LAYER_EVENT_TEST_MAP_EVENT4 },
    { "event5", APP_LAYER_EVENT_TEST_MAP_EVENT5 },
    { "event6", APP_LAYER_EVENT_TEST_MAP_EVENT6 },
};

static int DetectAppLayerEventTestGetEventInfo(const char *event_name,
                                               int *event_id,
                                               AppLayerEventType *event_type)
{
    *event_id = SCMapEnumNameToValue(event_name, app_layer_event_test_map);
    if (*event_id == -1) {
        SCLogError(SC_ERR_INVALID_ENUM_MAP, "event \"%s\" not present in "
                   "app-layer-event's test enum map table.",  event_name);
        /* this should be treated as fatal */
        return -1;
    }

    *event_type = APP_LAYER_EVENT_TYPE_TRANSACTION;

    return 0;
}


static int DetectAppLayerEventTest01(void)
{
    AppLayerParserBackupParserTable();
    AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_SMTP,
                            DetectAppLayerEventTestGetEventInfo);

    AppLayerEventType event_type;
    int result = 0;
    uint8_t ipproto_bitarray[256 / 8];
    memset(ipproto_bitarray, 0, sizeof(ipproto_bitarray));
    ipproto_bitarray[IPPROTO_TCP / 8] |= 1 << (IPPROTO_TCP % 8);

    DetectAppLayerEventData *aled = DetectAppLayerEventParse("smtp.event1",
                                                             &event_type);
    if (aled == NULL)
        goto end;
    if (DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0) {
        printf("failure 1\n");
        goto end;
    }
    if (aled->alproto != ALPROTO_SMTP ||
        aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT1) {
        printf("test failure.  Holding wrong state\n");
        goto end;
    }

    result = 1;

 end:
    AppLayerParserRestoreParserTable();
    if (aled != NULL)
        DetectAppLayerEventFree(aled);
    return result;
}

static int DetectAppLayerEventTest02(void)
{
    AppLayerParserBackupParserTable();

    AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_SMTP,
                            DetectAppLayerEventTestGetEventInfo);
    AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_HTTP,
                            DetectAppLayerEventTestGetEventInfo);
    AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_SMB,
                            DetectAppLayerEventTestGetEventInfo);
    AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_FTP,
                            DetectAppLayerEventTestGetEventInfo);

    AppLayerEventType event_type;
    uint8_t ipproto_bitarray[256 / 8];
    memset(ipproto_bitarray, 0, sizeof(ipproto_bitarray));
    ipproto_bitarray[IPPROTO_TCP / 8] |= 1 << (IPPROTO_TCP % 8);

    DetectAppLayerEventData *aled = DetectAppLayerEventParse("smtp.event1",
                                                             &event_type);
    FAIL_IF_NULL(aled);
    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);
    FAIL_IF(aled->alproto != ALPROTO_SMTP);
    FAIL_IF(aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT1);

    aled = DetectAppLayerEventParse("smtp.event4",
                                    &event_type);
    FAIL_IF_NULL(aled);
    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);
    FAIL_IF(aled->alproto != ALPROTO_SMTP);
    FAIL_IF(aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT4);

    aled = DetectAppLayerEventParse("http.event2",
                                    &event_type);
    FAIL_IF_NULL(aled);
    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);
    FAIL_IF(aled->alproto != ALPROTO_HTTP);
    FAIL_IF(aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT2);

    aled = DetectAppLayerEventParse("smb.event3",
                                    &event_type);
    FAIL_IF_NULL(aled);
    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);
    FAIL_IF(aled->alproto != ALPROTO_SMB);
    FAIL_IF(aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT3);

    aled = DetectAppLayerEventParse("ftp.event5",
                                    &event_type);
    FAIL_IF_NULL(aled);
    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);
    FAIL_IF(aled->alproto != ALPROTO_FTP);
    FAIL_IF(aled->event_id != APP_LAYER_EVENT_TEST_MAP_EVENT5);

    AppLayerParserRestoreParserTable();
    DetectAppLayerEventFree(aled);
    PASS;
}

static int DetectAppLayerEventTest03(void)
{
    ThreadVars tv;
    TcpReassemblyThreadCtx *ra_ctx = NULL;
    Packet *p = NULL;
    Flow *f = NULL;
    TcpSession ssn;
    TcpStream stream_ts, stream_tc;
    DetectEngineCtx *de_ctx = NULL;
    DetectEngineThreadCtx *det_ctx = NULL;

    uint8_t buf_ts[] = "GET /index.html HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "User-Agent: Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100402 Firefox/3.6.3\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-us,en;q=0.5\r\n"
        "Accept-Encoding: gzip,deflate\r\n"
        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
        "Keep-Alive: 115\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    uint8_t buf_tc[] = "HTTP/1.1 200 OK\r\n"
        "Date: Fri, 22 Oct 2010 12:31:08 GMT\r\n"
        "Server: Apache/2.2.15 (Unix) DAV/2\r\n"
        "Last-Modified: Sat, 20 Nov 2004 20:16:24 GMT\r\n"
        "ETag: \"ab8486-2c-3e9564c23b600\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 44\r\n"
        "Keep-Alive: timeout=5, max=100\r\n"
        "Connection: Keep-Alive\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<html><body><h1>It works!</h1></body></html>";

    memset(&tv, 0, sizeof (ThreadVars));
    memset(&ssn, 0, sizeof(TcpSession));
    memset(&stream_ts, 0, sizeof(TcpStream));
    memset(&stream_tc, 0, sizeof(TcpStream));

    ssn.data_first_seen_dir = STREAM_TOSERVER;

    de_ctx = DetectEngineCtxInit();
    FAIL_IF(de_ctx == NULL);
    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(app-layer-event: applayer_mismatch_protocol_both_directions; "
                               "sid:1;)");
    FAIL_IF(de_ctx->sig_list == NULL);
    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&tv, (void *)de_ctx, (void *)&det_ctx);

    f = UTHBuildFlow(AF_INET, "1.2.3.4", "1.2.3.5", 200, 220);
    FAIL_IF(f == NULL);
    FLOW_INITIALIZE(f);
    f->protoctx = &ssn;
    f->proto = IPPROTO_TCP;
    f->flags |= FLOW_IPV4;

    p = PacketGetFromAlloc();
    FAIL_IF(unlikely(p == NULL));
    p->flow = f;
    p->src.family = AF_INET;
    p->dst.family = AF_INET;
    p->proto = IPPROTO_TCP;

    StreamTcpUTInit(&ra_ctx);

    p->flowflags = FLOW_PKT_TOSERVER;
    TcpStream *stream = &stream_ts;
    FAIL_IF(AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_ts,
                             sizeof(buf_ts), STREAM_TOSERVER | STREAM_START) < 0);

    SigMatchSignatures(&tv, de_ctx, det_ctx, p);

    FAIL_IF (PacketAlertCheck(p, 1));

    p->flowflags = FLOW_PKT_TOCLIENT;
    stream = &stream_tc;
    FAIL_IF (AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_tc,
                              sizeof(buf_tc), STREAM_TOCLIENT | STREAM_START) < 0);

    SigMatchSignatures(&tv, de_ctx, det_ctx, p);

    FAIL_IF(PacketAlertCheck(p, 1));

    StreamTcpUTDeinit(ra_ctx);
    PASS;
}

static int DetectAppLayerEventTest04(void)
{
    ThreadVars tv;
    TcpReassemblyThreadCtx *ra_ctx = NULL;
    Packet *p = NULL;
    Flow *f = NULL;
    TcpSession ssn;
    TcpStream stream_ts, stream_tc;
    DetectEngineCtx *de_ctx = NULL;
    DetectEngineThreadCtx *det_ctx = NULL;

    uint8_t buf_ts[] = "GET /index.html HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "User-Agent: Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100402 Firefox/3.6.3\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-us,en;q=0.5\r\n"
        "Accept-Encoding: gzip,deflate\r\n"
        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
        "Keep-Alive: 115\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    uint8_t buf_tc[] = "XTTP/1.1 200 OK\r\n"
        "Date: Fri, 22 Oct 2010 12:31:08 GMT\r\n"
        "Server: Apache/2.2.15 (Unix) DAV/2\r\n"
        "Last-Modified: Sat, 20 Nov 2004 20:16:24 GMT\r\n"
        "ETag: \"ab8486-2c-3e9564c23b600\"\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Length: 44\r\n"
        "Keep-Alive: timeout=5, max=100\r\n"
        "Connection: Keep-Alive\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<html><body><h1>It works!</h1></body></html>";

    memset(&tv, 0, sizeof (ThreadVars));
    memset(&ssn, 0, sizeof(TcpSession));
    memset(&stream_ts, 0, sizeof(TcpStream));
    memset(&stream_tc, 0, sizeof(TcpStream));

    ssn.data_first_seen_dir = STREAM_TOSERVER;

    de_ctx = DetectEngineCtxInit();
    FAIL_IF (de_ctx == NULL);
    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(app-layer-event: applayer_detect_protocol_only_one_direction; "
                               "sid:1;)");
    FAIL_IF(de_ctx->sig_list == NULL);
    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&tv, (void *)de_ctx, (void *)&det_ctx);

    f = UTHBuildFlow(AF_INET, "1.2.3.4", "1.2.3.5", 200, 220);
    FAIL_IF (f == NULL);
    FLOW_INITIALIZE(f);
    f->protoctx = &ssn;
    f->proto = IPPROTO_TCP;
    f->flags |= FLOW_IPV4;

    p = PacketGetFromAlloc();
    FAIL_IF(unlikely(p == NULL));
    p->flow = f;
    p->src.family = AF_INET;
    p->dst.family = AF_INET;
    p->proto = IPPROTO_TCP;

    StreamTcpUTInit(&ra_ctx);

    p->flowflags = FLOW_PKT_TOSERVER;
    TcpStream *stream = &stream_ts;
    FAIL_IF(AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_ts,
                              sizeof(buf_ts), STREAM_TOSERVER | STREAM_START) < 0);
    SigMatchSignatures(&tv, de_ctx, det_ctx, p);
    FAIL_IF (PacketAlertCheck(p, 1));

    p->flowflags = FLOW_PKT_TOCLIENT;
    stream = &stream_tc;
    FAIL_IF (AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_tc,
                              sizeof(buf_tc), STREAM_TOCLIENT | STREAM_START) < 0);
    SigMatchSignatures(&tv, de_ctx, det_ctx, p);
    FAIL_IF (!PacketAlertCheck(p, 1));

    StreamTcpUTDeinit(ra_ctx);
    PASS;
}

static int DetectAppLayerEventTest05(void)
{
    ThreadVars tv;
    TcpReassemblyThreadCtx *ra_ctx = NULL;
    Packet *p = NULL;
    Flow *f = NULL;
    TcpSession ssn;
    TcpStream stream_ts, stream_tc;
    DetectEngineCtx *de_ctx = NULL;
    DetectEngineThreadCtx *det_ctx = NULL;

    uint8_t buf_ts[] = "GET /index.html HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "User-Agent: Mozilla/5.0 (X11; U; Linux x86_64; en-US; rv:1.9.2.3) Gecko/20100402 Firefox/3.6.3\r\n"
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
        "Accept-Language: en-us,en;q=0.5\r\n"
        "Accept-Encoding: gzip,deflate\r\n"
        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
        "Keep-Alive: 115\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    /* tls */
    uint8_t buf_tc[] = {
        0x16, 0x03, 0x01, 0x00, 0x86, 0x10, 0x00, 0x00,
        0x82, 0x00, 0x80, 0xd3, 0x6f, 0x1f, 0x63, 0x82,
        0x8d, 0x75, 0x77, 0x8c, 0x91, 0xbc, 0xa1, 0x3d,
        0xbb, 0xe1, 0xb5, 0xd3, 0x31, 0x92, 0x59, 0x2b,
        0x2c, 0x43, 0x96, 0xa3, 0xaa, 0x23, 0x92, 0xd0,
        0x91, 0x2a, 0x5e, 0x10, 0x5b, 0xc8, 0xc1, 0xe2,
        0xd3, 0x5c, 0x8b, 0x8c, 0x91, 0x9e, 0xc2, 0xf2,
        0x9c, 0x3c, 0x4f, 0x37, 0x1e, 0x20, 0x5e, 0x33,
        0xd5, 0xf0, 0xd6, 0xaf, 0x89, 0xf5, 0xcc, 0xb2,
        0xcf, 0xc1, 0x60, 0x3a, 0x46, 0xd5, 0x4e, 0x2a,
        0xb6, 0x6a, 0xb9, 0xfc, 0x32, 0x8b, 0xe0, 0x6e,
        0xa0, 0xed, 0x25, 0xa0, 0xa4, 0x82, 0x81, 0x73,
        0x90, 0xbf, 0xb5, 0xde, 0xeb, 0x51, 0x8d, 0xde,
        0x5b, 0x6f, 0x94, 0xee, 0xba, 0xe5, 0x69, 0xfa,
        0x1a, 0x80, 0x30, 0x54, 0xeb, 0x12, 0x01, 0xb9,
        0xfe, 0xbf, 0x82, 0x95, 0x01, 0x7b, 0xb0, 0x97,
        0x14, 0xc2, 0x06, 0x3c, 0x69, 0xfb, 0x1c, 0x66,
        0x47, 0x17, 0xd9, 0x14, 0x03, 0x01, 0x00, 0x01,
        0x01, 0x16, 0x03, 0x01, 0x00, 0x30, 0xf6, 0xbc,
        0x0d, 0x6f, 0xe8, 0xbb, 0xaa, 0xbf, 0x14, 0xeb,
        0x7b, 0xcc, 0x6c, 0x28, 0xb0, 0xfc, 0xa6, 0x01,
        0x2a, 0x97, 0x96, 0x17, 0x5e, 0xe8, 0xb4, 0x4e,
        0x78, 0xc9, 0x04, 0x65, 0x53, 0xb6, 0x93, 0x3d,
        0xeb, 0x44, 0xee, 0x86, 0xf9, 0x80, 0x49, 0x45,
        0x21, 0x34, 0xd1, 0xee, 0xc8, 0x9c,
    };

    memset(&tv, 0, sizeof (ThreadVars));
    memset(&ssn, 0, sizeof(TcpSession));
    memset(&stream_ts, 0, sizeof(TcpStream));
    memset(&stream_tc, 0, sizeof(TcpStream));

    ssn.data_first_seen_dir = STREAM_TOSERVER;

    de_ctx = DetectEngineCtxInit();
    FAIL_IF (de_ctx == NULL);
    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tcp any any -> any any "
                               "(app-layer-event: applayer_mismatch_protocol_both_directions; "
                               "sid:1;)");
    FAIL_IF (de_ctx->sig_list == NULL);
    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&tv, (void *)de_ctx, (void *)&det_ctx);

    f = UTHBuildFlow(AF_INET, "1.2.3.4", "1.2.3.5", 200, 220);
    FAIL_IF (f == NULL);
    FLOW_INITIALIZE(f);
    f->protoctx = &ssn;
    f->proto = IPPROTO_TCP;
    f->flags |= FLOW_IPV4;

    p = PacketGetFromAlloc();
    FAIL_IF (unlikely(p == NULL));
    p->flow = f;
    p->src.family = AF_INET;
    p->dst.family = AF_INET;
    p->proto = IPPROTO_TCP;

    StreamTcpUTInit(&ra_ctx);

    p->flowflags = FLOW_PKT_TOSERVER;
    TcpStream *stream = &stream_ts;
    FAIL_IF (AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_ts,
                              sizeof(buf_ts), STREAM_TOSERVER | STREAM_START) < 0);
    SigMatchSignatures(&tv, de_ctx, det_ctx, p);
    FAIL_IF (PacketAlertCheck(p, 1));

    p->flowflags = FLOW_PKT_TOCLIENT;
    stream = &stream_tc;
    FAIL_IF (AppLayerHandleTCPData(&tv, ra_ctx, p, f, &ssn, &stream, buf_tc,
                              sizeof(buf_tc), STREAM_TOCLIENT | STREAM_START) < 0);
    SigMatchSignatures(&tv, de_ctx, det_ctx, p);
    FAIL_IF (!PacketAlertCheck(p, 1));

    StreamTcpUTDeinit(ra_ctx);
    PASS;
}

static int DetectAppLayerEventTest06(void)
{
    AppLayerEventType event_type;
    uint8_t ipproto_bitarray[256 / 8];
    memset(ipproto_bitarray, 0, sizeof(ipproto_bitarray));
    ipproto_bitarray[IPPROTO_TCP / 8] |= 1 << (IPPROTO_TCP % 8);

    DetectAppLayerEventData *aled = DetectAppLayerEventParse("file.test",
                                                             &event_type);

    FAIL_IF_NULL(aled);

    FAIL_IF(DetectAppLayerEventParseAppP2(aled, ipproto_bitarray, &event_type) < 0);

    FAIL_IF(aled->alproto != ALPROTO_UNKNOWN);
    FAIL_IF(aled->event_id != DET_CTX_EVENT_TEST);

    DetectAppLayerEventFree(aled);
    PASS;
}
#endif /* UNITTESTS */

/**
 * \brief This function registers unit tests for "app-layer-event" keyword.
 */
void DetectAppLayerEventRegisterTests(void)
{
#ifdef UNITTESTS /* UNITTESTS */
    UtRegisterTest("DetectAppLayerEventTest01", DetectAppLayerEventTest01);
    UtRegisterTest("DetectAppLayerEventTest02", DetectAppLayerEventTest02);
    UtRegisterTest("DetectAppLayerEventTest03", DetectAppLayerEventTest03);
    UtRegisterTest("DetectAppLayerEventTest04", DetectAppLayerEventTest04);
    UtRegisterTest("DetectAppLayerEventTest05", DetectAppLayerEventTest05);
    UtRegisterTest("DetectAppLayerEventTest06", DetectAppLayerEventTest06);
#endif /* UNITTESTS */

    return;
}
