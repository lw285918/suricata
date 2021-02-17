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
 * \author Gerardo Iglesias <iglesiasg@gmail.com>
 *
 * Implements icode keyword support
 */

#include "suricata-common.h"
#include "debug.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"
#include "detect-engine-prefilter-common.h"
#include "detect-engine-uint.h"

#include "detect-icode.h"

#include "util-byte.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"
#include "util-debug.h"

/**
 *\brief Regex for parsing our icode options
 */

static int DetectICodeMatch(DetectEngineThreadCtx *, Packet *,
        const Signature *, const SigMatchCtx *);
static int DetectICodeSetup(DetectEngineCtx *, Signature *, const char *);
#ifdef UNITTESTS
static void DetectICodeRegisterTests(void);
#endif
void DetectICodeFree(DetectEngineCtx *, void *);

static int PrefilterSetupICode(DetectEngineCtx *de_ctx, SigGroupHead *sgh);
static bool PrefilterICodeIsPrefilterable(const Signature *s);

/**
 * \brief Registration function for icode: keyword
 */
void DetectICodeRegister (void)
{
    sigmatch_table[DETECT_ICODE].name = "icode";
    sigmatch_table[DETECT_ICODE].desc = "match on specific ICMP id-value";
    sigmatch_table[DETECT_ICODE].url = "/rules/header-keywords.html#icode";
    sigmatch_table[DETECT_ICODE].Match = DetectICodeMatch;
    sigmatch_table[DETECT_ICODE].Setup = DetectICodeSetup;
    sigmatch_table[DETECT_ICODE].Free = DetectICodeFree;
#ifdef UNITTESTS
    sigmatch_table[DETECT_ICODE].RegisterTests = DetectICodeRegisterTests;
#endif
    sigmatch_table[DETECT_ICODE].SupportsPrefilter = PrefilterICodeIsPrefilterable;
    sigmatch_table[DETECT_ICODE].SetupPrefilter = PrefilterSetupICode;
}


/**
 * \brief This function is used to match icode rule option set on a packet with those passed via icode:
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param ctx pointer to the sigmatch that we will cast into DetectU8Data
 *
 * \retval 0 no match
 * \retval 1 match
 */
static int DetectICodeMatch (DetectEngineThreadCtx *det_ctx, Packet *p,
        const Signature *s, const SigMatchCtx *ctx)
{
    if (PKT_IS_PSEUDOPKT(p))
        return 0;

    uint8_t picode;
    if (PKT_IS_ICMPV4(p)) {
        picode = ICMPV4_GET_CODE(p);
    } else if (PKT_IS_ICMPV6(p)) {
        picode = ICMPV6_GET_CODE(p);
    } else {
        /* Packet not ICMPv4 nor ICMPv6 */
        return 0;
    }

    const DetectU8Data *icd = (const DetectU8Data *)ctx;
    return DetectU8Match(picode, icd);
}

/**
 * \brief this function is used to add the parsed icode data into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param icodestr pointer to the user provided icode options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectICodeSetup(DetectEngineCtx *de_ctx, Signature *s, const char *icodestr)
{

    DetectU8Data *icd = NULL;
    SigMatch *sm = NULL;

    icd = DetectU8Parse(icodestr);
    if (icd == NULL) goto error;

    sm = SigMatchAlloc();
    if (sm == NULL) goto error;

    sm->type = DETECT_ICODE;
    sm->ctx = (SigMatchCtx *)icd;

    SigMatchAppendSMToList(s, sm, DETECT_SM_LIST_MATCH);
    s->flags |= SIG_FLAG_REQUIRE_PACKET;

    return 0;

error:
    if (icd != NULL) SCFree(icd);
    if (sm != NULL) SCFree(sm);
    return -1;
}

/**
 * \brief this function will free memory associated with DetectU8Data
 *
 * \param ptr pointer to DetectU8Data
 */
void DetectICodeFree(DetectEngineCtx *de_ctx, void *ptr)
{
    SCFree(ptr);
}

/* prefilter code */

static void PrefilterPacketICodeMatch(DetectEngineThreadCtx *det_ctx,
        Packet *p, const void *pectx)
{
    if (PKT_IS_PSEUDOPKT(p)) {
        SCReturn;
    }

    uint8_t picode;
    if (PKT_IS_ICMPV4(p)) {
        picode = ICMPV4_GET_CODE(p);
    } else if (PKT_IS_ICMPV6(p)) {
        picode = ICMPV6_GET_CODE(p);
    } else {
        /* Packet not ICMPv4 nor ICMPv6 */
        return;
    }

    const PrefilterPacketU8HashCtx *h = pectx;
    const SigsArray *sa = h->array[picode];
    if (sa) {
        PrefilterAddSids(&det_ctx->pmq, sa->sigs, sa->cnt);
    }
}

static void
PrefilterPacketICodeSet(PrefilterPacketHeaderValue *v, void *smctx)
{
    const DetectU8Data *a = smctx;
    v->u8[0] = a->mode;
    v->u8[1] = a->arg1;
    v->u8[2] = a->arg2;
}

static bool
PrefilterPacketICodeCompare(PrefilterPacketHeaderValue v, void *smctx)
{
    const DetectU8Data *a = smctx;
    if (v.u8[0] == a->mode &&
        v.u8[1] == a->arg1 &&
        v.u8[2] == a->arg2)
        return TRUE;
    return FALSE;
}

static int PrefilterSetupICode(DetectEngineCtx *de_ctx, SigGroupHead *sgh)
{
    return PrefilterSetupPacketHeaderU8Hash(de_ctx, sgh, DETECT_ICODE,
            PrefilterPacketICodeSet,
            PrefilterPacketICodeCompare,
            PrefilterPacketICodeMatch);
}

static bool PrefilterICodeIsPrefilterable(const Signature *s)
{
    const SigMatch *sm;
    for (sm = s->init_data->smlists[DETECT_SM_LIST_MATCH] ; sm != NULL; sm = sm->next) {
        switch (sm->type) {
            case DETECT_ICODE:
                return TRUE;
        }
    }
    return FALSE;
}

#ifdef UNITTESTS
#include "detect-engine.h"
#include "detect-engine-mpm.h"

/**
 * \test DetectICodeParseTest01 is a test for setting a valid icode value
 */
static int DetectICodeParseTest01(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("8");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->mode == DETECT_UINT_EQ)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest02 is a test for setting a valid icode value
 *       with ">" operator
 */
static int DetectICodeParseTest02(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse(">8");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->mode == DETECT_UINT_GT)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest03 is a test for setting a valid icode value
 *       with "<" operator
 */
static int DetectICodeParseTest03(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("<8");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->mode == DETECT_UINT_LT)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest04 is a test for setting a valid icode value
 *       with "<>" operator
 */
static int DetectICodeParseTest04(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("8<>20");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->arg2 == 20 && icd->mode == DETECT_UINT_RA)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest05 is a test for setting a valid icode value
 *       with spaces all around
 */
static int DetectICodeParseTest05(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("  8 ");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->mode == DETECT_UINT_EQ)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest06 is a test for setting a valid icode value
 *       with ">" operator and spaces all around
 */
static int DetectICodeParseTest06(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("  >  8 ");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->mode == DETECT_UINT_GT)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest07 is a test for setting a valid icode value
 *       with "<>" operator and spaces all around
 */
static int DetectICodeParseTest07(void)
{
    DetectU8Data *icd = NULL;
    int result = 0;
    icd = DetectU8Parse("  8  <>  20 ");
    if (icd != NULL) {
        if (icd->arg1 == 8 && icd->arg2 == 20 && icd->mode == DETECT_UINT_RA)
            result = 1;
        DetectICodeFree(NULL, icd);
    }
    return result;
}

/**
 * \test DetectICodeParseTest08 is a test for setting an invalid icode value
 */
static int DetectICodeParseTest08(void)
{
    DetectU8Data *icd = NULL;
    icd = DetectU8Parse("> 8 <> 20");
    if (icd == NULL)
        return 1;
    DetectICodeFree(NULL, icd);
    return 0;
}

/**
 * \test DetectICodeMatchTest01 is a test for checking the working of icode
 *       keyword by creating 5 rules and matching a crafted packet against
 *       them. 4 out of 5 rules shall trigger.
 */
static int DetectICodeMatchTest01(void)
{

    Packet *p = NULL;
    Signature *s = NULL;
    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx;
    int result = 0;

    memset(&th_v, 0, sizeof(th_v));

    p = UTHBuildPacket(NULL, 0, IPPROTO_ICMP);

    p->icmpv4h->code = 10;

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    s = de_ctx->sig_list = SigInit(de_ctx,"alert icmp any any -> any any (icode:10; sid:1;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,"alert icmp any any -> any any (icode:<15; sid:2;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,"alert icmp any any -> any any (icode:>20; sid:3;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,"alert icmp any any -> any any (icode:8<>20; sid:4;)");
    if (s == NULL) {
        goto end;
    }

    s = s->next = SigInit(de_ctx,"alert icmp any any -> any any (icode:20<>8; sid:5;)");
    if (s == NULL) {
        goto end;
    }

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&th_v, (void *)de_ctx, (void *)&det_ctx);

    SigMatchSignatures(&th_v, de_ctx, det_ctx, p);
    if (PacketAlertCheck(p, 1) == 0) {
        SCLogDebug("sid 1 did not alert, but should have");
        goto cleanup;
    } else if (PacketAlertCheck(p, 2) == 0) {
        SCLogDebug("sid 2 did not alert, but should have");
        goto cleanup;
    } else if (PacketAlertCheck(p, 3)) {
        SCLogDebug("sid 3 alerted, but should not have");
        goto cleanup;
    } else if (PacketAlertCheck(p, 4) == 0) {
        SCLogDebug("sid 4 did not alert, but should have");
        goto cleanup;
    } else if (PacketAlertCheck(p, 5) == 0) {
        SCLogDebug("sid 5 did not alert, but should have");
        goto cleanup;
    }

    result = 1;

cleanup:
    SigGroupCleanup(de_ctx);
    SigCleanSignatures(de_ctx);

    DetectEngineThreadCtxDeinit(&th_v, (void *)det_ctx);
    DetectEngineCtxFree(de_ctx);

    UTHFreePackets(&p, 1);
end:
    return result;
}

/**
 * \brief this function registers unit tests for DetectICode
 */
void DetectICodeRegisterTests(void)
{
    UtRegisterTest("DetectICodeParseTest01", DetectICodeParseTest01);
    UtRegisterTest("DetectICodeParseTest02", DetectICodeParseTest02);
    UtRegisterTest("DetectICodeParseTest03", DetectICodeParseTest03);
    UtRegisterTest("DetectICodeParseTest04", DetectICodeParseTest04);
    UtRegisterTest("DetectICodeParseTest05", DetectICodeParseTest05);
    UtRegisterTest("DetectICodeParseTest06", DetectICodeParseTest06);
    UtRegisterTest("DetectICodeParseTest07", DetectICodeParseTest07);
    UtRegisterTest("DetectICodeParseTest08", DetectICodeParseTest08);
    UtRegisterTest("DetectICodeMatchTest01", DetectICodeMatchTest01);
}
#endif /* UNITTESTS */
