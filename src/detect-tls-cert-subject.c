/* Copyright (C) 2007-2016 Open Information Security Foundation
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
 * \author Mats Klepsland <mats.klepsland@gmail.com>
 *
 * Implements support for tls_cert_subject keyword.
 */

#include "suricata-common.h"
#include "threads.h"
#include "debug.h"
#include "decode.h"
#include "detect.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-content.h"
#include "detect-pcre.h"

#include "flow.h"
#include "flow-util.h"
#include "flow-var.h"

#include "util-debug.h"
#include "util-unittest.h"
#include "util-spm.h"
#include "util-print.h"

#include "stream-tcp.h"

#include "app-layer.h"
#include "app-layer-ssl.h"

#include "util-unittest.h"
#include "util-unittest-helper.h"

static int DetectTlsSubjectSetup(DetectEngineCtx *, Signature *, char *);
static void DetectTlsSubjectRegisterTests(void);

/**
 * \brief Registration function for keyword: tls_cert_issuer
 */
void DetectTlsSubjectRegister(void)
{
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].name = "tls_cert_subject";
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].desc = "content modifier to match specifically and only on the TLS cert subject buffer";
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].Match = NULL;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].AppLayerMatch = NULL;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].alproto = ALPROTO_TLS;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].Setup = DetectTlsSubjectSetup;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].Free  = NULL;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].RegisterTests = DetectTlsSubjectRegisterTests;

    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].flags |= SIGMATCH_NOOPT;
    sigmatch_table[DETECT_AL_TLS_CERT_SUBJECT].flags |= SIGMATCH_PAYLOAD;
}

/**
 * \brief this function setup the tls_cert_subject modifier keyword used in the rule
 *
 * \param de_ctx   Pointer to the Detection Engine Context
 * \param s        Pointer to the Signature to which the current keyword belongs
 * \param str      Should hold an empty string always
 *
 * \retval 0       On success
 */
static int DetectTlsSubjectSetup(DetectEngineCtx *de_ctx, Signature *s, char *str)
{
    s->list = DETECT_SM_LIST_TLSSUBJECT_MATCH;
    s->alproto = ALPROTO_TLS;
    return 0;
}

#ifdef UNITTESTS

/**
 * \test Test that a signature containing a tls_cert_subject is correctly parsed
 *       and that the keyword is registered.
 */
static int DetectTlsSubjectTest01(void)
{
    DetectEngineCtx *de_ctx = NULL;
    int result = 0;
    SigMatch *sm = NULL;

    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;
    de_ctx->sig_list = SigInit(de_ctx, "alert tls any any -> any any "
                               "(msg:\"Testing tls_cert_subject\"; "
                               "tls_cert_subject; content:\"test\"; sid:1;)");
    if (de_ctx->sig_list == NULL) {
        goto end;
    }

    /* sm should not be in the MATCH list */
    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_MATCH];
    if (sm != NULL) {
        goto end;
    }

    sm = de_ctx->sig_list->sm_lists[DETECT_SM_LIST_TLSSUBJECT_MATCH];
    if (sm == NULL) {
        goto end;
    }

    if (sm->type != DETECT_CONTENT) {
        printf("sm type not DETECT_AL_TLS_CERT_SUBJECT: ");
        goto end;
    }

    if (sm->next != NULL) {
        goto end;
    }

    result = 1;

end:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);
    DetectEngineCtxFree(de_ctx);

    return result;
}

/**
 * \test Test matching for google in the subject of a certificate
 *
 */
static int DetectTlsSubjectTest02(void)
{
    /* client hello */
    uint8_t client_hello[] = {
            0x16, 0x03, 0x01, 0x00, 0xc8, 0x01, 0x00, 0x00,
            0xc4, 0x03, 0x03, 0xd6, 0x08, 0x5a, 0xa2, 0x86,
            0x5b, 0x85, 0xd4, 0x40, 0xab, 0xbe, 0xc0, 0xbc,
            0x41, 0xf2, 0x26, 0xf0, 0xfe, 0x21, 0xee, 0x8b,
            0x4c, 0x7e, 0x07, 0xc8, 0xec, 0xd2, 0x00, 0x46,
            0x4c, 0xeb, 0xb7, 0x00, 0x00, 0x16, 0xc0, 0x2b,
            0xc0, 0x2f, 0xc0, 0x0a, 0xc0, 0x09, 0xc0, 0x13,
            0xc0, 0x14, 0x00, 0x33, 0x00, 0x39, 0x00, 0x2f,
            0x00, 0x35, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x85,
            0x00, 0x00, 0x00, 0x12, 0x00, 0x10, 0x00, 0x00,
            0x0d, 0x77, 0x77, 0x77, 0x2e, 0x67, 0x6f, 0x6f,
            0x67, 0x6c, 0x65, 0x2e, 0x6e, 0x6f, 0xff, 0x01,
            0x00, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x08, 0x00,
            0x06, 0x00, 0x17, 0x00, 0x18, 0x00, 0x19, 0x00,
            0x0b, 0x00, 0x02, 0x01, 0x00, 0x00, 0x23, 0x00,
            0x00, 0x33, 0x74, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x29, 0x00, 0x27, 0x05, 0x68, 0x32, 0x2d, 0x31,
            0x36, 0x05, 0x68, 0x32, 0x2d, 0x31, 0x35, 0x05,
            0x68, 0x32, 0x2d, 0x31, 0x34, 0x02, 0x68, 0x32,
            0x08, 0x73, 0x70, 0x64, 0x79, 0x2f, 0x33, 0x2e,
            0x31, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x31,
            0x2e, 0x31, 0x00, 0x05, 0x00, 0x05, 0x01, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x16, 0x00,
            0x14, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01, 0x02,
            0x01, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x02,
            0x03, 0x04, 0x02, 0x02, 0x02
    };

    /* server hello */
    uint8_t server_hello[] = {
            0x16, 0x03, 0x03, 0x00, 0x48, 0x02, 0x00, 0x00,
            0x44, 0x03, 0x03, 0x57, 0x91, 0xb8, 0x63, 0xdd,
            0xdb, 0xbb, 0x23, 0xcf, 0x0b, 0x43, 0x02, 0x1d,
            0x46, 0x11, 0x27, 0x5c, 0x98, 0xcf, 0x67, 0xe1,
            0x94, 0x3d, 0x62, 0x7d, 0x38, 0x48, 0x21, 0x23,
            0xa5, 0x62, 0x31, 0x00, 0xc0, 0x2f, 0x00, 0x00,
            0x1c, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x10,
            0x00, 0x05, 0x00, 0x03, 0x02, 0x68, 0x32, 0x00,
            0x0b, 0x00, 0x02, 0x01, 0x00
    };

    /* certificate */
    uint8_t certificate[] = {
            0x16, 0x03, 0x03, 0x04, 0x93, 0x0b, 0x00, 0x04,
            0x8f, 0x00, 0x04, 0x8c, 0x00, 0x04, 0x89, 0x30,
            0x82, 0x04, 0x85, 0x30, 0x82, 0x03, 0x6d, 0xa0,
            0x03, 0x02, 0x01, 0x02, 0x02, 0x08, 0x5c, 0x19,
            0xb7, 0xb1, 0x32, 0x3b, 0x1c, 0xa1, 0x30, 0x0d,
            0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
            0x01, 0x01, 0x0b, 0x05, 0x00, 0x30, 0x49, 0x31,
            0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
            0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11,
            0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0a, 0x47,
            0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e,
            0x63, 0x31, 0x25, 0x30, 0x23, 0x06, 0x03, 0x55,
            0x04, 0x03, 0x13, 0x1c, 0x47, 0x6f, 0x6f, 0x67,
            0x6c, 0x65, 0x20, 0x49, 0x6e, 0x74, 0x65, 0x72,
            0x6e, 0x65, 0x74, 0x20, 0x41, 0x75, 0x74, 0x68,
            0x6f, 0x72, 0x69, 0x74, 0x79, 0x20, 0x47, 0x32,
            0x30, 0x1e, 0x17, 0x0d, 0x31, 0x36, 0x30, 0x37,
            0x31, 0x33, 0x31, 0x33, 0x32, 0x34, 0x35, 0x32,
            0x5a, 0x17, 0x0d, 0x31, 0x36, 0x31, 0x30, 0x30,
            0x35, 0x31, 0x33, 0x31, 0x36, 0x30, 0x30, 0x5a,
            0x30, 0x65, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
            0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31,
            0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08,
            0x0c, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f,
            0x72, 0x6e, 0x69, 0x61, 0x31, 0x16, 0x30, 0x14,
            0x06, 0x03, 0x55, 0x04, 0x07, 0x0c, 0x0d, 0x4d,
            0x6f, 0x75, 0x6e, 0x74, 0x61, 0x69, 0x6e, 0x20,
            0x56, 0x69, 0x65, 0x77, 0x31, 0x13, 0x30, 0x11,
            0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x47,
            0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e,
            0x63, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55,
            0x04, 0x03, 0x0c, 0x0b, 0x2a, 0x2e, 0x67, 0x6f,
            0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x6e, 0x6f, 0x30,
            0x82, 0x01, 0x22, 0x30, 0x0d, 0x06, 0x09, 0x2a,
            0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
            0x05, 0x00, 0x03, 0x82, 0x01, 0x0f, 0x00, 0x30,
            0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01, 0x00,
            0xa5, 0x0a, 0xb9, 0xb1, 0xca, 0x36, 0xd1, 0xae,
            0x22, 0x38, 0x07, 0x06, 0xc9, 0x1a, 0x56, 0x4f,
            0xbb, 0xdf, 0xa8, 0x6d, 0xbd, 0xee, 0x76, 0x16,
            0xbc, 0x53, 0x3c, 0x03, 0x6a, 0x5c, 0x94, 0x50,
            0x87, 0x2f, 0x28, 0xb4, 0x4e, 0xd5, 0x9b, 0x8f,
            0xfe, 0x02, 0xde, 0x2a, 0x83, 0x01, 0xf9, 0x45,
            0x61, 0x0e, 0x66, 0x0e, 0x24, 0x22, 0xe2, 0x59,
            0x66, 0x0d, 0xd3, 0xe9, 0x77, 0x8a, 0x7e, 0x42,
            0xaa, 0x5a, 0xf9, 0x05, 0xbf, 0x30, 0xc7, 0x03,
            0x2b, 0xdc, 0xa6, 0x9c, 0xe0, 0x9f, 0x0d, 0xf1,
            0x28, 0x19, 0xf8, 0xf2, 0x02, 0xfa, 0xbd, 0x62,
            0xa0, 0xf3, 0x02, 0x2b, 0xcd, 0xf7, 0x09, 0x04,
            0x3b, 0x52, 0xd8, 0x65, 0x4b, 0x4a, 0x70, 0xe4,
            0x57, 0xc9, 0x2e, 0x2a, 0xf6, 0x9c, 0x6e, 0xd8,
            0xde, 0x01, 0x52, 0xc9, 0x6f, 0xe9, 0xef, 0x82,
            0xbc, 0x0b, 0x95, 0xb2, 0xef, 0xcb, 0x91, 0xa6,
            0x0b, 0x2d, 0x14, 0xc6, 0x00, 0xa9, 0x33, 0x86,
            0x64, 0x00, 0xd4, 0x92, 0x19, 0x53, 0x3d, 0xfd,
            0xcd, 0xc6, 0x1a, 0xf2, 0x0e, 0x67, 0xc2, 0x1d,
            0x2c, 0xe0, 0xe8, 0x29, 0x97, 0x1c, 0xb6, 0xc4,
            0xb2, 0x02, 0x0c, 0x83, 0xb8, 0x60, 0x61, 0xf5,
            0x61, 0x2d, 0x73, 0x5e, 0x85, 0x4d, 0xbd, 0x0d,
            0xe7, 0x1a, 0x37, 0x56, 0x8d, 0xe5, 0x50, 0x0c,
            0xc9, 0x64, 0x4c, 0x11, 0xea, 0xf3, 0xcb, 0x26,
            0x34, 0xbd, 0x02, 0xf5, 0xc1, 0xfb, 0xa2, 0xec,
            0x27, 0xbb, 0x60, 0xbe, 0x0b, 0xf6, 0xe7, 0x3c,
            0x2d, 0xc9, 0xe7, 0xb0, 0x30, 0x28, 0x17, 0x3d,
            0x90, 0xf1, 0x63, 0x8e, 0x49, 0xf7, 0x15, 0x78,
            0x21, 0xcc, 0x45, 0xe6, 0x86, 0xb2, 0xd8, 0xb0,
            0x2e, 0x5a, 0xb0, 0x58, 0xd3, 0xb6, 0x11, 0x40,
            0xae, 0x81, 0x1f, 0x6b, 0x7a, 0xaf, 0x40, 0x50,
            0xf9, 0x2e, 0x81, 0x8b, 0xec, 0x26, 0x11, 0x3f,
            0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x82, 0x01,
            0x53, 0x30, 0x82, 0x01, 0x4f, 0x30, 0x1d, 0x06,
            0x03, 0x55, 0x1d, 0x25, 0x04, 0x16, 0x30, 0x14,
            0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07,
            0x03, 0x01, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
            0x05, 0x07, 0x03, 0x02, 0x30, 0x21, 0x06, 0x03,
            0x55, 0x1d, 0x11, 0x04, 0x1a, 0x30, 0x18, 0x82,
            0x0b, 0x2a, 0x2e, 0x67, 0x6f, 0x6f, 0x67, 0x6c,
            0x65, 0x2e, 0x6e, 0x6f, 0x82, 0x09, 0x67, 0x6f,
            0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x6e, 0x6f, 0x30,
            0x68, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
            0x07, 0x01, 0x01, 0x04, 0x5c, 0x30, 0x5a, 0x30,
            0x2b, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05,
            0x07, 0x30, 0x02, 0x86, 0x1f, 0x68, 0x74, 0x74,
            0x70, 0x3a, 0x2f, 0x2f, 0x70, 0x6b, 0x69, 0x2e,
            0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x63,
            0x6f, 0x6d, 0x2f, 0x47, 0x49, 0x41, 0x47, 0x32,
            0x2e, 0x63, 0x72, 0x74, 0x30, 0x2b, 0x06, 0x08,
            0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01,
            0x86, 0x1f, 0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f,
            0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x73,
            0x31, 0x2e, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65,
            0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x6f, 0x63, 0x73,
            0x70, 0x30, 0x1d, 0x06, 0x03, 0x55, 0x1d, 0x0e,
            0x04, 0x16, 0x04, 0x14, 0xc6, 0x53, 0x87, 0x42,
            0x2d, 0xc8, 0xee, 0x7a, 0x62, 0x1e, 0x83, 0xdb,
            0x0d, 0xe2, 0x32, 0xeb, 0x8b, 0xaf, 0x69, 0x40,
            0x30, 0x0c, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01,
            0x01, 0xff, 0x04, 0x02, 0x30, 0x00, 0x30, 0x1f,
            0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30,
            0x16, 0x80, 0x14, 0x4a, 0xdd, 0x06, 0x16, 0x1b,
            0xbc, 0xf6, 0x68, 0xb5, 0x76, 0xf5, 0x81, 0xb6,
            0xbb, 0x62, 0x1a, 0xba, 0x5a, 0x81, 0x2f, 0x30,
            0x21, 0x06, 0x03, 0x55, 0x1d, 0x20, 0x04, 0x1a,
            0x30, 0x18, 0x30, 0x0c, 0x06, 0x0a, 0x2b, 0x06,
            0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x05, 0x01,
            0x30, 0x08, 0x06, 0x06, 0x67, 0x81, 0x0c, 0x01,
            0x02, 0x02, 0x30, 0x30, 0x06, 0x03, 0x55, 0x1d,
            0x1f, 0x04, 0x29, 0x30, 0x27, 0x30, 0x25, 0xa0,
            0x23, 0xa0, 0x21, 0x86, 0x1f, 0x68, 0x74, 0x74,
            0x70, 0x3a, 0x2f, 0x2f, 0x70, 0x6b, 0x69, 0x2e,
            0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x2e, 0x63,
            0x6f, 0x6d, 0x2f, 0x47, 0x49, 0x41, 0x47, 0x32,
            0x2e, 0x63, 0x72, 0x6c, 0x30, 0x0d, 0x06, 0x09,
            0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01,
            0x0b, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00,
            0x7b, 0x27, 0x00, 0x46, 0x8f, 0xfd, 0x5b, 0xff,
            0xcb, 0x05, 0x9b, 0xf7, 0xf1, 0x68, 0xf6, 0x9a,
            0x7b, 0xba, 0x53, 0xdf, 0x63, 0xed, 0x11, 0x94,
            0x39, 0xf2, 0xd0, 0x20, 0xcd, 0xa3, 0xc4, 0x98,
            0xa5, 0x10, 0x74, 0xe7, 0x10, 0x6d, 0x07, 0xf8,
            0x33, 0x87, 0x05, 0x43, 0x0e, 0x64, 0x77, 0x09,
            0x18, 0x4f, 0x38, 0x2e, 0x45, 0xae, 0xa8, 0x34,
            0x3a, 0xa8, 0x33, 0xac, 0x9d, 0xdd, 0x25, 0x91,
            0x59, 0x43, 0xbe, 0x0f, 0x87, 0x16, 0x2f, 0xb5,
            0x27, 0xfd, 0xce, 0x2f, 0x35, 0x5d, 0x12, 0xa1,
            0x66, 0xac, 0xf7, 0x95, 0x38, 0x0f, 0xe5, 0xb1,
            0x18, 0x18, 0xe6, 0x80, 0x52, 0x31, 0x8a, 0x66,
            0x02, 0x52, 0x1a, 0xa4, 0x32, 0x6a, 0x61, 0x05,
            0xcf, 0x1d, 0xf9, 0x90, 0x73, 0xf0, 0xeb, 0x20,
            0x31, 0x7b, 0x2e, 0xc0, 0xb0, 0xfb, 0x5c, 0xcc,
            0xdc, 0x76, 0x55, 0x72, 0xaf, 0xb1, 0x05, 0xf4,
            0xad, 0xf9, 0xd7, 0x73, 0x5c, 0x2c, 0xbf, 0x0d,
            0x84, 0x18, 0x01, 0x1d, 0x4d, 0x08, 0xa9, 0x4e,
            0x37, 0xb7, 0x58, 0xc4, 0x05, 0x0e, 0x65, 0x63,
            0xd2, 0x88, 0x02, 0xf5, 0x82, 0x17, 0x08, 0xd5,
            0x8f, 0x80, 0xc7, 0x82, 0x29, 0xbb, 0xe1, 0x04,
            0xbe, 0xf6, 0xe1, 0x8c, 0xbc, 0x3a, 0xf8, 0xf9,
            0x56, 0xda, 0xdc, 0x8e, 0xc6, 0xe6, 0x63, 0x98,
            0x12, 0x08, 0x41, 0x2c, 0x9d, 0x7c, 0x82, 0x0d,
            0x1e, 0xea, 0xba, 0xde, 0x32, 0x09, 0xda, 0x52,
            0x24, 0x4f, 0xcc, 0xb6, 0x09, 0x33, 0x8b, 0x00,
            0xf9, 0x83, 0xb3, 0xc6, 0xa4, 0x90, 0x49, 0x83,
            0x2d, 0x36, 0xd9, 0x11, 0x78, 0xd0, 0x62, 0x9f,
            0xc4, 0x8f, 0x84, 0xba, 0x7f, 0xaa, 0x04, 0xf1,
            0xd9, 0xa4, 0xad, 0x5d, 0x63, 0xee, 0x72, 0xc6,
            0x4d, 0xd1, 0x4b, 0x41, 0x8f, 0x40, 0x0f, 0x7d,
            0xcd, 0xb8, 0x2e, 0x5b, 0x6e, 0x21, 0xc9, 0x3d
    };

    int result = 0;
    Flow f;
    SSLState *ssl_state = NULL;
    TcpSession ssn;
    Packet *p1 = NULL;
    Packet *p2 = NULL;
    Packet *p3 = NULL;
    Signature *s = NULL;
    ThreadVars tv;
    DetectEngineThreadCtx *det_ctx = NULL;
    AppLayerParserThreadCtx *alp_tctx = AppLayerParserThreadCtxAlloc();

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&f, 0, sizeof(Flow));
    memset(&ssn, 0, sizeof(TcpSession));

    p1 = UTHBuildPacketReal(client_hello, sizeof(client_hello), IPPROTO_TCP,
                            "192.168.1.5", "192.168.1.1", 51251, 443);
    p2 = UTHBuildPacketReal(server_hello, sizeof(server_hello), IPPROTO_TCP,
                            "192.168.1.1", "192.168.1.5", 443, 51251);
    p3 = UTHBuildPacketReal(certificate, sizeof(certificate), IPPROTO_TCP,
                            "192.168.1.1", "192.168.1.5", 443, 51251);

    FLOW_INITIALIZE(&f);
    f.flags |= FLOW_IPV4;
    f.proto = IPPROTO_TCP;
    f.protomap = FlowGetProtoMapping(f.proto);
    f.alproto = ALPROTO_TLS;

    p1->flow = &f;
    p1->flags |= PKT_HAS_FLOW | PKT_STREAM_EST;
    p1->flowflags |= FLOW_PKT_TOSERVER;
    p1->flowflags |= FLOW_PKT_ESTABLISHED;
    p1->pcap_cnt = 1;

    p2->flow = &f;
    p2->flags |= PKT_HAS_FLOW | PKT_STREAM_EST;
    p2->flowflags |= FLOW_PKT_TOCLIENT;
    p2->flowflags |= FLOW_PKT_ESTABLISHED;
    p2->pcap_cnt = 2;

    p3->flow = &f;
    p3->flags |= PKT_HAS_FLOW | PKT_STREAM_EST;
    p3->flowflags |= FLOW_PKT_TOCLIENT;
    p3->flowflags |= FLOW_PKT_ESTABLISHED;
    p3->pcap_cnt = 3;

    StreamTcpInitConfig(TRUE);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }
    de_ctx->mpm_matcher = DEFAULT_MPM;
    de_ctx->flags |= DE_QUIET;

    s = DetectEngineAppendSig(de_ctx, "alert tls any any -> any any "
                              "(msg:\"Test tls_cert_subject\"; "
                              "tls_cert_subject; content:\"google\"; nocase; "
                              "sid:1;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&tv, (void *)de_ctx, (void *)&det_ctx);

    SCMutexLock(&f.m);
    int r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOSERVER,
                                client_hello, sizeof(client_hello));
    SCMutexUnlock(&f.m);

    if (r != 0) {
        printf("client_hello chunk returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    ssl_state = f.alstate;
    if (ssl_state == NULL) {
        printf("no tls state: ");
        goto end;
    }

    SigMatchSignatures(&tv, de_ctx, det_ctx, p1);

    if (PacketAlertCheck(p1, 1)) {
        printf("(p1) sig did alert, but it should not have: ");
        goto end;
    }

    SCMutexLock(&f.m);
    r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOCLIENT,
                            server_hello, sizeof(server_hello));
    SCMutexUnlock(&f.m);

    if (r != 0) {
        printf("server_hello chunk returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    SigMatchSignatures(&tv, de_ctx, det_ctx, p2);

    if (PacketAlertCheck(p2, 1)) {
        printf("(p2) sig did alert, but it should not have: ");
        goto end;
    }

    SCMutexLock(&f.m);
    r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOCLIENT,
                            certificate, sizeof(certificate));
    SCMutexUnlock(&f.m);

    if (r != 0) {
        printf("certificate chunk returned %" PRId32 ", expected 0: ", r);
        goto end;
    }

    SigMatchSignatures(&tv, de_ctx, det_ctx, p3);

    if (!(PacketAlertCheck(p3, 1))) {
        printf("(p3) sig didn't alert, but it should have: ");
        goto end;
    }

    result = 1;

end:
    if (alp_tctx != NULL)
        AppLayerParserThreadCtxFree(alp_tctx);
    if (det_ctx != NULL)
        DetectEngineThreadCtxDeinit(&tv, det_ctx);
    if (de_ctx != NULL)
        SigGroupCleanup(de_ctx);
    if (de_ctx != NULL)
        DetectEngineCtxFree(de_ctx);

    StreamTcpFreeConfig(TRUE);
    FLOW_DESTROY(&f);
    UTHFreePacket(p1);
    UTHFreePacket(p2);
    UTHFreePacket(p3);
    return result;
}

#endif

static void DetectTlsSubjectRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("DetectTlsSubjectTest01", DetectTlsSubjectTest01);
    UtRegisterTest("DetectTlsSubjectTest02", DetectTlsSubjectTest02);
#endif
}
