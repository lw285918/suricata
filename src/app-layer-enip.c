/* Copyright (C) 2015 Open Information Security Foundation
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
 * \author Kevin Wong <kwong@solananetworks.com>
 *
 * App-layer parser for ENIP protocol
 *
 */

#include "suricata-common.h"

#include "util-debug.h"
#include "util-byte.h"
#include "util-enum.h"
#include "util-mem.h"
#include "util-misc.h"

#include "stream.h"

#include "app-layer-protos.h"
#include "app-layer-parser.h"
#include "app-layer-enip.h"
#include "app-layer-enip-common.h"

#include "app-layer-detect-proto.h"

#include "conf.h"
#include "decode.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "util-byte.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "pkt-var.h"
#include "util-profiling.h"


SCEnumCharMap enip_decoder_event_table[ ] = {
    { NULL,                         -1 },
};

/** \brief get value for 'complete' status in ENIP
 *
 *  For ENIP we use a simple bool.
 */
int ENIPGetAlstateProgress(void *tx, uint8_t direction)
{

   // printf("ENIPGetAlstateProgress direction %d\n", direction);

    return 1;
}

/** \brief get value for 'complete' status in ENIP
 *
 *  For ENIP we use a simple bool.
 */
int ENIPGetAlstateProgressCompletionStatus(uint8_t direction)
{
  //  printf("ENIPGetAlstateProgressCompletionStatus direction %d\n", direction);

    return 1;
}



DetectEngineState *ENIPGetTxDetectState(void *vtx)
{
    ENIPTransaction *tx = (ENIPTransaction *)vtx;
    return tx->de_state;
}

int ENIPSetTxDetectState(void *state, void *vtx, DetectEngineState *s)
{
    ENIPTransaction *tx = (ENIPTransaction *)vtx;
    tx->de_state = s;
    return 0;
}



void *ENIPGetTx(void *alstate, uint64_t tx_id) {
    ENIPState         *enip = (ENIPState *) alstate;
    ENIPTransaction   *tx = NULL;

    if (enip->curr && enip->curr->tx_num == tx_id + 1)
        return enip->curr;

    TAILQ_FOREACH(tx, &enip->tx_list, next) {
        if (tx->tx_num != (tx_id+1))
            continue;

        SCLogDebug("returning tx %p", tx);
        return tx;
    }

    return NULL;
}

uint64_t ENIPGetTxCnt(void *alstate) {
    return ((uint64_t) ((ENIPState *) alstate)->transaction_max);
}



AppLayerDecoderEvents *ENIPGetEvents(void *state, uint64_t id) {
    ENIPState         *enip = (ENIPState *) state;
    ENIPTransaction   *tx;

    if (enip->curr && enip->curr->tx_num == (id + 1))
        return enip->curr->decoder_events;

    TAILQ_FOREACH(tx, &enip->tx_list, next) {
        if (tx->tx_num == (id+1))
            return tx->decoder_events;
    }

    return NULL;
}

int ENIPHasEvents(void *state) {
    return (((ENIPState *) state)->events > 0);
}


int ENIPStateGetEventInfo(const char *event_name, int *event_id, AppLayerEventType *event_type) {
    *event_id = SCMapEnumNameToValue(event_name, enip_decoder_event_table);

    if (*event_id == -1) {
        SCLogError(SC_ERR_INVALID_ENUM_MAP, "event \"%s\" not present in "
                   "enip's enum map table.",  event_name);
        /* yes this is fatal */
        return -1;
    }

    *event_type = APP_LAYER_EVENT_TYPE_TRANSACTION;

    return 0;
}

/** \brief Allocate enip state
 *
 *  return state
 */
void *ENIPStateAlloc(void)
{
    SCLogDebug("ENIPStateAlloc \n");
    void *s = SCMalloc(sizeof(ENIPState));
    if (unlikely(s == NULL))
        return NULL;

    memset(s, 0, sizeof(ENIPState));

    ENIPState *enip_state = (ENIPState *) s;

    TAILQ_INIT(&enip_state->tx_list);
    return s;
}

/** \internal
 *  \brief Free a ENIP TX
 *  \param tx ENIP TX to free */
static void ENIPTransactionFree(ENIPTransaction *tx, ENIPState *state)
{
    SCEnter();
    SCLogDebug("ENIPTransactionFree \n");
    CIPServiceEntry *svc = NULL;
    while ((svc = TAILQ_FIRST(&tx->service_list)))
    {
        TAILQ_REMOVE(&tx->service_list, svc, next);

        SegmentEntry *seg = NULL;
        while ((seg = TAILQ_FIRST(&svc->segment_list)))
        {
            TAILQ_REMOVE(&svc->segment_list, seg, next);
            SCFree(seg);
        }

        AttributeEntry *attr = NULL;
        while ((attr = TAILQ_FIRST(&svc->attrib_list)))
        {
            TAILQ_REMOVE(&svc->attrib_list, attr, next);
            SCFree(attr);
        }

        SCFree(svc);
    }

    AppLayerDecoderEventsFreeEvents(&tx->decoder_events);

    if (tx->de_state != NULL)
    {
        DetectEngineStateFree(tx->de_state);

        state->tx_with_detect_state_cnt--;
    }

    if (state->iter == tx)
        state->iter = NULL;

    SCFree(tx);
    SCReturn;
}

/** \brief Free enip state
 *
 */
void ENIPStateFree(void *s)
{
    SCEnter();
    SCLogDebug("ENIPStateFree \n");
    if (s)
    {
        ENIPState *enip_state = (ENIPState *) s;

        ENIPTransaction *tx = NULL;
        while ((tx = TAILQ_FIRST(&enip_state->tx_list)))
        {
            TAILQ_REMOVE(&enip_state->tx_list, tx, next);
            ENIPTransactionFree(tx, enip_state);
        }

        if (enip_state->buffer != NULL)
        {
            SCFree(enip_state->buffer);
        }

        SCFree(s);
    }
    SCReturn;
}

/** \internal
 *  \brief Allocate a ENIP TX
 *  \retval tx or NULL */
static ENIPTransaction *ENIPTransactionAlloc(ENIPState *state)
{
    SCLogDebug("ENIPStateTransactionAlloc \n");
    ENIPTransaction *tx = (ENIPTransaction *) SCCalloc(1,
            sizeof(ENIPTransaction));
    if (unlikely(tx == NULL))
        return NULL;

    state->curr = tx;
    state->transaction_max++;

    memset(tx, 0x00, sizeof(ENIPTransaction));
    TAILQ_INIT(&tx->service_list);

    tx->enip  = state;
    tx->tx_num  = state->transaction_max;

    TAILQ_INSERT_TAIL(&state->tx_list, tx, next);

    return tx;

}

/**
 *  \brief enip transaction cleanup callback
 */
void ENIPStateTransactionFree(void *state, uint64_t tx_id)
{
    SCEnter();
    SCLogDebug("ENIPStateTransactionFree \n");
    ENIPState *enip_state = state;
    ENIPTransaction *tx = NULL;
    TAILQ_FOREACH(tx, &enip_state->tx_list, next)
    {

        if ((tx_id+1) < tx->tx_num)
        break;
        else if ((tx_id+1) > tx->tx_num)
        continue;

        if (tx == enip_state->curr)
        enip_state->curr = NULL;

        if (tx->decoder_events != NULL)
        {
            if (tx->decoder_events->cnt <= enip_state->events)
            enip_state->events -= tx->decoder_events->cnt;
            else
            enip_state->events = 0;
        }

        TAILQ_REMOVE(&enip_state->tx_list, tx, next);
        ENIPTransactionFree(tx, state);
        break;
    }
    SCReturn;
}

/** \internal
 *
 * \brief This function is called to retrieve a ENIP
 *
 * \param state     ENIP state structure for the parser
 * \param input     Input line of the command
 * \param input_len Length of the request
 *
 * \retval 1 when the command is parsed, 0 otherwise
 */
static int ENIPParse(Flow *f, void *state, AppLayerParserState *pstate,
        uint8_t *input, uint32_t input_len, void *local_data)
{
    SCEnter();
    ENIPState *enip = (ENIPState *) state;
    ENIPTransaction *tx;
 
    if (input == NULL && AppLayerParserStateIssetFlag(pstate,
            APP_LAYER_PARSER_EOF))
    {
        SCReturnInt(1);
    } else if (input == NULL || input_len == 0)
    {
        SCReturnInt(-1);
    }

    while (input_len > 0)
    {

        tx = ENIPTransactionAlloc(enip);

        if (tx == NULL)
            SCReturnInt(0);
        //SCLogDebug("ENIPParse input len %d\n", input_len);
        DecodeENIPPDU(input, input_len, tx);
        uint32_t pkt_len = tx->header.length + sizeof(ENIPEncapHdr);
        //SCLogDebug("ENIPParse packet len %d\n", pkt_len);
        if (pkt_len > input_len)
        {
            SCLogDebug("Invalid packet length \n");
            break;
        }

        input += pkt_len;
        input_len -= pkt_len;
        //SCLogDebug("remaining %d\n", input_len);

        if (input_len < sizeof(ENIPEncapHdr))
        {
            //SCLogDebug("Not enough data\n"); //not enough data for ENIP
            break;
        }

    }

    return 1;
}



static uint16_t ENIPProbingParser(uint8_t *input, uint32_t input_len,
        uint32_t *offset)
{
   // SCLogDebug("ENIPProbingParser %d\n", input_len);
    if (input_len < sizeof(ENIPEncapHdr))
    {
        SCLogDebug("Length too small to be a ENIP header \n");
        return ALPROTO_UNKNOWN;
    }

    return ALPROTO_ENIP;
}

/**
 * \brief Function to register the ENIP protocol parsers and other functions
 */
void RegisterENIPUDPParsers(void)
{
    SCEnter();
    char *proto_name = "enip";

    if (AppLayerProtoDetectConfProtoDetectionEnabled("udp", proto_name))
    {
        AppLayerProtoDetectRegisterProtocol(ALPROTO_ENIP, proto_name);

        if (RunmodeIsUnittests())
        {
            AppLayerProtoDetectPPRegister(IPPROTO_UDP, "44818", ALPROTO_ENIP,
                    0, sizeof(ENIPEncapHdr), STREAM_TOSERVER, ENIPProbingParser);

            AppLayerProtoDetectPPRegister(IPPROTO_UDP, "44818", ALPROTO_ENIP,
                    0, sizeof(ENIPEncapHdr), STREAM_TOCLIENT, ENIPProbingParser);

        } else
        {

            if (!AppLayerProtoDetectPPParseConfPorts("udp", IPPROTO_UDP,
                    proto_name, ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr),
                    ENIPProbingParser))
            {
                SCLogDebug(
                        "no ENIP UDP config found enabling ENIP detection on port 44818.");

                AppLayerProtoDetectPPRegister(IPPROTO_UDP, "44818",
                        ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr), STREAM_TOSERVER,
                        ENIPProbingParser);

                AppLayerProtoDetectPPRegister(IPPROTO_UDP, "44818",
                        ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr), STREAM_TOCLIENT,
                        ENIPProbingParser);

            }
        }

    } else
    {
        printf("Protocol detection and parser disabled for %s protocol.",
                proto_name);
        return;
    }

    if (AppLayerParserConfParserEnabled("udp", proto_name))
    {

        AppLayerParserRegisterParser(IPPROTO_UDP, ALPROTO_ENIP,
                STREAM_TOSERVER, ENIPParse);
        AppLayerParserRegisterParser(IPPROTO_UDP, ALPROTO_ENIP,
                STREAM_TOCLIENT, ENIPParse);

        AppLayerParserRegisterStateFuncs(IPPROTO_UDP, ALPROTO_ENIP,
                ENIPStateAlloc, ENIPStateFree);


        AppLayerParserRegisterGetEventsFunc(IPPROTO_UDP, ALPROTO_ENIP, ENIPGetEvents);
        AppLayerParserRegisterHasEventsFunc(IPPROTO_UDP, ALPROTO_ENIP, ENIPHasEvents);

        AppLayerParserRegisterDetectStateFuncs(IPPROTO_UDP, ALPROTO_ENIP, NULL,
                                                       ENIPGetTxDetectState, ENIPSetTxDetectState);


        AppLayerParserRegisterGetTx(IPPROTO_UDP, ALPROTO_ENIP, ENIPGetTx);
        AppLayerParserRegisterGetTxCnt(IPPROTO_UDP, ALPROTO_ENIP, ENIPGetTxCnt);
        AppLayerParserRegisterTxFreeFunc(IPPROTO_UDP, ALPROTO_ENIP, ENIPStateTransactionFree);


        AppLayerParserRegisterGetStateProgressFunc(IPPROTO_UDP, ALPROTO_ENIP, ENIPGetAlstateProgress);
        AppLayerParserRegisterGetStateProgressCompletionStatus(IPPROTO_UDP, ALPROTO_ENIP, ENIPGetAlstateProgressCompletionStatus);

        AppLayerParserRegisterGetEventInfo(IPPROTO_UDP, ALPROTO_ENIP, ENIPStateGetEventInfo);

        AppLayerParserRegisterParserAcceptableDataDirection(IPPROTO_UDP,
                ALPROTO_ENIP, STREAM_TOSERVER | STREAM_TOCLIENT);

    } else
    {
        SCLogInfo(
                "Parsed disabled for %s protocol. Protocol detection" "still on.",
                proto_name);
    }

#ifdef UNITTESTS
    AppLayerParserRegisterProtocolUnittests(IPPROTO_UDP, ALPROTO_ENIP, ENIPParserRegisterTests);
#endif

    SCReturn;
}

/**
 * \brief Function to register the ENIP protocol parsers and other functions
 */
void RegisterENIPTCPParsers(void)
{
    SCEnter();
    char *proto_name = "enip";

    if (AppLayerProtoDetectConfProtoDetectionEnabled("tcp", proto_name))
    {
        AppLayerProtoDetectRegisterProtocol(ALPROTO_ENIP, proto_name);

        if (RunmodeIsUnittests())
        {
            AppLayerProtoDetectPPRegister(IPPROTO_TCP, "44818", ALPROTO_ENIP,
                    0, sizeof(ENIPEncapHdr), STREAM_TOSERVER, ENIPProbingParser);

            AppLayerProtoDetectPPRegister(IPPROTO_TCP, "44818", ALPROTO_ENIP,
                    0, sizeof(ENIPEncapHdr), STREAM_TOCLIENT, ENIPProbingParser);

        } else
        {

            if (!AppLayerProtoDetectPPParseConfPorts("tcp", IPPROTO_TCP,
                    proto_name, ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr),
                    ENIPProbingParser))
            {
                SCLogDebug(
                        "no ENIP TCP config found enabling ENIP detection on port 44818.");

                AppLayerProtoDetectPPRegister(IPPROTO_TCP, "44818",
                        ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr), STREAM_TOSERVER,
                        ENIPProbingParser);
                AppLayerProtoDetectPPRegister(IPPROTO_TCP, "44818",
                        ALPROTO_ENIP, 0, sizeof(ENIPEncapHdr), STREAM_TOCLIENT,
                        ENIPProbingParser);

            }
        }

    } else
    {
        SCLogDebug("Protocol detection and parser disabled for %s protocol.",
                proto_name);
        return;
    }

    if (AppLayerParserConfParserEnabled("tcp", proto_name))
    {
        AppLayerParserRegisterParser(IPPROTO_TCP, ALPROTO_ENIP,
                STREAM_TOSERVER, ENIPParse);
        AppLayerParserRegisterParser(IPPROTO_TCP, ALPROTO_ENIP,
                STREAM_TOCLIENT, ENIPParse);
        AppLayerParserRegisterStateFuncs(IPPROTO_TCP, ALPROTO_ENIP,
                ENIPStateAlloc, ENIPStateFree);

        AppLayerParserRegisterGetEventsFunc(IPPROTO_TCP, ALPROTO_ENIP, ENIPGetEvents);
        AppLayerParserRegisterHasEventsFunc(IPPROTO_TCP, ALPROTO_ENIP, ENIPHasEvents);

        AppLayerParserRegisterDetectStateFuncs(IPPROTO_TCP, ALPROTO_ENIP, NULL,
                                                       ENIPGetTxDetectState, ENIPSetTxDetectState);


        AppLayerParserRegisterGetTx(IPPROTO_TCP, ALPROTO_ENIP, ENIPGetTx);
        AppLayerParserRegisterGetTxCnt(IPPROTO_TCP, ALPROTO_ENIP, ENIPGetTxCnt);
        AppLayerParserRegisterTxFreeFunc(IPPROTO_TCP, ALPROTO_ENIP, ENIPStateTransactionFree);


        AppLayerParserRegisterGetStateProgressFunc(IPPROTO_TCP, ALPROTO_ENIP, ENIPGetAlstateProgress);
        AppLayerParserRegisterGetStateProgressCompletionStatus(IPPROTO_TCP, ALPROTO_ENIP, ENIPGetAlstateProgressCompletionStatus);

        AppLayerParserRegisterGetEventInfo(IPPROTO_TCP, ALPROTO_ENIP, ENIPStateGetEventInfo);

        AppLayerParserRegisterParserAcceptableDataDirection(IPPROTO_TCP,
                ALPROTO_ENIP, STREAM_TOSERVER | STREAM_TOCLIENT);

    } else
    {
        SCLogInfo(
                "Parsed disabled for %s protocol. Protocol detection" "still on.",
                proto_name);
    }

#ifdef UNITTESTS
    AppLayerParserRegisterProtocolUnittests(IPPROTO_TCP, ALPROTO_ENIP, ENIPParserRegisterTests);
#endif

    SCReturn;
}

/* UNITTESTS */
#ifdef UNITTESTS
#include "app-layer-parser.h"

#include "detect-parse.h"

#include "detect-engine.h"

#include "flow-util.h"

#include "stream-tcp.h"

#include "util-unittest.h"
#include "util-unittest-helper.h"


static uint8_t listIdentity[] = {/* List ID */    0x63, 0x00,
                                    /* Length */     0x00, 0x00,
                                    /* Session */    0x00, 0x00, 0x00, 0x00,
                                    /* Status */     0x00, 0x00, 0x00, 0x00,
                                    /*  Delay*/     0x00,
                                    /* Context */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    /* Quantity of coils */ 0x00, 0x00, 0x00, 0x00,};

/**
 * \brief Test if ENIP Packet matches signature
 */
int ALDecodeENIPTest(void)
{
    AppLayerParserThreadCtx *alp_tctx = AppLayerParserThreadCtxAlloc();
    Flow f;
    TcpSession ssn;

    int result = 0;

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));

    f.protoctx  = (void *)&ssn;
    f.proto     = IPPROTO_TCP;

    StreamTcpInitConfig(TRUE);

    SCMutexLock(&f.m);
    int r = AppLayerParserParse(alp_tctx, &f, ALPROTO_ENIP, STREAM_TOSERVER,
            listIdentity, sizeof(listIdentity));
    if (r != 0) {
        printf("toserver chunk 1 returned %" PRId32 ", expected 0: ", r);
        SCMutexUnlock(&f.m);
        goto end;
    }
    SCMutexUnlock(&f.m);

    ENIPState    *enip_state = f.alstate;
    if (enip_state == NULL) {
        printf("no enip state: ");
        goto end;
    }

    ENIPTransaction *tx = ENIPGetTx(enip_state, 0);

    if (tx->header.command != 99)  {
        printf("expected function %" PRIu8 ", got %" PRIu8 ": ", 99, tx->header.command);

        goto end;
    }

    result = 1;
end:
    if (alp_tctx != NULL)
        AppLayerParserThreadCtxFree(alp_tctx);
    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    return result;
}


#endif /* UNITTESTS */

void ENIPParserRegisterTests(void)
{
#ifdef UNITTESTS
      UtRegisterTest("ALDecodeENIPTest", ALDecodeENIPTest);


#endif /* UNITTESTS */
}
