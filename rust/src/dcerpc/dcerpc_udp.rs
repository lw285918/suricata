/* Copyright (C) 2020 Open Information Security Foundation
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

use std::mem::transmute;

use crate::applayer::{AppLayerResult, AppLayerTxData};
use crate::core;
use crate::dcerpc::dcerpc::{
    DCERPCTransaction, DCERPC_MAX_TX, DCERPC_TYPE_REQUEST, DCERPC_TYPE_RESPONSE, PFCL1_FRAG, PFCL1_LASTFRAG,
};
use std::collections::VecDeque;
use crate::dcerpc::parser;

// Constant DCERPC UDP Header length
pub const DCERPC_UDP_HDR_LEN: i32 = 80;

#[derive(Debug)]
pub struct DCERPCHdrUdp {
    pub rpc_vers: u8,
    pub pkt_type: u8,
    pub flags1: u8,
    pub flags2: u8,
    pub drep: Vec<u8>,
    pub serial_hi: u8,
    pub objectuuid: Vec<u8>,
    pub interfaceuuid: Vec<u8>,
    pub activityuuid: Vec<u8>,
    pub server_boot: u32,
    pub if_vers: u32,
    pub seqnum: u32,
    pub opnum: u16,
    pub ihint: u16,
    pub ahint: u16,
    pub fraglen: u16,
    pub fragnum: u16,
    pub auth_proto: u8,
    pub serial_lo: u8,
}

#[derive(Debug)]
pub struct DCERPCUDPState {
    pub tx_id: u64,
    pub transactions: VecDeque<DCERPCTransaction>,
    tx_index_completed: usize,
}

impl DCERPCUDPState {
    pub fn new() -> DCERPCUDPState {
        return DCERPCUDPState {
            tx_id: 0,
            tx_index_completed: 0,
            transactions: VecDeque::new(),
        };
    }

    fn create_tx(&mut self,  hdr: &DCERPCHdrUdp) -> DCERPCTransaction {
        let mut tx = DCERPCTransaction::new();
        tx.id = self.tx_id;
        tx.endianness = hdr.drep[0] & 0x10;
        tx.activityuuid = hdr.activityuuid.to_vec();
        tx.seqnum = hdr.seqnum;
        self.tx_id += 1;
        if self.transactions.len() > unsafe { DCERPC_MAX_TX } {
            let mut index = self.tx_index_completed;
            for tx_old in &mut self.transactions.range_mut(self.tx_index_completed..) {
                index += 1;
                if !tx_old.req_done || !tx_old.resp_done {
                    tx_old.req_done = true;
                    tx_old.resp_done = true;
                    break;
                }
            }
            self.tx_index_completed = index;
        }
        tx
    }

    pub fn free_tx(&mut self, tx_id: u64) {
        SCLogDebug!("Freeing TX with ID {} TX.ID {}", tx_id, tx_id+1);
        let len = self.transactions.len();
        let mut found = false;
        let mut index = 0;
        for i in 0..len {
            let tx = &self.transactions[i];
            if tx.id as u64 == tx_id { //+ 1 {
                found = true;
                index = i;
                SCLogDebug!("tx {} progress {}/{}", tx.id, tx.req_done, tx.resp_done);
                break;
            }
        }
        if found {
            SCLogDebug!("freeing TX with ID {} TX.ID {} at index {} left: {} max id: {}",
                            tx_id, tx_id+1, index, self.transactions.len(), self.tx_id);
            self.tx_index_completed = 0;
            self.transactions.remove(index);
        }
    }

    /// Get transaction as per the given transaction ID. Transaction ID with
    /// which the lookup is supposed to be done as per the calls from AppLayer
    /// parser in C. This requires an internal transaction ID to be maintained.
    ///
    /// Arguments:
    /// * `tx_id`:
    ///    description: internal transaction ID to track transactions
    ///
    /// Return value:
    /// Option mutable reference to DCERPCTransaction
    pub fn get_tx(&mut self, tx_id: u64) -> Option<&mut DCERPCTransaction> {
        for tx in &mut self.transactions {
            let found = tx.id == tx_id;
            if found {
                return Some(tx);
            }
        }
        None
    }

    fn find_incomplete_tx(&mut self, hdr: &DCERPCHdrUdp) -> Option<&mut DCERPCTransaction> {
        for tx in &mut self.transactions {
            if tx.seqnum == hdr.seqnum && tx.activityuuid == hdr.activityuuid {
                if (hdr.pkt_type == DCERPC_TYPE_REQUEST && !tx.req_done) ||
                   (hdr.pkt_type == DCERPC_TYPE_RESPONSE && !tx.resp_done) {
                    SCLogDebug!("found tx id {}, last tx_id {}, {} {}", tx.id, self.tx_id, tx.seqnum, tx.activityuuid[0]);
                    return Some(tx);
                }
            }
        }
        None
    }

    pub fn handle_fragment_data(&mut self, hdr: &DCERPCHdrUdp, input: &[u8]) -> bool {
        if hdr.pkt_type != DCERPC_TYPE_REQUEST && hdr.pkt_type != DCERPC_TYPE_RESPONSE {
            SCLogDebug!("Unrecognized packet type");
            return false;
        }

        let mut otx = self.find_incomplete_tx(hdr);
        if otx.is_none() {
            let ntx = self.create_tx(hdr);
            SCLogDebug!("new tx id {}, last tx_id {}, {} {}", ntx.id, self.tx_id, ntx.seqnum, ntx.activityuuid[0]);
            self.transactions.push_back(ntx);
            otx = self.transactions.back_mut();
        }

        if let Some(tx) = otx {
            let done = (hdr.flags1 & PFCL1_FRAG) == 0 || (hdr.flags1 & PFCL1_LASTFRAG) != 0;

            match hdr.pkt_type {
                DCERPC_TYPE_REQUEST => {
                    tx.stub_data_buffer_ts.extend_from_slice(&input);
                    tx.frag_cnt_ts += 1;
                    if done {
                        tx.req_done = true;
                    }
                    return true;
                }
                DCERPC_TYPE_RESPONSE => {
                    tx.stub_data_buffer_tc.extend_from_slice(&input);
                    tx.frag_cnt_tc += 1;
                    if done {
                        tx.resp_done = true;
                    }
                    return true;
                }
                _ => {
                    // unreachable
                }
            }
        }
        return false; // unreachable
    }

    pub fn handle_input_data(&mut self, input: &[u8]) -> AppLayerResult {
        // Input length should at least be header length
        if (input.len() as i32) < DCERPC_UDP_HDR_LEN {
            return AppLayerResult::err();
        }

        // Call header parser first
        match parser::parse_dcerpc_udp_header(input) {
            Ok((leftover_bytes, header)) => {
                if header.rpc_vers != 4 {
                    SCLogDebug!("DCERPC UDP Header did not validate.");
                    return AppLayerResult::err();
                }
                if leftover_bytes.len() < header.fraglen as usize {
                    SCLogDebug!("Insufficient data: leftover_bytes {}, fraglen {}", leftover_bytes.len(), header.fraglen);
                    return AppLayerResult::err();
                }
                if !self.handle_fragment_data(&header, &leftover_bytes[..header.fraglen as usize]) {
                    return AppLayerResult::err();
                }
            }
            Err(nom::Err::Incomplete(_)) => {
                // Insufficient data.
                SCLogDebug!("Insufficient data while parsing DCERPC request");
                return AppLayerResult::err();
            }
            Err(_) => {
                // Error, probably malformed data.
                SCLogDebug!("An error occurred while parsing DCERPC request");
                return AppLayerResult::err();
            }
        }
        return AppLayerResult::ok();
    }
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_parse(
    _flow: *mut core::Flow, state: &mut DCERPCUDPState, _pstate: *mut std::os::raw::c_void,
    input: *const u8, input_len: u32, _data: *mut std::os::raw::c_void, _flags: u8,
) -> AppLayerResult {
    if input_len > 0 && input != std::ptr::null_mut() {
        let buf = build_slice!(input, input_len as usize);
        return state.handle_input_data(buf);
    }
    AppLayerResult::err()
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_state_free(state: *mut std::os::raw::c_void) {
    let _drop: Box<DCERPCUDPState> = unsafe { transmute(state) };
}

#[no_mangle]
pub unsafe extern "C" fn rs_dcerpc_udp_state_new(_orig_state: *mut std::os::raw::c_void, _orig_proto: core::AppProto) -> *mut std::os::raw::c_void {
    let state = DCERPCUDPState::new();
    let boxed = Box::new(state);
    transmute(boxed)
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_state_transaction_free(
    state: *mut std::os::raw::c_void, tx_id: u64,
) {
    let dce_state = cast_pointer!(state, DCERPCUDPState);
    SCLogDebug!("freeing tx {}", tx_id as u64);
    dce_state.free_tx(tx_id);
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_get_tx_detect_state(
    vtx: *mut std::os::raw::c_void,
) -> *mut core::DetectEngineState {
    let dce_state = cast_pointer!(vtx, DCERPCTransaction);
    match dce_state.de_state {
        Some(ds) => ds,
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_set_tx_detect_state(
    vtx: *mut std::os::raw::c_void, de_state: *mut core::DetectEngineState,
) -> u8 {
    let dce_state = cast_pointer!(vtx, DCERPCTransaction);
    dce_state.de_state = Some(de_state);
    0
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_get_tx_data(
    tx: *mut std::os::raw::c_void)
    -> *mut AppLayerTxData
{
    let tx = cast_pointer!(tx, DCERPCTransaction);
    return &mut tx.tx_data;
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_get_tx(
    state: *mut std::os::raw::c_void, tx_id: u64,
) -> *mut DCERPCTransaction {
    let dce_state = cast_pointer!(state, DCERPCUDPState);
    match dce_state.get_tx(tx_id) {
        Some(tx) => {
            return unsafe{&mut *(tx as *mut DCERPCTransaction)};
        },
        None => {
            return std::ptr::null_mut();
        }
    } 
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_udp_get_tx_cnt(vtx: *mut std::os::raw::c_void) -> u64 {
    let dce_state = cast_pointer!(vtx, DCERPCUDPState);
    dce_state.tx_id
}

/// Probe input to see if it looks like DCERPC.
fn probe(input: &[u8]) -> (bool, bool) {
    match parser::parse_dcerpc_udp_header(input) {
        Ok((_, hdr)) => {
            let is_request = hdr.pkt_type == 0x00;
            let is_dcerpc = hdr.rpc_vers == 0x04 &&
                (hdr.flags2 & 0xfc == 0) &&
                (hdr.drep[0] & 0xee == 0) &&
                (hdr.drep[1] <= 3);
            return (is_dcerpc, is_request);
        },
        Err(_) => (false, false),
    }
}

#[no_mangle]
pub extern "C" fn rs_dcerpc_probe_udp(direction: u8, input: *const u8,
                                      len: u32, rdir: *mut u8) -> i32
{
    SCLogDebug!("Probing the packet for DCERPC/UDP");
    if len == 0 {
        return core::ALPROTO_UNKNOWN;
    }
    let slice: &[u8] = unsafe {
        std::slice::from_raw_parts(input as *mut u8, len as usize)
    };
    //is_incomplete is checked by caller
    let (is_dcerpc, is_request) = probe(slice);
    if is_dcerpc {
        let dir = if is_request {
            core::STREAM_TOSERVER
        } else {
            core::STREAM_TOCLIENT
        };
        if direction & (core::STREAM_TOSERVER|core::STREAM_TOCLIENT) != dir {
            unsafe { *rdir = dir };
        }
        return 1;
    }
    return 0;

}


#[cfg(test)]
mod tests {
    use crate::applayer::AppLayerResult;
    use crate::dcerpc::dcerpc_udp::DCERPCUDPState;
    use crate::dcerpc::parser;

    #[test]
    fn test_process_header_udp_incomplete_hdr() {
        let request: &[u8] = &[
            0x04, 0x00, 0x08, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x4a, 0x9f, 0x4d,
            0x1c, 0x7d, 0xcf, 0x11,
        ];

        match parser::parse_dcerpc_udp_header(request) {
            Ok((_rem, _header)) => {
                { assert!(false); }
            }
            _ => {}
        }
    }

    #[test]
    fn test_process_header_udp_perfect_hdr() {
        let request: &[u8] = &[
            0x04, 0x00, 0x08, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x4a, 0x9f, 0x4d,
            0x1c, 0x7d, 0xcf, 0x11, 0x86, 0x1e, 0x00, 0x20, 0xaf, 0x6e, 0x7c, 0x57, 0x86, 0xc2,
            0x37, 0x67, 0xf7, 0x1e, 0xd1, 0x11, 0xbc, 0xd9, 0x00, 0x60, 0x97, 0x92, 0xd2, 0x6c,
            0x79, 0xbe, 0x01, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0x68, 0x00, 0x00, 0x00, 0x0a, 0x00,
        ];
        match parser::parse_dcerpc_udp_header(request) {
            Ok((rem, header)) => {
                assert_eq!(4, header.rpc_vers);
                assert_eq!(80, request.len() - rem.len());
            }
            _ => { assert!(false); }
        }
    }

    #[test]
    fn test_handle_fragment_data_udp_no_body() {
        let request: &[u8] = &[
            0x04, 0x00, 0x08, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x4a, 0x9f, 0x4d,
            0x1c, 0x7d, 0xcf, 0x11, 0x86, 0x1e, 0x00, 0x20, 0xaf, 0x6e, 0x7c, 0x57, 0x86, 0xc2,
            0x37, 0x67, 0xf7, 0x1e, 0xd1, 0x11, 0xbc, 0xd9, 0x00, 0x60, 0x97, 0x92, 0xd2, 0x6c,
            0x79, 0xbe, 0x01, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0x68, 0x00, 0x00, 0x00, 0x0a, 0x00,
        ];
        match parser::parse_dcerpc_udp_header(request) {
            Ok((rem, header)) => {
                assert_eq!(4, header.rpc_vers);
                assert_eq!(80, request.len() - rem.len());
                assert_eq!(0, rem.len());
            }
            _ => { assert!(false); }
        }
    }

    #[test]
    fn test_handle_input_data_udp_full_body() {
        let request: &[u8] = &[
            0x04, 0x00, 0x2c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x3f, 0x98,
            0xf0, 0x5c, 0xd9, 0x63, 0xcc, 0x46, 0xc2, 0x74, 0x51, 0x6c, 0x8a, 0x53, 0x7d, 0x6f,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
            0xff, 0xff, 0xff, 0xff, 0x70, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x24, 0x58, 0xfd, 0xcc, 0x45,
            0x64, 0x49, 0xb0, 0x70, 0xdd, 0xae, 0x74, 0x2c, 0x96, 0xd2, 0x60, 0x5e, 0x0d, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x5e, 0x0d, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x7c, 0x5e, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
            0x80, 0x96, 0xf1, 0xf1, 0x2a, 0x4d, 0xce, 0x11, 0xa6, 0x6a, 0x00, 0x20, 0xaf, 0x6e,
            0x72, 0xf4, 0x0c, 0x00, 0x00, 0x00, 0x4d, 0x41, 0x52, 0x42, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x0d, 0xf0, 0xad, 0xba, 0x00, 0x00, 0x00, 0x00, 0xa8, 0xf4,
            0x0b, 0x00, 0x10, 0x09, 0x00, 0x00, 0x10, 0x09, 0x00, 0x00, 0x4d, 0x45, 0x4f, 0x57,
            0x04, 0x00, 0x00, 0x00, 0xa2, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x38, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x08,
            0x00, 0x00, 0xd8, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x08, 0x00,
            0xcc, 0xcc, 0xcc, 0xcc, 0xc8, 0x00, 0x00, 0x00, 0x4d, 0x45, 0x4f, 0x57, 0xd8, 0x08,
            0x00, 0x00, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
            0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc4, 0x28, 0xcd, 0x00, 0x64, 0x29, 0xcd, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xb9, 0x01, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0xab, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0xa5, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
            0xa6, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x46, 0xa4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x46, 0xad, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0xaa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x07, 0x00, 0x00, 0x00, 0x60, 0x00,
            0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x00, 0x28, 0x06, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x01, 0x00,
            0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x50, 0x00, 0x00, 0x00,
            0x4f, 0xb6, 0x88, 0x20, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x48, 0x00, 0x00, 0x00, 0x07, 0x00,
            0x66, 0x00, 0x06, 0x09, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x46, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x19, 0x0c, 0x00,
            0x58, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x70, 0xd8,
            0x98, 0x93, 0x98, 0x4f, 0xd2, 0x11, 0xa9, 0x3d, 0xbe, 0x57, 0xb2, 0x00, 0x00, 0x00,
            0x32, 0x00, 0x31, 0x00, 0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x80, 0x00,
            0x00, 0x00, 0x0d, 0xf0, 0xad, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x43, 0x14, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x4d, 0x45, 0x4f, 0x57,
            0x04, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x3b, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x81, 0xc5, 0x17, 0x03, 0x80, 0x0e, 0xe9, 0x4a,
            0x99, 0x99, 0xf1, 0x8a, 0x50, 0x6f, 0x7a, 0x85, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc,
            0xcc, 0xcc, 0x30, 0x00, 0x00, 0x00, 0x78, 0x00, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xd8, 0xda, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x2f,
            0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x46, 0x00, 0x58, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x10, 0x00, 0x00, 0x00,
            0x30, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0xcc, 0xcc, 0xcc, 0xcc,
            0x68, 0x00, 0x00, 0x00, 0x0e, 0x00, 0xff, 0xff, 0x68, 0x8b, 0x0b, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x02, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xfe, 0x02, 0x00, 0x00, 0x5c, 0x00, 0x5c, 0x00, 0x31, 0x00,
            0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00,
            0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x31, 0x00,
            0x31, 0x00, 0x31, 0x00, 0x31, 0x00, 0x9d, 0x13, 0x00, 0x01, 0xcc, 0xe0, 0xfd, 0x7f,
            0xcc, 0xe0, 0xfd, 0x7f, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
            0x90, 0x90,
        ];
        let mut dcerpcudp_state = DCERPCUDPState::new();
        assert_eq!(
            AppLayerResult::ok(),
            dcerpcudp_state.handle_input_data(request)
        );
        assert_eq!(
            1392,
            dcerpcudp_state.transactions[0].stub_data_buffer_ts.len()
        );
    }
}
