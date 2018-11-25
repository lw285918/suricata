/* Copyright (C) 2007-2010 Open Information Security Foundation
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
 * \ingroup httplayer
 *
 * @{
 */


/**
 * \file
 *
 * \author Brian Rectanus <brectanu@gmail.com>
 *
 * Implements the http_method keyword
 */

#include "suricata-common.h"
#include "threads.h"
#include "debug.h"
#include "decode.h"
#include "detect.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"
#include "detect-engine-prefilter.h"
#include "detect-content.h"
#include "detect-pcre.h"

#include "flow.h"
#include "flow-var.h"
#include "flow-util.h"

#include "util-debug.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "util-spm.h"

#include "app-layer.h"
#include "app-layer-parser.h"

#include "app-layer-htp.h"
#include "detect-http-method.h"
#include "stream-tcp.h"

static int g_http_method_buffer_id = 0;
static int DetectHttpMethodSetup(DetectEngineCtx *, Signature *, const char *);
#ifdef UNITTESTS
void DetectHttpMethodRegisterTests(void);
#endif
void DetectHttpMethodFree(void *);
static _Bool DetectHttpMethodValidateCallback(const Signature *s, const char **sigerror);
static InspectionBuffer *GetData(DetectEngineThreadCtx *det_ctx,
        const DetectEngineTransforms *transforms, Flow *_f,
        const uint8_t _flow_flags, void *txv, const int list_id);

/**
 * \brief Registration function for keyword: http_method
 */
void DetectHttpMethodRegister(void)
{
    sigmatch_table[DETECT_AL_HTTP_METHOD].name = "http_method";
    sigmatch_table[DETECT_AL_HTTP_METHOD].desc = "content modifier to match only on the HTTP method-buffer";
    sigmatch_table[DETECT_AL_HTTP_METHOD].url = DOC_URL DOC_VERSION "/rules/http-keywords.html#http-method";
    sigmatch_table[DETECT_AL_HTTP_METHOD].Match = NULL;
    sigmatch_table[DETECT_AL_HTTP_METHOD].Setup = DetectHttpMethodSetup;
#ifdef UNITTESTS
    sigmatch_table[DETECT_AL_HTTP_METHOD].RegisterTests = DetectHttpMethodRegisterTests;
#endif
    sigmatch_table[DETECT_AL_HTTP_METHOD].flags |= SIGMATCH_NOOPT;

    DetectAppLayerInspectEngineRegister2("http_method", ALPROTO_HTTP,
            SIG_FLAG_TOSERVER, HTP_REQUEST_LINE,
            DetectEngineInspectBufferGeneric, GetData);

    DetectAppLayerMpmRegister2("http_method", SIG_FLAG_TOSERVER, 4,
            PrefilterGenericMpmRegister, GetData, ALPROTO_HTTP,
            HTP_REQUEST_LINE);

    DetectBufferTypeSetDescriptionByName("http_method",
            "http request method");

    DetectBufferTypeRegisterValidateCallback("http_method",
            DetectHttpMethodValidateCallback);

    g_http_method_buffer_id = DetectBufferTypeGetByName("http_method");

    SCLogDebug("registering http_method rule option");
}

/**
 * \brief This function is used to add the parsed "http_method" option
 *        into the current signature.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer to the Current Signature.
 * \param str    Pointer to the user provided option string.
 *
 * \retval  0 on Success.
 * \retval -1 on Failure.
 */
static int DetectHttpMethodSetup(DetectEngineCtx *de_ctx, Signature *s, const char *str)
{
    return DetectEngineContentModifierBufferSetup(de_ctx, s, str,
                                                  DETECT_AL_HTTP_METHOD,
                                                  g_http_method_buffer_id,
                                                  ALPROTO_HTTP);
}

/**
 *  \retval 1 valid
 *  \retval 0 invalid
 */
static _Bool DetectHttpMethodValidateCallback(const Signature *s, const char **sigerror)
{
    const SigMatch *sm = s->init_data->smlists[g_http_method_buffer_id];
    for ( ; sm != NULL; sm = sm->next) {
        if (sm->type != DETECT_CONTENT)
            continue;
        const DetectContentData *cd = (const DetectContentData *)sm->ctx;
        if (cd->content && cd->content_len) {
            if (cd->content[cd->content_len-1] == 0x20) {
                *sigerror = "http_method pattern with trailing space";
                SCLogError(SC_ERR_INVALID_SIGNATURE, "%s", *sigerror);
                return FALSE;
            } else if (cd->content[0] == 0x20) {
                *sigerror = "http_method pattern with leading space";
                SCLogError(SC_ERR_INVALID_SIGNATURE, "%s", *sigerror);
                return FALSE;
            } else if (cd->content[cd->content_len-1] == 0x09) {
                *sigerror = "http_method pattern with trailing tab";
                SCLogError(SC_ERR_INVALID_SIGNATURE, "%s", *sigerror);
                return FALSE;
            } else if (cd->content[0] == 0x09) {
                *sigerror = "http_method pattern with leading tab";
                SCLogError(SC_ERR_INVALID_SIGNATURE, "%s", *sigerror);
                return FALSE;
            }
        }
    }
    return TRUE;
}

static InspectionBuffer *GetData(DetectEngineThreadCtx *det_ctx,
        const DetectEngineTransforms *transforms, Flow *_f,
        const uint8_t _flow_flags, void *txv, const int list_id)
{
    InspectionBuffer *buffer = InspectionBufferGet(det_ctx, list_id);
    if (buffer->inspect == NULL) {
        htp_tx_t *tx = (htp_tx_t *)txv;

        if (tx->request_method == NULL)
            return NULL;

        const uint32_t data_len = bstr_len(tx->request_method);
        const uint8_t *data = bstr_ptr(tx->request_method);

        InspectionBufferSetup(buffer, data, data_len);
        InspectionBufferApplyTransforms(buffer, transforms);
    }

    return buffer;
}

#ifdef UNITTESTS
#include "tests/detect-http-method.c"
#endif

/**
 * @}
 */
