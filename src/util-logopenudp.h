/* Copyright (C) 2007-2014 Open Information Security Foundation
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
 * \author Bao Lei <3862821@qq.com>
 */
#ifndef __UTIL_LOGOPENUDP_H__
#define __UTIL_LOGOPENUDP_H__
int SCLogOpenUDPSocket(ConfNode *udp_node, LogFileCtx *log_ctx);
int SCLogUDPSocketReconnect(LogFileCtx *log_ctx);
int SCLogUDPSocketWrite(const char *buffer,int buffer_len,LogFileCtx *log_ctx);
void SCLogUDPSocketClose(LogFileCtx *log_ctx);
#endif
