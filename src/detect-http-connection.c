/* Copyright (C) 2007-2017 Open Information Security Foundation
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
 * \author Victor Julien <victor@inliniac.net>
 *
 * Implements the http_connection sticky buffer
 */

#define KEYWORD_NAME "http_connection"
#define KEYWORD_DOC "http-keywords#http-connection"
#define BUFFER_NAME "http_connection"
#define BUFFER_DESC "http connection header"
#define HEADER_NAME "Connection"
#define KEYWORD_ID DETECT_AL_HTTP_HEADER_CONNECTION
#define KEYWORD_TOSERVER 1

#include "detect-http-headers-stub.h"

void RegisterHttpHeadersConnection(void)
{
    DetectHttpHeadersRegisterStub();
}
