/* Copyright (C) 2017 Open Information Security Foundation
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

//! Definitions from Suricata core.

extern crate libc;

pub const TO_SERVER: u8 = 0;
pub const TO_CLIENT: u8 = 1;

/// The Rust place holder for DetectEngineState *.
pub enum DetectEngineState {}

extern {
    pub fn DetectEngineStateFree(state: *mut DetectEngineState);
}

/// The Rust place holder for AppLayerDecoderEvents *.
pub enum AppLayerDecoderEvents {}

extern {
    pub fn AppLayerDecoderEventsSetEventRaw(
        events: *mut *mut AppLayerDecoderEvents, event: libc::uint8_t);
    pub fn AppLayerDecoderEventsFreeEvents(
        events: *mut *mut AppLayerDecoderEvents);
}
