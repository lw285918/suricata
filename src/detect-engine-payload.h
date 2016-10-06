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
 * \file
 *
 * \author Victor Julien <victor@inliniac.net>
 */

#ifndef __DETECT_ENGINE_PAYLOAD_H__
#define __DETECT_ENGINE_PAYLOAD_H__

int PrefilterPktPayloadRegister(SigGroupHead *sgh, MpmCtx *mpm_ctx);
int PrefilterPktStreamRegister(SigGroupHead *sgh, MpmCtx *mpm_ctx);

int DetectEngineInspectPacketPayload(DetectEngineCtx *,
        DetectEngineThreadCtx *, const Signature *, Flow *, Packet *);
int DetectEngineInspectStreamPayload(DetectEngineCtx *,
        DetectEngineThreadCtx *, const Signature *, Flow *,
        uint8_t *, uint32_t);

void PayloadRegisterTests(void);

#endif /* __DETECT_ENGINE_PAYLOAD_H__ */

