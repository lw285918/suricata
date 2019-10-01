/* Copyright (C) 2007-2019 Open Information Security Foundation
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
 * \author Victor Julien <victor@inliniac.net>
 *
 * Implements classtype keyword.
 */

#include "suricata-common.h"
#include "decode.h"

#include "detect.h"
#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-classtype.h"
#include "util-classification-config.h"
#include "util-error.h"
#include "util-debug.h"
#include "util-unittest.h"

#define PARSE_REGEX "^\\s*([a-zA-Z][a-zA-Z0-9-_]*)\\s*$"

static pcre *regex = NULL;
static pcre_extra *regex_study = NULL;

static int DetectClasstypeSetup(DetectEngineCtx *, Signature *, const char *);
static void DetectClasstypeRegisterTests(void);

/**
 * \brief Registers the handler functions for the "Classtype" keyword.
 */
void DetectClasstypeRegister(void)
{
    sigmatch_table[DETECT_CLASSTYPE].name = "classtype";
    sigmatch_table[DETECT_CLASSTYPE].desc = "information about the classification of rules and alerts";
    sigmatch_table[DETECT_CLASSTYPE].url = DOC_URL DOC_VERSION "/rules/meta.html#classtype";
    sigmatch_table[DETECT_CLASSTYPE].Setup = DetectClasstypeSetup;
    sigmatch_table[DETECT_CLASSTYPE].RegisterTests = DetectClasstypeRegisterTests;

    DetectSetupParseRegexes(PARSE_REGEX, &regex, &regex_study);
}

/**
 * \brief Parses the raw string supplied with the "Classtype" keyword.
 *
 * \param Pointer to the string to be parsed.
 *
 * \retval bool success or failure.
 */
static int DetectClasstypeParseRawString(const char *rawstr, char *out, size_t outsize)
{
#define MAX_SUBSTRINGS 30
    int ov[MAX_SUBSTRINGS];
    size_t len = strlen(rawstr);

    int ret = pcre_exec(regex, regex_study, rawstr, len, 0, 0, ov, 30);
    if (ret < 0) {
        SCLogError(SC_ERR_PCRE_MATCH, "Invalid Classtype in Signature");
        return -1;
    }

    ret = pcre_copy_substring((char *)rawstr, ov, 30, 1, out, outsize);
    if (ret < 0) {
        SCLogError(SC_ERR_PCRE_GET_SUBSTRING, "pcre_copy_substring failed");
        return -1;
    }

    return 0;
}

/**
 * \brief The setup function that would be called when the Signature parsing
 *        module encounters the "Classtype" keyword.
 *
 * \param de_ctx Pointer to the Detection Engine Context.
 * \param s      Pointer the current Signature instance that is being parsed.
 * \param rawstr Pointer to the argument supplied to the classtype keyword.
 *
 * \retval  0 On success
 * \retval -1 On failure
 */
static int DetectClasstypeSetup(DetectEngineCtx *de_ctx, Signature *s, const char *rawstr)
{
    char parsed_ct_name[1024] = "";

    if ((s->class_id > 0) || (s->class_msg != NULL)) {
        SCLogWarning(SC_ERR_CONFLICTING_RULE_KEYWORDS, "duplicated 'classtype' "
                "keyword detected. Using instance with highest priority");
    }

    if (DetectClasstypeParseRawString(rawstr, parsed_ct_name, sizeof(parsed_ct_name)) < 0) {
        SCLogError(SC_ERR_PCRE_PARSE, "invalid value for classtype keyword: "
                "\"%s\"", rawstr);
        return -1;
    }

    SCClassConfClasstype *ct = SCClassConfGetClasstype(parsed_ct_name, de_ctx);
    if (ct == NULL) {
        SCLogError(SC_ERR_UNKNOWN_VALUE, "unknown classtype: \"%s\"",
                   parsed_ct_name);
        return -1;
    }

    if ((s->init_data->init_flags & SIG_FLAG_INIT_PRIO_EXPLICT) != 0) {
        /* don't touch Signature::prio */
        s->class_id = ct->classtype_id;
        s->class_msg = ct->classtype_desc;
    } else if (s->prio == -1) {
        s->prio = ct->priority;
        s->class_id = ct->classtype_id;
        s->class_msg = ct->classtype_desc;
    } else {
        if (ct->priority < s->prio) {
            s->prio = ct->priority;
            s->class_id = ct->classtype_id;
            s->class_msg = ct->classtype_desc;
        }
    }

    return 0;
}

#ifdef UNITTESTS

/**
 * \test Check that supplying an invalid classtype in the rule, results in the
 *       rule being invalidated.
 */
static int DetectClasstypeTest01(void)
{
    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    FAIL_IF_NULL(de_ctx);

    FILE *fd = SCClassConfGenerateValidDummyClassConfigFD01();
    FAIL_IF_NULL(fd);
    SCClassConfLoadClassficationConfigFile(de_ctx, fd);
    de_ctx->sig_list = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                               "(msg:\"Classtype test\"; "
                               "Classtype:not_available; sid:1;)");
    FAIL_IF_NOT_NULL(de_ctx->sig_list);

    DetectEngineCtxFree(de_ctx);
    PASS;
}

/**
 * \test Check that both valid and invalid classtypes in a rule are handled
 *       properly, with rules containing invalid classtypes being rejected
 *       and the ones containing valid classtypes parsed and returned.
 */
static int DetectClasstypeTest02(void)
{
    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    FAIL_IF_NULL(de_ctx);

    FILE *fd = SCClassConfGenerateValidDummyClassConfigFD01();
    FAIL_IF_NULL(fd);
    SCClassConfLoadClassficationConfigFile(de_ctx, fd);

    Signature *sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:bad-unknown; sid:1;)");
    FAIL_IF_NULL(sig);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:not-there; sid:2;)");
    FAIL_IF_NOT_NULL(sig);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:Bad-UnkNown; sid:3;)");
    FAIL_IF_NULL(sig);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:nothing-wrong; sid:4;)");
    FAIL_IF_NULL(sig);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:attempted_dos; sid:5;)");
    FAIL_IF_NOT_NULL(sig);

    /* duplicate test */
    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(Classtype:nothing-wrong; Classtype:Bad-UnkNown; sid:6;)");
    FAIL_IF_NULL(sig);
    FAIL_IF_NOT(sig->prio == 2);

    DetectEngineCtxFree(de_ctx);
    PASS;
}

/**
 * \test Check that the signatures are assigned priority based on classtype they
 *       are given.
 */
static int DetectClasstypeTest03(void)
{
    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    FAIL_IF_NULL(de_ctx);

    FILE *fd = SCClassConfGenerateValidDummyClassConfigFD01();
    FAIL_IF_NULL(fd);
    SCClassConfLoadClassficationConfigFile(de_ctx, fd);

    Signature *sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(msg:\"Classtype test\"; Classtype:bad-unknown; priority:1; sid:1;)");
    FAIL_IF_NULL(sig);
    FAIL_IF_NOT(sig->prio == 1);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any "
                  "(msg:\"Classtype test\"; Classtype:unKnoWn; "
                  "priority:3; sid:2;)");
    FAIL_IF_NULL(sig);
    FAIL_IF_NOT(sig->prio == 3);

    sig = DetectEngineAppendSig(de_ctx, "alert tcp any any -> any any (msg:\"Classtype test\"; "
                  "Classtype:nothing-wrong; priority:1; sid:3;)");
    FAIL_IF_NOT(sig->prio == 1);

    DetectEngineCtxFree(de_ctx);
    PASS;
}

#endif /* UNITTESTS */

/**
 * \brief This function registers unit tests for Classification Config API.
 */
static void DetectClasstypeRegisterTests(void)
{
#ifdef UNITTESTS
    UtRegisterTest("DetectClasstypeTest01", DetectClasstypeTest01);
    UtRegisterTest("DetectClasstypeTest02", DetectClasstypeTest02);
    UtRegisterTest("DetectClasstypeTest03", DetectClasstypeTest03);
#endif /* UNITTESTS */
}
