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
 * \author Mats Klepsland <mats.klepsland@gmail.com>
 *
 * Implements tls certificate validity keywords
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
#include "detect-tls-cert-validity.h"

#include "flow.h"
#include "flow-util.h"
#include "flow-var.h"

#include "stream-tcp.h"

#include "app-layer.h"
#include "app-layer-ssl.h"

#include "util-time.h"
#include "util-unittest.h"
#include "util-unittest-helper.h"

/**
 *   [tls_notbefore|tls_notafter]:[<|>]<date string>[<><date string>];
 */
#define PARSE_REGEX "^\\s*(<|>)?\\s*([ -:TW0-9]+)\\s*(?:(<>)\\s*([ -:TW0-9]+))?\\s*$"
static pcre *parse_regex;
static pcre_extra *parse_regex_study;

static int DetectTlsValidityMatch (ThreadVars *, DetectEngineThreadCtx *, Flow *,
                                   uint8_t, void *, void *, const Signature *,
                                   const SigMatchCtx *);

static time_t DateStringToEpoch (char *);
static DetectTlsValidityData *DetectTlsValidityParse (char *);
static int DetectTlsNotBeforeSetup (DetectEngineCtx *, Signature *s, char *str);
static int DetectTlsNotAfterSetup (DetectEngineCtx *, Signature *s, char *str);
static int DetectTlsValiditySetup (DetectEngineCtx *, Signature *s, char *str, uint8_t);
void TlsNotBeforeRegisterTests(void);
void TlsNotAfterRegisterTests(void);
static void DetectTlsValidityFree(void *);

/**
 * \brief Registration function for tls validity keywords.
 */
void DetectTlsValidityRegister (void)
{
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].name = "tls_cert_notbefore";
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].desc = "match TLS certificate notBefore field";
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].url = "https://redmine.openinfosecfoundation.org/projects/suricata/wiki/TLS-keywords#tlsnotbefore";
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].Match = NULL;
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].AppLayerTxMatch = DetectTlsValidityMatch;
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].Setup = DetectTlsNotBeforeSetup;
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].Free = DetectTlsValidityFree;
    sigmatch_table[DETECT_AL_TLS_NOTBEFORE].RegisterTests = TlsNotBeforeRegisterTests;

    sigmatch_table[DETECT_AL_TLS_NOTAFTER].name = "tls_cert_notafter";
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].desc = "match TLS certificate notAfter field";
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].url = "https://redmine.openinfosecfoundation.org/projects/suricata/wiki/TLS-keywords#tlsnotafter";
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].Match = NULL;
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].AppLayerTxMatch = DetectTlsValidityMatch;
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].Setup = DetectTlsNotAfterSetup;
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].Free = DetectTlsValidityFree;
    sigmatch_table[DETECT_AL_TLS_NOTAFTER].RegisterTests = TlsNotAfterRegisterTests;

    DetectSetupParseRegexes(PARSE_REGEX, &parse_regex, &parse_regex_study);
}

/**
 * \internal
 * \brief Function to match validity field in a tls certificate.
 *
 * \param t       Pointer to thread vars.
 * \param det_ctx Pointer to the pattern matcher thread.
 * \param f       Pointer to the current flow.
 * \param flags   Flags.
 * \param state   App layer state.
 * \param s       Pointer to the Signature.
 * \param m       Pointer to the sigmatch that we will cast into
 *                DetectTlsValidityData.
 *
 * \retval 0 no match.
 * \retval 1 match.
 */
static int DetectTlsValidityMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx,
                                   Flow *f, uint8_t flags, void *state,
                                   void *txv, const Signature *s,
                                   const SigMatchCtx *ctx)
{
    SCEnter();

    SSLState *ssl_state = (SSLState *)state;
    if (ssl_state == NULL) {
        SCLogDebug("no tls state, no match");
        SCReturnInt(0);
    }

    int ret = 0;

    SSLStateConnp *connp = NULL;
    if (flags & STREAM_TOSERVER)
        connp = &ssl_state->client_connp;
    else
        connp = &ssl_state->server_connp;

    const DetectTlsValidityData *dd = (const DetectTlsValidityData *)ctx;

    time_t cert_epoch = 0;
    if (dd->type == DETECT_TLS_TYPE_NOTBEFORE)
        cert_epoch = connp->cert0_not_before;
    else if (dd->type == DETECT_TLS_TYPE_NOTAFTER)
        cert_epoch = connp->cert0_not_after;

    if (cert_epoch == 0)
        SCReturnInt(0);

    if ((dd->mode & DETECT_TLS_VALIDITY_EQ) && cert_epoch == dd->epoch)
        ret = 1;
    else if ((dd->mode & DETECT_TLS_VALIDITY_LT) && cert_epoch <= dd->epoch)
        ret = 1;
    else if ((dd->mode & DETECT_TLS_VALIDITY_GT) && cert_epoch >= dd->epoch)
        ret = 1;
    else if ((dd->mode & DETECT_TLS_VALIDITY_RA) &&
            cert_epoch >= dd->epoch && cert_epoch <= dd->epoch2)
        ret = 1;

    SCReturnInt(ret);
}

/**
 * \internal
 * \brief Function to check if string is epoch.
 *
 * \param string Date string.
 *
 * \retval epoch time on success.
 * \retval 0 on failure.
 */
static time_t StringIsEpoch (char *string)
{
    if (strlen(string) == 0)
        return -1;

    /* We assume that the date string is epoch if it consists of only
       digits. */
    char *sp = string;
    while (*sp) {
        if (isdigit(*sp++) == 0)
            return -1;
    }

    return strtol(string, NULL, 10);
}

/**
 * \internal
 * \brief Function to convert date string to epoch.
 *
 * \param string Date string.
 *
 * \retval epoch on success.
 * \retval 0 on failure.
 */
static time_t DateStringToEpoch (char *string)
{
    int r = 0;
    struct tm tm;
    char *patterns[] = {
            /* ISO 8601 */
            "%Y-%m",
            "%Y-%m-%d",
            "%Y-%m-%d %H",
            "%Y-%m-%d %H:%M",
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%dT%H",
            "%Y-%m-%dT%H:%M",
            "%Y-%m-%dT%H:%M:%S",
            "%H:%M",
            "%H:%M:%S",
    };

    /* Skip leading whitespace.  */
    while (isspace(*string))
        string++;

    size_t inlen, oldlen;

    oldlen = inlen = strlen(string);

    /* Skip trailing whitespace */
    while (inlen > 0 && isspace(string[inlen - 1]))
        inlen--;

    char tmp[inlen + 1];

    if (inlen < oldlen) {
        strlcpy(tmp, string, inlen + 1);
        string = tmp;
    }

    time_t epoch = StringIsEpoch(string);
    if (epoch != -1) {
        return epoch;;
    }

    r = SCStringPatternToTime(string, patterns, 10, &tm);

    if (r != 0)
        return -1;

    return SCMkTimeUtc(&tm);
}

/**
 * \internal
 * \brief Function to parse options passed via tls validity keywords.
 *
 * \param rawstr Pointer to the user provided options.
 *
 * \retval dd pointer to DetectTlsValidityData on success.
 * \retval NULL on failure.
 */
static DetectTlsValidityData *DetectTlsValidityParse (char *rawstr)
{
    DetectTlsValidityData *dd = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    char mode[2] = "";
    char value1[20] = "";
    char value2[20] = "";
    char range[3] = "";

    ret = pcre_exec(parse_regex, parse_regex_study, rawstr, strlen(rawstr), 0,
                    0, ov, MAX_SUBSTRINGS);
    if (ret < 3 || ret > 5) {
        SCLogError(SC_ERR_PCRE_MATCH, "Parse error %s", rawstr);
        goto error;
    }

    res = pcre_copy_substring((char *)rawstr, ov, MAX_SUBSTRINGS, 1, mode,
                              sizeof(mode));
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_copy_substring failed");
        goto error;
    }
    SCLogDebug("mode \"%s\"", mode);

    res = pcre_copy_substring((char *)rawstr, ov, MAX_SUBSTRINGS, 2, value1,
                              sizeof(value1));
    if (res < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_copy_substring failed");
        goto error;
    }
    SCLogDebug("value1 \"%s\"", value1);

    if (ret > 3) {
        res = pcre_copy_substring((char *)rawstr, ov, MAX_SUBSTRINGS, 3,
                                  range, sizeof(range));
        if (res < 0) {
            SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_copy_substring failed");
            goto error;
        }
        SCLogDebug("range \"%s\"", range);

        if (ret > 4) {
            res = pcre_copy_substring((char *)rawstr, ov, MAX_SUBSTRINGS, 4,
                                      value2, sizeof(value2));
            if (res < 0) {
                SCLogError(SC_ERR_PCRE_GET_SUBSTRING,
                           "pcre_copy_substring failed");
                goto error;
            }
            SCLogDebug("value2 \"%s\"", value2);
        }
    }

    dd = SCMalloc(sizeof(DetectTlsValidityData));
    if (unlikely(dd == NULL))
        goto error;

    dd->epoch = 0;
    dd->epoch2 = 0;
    dd->mode = 0;

    if (strlen(mode) > 0) {
        if (mode[0] == '<')
            dd->mode |= DETECT_TLS_VALIDITY_LT;
        else if (mode[0] == '>')
            dd->mode |= DETECT_TLS_VALIDITY_GT;
    }

    if (strlen(range) > 0) {
        if (strcmp("<>", range) == 0)
            dd->mode |= DETECT_TLS_VALIDITY_RA;
    }

    if (strlen(range) != 0 && strlen(mode) != 0) {
        SCLogError(SC_ERR_INVALID_ARGUMENT,
                   "Range specified but mode also set");
        goto error;
    }

    if (dd->mode == 0) {
        dd->mode |= DETECT_TLS_VALIDITY_EQ;
    }

    /* set the first value */
    dd->epoch = DateStringToEpoch(value1);
    if (dd->epoch == -1)
        goto error;

    /* set the second value if specified */
    if (strlen(value2) > 0) {
        if (!(dd->mode & DETECT_TLS_VALIDITY_RA)) {
            SCLogError(SC_ERR_INVALID_ARGUMENT,
                "Multiple tls validity values specified but mode is not range");
            goto error;
        }

        dd->epoch2 = DateStringToEpoch(value2);
        if (dd->epoch2 == -1)
            goto error;

        if (dd->epoch2 <= dd->epoch) {
            SCLogError(SC_ERR_INVALID_ARGUMENT,
                "Second value in range must not be smaller than the first");
            goto error;
        }
    }
    return dd;

error:
    if (dd)
        SCFree(dd);
    return NULL;
}

/**
 * \brief Function to add the parsed tls_notbefore into the current signature.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer to the Current Signature.
 * \param rawstr Pointer to the user provided flags options.
 *
 * \retval 0 on Success.
 * \retval -1 on Failure.
 */
static int DetectTlsNotBeforeSetup (DetectEngineCtx *de_ctx, Signature *s,
                                    char *rawstr)
{
    uint8_t type = DETECT_TLS_TYPE_NOTBEFORE;
    int r = DetectTlsValiditySetup(de_ctx, s, rawstr, type);

    SCReturnInt(r);
}

/**
 * \brief Function to add the parsed tls_notafter into the current signature.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer to the Current Signature.
 * \param rawstr Pointer to the user provided flags options.
 *
 * \retval 0 on Success.
 * \retval -1 on Failure.
 */
static int DetectTlsNotAfterSetup (DetectEngineCtx *de_ctx, Signature *s,
                                    char *rawstr)
{
    uint8_t type = DETECT_TLS_TYPE_NOTAFTER;
    int r = DetectTlsValiditySetup(de_ctx, s, rawstr, type);

    SCReturnInt(r);
}

/**
 * \brief Function to add the parsed tls validity field into the current signature.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer to the Current Signature.
 * \param rawstr Pointer to the user provided flags options.
 * \param type   Defines if this is notBefore or notAfter.
 *
 * \retval 0 on Success.
 * \retval -1 on Failure.
 */
static int DetectTlsValiditySetup (DetectEngineCtx *de_ctx, Signature *s,
                                   char *rawstr, uint8_t type)
{
    DetectTlsValidityData *dd = NULL;
    SigMatch *sm = NULL;

    SCLogDebug("\'%s\'", rawstr);

    dd = DetectTlsValidityParse(rawstr);
    if (dd == NULL) {
        SCLogError(SC_ERR_INVALID_ARGUMENT,"Parsing \'%s\' failed", rawstr);
        goto error;
    }

    /* okay so far so good, lets get this into a SigMatch
     * and put it in the Signature. */
    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    if (s->alproto != ALPROTO_UNKNOWN && s->alproto != ALPROTO_TLS) {
        SCLogError(SC_ERR_CONFLICTING_RULE_KEYWORDS,
                   "rule contains conflicting keywords.");
        goto error;
    }

    if (type == DETECT_TLS_TYPE_NOTBEFORE) {
        dd->type = DETECT_TLS_TYPE_NOTBEFORE;
        sm->type = DETECT_AL_TLS_NOTBEFORE;
    }
    else if (type == DETECT_TLS_TYPE_NOTAFTER) {
        dd->type = DETECT_TLS_TYPE_NOTAFTER;
        sm->type = DETECT_AL_TLS_NOTAFTER;
    }
    else {
        goto error;
    }

    sm->ctx = (void *)dd;

    s->flags |= SIG_FLAG_APPLAYER;
    s->alproto = ALPROTO_TLS;

    SigMatchAppendSMToList(s, sm, DETECT_SM_LIST_TLSVALIDITY_MATCH);

    return 0;

error:
    DetectTlsValidityFree(dd);
    if (sm)
        SCFree(sm);
    return -1;
}

/**
 * \internal
 * \brief Function to free memory associated with DetectTlsValidityData.
 *
 * \param de_ptr Pointer to DetectTlsValidityData.
 */
void DetectTlsValidityFree(void *de_ptr)
{
    DetectTlsValidityData *dd = (DetectTlsValidityData *)de_ptr;
    if (dd)
        SCFree(dd);
}

#ifdef UNITTESTS

/**
 * \test This is a test for a valid value 1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse01 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("1430000000");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value >1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse02 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse(">1430000000");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->mode == DETECT_TLS_VALIDITY_GT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value <1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse03 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("<1430000000");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->mode == DETECT_TLS_VALIDITY_LT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 1430000000<>1470000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse04 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("1430000000<>1470000000");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->epoch2 == 1470000000 &&
                dd->mode == DETECT_TLS_VALIDITY_RA);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value A.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse05 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("A");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value >1430000000<>1470000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse06 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse(">1430000000<>1470000000");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value 1430000000<>.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse07 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("1430000000<>");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value <>1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse08 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("<>1430000000");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value "".
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse09 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value " ".
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse10 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse(" ");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a invalid value 1490000000<>1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse11 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("1490000000<>1430000000");
    FAIL_IF_NOT_NULL(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 1430000000 <> 1490000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse12 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("1430000000 <> 1490000000");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->epoch2 == 1490000000 &&
                dd->mode == DETECT_TLS_VALIDITY_RA);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value > 1430000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse13 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("> 1430000000 ");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1430000000 && dd->mode == DETECT_TLS_VALIDITY_GT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value <   1490000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse14 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("<   1490000000 ");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1490000000 && dd->mode == DETECT_TLS_VALIDITY_LT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value    1490000000.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse15 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("   1490000000 ");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1490000000 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse16 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1443657600 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value >2015-10-22.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse17 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse(">2015-10-22");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445472000 && dd->mode == DETECT_TLS_VALIDITY_GT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value <2015-10-22 23.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse18 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("<2015-10-22 23");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445554800 && dd->mode == DETECT_TLS_VALIDITY_LT);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10-22 23:59.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse19 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10-22 23:59");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445558340 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10-22 23:59:59.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse20 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10-22 23:59:59");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445558399 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10-22T23.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse21 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10-22T23");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445554800 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10-22T23:59.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse22 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10-22T23:59");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445558340 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test This is a test for a valid value 2015-10-22T23:59:59.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestParse23 (void)
{
    DetectTlsValidityData *dd = NULL;
    dd = DetectTlsValidityParse("2015-10-22T23:59:59");
    FAIL_IF_NULL(dd);
    FAIL_IF_NOT(dd->epoch == 1445558399 && dd->mode == DETECT_TLS_VALIDITY_EQ);
    DetectTlsValidityFree(dd);
    PASS;
}

/**
 * \test Test matching on validity dates in a certificate.
 *
 * \retval 1 on success.
 * \retval 0 on failure.
 */
int ValidityTestDetect01(void)
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
    FAIL_IF_NULL(de_ctx);

    de_ctx->flags |= DE_QUIET;

    s = DetectEngineAppendSig(de_ctx, "alert tls any any -> any any "
                              "(msg:\"Test tls_cert_notbefore\"; "
                              "tls_cert_notbefore:<2016-07-20; sid:1;)");
    FAIL_IF_NULL(s);

    s = DetectEngineAppendSig(de_ctx, "alert tls any any -> any any "
                              "(msg:\"Test tls_cert_notafter\"; "
                              "tls_cert_notafter:>2016-09-01; sid:2;)");
    FAIL_IF_NULL(s);

    SigGroupBuild(de_ctx);
    DetectEngineThreadCtxInit(&tv, (void *)de_ctx, (void *)&det_ctx);

    SCMutexLock(&f.m);
    int r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOSERVER,
                                client_hello, sizeof(client_hello));
    SCMutexUnlock(&f.m);

    FAIL_IF(r != 0);

    ssl_state = f.alstate;
    FAIL_IF_NULL(ssl_state);

    SigMatchSignatures(&tv, de_ctx, det_ctx, p1);

    FAIL_IF(PacketAlertCheck(p1, 1));
    FAIL_IF(PacketAlertCheck(p1, 2));

    SCMutexLock(&f.m);
    r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOCLIENT,
                            server_hello, sizeof(server_hello));
    SCMutexUnlock(&f.m);

    FAIL_IF(r != 0);

    SigMatchSignatures(&tv, de_ctx, det_ctx, p2);

    FAIL_IF(PacketAlertCheck(p2, 1));
    FAIL_IF(PacketAlertCheck(p2, 2));

    SCMutexLock(&f.m);
    r = AppLayerParserParse(alp_tctx, &f, ALPROTO_TLS, STREAM_TOCLIENT,
                            certificate, sizeof(certificate));
    SCMutexUnlock(&f.m);

    FAIL_IF(r != 0);

    SigMatchSignatures(&tv, de_ctx, det_ctx, p3);

    FAIL_IF_NOT(PacketAlertCheck(p3, 1));
    FAIL_IF_NOT(PacketAlertCheck(p3, 2));

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

    PASS;
}

#endif /* UNITTESTS */

/**
 * \brief Register unit tests for tls_notbefore.
 */
void TlsNotBeforeRegisterTests(void)
{
#ifdef UNITTESTS /* UNITTESTS */
    UtRegisterTest("ValidityTestParse01", ValidityTestParse01);
    UtRegisterTest("ValidityTestParse03", ValidityTestParse03);
    UtRegisterTest("ValidityTestParse05", ValidityTestParse05);
    UtRegisterTest("ValidityTestParse07", ValidityTestParse07);
    UtRegisterTest("ValidityTestParse09", ValidityTestParse09);
    UtRegisterTest("ValidityTestParse11", ValidityTestParse11);
    UtRegisterTest("ValidityTestParse13", ValidityTestParse13);
    UtRegisterTest("ValidityTestParse15", ValidityTestParse15);
    UtRegisterTest("ValidityTestParse17", ValidityTestParse17);
    UtRegisterTest("ValidityTestParse19", ValidityTestParse19);
    UtRegisterTest("ValidityTestParse21", ValidityTestParse21);
    UtRegisterTest("ValidityTestParse23", ValidityTestParse23);
    UtRegisterTest("ValidityTestDetect01", ValidityTestDetect01);
#endif /* UNITTESTS */
}

/**
 * \brief Register unit tests for tls_notafter.
 */
void TlsNotAfterRegisterTests(void)
{
#ifdef UNITTESTS /* UNITTESTS */
    UtRegisterTest("ValidityTestParse02", ValidityTestParse02);
    UtRegisterTest("ValidityTestParse04", ValidityTestParse04);
    UtRegisterTest("ValidityTestParse06", ValidityTestParse06);
    UtRegisterTest("ValidityTestParse08", ValidityTestParse08);
    UtRegisterTest("ValidityTestParse10", ValidityTestParse10);
    UtRegisterTest("ValidityTestParse12", ValidityTestParse12);
    UtRegisterTest("ValidityTestParse14", ValidityTestParse14);
    UtRegisterTest("ValidityTestParse16", ValidityTestParse16);
    UtRegisterTest("ValidityTestParse18", ValidityTestParse18);
    UtRegisterTest("ValidityTestParse20", ValidityTestParse20);
    UtRegisterTest("ValidityTestParse22", ValidityTestParse22);
#endif /* UNITTESTS */
}
