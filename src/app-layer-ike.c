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
 * \author Pierre Chifflier <chifflier@wzdftpd.net>
 * \author Frank Honza <frank.honza@dcso.de>
 *
 * IKE application layer detector and parser.
 *
 */

#include "suricata-common.h"
#include "stream.h"
#include "conf.h"

#include "util-unittest.h"

#include "app-layer-detect-proto.h"
#include "app-layer-parser.h"

#include "app-layer-ike.h"
#include "rust.h"

void RegisterIKEParsers(void)
{
    rs_ike_register_parser();
#ifdef UNITTESTS
    AppLayerParserRegisterProtocolUnittests(IPPROTO_UDP, ALPROTO_IKE, IKEParserRegisterTests);
#endif
}

#ifdef UNITTESTS
#include "stream-tcp.h"
#include "util-unittest-helper.h"
#include "flow-util.h"

static int IkeParserTest(void)
{
    uint64_t ret[4];

    Flow f;
    TcpSession ssn;
    AppLayerParserThreadCtx *alp_tctx = AppLayerParserThreadCtxAlloc();
    FAIL_IF_NULL(alp_tctx);

    memset(&f, 0, sizeof(f));
    memset(&ssn, 0, sizeof(ssn));
    FLOW_INITIALIZE(&f);
    f.protoctx = (void *)&ssn;
    f.proto = IPPROTO_UDP;
    f.protomap = FlowGetProtoMapping(f.proto);
    f.alproto = ALPROTO_IKE;

    StreamTcpInitConfig(true);

    static const unsigned char initiator_sa[] = { 0xe4, 0x7a, 0x59, 0x1f, 0xd0, 0x57, 0x58, 0x7f,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xa8, 0x0d, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x30, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x28, 0x01,
        0x01, 0x00, 0x00, 0x80, 0x01, 0x00, 0x07, 0x80, 0x0e, 0x00, 0x80, 0x80, 0x02, 0x00, 0x02,
        0x80, 0x04, 0x00, 0x02, 0x80, 0x03, 0x00, 0x01, 0x80, 0x0b, 0x00, 0x01, 0x00, 0x0c, 0x00,
        0x04, 0x00, 0x01, 0x51, 0x80, 0x0d, 0x00, 0x00, 0x14, 0x4a, 0x13, 0x1c, 0x81, 0x07, 0x03,
        0x58, 0x45, 0x5c, 0x57, 0x28, 0xf2, 0x0e, 0x95, 0x45, 0x2f, 0x0d, 0x00, 0x00, 0x14, 0x43,
        0x9b, 0x59, 0xf8, 0xba, 0x67, 0x6c, 0x4c, 0x77, 0x37, 0xae, 0x22, 0xea, 0xb8, 0xf5, 0x82,
        0x0d, 0x00, 0x00, 0x14, 0x7d, 0x94, 0x19, 0xa6, 0x53, 0x10, 0xca, 0x6f, 0x2c, 0x17, 0x9d,
        0x92, 0x15, 0x52, 0x9d, 0x56, 0x00, 0x00, 0x00, 0x14, 0x90, 0xcb, 0x80, 0x91, 0x3e, 0xbb,
        0x69, 0x6e, 0x08, 0x63, 0x81, 0xb5, 0xec, 0x42, 0x7b, 0x1f };

    // the initiator sending the security association with proposals
    int r = AppLayerParserParse(NULL, alp_tctx, &f, ALPROTO_IKE, STREAM_TOSERVER | STREAM_START,
            (uint8_t *)initiator_sa, sizeof(initiator_sa));
    FAIL_IF_NOT(r == 0);

    static const unsigned char responder_sa[] = { 0xe4, 0x7a, 0x59, 0x1f, 0xd0, 0x57, 0x58, 0x7f,
        0xa0, 0x0b, 0x8e, 0xf0, 0x90, 0x2b, 0xb8, 0xec, 0x01, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x6c, 0x0d, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x30, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x28, 0x01,
        0x01, 0x00, 0x00, 0x80, 0x01, 0x00, 0x07, 0x80, 0x0e, 0x00, 0x80, 0x80, 0x02, 0x00, 0x02,
        0x80, 0x04, 0x00, 0x02, 0x80, 0x03, 0x00, 0x01, 0x80, 0x0b, 0x00, 0x01, 0x00, 0x0c, 0x00,
        0x04, 0x00, 0x01, 0x51, 0x80, 0x00, 0x00, 0x00, 0x14, 0x4a, 0x13, 0x1c, 0x81, 0x07, 0x03,
        0x58, 0x45, 0x5c, 0x57, 0x28, 0xf2, 0x0e, 0x95, 0x45, 0x2f };

    // responder answering with chosen proposal
    r = AppLayerParserParse(NULL, alp_tctx, &f, ALPROTO_IKE, STREAM_TOCLIENT,
            (uint8_t *)responder_sa, sizeof(responder_sa));
    FAIL_IF_NOT(r == 0);

    static const unsigned char initiator_key[] = { 0xe4, 0x7a, 0x59, 0x1f, 0xd0, 0x57, 0x58, 0x7f,
        0xa0, 0x0b, 0x8e, 0xf0, 0x90, 0x2b, 0xb8, 0xec, 0x04, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x1c, 0x0a, 0x00, 0x00, 0x84, 0x35, 0x04, 0xd3, 0xd2, 0xed, 0x14,
        0xe0, 0xca, 0x03, 0xb8, 0x51, 0xa5, 0x1a, 0x9d, 0xa2, 0xe5, 0xa4, 0xc1, 0x4c, 0x1d, 0x7e,
        0xc3, 0xe1, 0xfb, 0xe9, 0x50, 0x02, 0x54, 0x24, 0x51, 0x4b, 0x3c, 0x69, 0xed, 0x7f, 0xbb,
        0x44, 0xe0, 0x92, 0x25, 0xda, 0x52, 0xd2, 0xa9, 0x26, 0x04, 0xa9, 0x9b, 0xf6, 0x1b, 0x7b,
        0xee, 0xd7, 0xfb, 0xfa, 0x63, 0x5e, 0x82, 0xf0, 0x65, 0xf4, 0xfe, 0x78, 0x07, 0x51, 0x35,
        0x4d, 0xbe, 0x47, 0x4c, 0x3d, 0xe7, 0x20, 0x7d, 0xcf, 0x69, 0xfd, 0xbb, 0xed, 0x32, 0xc1,
        0x69, 0x1c, 0xc1, 0x49, 0xb3, 0x18, 0xee, 0xe0, 0x03, 0x70, 0xe6, 0x5f, 0xc3, 0x06, 0x9b,
        0xba, 0xcf, 0xb0, 0x13, 0x46, 0x71, 0x73, 0x96, 0x6e, 0x9d, 0x5f, 0x4b, 0xc4, 0xf3, 0x85,
        0x7e, 0x35, 0x9b, 0xba, 0x3a, 0xdb, 0xb6, 0xef, 0xee, 0xa5, 0x16, 0xf3, 0x89, 0x7d, 0x85,
        0x34, 0xf3, 0x0d, 0x00, 0x00, 0x18, 0x89, 0xd7, 0xc8, 0xfb, 0xf9, 0x4b, 0x51, 0x5b, 0x52,
        0x1d, 0x5d, 0x95, 0x89, 0xc2, 0x60, 0x20, 0x21, 0xe1, 0xa7, 0x09, 0x0d, 0x00, 0x00, 0x14,
        0xaf, 0xca, 0xd7, 0x13, 0x68, 0xa1, 0xf1, 0xc9, 0x6b, 0x86, 0x96, 0xfc, 0x77, 0x57, 0x01,
        0x00, 0x0d, 0x00, 0x00, 0x14, 0x11, 0xbd, 0xfe, 0x02, 0xd0, 0x56, 0x58, 0x7f, 0x2c, 0x18,
        0x12, 0x59, 0x72, 0xc3, 0x24, 0x01, 0x14, 0x00, 0x00, 0x0c, 0x09, 0x00, 0x26, 0x89, 0xdf,
        0xd6, 0xb7, 0x12, 0x14, 0x00, 0x00, 0x18, 0x15, 0x74, 0xd6, 0x4c, 0x01, 0x65, 0xba, 0xd1,
        0x6a, 0x02, 0x3f, 0x03, 0x8d, 0x45, 0xa0, 0x74, 0x98, 0xd8, 0xd0, 0x51, 0x00, 0x00, 0x00,
        0x18, 0xfe, 0xbf, 0x46, 0x2f, 0x1c, 0xd7, 0x58, 0x05, 0xa7, 0xba, 0xa2, 0x87, 0x47, 0xe7,
        0x69, 0xd6, 0x74, 0xf8, 0x56, 0x00 };

    // the initiator sending key exchange
    r = AppLayerParserParse(NULL, alp_tctx, &f, ALPROTO_IKE, STREAM_TOSERVER,
            (uint8_t *)initiator_key, sizeof(initiator_key));
    FAIL_IF_NOT(r == 0);

    static const unsigned char responder_key[] = { 0xe4, 0x7a, 0x59, 0x1f, 0xd0, 0x57, 0x58, 0x7f,
        0xa0, 0x0b, 0x8e, 0xf0, 0x90, 0x2b, 0xb8, 0xec, 0x04, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x30, 0x0a, 0x00, 0x00, 0x84, 0x6d, 0x02, 0x6d, 0x56, 0x16, 0xc4,
        0x5b, 0xe0, 0x5e, 0x5b, 0x89, 0x84, 0x11, 0xe9, 0xf9, 0x5d, 0x19, 0x5c, 0xea, 0x00, 0x9a,
        0xd2, 0x2c, 0x62, 0xbe, 0xf0, 0x6c, 0x57, 0x1b, 0x7c, 0xfb, 0xc4, 0x79, 0x2f, 0x45, 0x56,
        0x4e, 0xc7, 0x10, 0xac, 0x58, 0x4a, 0xa1, 0x8d, 0x20, 0xcb, 0xc8, 0xf5, 0xf8, 0x91, 0x06,
        0x66, 0xb8, 0x9e, 0x4e, 0xe2, 0xf9, 0x5a, 0xbc, 0x02, 0x30, 0xe2, 0xcb, 0xa1, 0xb8, 0x8a,
        0xc4, 0xbb, 0xa7, 0xfc, 0xc8, 0x18, 0xa9, 0x86, 0xc0, 0x1a, 0x4c, 0xa8, 0x65, 0xa5, 0xeb,
        0x82, 0x88, 0x4d, 0xbe, 0xc8, 0x5b, 0xfd, 0x7d, 0x1a, 0x30, 0x3b, 0x09, 0x89, 0x4d, 0xcf,
        0x2e, 0x37, 0x85, 0xfd, 0x79, 0xdb, 0xa2, 0x25, 0x37, 0x7c, 0xf8, 0xcc, 0xa0, 0x09, 0xce,
        0xff, 0xbb, 0x6a, 0xa3, 0x8b, 0x64, 0x8c, 0x4b, 0x05, 0x40, 0x4f, 0x1c, 0xfa, 0xac, 0x36,
        0x1a, 0xff, 0x0d, 0x00, 0x00, 0x18, 0x15, 0xb6, 0x88, 0x42, 0x1e, 0xd5, 0xc3, 0xdd, 0x92,
        0xd3, 0xb8, 0x6e, 0x47, 0xa7, 0x6f, 0x0d, 0x39, 0xcc, 0x09, 0xe0, 0x0d, 0x00, 0x00, 0x14,
        0x12, 0xf5, 0xf2, 0x8c, 0x45, 0x71, 0x68, 0xa9, 0x70, 0x2d, 0x9f, 0xe2, 0x74, 0xcc, 0x01,
        0x00, 0x0d, 0x00, 0x00, 0x14, 0xaf, 0xca, 0xd7, 0x13, 0x68, 0xa1, 0xf1, 0xc9, 0x6b, 0x86,
        0x96, 0xfc, 0x77, 0x57, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x14, 0x55, 0xcc, 0x29, 0xed, 0x90,
        0x2a, 0xb8, 0xec, 0x53, 0xb1, 0xdf, 0x86, 0x7c, 0x61, 0x09, 0x29, 0x14, 0x00, 0x00, 0x0c,
        0x09, 0x00, 0x26, 0x89, 0xdf, 0xd6, 0xb7, 0x12, 0x14, 0x00, 0x00, 0x18, 0xfe, 0xbf, 0x46,
        0x2f, 0x1c, 0xd7, 0x58, 0x05, 0xa7, 0xba, 0xa2, 0x87, 0x47, 0xe7, 0x69, 0xd6, 0x74, 0xf8,
        0x56, 0x00, 0x00, 0x00, 0x00, 0x18, 0x15, 0x74, 0xd6, 0x4c, 0x01, 0x65, 0xba, 0xd1, 0x6a,
        0x02, 0x3f, 0x03, 0x8d, 0x45, 0xa0, 0x74, 0x98, 0xd8, 0xd0, 0x51 };

    // responder sending key exchange
    r = AppLayerParserParse(NULL, alp_tctx, &f, ALPROTO_IKE, STREAM_TOCLIENT,
            (uint8_t *)responder_key, sizeof(responder_key));
    FAIL_IF_NOT(r == 0);

    static const unsigned char encrypted[] = { 0xe4, 0x7a, 0x59, 0x1f, 0xd0, 0x57, 0x58, 0x7f, 0xa0,
        0x0b, 0x8e, 0xf0, 0x90, 0x2b, 0xb8, 0xec, 0x05, 0x10, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x6c, 0xa4, 0x85, 0xa5, 0xd5, 0x86, 0x8a, 0x3c, 0x92, 0x5d, 0xed, 0xf2,
        0xd1, 0x0d, 0x5e, 0x47, 0x11, 0x2b, 0xc2, 0x94, 0x60, 0x18, 0xc7, 0x61, 0x28, 0xed, 0x7b,
        0xb2, 0x9d, 0xb0, 0x61, 0xfd, 0xab, 0xf7, 0x9a, 0x18, 0xe7, 0x56, 0x89, 0x53, 0x6d, 0x27,
        0xcb, 0xe0, 0x92, 0x1d, 0x67, 0xf7, 0x02, 0xf3, 0x47, 0xae, 0x6e, 0x79, 0xde, 0xe1, 0x09,
        0x4d, 0xc8, 0x6a, 0x5a, 0x26, 0x44, 0x8a, 0xde, 0x72, 0x83, 0x06, 0x94, 0xe1, 0x5d, 0xca,
        0x2d, 0x96, 0x03, 0xeb, 0xc5, 0xf7, 0x90, 0x47, 0x3d };
    // the initiator sending encrypted data
    r = AppLayerParserParse(NULL, alp_tctx, &f, ALPROTO_IKE, STREAM_TOSERVER, (uint8_t *)encrypted,
            sizeof(encrypted));
    FAIL_IF_NOT(r == 0);

    AppLayerCleanupTxList app_tx_list = { 0 };
    AppLayerParserTransactionsCleanup(&f, STREAM_TOCLIENT, &app_tx_list);
    UTHAppLayerParserStateGetIds(f.alparser, &ret[0], &ret[1], &ret[2], &ret[3]);
    FAIL_IF_NOT(ret[0] == 5); // inspect_id[0]
    FAIL_IF_NOT(ret[1] == 5); // inspect_id[1]
    FAIL_IF_NOT(ret[2] == 5); // log_id
    FAIL_IF_NOT(ret[3] == 5); // min_id

    AppLayerParserThreadCtxFree(alp_tctx);
    StreamTcpFreeConfig(true);
    FLOW_DESTROY(&f);
    PASS;
}
#endif

void IKEParserRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("IkeParserTest", IkeParserTest);
#endif
}
