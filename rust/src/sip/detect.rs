/* Copyright (C) 2019 Open Information Security Foundation
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

// written by Giuseppe Longo <giuseppe@glongo.it>

use std::ptr;

use sip::sip::SIPTransaction;

#[no_mangle]
pub unsafe extern "C" fn rs_sip_tx_get_method(tx:  &mut SIPTransaction,
                                              buffer: *mut *const u8,
                                              buffer_len: *mut u32)
                                              -> u8
{
    match tx.request {
        Some(ref r) => {
            let m = &r.method;
            if m.len() > 0 {
                *buffer = m.as_ptr();
                *buffer_len = m.len() as u32;
                return 1;
            }
        }
        None => {}
    }

    *buffer = ptr::null();
    *buffer_len = 0;

    return 0;
}

