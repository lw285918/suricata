/* Copyright (C) 2024 Open Information Security Foundation
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
 * \author Jeff Lucovsky <jlucovsky@oisf.net>
 *
 * Implements the from_base64 transformation keyword
 */

#include "suricata-common.h"

#include "detect.h"
#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-byte.h"

#include "rust.h"

#include "detect-transform-base64.h"

#include "util-base64.h"
#include "util-unittest.h"
#include "util-print.h"

static int DetectTransformFromBase64DecodeSetup(DetectEngineCtx *, Signature *, const char *);
static void DetectTransformFromBase64DecodeFree(DetectEngineCtx *, void *);
#ifdef UNITTESTS
static void DetectTransformFromBase64DecodeRegisterTests(void);
#endif
static void TransformFromBase64Decode(InspectionBuffer *buffer, void *options);

#define DETECT_TRANSFORM_FROM_BASE64_MODE_DEFAULT (uint8_t) ModeRFC4648

void DetectTransformFromBase64DecodeRegister(void)
{
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].name = "from_base64";
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].desc = "convert the base64 decode of the buffer";
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].url = "/rules/transforms.html#from_base64";
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].Setup = DetectTransformFromBase64DecodeSetup;
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].Transform = TransformFromBase64Decode;
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].Free = DetectTransformFromBase64DecodeFree;
#ifdef UNITTESTS
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].RegisterTests =
            DetectTransformFromBase64DecodeRegisterTests;
#endif
    sigmatch_table[DETECT_TRANSFORM_FROM_BASE64].flags |= SIGMATCH_OPTIONAL_OPT;
}

static void DetectTransformFromBase64DecodeFree(DetectEngineCtx *de_ctx, void *ptr)
{
    ScTransformBase64Free(ptr);
}

static DetectTransformFromBase64Data *DetectTransformFromBase64DecodeParse(const char *str)
{
    DetectTransformFromBase64Data *tbd = ScTransformBase64Parse(str);
    if (tbd == NULL) {
        SCLogError("invalid transform_base64 values");
    }
    return tbd;
}

/**
 *  \internal
 *  \brief Base64 decode the input buffer
 *  \param det_ctx detection engine ctx
 *  \param s signature
 *  \param opts_str transform options, if any
 *  \retval 0 No decode
 *  \retval >0 Decoded byte count
 */
static int DetectTransformFromBase64DecodeSetup(
        DetectEngineCtx *de_ctx, Signature *s, const char *opts_str)
{
    int r = -1;

    SCEnter();

    DetectTransformFromBase64Data *b64d = DetectTransformFromBase64DecodeParse(opts_str);
    if (b64d == NULL)
        SCReturnInt(r);

    if (b64d->flags & DETECT_TRANSFORM_BASE64_FLAG_OFFSET_VAR) {
        SCLogError("offset value must be a value, not a variable name");
        goto exit_path;
    }

    if (b64d->flags & DETECT_TRANSFORM_BASE64_FLAG_NBYTES_VAR) {
        SCLogError("byte value must be a value, not a variable name");
        goto exit_path;
    }

    r = DetectSignatureAddTransform(s, DETECT_TRANSFORM_FROM_BASE64, b64d);

exit_path:
    if (r != 0)
        DetectTransformFromBase64DecodeFree(de_ctx, b64d);
    SCReturnInt(r);
}

static void TransformFromBase64Decode(InspectionBuffer *buffer, void *options)
{
    DetectTransformFromBase64Data *b64d = options;
    const uint8_t *input = buffer->inspect;
    const uint32_t input_len = buffer->inspect_len;
    /*
     * The output buffer is larger than needed. On average,
     * a base64 encoded string is 75% of the unencoded
     * string.
     */
    uint8_t output[input_len];
    uint32_t decode_length = input_len;

    DetectBase64Mode mode = b64d->mode;
    uint32_t offset = b64d->offset;
    uint32_t nbytes = b64d->nbytes;

    if (offset) {
        if (offset > input_len) {
            SCLogDebug("offset %d exceeds length %d; returning", offset, input_len);
            return;
        }
        input += offset;
        decode_length -= offset;
    }

    if (nbytes) {
        if (nbytes > decode_length) {
            SCLogDebug("byte count %d plus offset %d exceeds length %d; returning", nbytes, offset,
                    input_len);
            return;
        }
        decode_length = nbytes;
    }

    // PrintRawDataFp(stdout, input, input_len);
    uint32_t decoded_length;
    uint32_t consumed;
    Base64Ecode code =
            DecodeBase64(output, input_len, input, decode_length, &consumed, &decoded_length, mode);
    if (code == BASE64_ECODE_OK) {
        // PrintRawDataFp(stdout, output, decoded_length);
        if (decoded_length) {
            InspectionBufferCopy(buffer, output, decoded_length);
        }
    }
}

#ifdef UNITTESTS
/* Simple success case -- check buffer */
static int DetectTransformFromBase64DecodeTest01(void)
{
    const uint8_t *input = (const uint8_t *)"VGhpcyBpcyBTdXJpY2F0YQ==";
    uint32_t input_len = strlen((char *)input);
    const char *result = "This is Suricata";
    uint32_t result_len = strlen((char *)result);
    DetectTransformFromBase64Data b64d = {
        .nbytes = input_len,
    };

    InspectionBuffer buffer;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(buffer.inspect_len == result_len);
    FAIL_IF_NOT(strncmp(result, (const char *)buffer.inspect, result_len) == 0);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

/* Decode failure case -- ensure no change to buffer */
static int DetectTransformFromBase64DecodeTest02(void)
{
    const uint8_t *input = (const uint8_t *)"This is Suricata\n";
    uint32_t input_len = strlen((char *)input);
    DetectTransformFromBase64Data b64d = {
        .nbytes = input_len,
    };
    InspectionBuffer buffer;
    InspectionBuffer buffer_orig;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    buffer_orig = buffer;
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(buffer.inspect_offset == buffer_orig.inspect_offset);
    FAIL_IF_NOT(buffer.inspect_len == buffer_orig.inspect_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

/* bytes > len so --> no transform */
static int DetectTransformFromBase64DecodeTest03(void)
{
    const uint8_t *input = (const uint8_t *)"VGhpcyBpcyBTdXJpY2F0YQ==";
    uint32_t input_len = strlen((char *)input);

    DetectTransformFromBase64Data b64d = {
        .nbytes = input_len + 1,
    };

    InspectionBuffer buffer;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(strncmp((const char *)input, (const char *)buffer.inspect, input_len) == 0);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

/* offset > len so --> no transform */
static int DetectTransformFromBase64DecodeTest04(void)
{
    const uint8_t *input = (const uint8_t *)"VGhpcyBpcyBTdXJpY2F0YQ==";
    uint32_t input_len = strlen((char *)input);

    DetectTransformFromBase64Data b64d = {
        .offset = input_len + 1,
    };

    InspectionBuffer buffer;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(strncmp((const char *)input, (const char *)buffer.inspect, input_len) == 0);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

/* partial transform */
static int DetectTransformFromBase64DecodeTest05(void)
{
    const uint8_t *input = (const uint8_t *)"VGhpcyBpcyBTdXJpY2F0YQ==";
    uint32_t input_len = strlen((char *)input);
    const char *result = "This is S";
    uint32_t result_len = strlen((char *)result);

    DetectTransformFromBase64Data b64d = {
        .nbytes = 12,
    };

    InspectionBuffer buffer;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(buffer.inspect_len == result_len);
    FAIL_IF_NOT(strncmp(result, (const char *)buffer.inspect, result_len) == 0);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

/* transform from non-zero offset */
static int DetectTransformFromBase64DecodeTest06(void)
{
    const uint8_t *input = (const uint8_t *)"VGhpcyBpcyBTdXJpY2F0YQ==";
    uint32_t input_len = strlen((char *)input);
    const char *result = "s is Suricata";
    uint32_t result_len = strlen((char *)result);

    DetectTransformFromBase64Data b64d = {
        .offset = 4,
    };

    InspectionBuffer buffer;
    InspectionBufferInit(&buffer, input_len);
    InspectionBufferSetup(NULL, -1, &buffer, input, input_len);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    TransformFromBase64Decode(&buffer, &b64d);
    FAIL_IF_NOT(buffer.inspect_len == result_len);
    FAIL_IF_NOT(strncmp(result, (const char *)buffer.inspect, result_len) == 0);
    PrintRawDataFp(stdout, buffer.inspect, buffer.inspect_len);
    InspectionBufferFree(&buffer);
    PASS;
}

static void DetectTransformFromBase64DecodeRegisterTests(void)
{
    UtRegisterTest("DetectTransformFromBase64DecodeTest01", DetectTransformFromBase64DecodeTest01);
    UtRegisterTest("DetectTransformFromBase64DecodeTest02", DetectTransformFromBase64DecodeTest02);
    UtRegisterTest("DetectTransformFromBase64DecodeTest03", DetectTransformFromBase64DecodeTest03);
    UtRegisterTest("DetectTransformFromBase64DecodeTest04", DetectTransformFromBase64DecodeTest04);
    UtRegisterTest("DetectTransformFromBase64DecodeTest05", DetectTransformFromBase64DecodeTest05);
    UtRegisterTest("DetectTransformFromBase64DecodeTest06", DetectTransformFromBase64DecodeTest06);
}
#endif
