/* Copyright (C) 2017 Open Information Security Foundation
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
 * \author Pierre Chifflier <chifflier@wzdftpd.net>
 *
 * Parser registration functions.
 */

#include "suricata-common.h"
#include "stream.h"
#include "conf.h"

#include "app-layer-detect-proto.h"
#include "app-layer-parser.h"

#include "app-layer-register.h"

static const char * IpProtoToString(int ip_proto);

AppProto AppLayerRegisterProtocolDetection(const struct AppLayerParser *p, int enable_default)
{
    AppProto alproto;
    const char *ip_proto_str = NULL;

    if (p == NULL)
        FatalError(SC_ERR_FATAL, "Call to %s with NULL pointer.", __FUNCTION__);

    alproto = StringToAppProto(p->name);
    if (alproto == ALPROTO_UNKNOWN || alproto == ALPROTO_FAILED)
        FatalError(SC_ERR_FATAL, "Unknown or invalid AppProto '%s'.", p->name);

    ip_proto_str = IpProtoToString(p->ip_proto);
    if (ip_proto_str == NULL)
        FatalError(SC_ERR_FATAL, "Unknown or unsupported ip_proto field in parser '%s'", p->name);

    SCLogDebug("%s %s protocol detection enabled.", ip_proto_str, p->name);

    AppLayerProtoDetectRegisterProtocol(alproto, p->name);

    if (RunmodeIsUnittests()) {

        SCLogDebug("Unittest mode, registering default configuration.");
        AppLayerProtoDetectPPRegister(p->ip_proto, p->default_port,
                alproto, p->min_depth, p->max_depth, STREAM_TOSERVER,
                p->ProbeTS, p->ProbeTC);

    }
    else {

        if (!AppLayerProtoDetectPPParseConfPorts(ip_proto_str, p->ip_proto,
                    p->name, alproto, p->min_depth, p->max_depth,
                    p->ProbeTS, p->ProbeTC)) {
            if (enable_default != 0) {
                SCLogDebug("No %s app-layer configuration, enabling %s"
                        " detection %s detection on port %s.",
                        p->name, p->name, ip_proto_str, p->default_port);
                AppLayerProtoDetectPPRegister(p->ip_proto,
                        p->default_port, alproto,
                        p->min_depth, p->max_depth, STREAM_TOSERVER,
                        p->ProbeTS, p->ProbeTC);
            } else {
                SCLogDebug("No %s app-layer configuration for detection port (%s).",
                        p->name, ip_proto_str);
            }
        }

    }

    return alproto;
}

int AppLayerRegisterParser(const struct AppLayerParser *p, AppProto alproto)
{
    const char *ip_proto_str = NULL;

    if (p == NULL)
        FatalError(SC_ERR_FATAL, "Call to %s with NULL pointer.", __FUNCTION__);

    if (alproto == ALPROTO_UNKNOWN || alproto >= ALPROTO_FAILED)
        FatalError(SC_ERR_FATAL, "Unknown or invalid AppProto '%s'.", p->name);

    ip_proto_str = IpProtoToString(p->ip_proto);
    if (ip_proto_str == NULL)
        FatalError(SC_ERR_FATAL, "Unknown or unsupported ip_proto field in parser '%s'", p->name);

    SCLogDebug("Registering %s protocol parser.", p->name);

    /* Register functions for state allocation and freeing. A
     * state is allocated for every new flow. */
    AppLayerParserRegisterStateFuncs(p->ip_proto, alproto,
        p->StateAlloc, p->StateFree);

    /* Register request parser for parsing frame from server to server. */
    if (p->ParseTS) {
        AppLayerParserRegisterParser(p->ip_proto, alproto,
                STREAM_TOSERVER, p->ParseTS);
    }

    /* Register response parser for parsing frames from server to client. */
    if (p->ParseTC) {
        AppLayerParserRegisterParser(p->ip_proto, alproto,
                STREAM_TOCLIENT, p->ParseTC);
    }

    /* Register a function to be called by the application layer
     * when a transaction is to be freed. */
    AppLayerParserRegisterTxFreeFunc(p->ip_proto, alproto,
        p->StateTransactionFree);

    /* Register a function to return the current transaction count. */
    AppLayerParserRegisterGetTxCnt(p->ip_proto, alproto,
        p->StateGetTxCnt);

    /* Transaction handling. */
    AppLayerParserRegisterGetStateProgressCompletionStatus(alproto,
        p->StateGetProgressCompletionStatus);
    AppLayerParserRegisterGetStateProgressFunc(p->ip_proto, alproto,
        p->StateGetProgress);
    AppLayerParserRegisterGetTx(p->ip_proto, alproto,
        p->StateGetTx);

    if (p->StateGetTxLogged && p->StateSetTxLogged) {
        AppLayerParserRegisterLoggerFuncs(p->ip_proto, alproto,
                p->StateGetTxLogged, p->StateSetTxLogged);
    }

    /* What is this being registered for? */
    AppLayerParserRegisterDetectStateFuncs(p->ip_proto, alproto,
        p->GetTxDetectState, p->SetTxDetectState);

    if (p->StateGetEventInfo) {
        AppLayerParserRegisterGetEventInfo(p->ip_proto, alproto,
                p->StateGetEventInfo);
    }
    if (p->StateGetEvents) {
        AppLayerParserRegisterGetEventsFunc(p->ip_proto, alproto,
                p->StateGetEvents);
    }
    if (p->LocalStorageAlloc && p->LocalStorageFree) {
        AppLayerParserRegisterLocalStorageFunc(p->ip_proto, alproto,
                p->LocalStorageAlloc, p->LocalStorageFree);
    }
    if (p->GetTxMpmIDs && p->SetTxMpmIDs) {
        AppLayerParserRegisterMpmIDsFuncs(p->ip_proto, alproto,
                p->GetTxMpmIDs, p->SetTxMpmIDs);
    }
    if (p->StateGetFiles) {
        AppLayerParserRegisterGetFilesFunc(p->ip_proto, alproto,
                p->StateGetFiles);
    }

    if (p->GetTxIterator) {
        AppLayerParserRegisterGetTxIterator(p->ip_proto, alproto,
                p->GetTxIterator);
    }

    return 0;
}

static const char * IpProtoToString(int ip_proto)
{
    switch (ip_proto) {
        case IPPROTO_TCP:
            return "tcp";
        case IPPROTO_UDP:
            return "udp";
        default:
            return NULL;
    };

}
