/* Copyright (C) 2018 Open Information Security Foundation
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

/* TODO
 * - check all parsers for calls on non-SUCCESS status
 */

extern crate libc;
use std::str;

use nom::{IResult};

use core::*;
use log::*;

use smb::smb1_records::*;
use smb::smb::*;
use smb::dcerpc::*;
use smb::events::*;
use smb::auth::*;
use smb::files::*;

// https://msdn.microsoft.com/en-us/library/ee441741.aspx
pub const SMB1_COMMAND_CREATE_DIRECTORY:        u8 = 0x00;
pub const SMB1_COMMAND_DELETE_DIRECTORY:        u8 = 0x01;
pub const SMB1_COMMAND_OPEN:                    u8 = 0x02;
pub const SMB1_COMMAND_CREATE:                  u8 = 0x03;
pub const SMB1_COMMAND_CLOSE:                   u8 = 0x04;
pub const SMB1_COMMAND_FLUSH:                   u8 = 0x05;
pub const SMB1_COMMAND_DELETE:                  u8 = 0x06;
pub const SMB1_COMMAND_RENAME:                  u8 = 0x07;
pub const SMB1_COMMAND_QUERY_INFORMATION:       u8 = 0x08;
pub const SMB1_COMMAND_SET_INFORMATION:         u8 = 0x09;
pub const SMB1_COMMAND_READ:                    u8 = 0x0a;
pub const SMB1_COMMAND_WRITE:                   u8 = 0x0b;
pub const SMB1_COMMAND_LOCK_BYTE_RANGE:         u8 = 0x0c;
pub const SMB1_COMMAND_UNLOCK_BYTE_RANGE:       u8 = 0x0d;
pub const SMB1_COMMAND_CREATE_TEMPORARY:        u8 = 0x0e;
pub const SMB1_COMMAND_CREATE_NEW:              u8 = 0x0f;
pub const SMB1_COMMAND_CHECK_DIRECTORY:         u8 = 0x10;
pub const SMB1_COMMAND_PROCESS_EXIT:            u8 = 0x11;
pub const SMB1_COMMAND_SEEK:                    u8 = 0x12;
pub const SMB1_COMMAND_LOCK_AND_READ:           u8 = 0x13;
pub const SMB1_COMMAND_WRITE_AND_UNLOCK:        u8 = 0x14;
pub const SMB1_COMMAND_LOCKING_ANDX:            u8 = 0x24;
pub const SMB1_COMMAND_TRANS:                   u8 = 0x25;
pub const SMB1_COMMAND_ECHO:                    u8 = 0x2b;
pub const SMB1_COMMAND_READ_ANDX:               u8 = 0x2e;
pub const SMB1_COMMAND_WRITE_ANDX:              u8 = 0x2f;
pub const SMB1_COMMAND_TRANS2:                  u8 = 0x32;
pub const SMB1_COMMAND_TRANS2_SECONDARY:        u8 = 0x33;
pub const SMB1_COMMAND_FIND_CLOSE2:             u8 = 0x34;
pub const SMB1_COMMAND_TREE_DISCONNECT:         u8 = 0x71;
pub const SMB1_COMMAND_NEGOTIATE_PROTOCOL:      u8 = 0x72;
pub const SMB1_COMMAND_SESSION_SETUP_ANDX:      u8 = 0x73;
pub const SMB1_COMMAND_LOGOFF_ANDX:             u8 = 0x74;
pub const SMB1_COMMAND_TREE_CONNECT_ANDX:       u8 = 0x75;
pub const SMB1_COMMAND_NT_TRANS:                u8 = 0xa0;
pub const SMB1_COMMAND_NT_CREATE_ANDX:          u8 = 0xa2;
pub const SMB1_COMMAND_NT_CANCEL:               u8 = 0xa4;

pub fn smb1_command_string(c: u8) -> String {
    match c {
        SMB1_COMMAND_CREATE_DIRECTORY   => "SMB1_COMMAND_CREATE_DIRECTORY",
        SMB1_COMMAND_DELETE_DIRECTORY   => "SMB1_COMMAND_DELETE_DIRECTORY",
        SMB1_COMMAND_OPEN               => "SMB1_COMMAND_OPEN",
        SMB1_COMMAND_CREATE             => "SMB1_COMMAND_CREATE",
        SMB1_COMMAND_CLOSE              => "SMB1_COMMAND_CLOSE",
        SMB1_COMMAND_FLUSH              => "SMB1_COMMAND_FLUSH",
        SMB1_COMMAND_DELETE             => "SMB1_COMMAND_DELETE",
        SMB1_COMMAND_RENAME             => "SMB1_COMMAND_RENAME",
        SMB1_COMMAND_QUERY_INFORMATION  => "SMB1_COMMAND_QUERY_INFORMATION",
        SMB1_COMMAND_SET_INFORMATION    => "SMB1_COMMAND_SET_INFORMATION",
        SMB1_COMMAND_READ               => "SMB1_COMMAND_READ",
        SMB1_COMMAND_WRITE              => "SMB1_COMMAND_WRITE",
        SMB1_COMMAND_LOCK_BYTE_RANGE    => "SMB1_COMMAND_LOCK_BYTE_RANGE",
        SMB1_COMMAND_UNLOCK_BYTE_RANGE  => "SMB1_COMMAND_UNLOCK_BYTE_RANGE",
        SMB1_COMMAND_CREATE_TEMPORARY   => "SMB1_COMMAND_CREATE_TEMPORARY",
        SMB1_COMMAND_CREATE_NEW         => "SMB1_COMMAND_CREATE_NEW",
        SMB1_COMMAND_CHECK_DIRECTORY    => "SMB1_COMMAND_CHECK_DIRECTORY",
        SMB1_COMMAND_PROCESS_EXIT       => "SMB1_COMMAND_PROCESS_EXIT",
        SMB1_COMMAND_SEEK               => "SMB1_COMMAND_SEEK",
        SMB1_COMMAND_LOCK_AND_READ      => "SMB1_COMMAND_LOCK_AND_READ",
        SMB1_COMMAND_WRITE_AND_UNLOCK   => "SMB1_COMMAND_WRITE_AND_UNLOCK",
        SMB1_COMMAND_LOCKING_ANDX       => "SMB1_COMMAND_LOCKING_ANDX",
        SMB1_COMMAND_ECHO               => "SMB1_COMMAND_ECHO",
        SMB1_COMMAND_READ_ANDX          => "SMB1_COMMAND_READ_ANDX",
        SMB1_COMMAND_WRITE_ANDX         => "SMB1_COMMAND_WRITE_ANDX",
        SMB1_COMMAND_TRANS              => "SMB1_COMMAND_TRANS",
        SMB1_COMMAND_TRANS2             => "SMB1_COMMAND_TRANS2",
        SMB1_COMMAND_TRANS2_SECONDARY   => "SMB1_COMMAND_TRANS2_SECONDARY",
        SMB1_COMMAND_FIND_CLOSE2        => "SMB1_COMMAND_FIND_CLOSE2",
        SMB1_COMMAND_TREE_DISCONNECT    => "SMB1_COMMAND_TREE_DISCONNECT",
        SMB1_COMMAND_NEGOTIATE_PROTOCOL => "SMB1_COMMAND_NEGOTIATE_PROTOCOL",
        SMB1_COMMAND_SESSION_SETUP_ANDX => "SMB1_COMMAND_SESSION_SETUP_ANDX",
        SMB1_COMMAND_LOGOFF_ANDX        => "SMB1_COMMAND_LOGOFF_ANDX",
        SMB1_COMMAND_TREE_CONNECT_ANDX  => "SMB1_COMMAND_TREE_CONNECT_ANDX",
        SMB1_COMMAND_NT_TRANS           => "SMB1_COMMAND_NT_TRANS",
        SMB1_COMMAND_NT_CREATE_ANDX     => "SMB1_COMMAND_NT_CREATE_ANDX",
        SMB1_COMMAND_NT_CANCEL          => "SMB1_COMMAND_NT_CANCEL",
        _ => { return (c).to_string(); },
    }.to_string()
}

// later we'll use this to determine if we need to
// track a ssn per type
pub fn smb1_create_new_tx(_cmd: u8) -> bool {
//    if _cmd == SMB1_COMMAND_READ_ANDX {
//        false
//    } else {
        true
//    }
}

pub fn smb1_request_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>) -> u32 {
    SCLogDebug!("record: {:?} command {}", r.greeter, r.command);

    // init default tx keys
    let mut key_ssn_id = r.ssn_id;
    let key_tree_id = r.tree_id;
    let key_multiplex_id = r.multiplex_id;
    let mut events : Vec<SMBEvent> = Vec::new();
    let mut no_response_expected = false;

    let have_tx = match r.command {
        SMB1_COMMAND_READ_ANDX => {
            match parse_smb_read_andx_request_record(r.data) {
                IResult::Done(_, rr) => {
                    SCLogDebug!("rr {:?}", rr);

                    // store read fid,offset in map
                    let fid_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_OFFSET);
                    let mut fid = rr.fid.to_vec();
                    fid.extend_from_slice(&u32_as_bytes(r.ssn_id));
                    let fidoff = SMBFileGUIDOffset::new(fid, rr.offset);
                    state.ssn2vecoffset_map.insert(fid_key, fidoff);
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                },
            }
            false
        },
        SMB1_COMMAND_WRITE_ANDX => {
            smb1_write_request_record(state, r);
            true // tx handling in func
        },
        SMB1_COMMAND_WRITE => {
            smb1_write_request_record(state, r);
            true // tx handling in func
        },
        SMB1_COMMAND_TRANS => {
            smb1_trans_request_record(state, r);
            true
        },
        SMB1_COMMAND_NEGOTIATE_PROTOCOL => {
            match parse_smb1_negotiate_protocol_record(r.data) {
                IResult::Done(_, pr) => {
                    SCLogDebug!("SMB_COMMAND_NEGOTIATE_PROTOCOL {:?}", pr);

                    let mut dialects : Vec<Vec<u8>> = Vec::new();
                    for d in &pr.dialects {
                        let x = &d[1..d.len()];
                        let dvec = x.to_vec();
                        dialects.push(dvec);
                    }

                    let found = match state.get_negotiate_tx(1) {
                        Some(tx) => {
                            SCLogDebug!("WEIRD, should not have NEGOTIATE tx!");
                            tx.set_event(SMBEvent::DuplicateNegotiate);
                            true
                        },
                        None => { false },
                    };
                    if !found {
                        let tx = state.new_negotiate_tx(1);
                        if let Some(SMBTransactionTypeData::NEGOTIATE(ref mut tdn)) = tx.type_data {
                            tdn.dialects = dialects;
                        }
                        tx.request_done = true;
                    }
                    true
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                    false
                },
            }
        },
        SMB1_COMMAND_NT_CREATE_ANDX => {
            match parse_smb_create_andx_request_record(r.data) {
                IResult::Done(_, cr) => {
                    SCLogDebug!("Create AndX {:?}", cr);
                    let del = cr.create_options & 0x0000_1000 != 0;
                    let dir = cr.create_options & 0x0000_0001 != 0;
                    SCLogDebug!("del {} dir {} options {:08x}", del, dir, cr.create_options);

                    let name_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_FILENAME);
                    let name_val = cr.file_name.to_vec();
                    state.ssn2vec_map.insert(name_key, name_val);

                    let tx_hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_GENERICTX);
                    let tx = state.new_create_tx(&cr.file_name.to_vec(),
                            cr.disposition, del, dir, tx_hdr);
                    tx.vercmd.set_smb1_cmd(r.command);
                    SCLogDebug!("TS CREATE TX {} created", tx.id);
                    true
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                    false
                },
            }
        },
        SMB1_COMMAND_SESSION_SETUP_ANDX => {
            SCLogDebug!("SMB1_COMMAND_SESSION_SETUP_ANDX user_id {}", r.user_id);
            match parse_smb_setup_andx_record(r.data) {
                IResult::Done(_, setup) => {
                    parse_secblob(state, setup.sec_blob);
/*
                        _ => {
                            events.push(SMBEvent::MalformedNtlmsspRequest);
                        },
*/
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                },
            }
            key_ssn_id = 0;
            false
        },
        SMB1_COMMAND_TREE_CONNECT_ANDX => {
            SCLogDebug!("SMB1_COMMAND_TREE_CONNECT_ANDX");
            match parse_smb_connect_tree_andx_record(r.data) {
                IResult::Done(_, create_record) => {
                    let name_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_TREE);
                    let mut name_val = create_record.share.to_vec();
                    name_val.retain(|&i|i != 0x00);
                    if name_val.len() > 1 {
                        name_val = name_val[1..].to_vec();
                    }
                    //state.ssn2vec_map.insert(name_key, name_val);

                    // store hdr as SMBHDR_TYPE_TREE, so with tree id 0
                    // when the response finds this we update it
                    let tx = state.new_treeconnect_tx(name_key, name_val);
                    tx.request_done = true;
                    tx.vercmd.set_smb1_cmd(SMB1_COMMAND_TREE_CONNECT_ANDX);
                    true
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                    false
                },
            }
        },
        SMB1_COMMAND_TREE_DISCONNECT => {
            let tree_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_SHARE);
            state.ssn2tree_map.remove(&tree_key);
            false
        },
        SMB1_COMMAND_CLOSE => {
            match parse_smb1_close_request_record(r.data) {
                IResult::Done(_, cd) => {
                    let mut fid = cd.fid.to_vec();
                    fid.extend_from_slice(&u32_as_bytes(r.ssn_id));
                    SCLogDebug!("closing FID {:?}/{:?}", cd.fid, fid);

                    // we can have created 2 txs for a FID: one for reads
                    // and one for writes. So close both.
                    match state.get_file_tx_by_guid(&fid, STREAM_TOSERVER) {
                        Some((tx, files, flags)) => {
                            SCLogDebug!("found tx {}", tx.id);
                            if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                                if !tx.request_done {
                                    SCLogDebug!("closing file tx {} FID {:?}/{:?}", tx.id, cd.fid, fid);
                                    tdf.file_tracker.close(files, flags);
                                    tx.request_done = true;
                                    tx.response_done = true;
                                    SCLogDebug!("tx {} is done", tx.id);
                                }
                                // as a precaution, reset guid so it can be reused
                                tdf.guid.clear(); // TODO review
                            }
                        },
                        None => { },
                    }
                    match state.get_file_tx_by_guid(&fid, STREAM_TOCLIENT) {
                        Some((tx, files, flags)) => {
                            SCLogDebug!("found tx {}", tx.id);
                            if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                                if !tx.request_done {
                                    SCLogDebug!("closing file tx {} FID {:?}/{:?}", tx.id, cd.fid, fid);
                                    tdf.file_tracker.close(files, flags);
                                    tx.request_done = true;
                                    tx.response_done = true;
                                    SCLogDebug!("tx {} is done", tx.id);
                                }
                                // as a precaution, reset guid so it can be reused
                                tdf.guid.clear(); // TODO review now that fid is improved
                            }
                        },
                        None => { },
                    }
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                },
            }
            false
        },
        SMB1_COMMAND_NT_CANCEL => {
            no_response_expected = true;
            false
        },
        SMB1_COMMAND_TRANS2_SECONDARY => {
            no_response_expected = true;
            false
        },
        _ => {
            if r.command == SMB1_COMMAND_TRANS2 ||
               r.command == SMB1_COMMAND_LOGOFF_ANDX ||
               r.command == SMB1_COMMAND_TREE_DISCONNECT ||
               r.command == SMB1_COMMAND_NT_TRANS ||
               r.command == SMB1_COMMAND_NT_CANCEL ||
               r.command == SMB1_COMMAND_RENAME ||
               r.command == SMB1_COMMAND_CHECK_DIRECTORY ||
               r.command == SMB1_COMMAND_LOCKING_ANDX ||
               r.command == SMB1_COMMAND_ECHO ||
               r.command == SMB1_COMMAND_TRANS
            { } else {
                 SCLogDebug!("unsupported command {}/{}",
                         r.command, &smb1_command_string(r.command));
            }
            false
        },
    };
    if !have_tx {
        if smb1_create_new_tx(r.command) {
            let tx_key = SMBCommonHdr::new(SMBHDR_TYPE_GENERICTX,
                    key_ssn_id as u64, key_tree_id as u32, key_multiplex_id as u64);
            let tx = state.new_generic_tx(1, r.command as u16, tx_key);
            SCLogDebug!("tx {} created for {}/{}", tx.id, r.command, &smb1_command_string(r.command));
            tx.set_events(events);
            if no_response_expected {
                tx.response_done = true;
            }
        }
    }
    0
}

pub fn smb1_response_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>) -> u32 {
    SCLogDebug!("record: {:?} command {}", r.greeter, r.command);

    let key_ssn_id = r.ssn_id;
    let key_tree_id = r.tree_id;
    let key_multiplex_id = r.multiplex_id;
    let mut tx_sync = false;
    let mut events : Vec<SMBEvent> = Vec::new();

    let have_tx = match r.command {
        SMB1_COMMAND_READ_ANDX => {
            smb1_read_response_record(state, &r);
            true // tx handling in func
        },
        SMB1_COMMAND_NEGOTIATE_PROTOCOL => {
            SCLogDebug!("SMB1_COMMAND_NEGOTIATE_PROTOCOL response");
            match parse_smb1_negotiate_protocol_response_record(r.data) {
                IResult::Done(_, pr) => {
                    let (have_ntx, dialect) = match state.get_negotiate_tx(1) {
                        Some(tx) => {
                            tx.set_status(r.nt_status, r.is_dos_error);
                            tx.response_done = true;
                            SCLogDebug!("tx {} is done", tx.id);
                            let d = match tx.type_data {
                                Some(SMBTransactionTypeData::NEGOTIATE(ref mut x)) => {
                                    let dialect_idx = pr.dialect_idx as usize;
                                    if x.dialects.len() <= dialect_idx {
                                        None
                                    } else {
                                        let d = x.dialects[dialect_idx].to_vec();
                                        Some(d)
                                    }
                                },
                                _ => { None },
                            };
                            if d == None {
                                tx.set_event(SMBEvent::MalformedData);
                            }
                            (true, d)
                        },
                        None => { (false, None) },
                    };
                    match dialect {
                        Some(d) => {
                            SCLogDebug!("dialect {:?}", d);
                            state.dialect_vec = Some(d);
                        },
                        _ => { },
                    }
                    have_ntx
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                    false
                },
            }
        },
        SMB1_COMMAND_TREE_CONNECT_ANDX => {
            if r.nt_status != SMB_NTSTATUS_SUCCESS {
                let name_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_TREE);
                match state.get_treeconnect_tx(name_key) {
                    Some(tx) => {
                        if let Some(SMBTransactionTypeData::TREECONNECT(ref mut tdn)) = tx.type_data {
                            tdn.tree_id = r.tree_id as u32;
                        }
                        tx.set_status(r.nt_status, r.is_dos_error);
                        tx.response_done = true;
                        SCLogDebug!("tx {} is done", tx.id);
                    },
                    None => { },
                }
                return 0;
            }

            match parse_smb_connect_tree_andx_response_record(r.data) {
                IResult::Done(_, tr) => {
                    let name_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_TREE);
                    let is_pipe = tr.service == "IPC".as_bytes();
                    let mut share_name = Vec::new();
                    let found = match state.get_treeconnect_tx(name_key) {
                        Some(tx) => {
                            if let Some(SMBTransactionTypeData::TREECONNECT(ref mut tdn)) = tx.type_data {
                                tdn.is_pipe = is_pipe;
                                tdn.tree_id = r.tree_id as u32;
                                share_name = tdn.share_name.to_vec();
                            }
                            tx.hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_HEADER);
                            tx.set_status(r.nt_status, r.is_dos_error);
                            tx.response_done = true;
                            SCLogDebug!("tx {} is done", tx.id);
                            true
                        },
                        None => { false },
                    };
                    if found {
                        let tree = SMBTree::new(share_name.to_vec(), is_pipe);
                        let tree_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_SHARE);
                        state.ssn2tree_map.insert(tree_key, tree);
                    }
                    found
                },
                _ => {
                    events.push(SMBEvent::MalformedData);
                    false
                },
            }
        },
        SMB1_COMMAND_TREE_DISCONNECT => {
            // normally removed when processing request,
            // but in case we missed that try again here
            let tree_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_SHARE);
            state.ssn2tree_map.remove(&tree_key);
            false
        },
        SMB1_COMMAND_NT_CREATE_ANDX => {
            match parse_smb_create_andx_response_record(r.data) {
                IResult::Done(_, cr) => {
                    SCLogDebug!("Create AndX {:?}", cr);

                    let guid_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_FILENAME);
                    match state.ssn2vec_map.remove(&guid_key) {
                        Some(mut p) => {
                            p.retain(|&i|i != 0x00);

                            let mut fid = cr.fid.to_vec();
                            fid.extend_from_slice(&u32_as_bytes(r.ssn_id));
                            SCLogDebug!("SMB1_COMMAND_NT_CREATE_ANDX fid {:?}", fid);
                            SCLogDebug!("fid {:?} name {:?}", fid, p);
                            state.guid2name_map.insert(fid, p);
                        },
                        _ => {
                            SCLogDebug!("SMBv1 response: GUID NOT FOUND");
                        },
                    }
                },
                _ => { events.push(SMBEvent::MalformedData); },
            }
            false
        },
        SMB1_COMMAND_TRANS => {
            smb1_trans_response_record(state, r);
            true
        },
        SMB1_COMMAND_SESSION_SETUP_ANDX => {
            tx_sync = true;
            false
        },
        SMB1_COMMAND_LOGOFF_ANDX => {
            tx_sync = true;
            false
        },
        _ => {
            false
        },
    };

    if !have_tx && tx_sync {
        match state.get_last_tx(1, r.command as u16) {
            Some(tx) => {
                SCLogDebug!("last TX {} is CMD {}", tx.id, &smb1_command_string(r.command));
                tx.response_done = true;
                SCLogDebug!("tx {} cmd {} is done", tx.id, r.command);
                tx.set_status(r.nt_status, r.is_dos_error);
                tx.set_events(events);
            },
            _ => {},
        }
    } else if !have_tx && smb1_create_new_tx(r.command) {
        let tx_key = SMBCommonHdr::new(SMBHDR_TYPE_GENERICTX,
                key_ssn_id as u64, key_tree_id as u32, key_multiplex_id as u64);
        let _have_tx2 = match state.get_generic_tx(1, r.command as u16, &tx_key) {
            Some(tx) => {
                tx.request_done = true;
                tx.response_done = true;
                SCLogDebug!("tx {} cmd {} is done", tx.id, r.command);
                tx.set_status(r.nt_status, r.is_dos_error);
                tx.set_events(events);
                true
            },
            _ => {
                SCLogDebug!("no TX found for key {:?}", tx_key);
                false
            },
        };
    } else {
        SCLogDebug!("have tx for cmd {}", r.command);
    }
    0
}

pub fn get_service_for_nameslice(nameslice: &[u8]) -> (&'static str, bool)
{
    let mut name = nameslice.to_vec();
    name.retain(|&i|i != 0x00);
    
    match str::from_utf8(&name) {
        Ok("\\PIPE\\LANMAN") => ("LANMAN", false),
        Ok("\\PIPE\\") => ("PIPE", true), // TODO not sure if this is true
        Err(_) => ("MALFORMED", false),
        Ok(&_) => {
            SCLogNotice!("don't know \"{}\"", String::from_utf8_lossy(&name));
            ("UNKNOWN", false)
        },
    }
}

pub fn smb1_trans_request_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>)
{
    match parse_smb_trans_request_record(r.data, r) {
        IResult::Done(_, rd) => {
            SCLogDebug!("TRANS request {:?}", rd);

            /* if we have a fid, store it so the response can pick it up */
            if rd.pipe != None {
                let pipe = rd.pipe.unwrap();
                state.ssn2vec_map.insert(SMBCommonHdr::from1(r, SMBHDR_TYPE_GUID),
                        pipe.fid.to_vec());
            }

            let (sername, is_dcerpc) = get_service_for_nameslice(&rd.txname.name);
            SCLogDebug!("service: {} dcerpc {}", sername, is_dcerpc);
            if is_dcerpc {
                // store tx name so the response also knows this is dcerpc
                let txn_hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_TXNAME);
                state.ssn2vec_map.insert(txn_hdr, rd.txname.name.to_vec());

                // trans request will tell us the max size of the response
                // if there is more response data, it will first give a
                // TRANS with 'max data cnt' worth of data, and the rest
                // will be pulled by a 'READ'. So we setup an expectation
                // here.
                if rd.params.max_data_cnt > 0 {
                    // expect max max_data_cnt for this fid in the other dir
                    let ehdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_MAX_SIZE);
                    state.ssn2maxsize_map.insert(ehdr, rd.params.max_data_cnt);
                }

                SCLogDebug!("SMBv1 TRANS TO PIPE");
                let hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_HEADER);
                let vercmd = SMBVerCmdStat::new1(r.command);
                smb_write_dcerpc_record(state, vercmd, hdr, &rd.data.data);
            }
        },
        _ => {
            state.set_event(SMBEvent::MalformedData);
        },
    }
}

pub fn smb1_trans_response_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>)
{
    match parse_smb_trans_response_record(r.data) {
        IResult::Done(_, rd) => {
            SCLogDebug!("TRANS response {:?}", rd);

            // see if we have a stored fid
            let fid = match state.ssn2vec_map.remove(
                    &SMBCommonHdr::from1(r, SMBHDR_TYPE_GUID)) {
                Some(f) => f,
                None => Vec::new(),
            };
            SCLogDebug!("FID {:?}", fid);

            // if we get status 'BUFFER_OVERFLOW' this is only a part of
            // the data. Store it in the ssn/tree for later use.
            if r.nt_status == SMB_NTSTATUS_BUFFER_OVERFLOW {
                state.ssnguid2vec_map.insert(SMBHashKeyHdrGuid::new(
                            SMBCommonHdr::from1(r, SMBHDR_TYPE_TRANS_FRAG), fid),
                            rd.data.to_vec());
            } else {
                let txn_hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_TXNAME);
                let is_dcerpc = match state.ssn2vec_map.remove(&txn_hdr) {
                    None => false,
                    Some(s) => {
                        let (sername, is_dcerpc) = get_service_for_nameslice(&s);
                        SCLogDebug!("service: {} dcerpc {}", sername, is_dcerpc);
                        is_dcerpc
                    },
                };
                if is_dcerpc {
                    SCLogDebug!("SMBv1 TRANS TO PIPE");
                    let hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_HEADER);
                    let vercmd = SMBVerCmdStat::new1_with_ntstatus(r.command, r.nt_status);
                    smb_read_dcerpc_record(state, vercmd, hdr, &fid, &rd.data);
                }
            }
        },
        _ => {
            state.set_event(SMBEvent::MalformedData);
        },
    }
}

/// Handle WRITE and WRITE_ANDX request records
pub fn smb1_write_request_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>)
{
    let result = if r.command == SMB1_COMMAND_WRITE_ANDX {
        parse_smb1_write_andx_request_record(r.data)
    } else {
        parse_smb1_write_request_record(r.data)
    };
    match result {
        IResult::Done(_, rd) => {
            SCLogDebug!("SMBv1: write andx => {:?}", rd);

            let mut file_fid = rd.fid.to_vec();
            file_fid.extend_from_slice(&u32_as_bytes(r.ssn_id));
            SCLogDebug!("SMBv1 WRITE: FID {:?} offset {}",
                    file_fid, rd.offset);

            let file_name = match state.guid2name_map.get(&file_fid) {
                Some(n) => n.to_vec(),
                None => Vec::new(),
            };
            let found = match state.get_file_tx_by_guid(&file_fid, STREAM_TOSERVER) {
                Some((tx, files, flags)) => {
                    let file_id : u32 = tx.id as u32;
                    if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                        filetracker_newchunk(&mut tdf.file_tracker, files, flags,
                                &file_name, rd.data, rd.offset,
                                rd.len, 0, false, &file_id);
                        SCLogDebug!("FID {:?} found at tx {}", file_fid, tx.id);
                    }
                    true
                },
                None => { false },
            };
            if !found {
                let tree_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_SHARE);
                let (share_name, is_pipe) = match state.ssn2tree_map.get(&tree_key) {
                    Some(n) => (n.name.to_vec(), n.is_pipe),
                    None => (Vec::new(), false),
                };
                if is_pipe {
                    SCLogDebug!("SMBv1 WRITE TO PIPE");
                    let hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_HEADER);
                    let vercmd = SMBVerCmdStat::new1_with_ntstatus(r.command, r.nt_status);
                    smb_write_dcerpc_record(state, vercmd, hdr, &rd.data);
                } else {
                    let (tx, files, flags) = state.new_file_tx(&file_fid, &file_name, STREAM_TOSERVER);
                    if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                        let file_id : u32 = tx.id as u32;
                        SCLogDebug!("FID {:?} found at tx {}", file_fid, tx.id);
                        filetracker_newchunk(&mut tdf.file_tracker, files, flags,
                                &file_name, rd.data, rd.offset,
                                rd.len, 0, false, &file_id);
                        tdf.share_name = share_name;
                    }
                    tx.vercmd.set_smb1_cmd(SMB1_COMMAND_WRITE_ANDX);
                }
            }
            state.file_ts_left = rd.len - rd.data.len() as u32;
            state.file_ts_guid = file_fid.to_vec();
            SCLogDebug!("SMBv1 WRITE RESPONSE: {} bytes left", state.file_tc_left);
        },
        _ => {
            state.set_event(SMBEvent::MalformedData);
        },
    }
}

fn smb1_read_response_record_generic<'b>(state: &mut SMBState, r: &SmbRecord<'b>) {
    // see if we want a tx per READ command
    if smb1_create_new_tx(r.command) {
        let tx_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_GENERICTX);
        let tx = state.get_generic_tx(1, r.command as u16, &tx_key);
        if let Some(tx) = tx {
            tx.request_done = true;
            tx.response_done = true;
            SCLogDebug!("tx {} cmd {} is done", tx.id, r.command);
            tx.set_status(r.nt_status, r.is_dos_error);
        }
    }
}

pub fn smb1_read_response_record<'b>(state: &mut SMBState, r: &SmbRecord<'b>)
{
    smb1_read_response_record_generic(state, r);
  
    if r.nt_status != SMB_NTSTATUS_SUCCESS {
        return;
    }

    match parse_smb_read_andx_response_record(r.data) {
        IResult::Done(_, rd) => {
            SCLogDebug!("SMBv1: read response => {:?}", rd);

            let fid_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_OFFSET);
            let (offset, file_fid) = match state.ssn2vecoffset_map.remove(&fid_key) {
                Some(o) => (o.offset, o.guid),
                None => {
                    SCLogNotice!("SMBv1 READ response: reply to unknown request: left {} {:?}",
                            rd.len - rd.data.len() as u32, rd);
                    state.skip_tc = rd.len - rd.data.len() as u32;
                    return;
                },
            };
            SCLogDebug!("SMBv1 READ: FID {:?} offset {}", file_fid, offset);

            let tree_key = SMBCommonHdr::from1(r, SMBHDR_TYPE_SHARE);
            let (is_pipe, share_name) = match state.ssn2tree_map.get(&tree_key) {
                Some(n) => (n.is_pipe, n.name.to_vec()),
                _ => { (false, Vec::new()) },
            };
            if !is_pipe {
                let file_name = match state.guid2name_map.get(&file_fid) {
                    Some(n) => n.to_vec(),
                    None => Vec::new(),
                };
                let found = match state.get_file_tx_by_guid(&file_fid, STREAM_TOCLIENT) {
                    Some((tx, files, flags)) => {
                        if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                            let file_id : u32 = tx.id as u32;
                            SCLogDebug!("FID {:?} found at tx {}", file_fid, tx.id);
                            filetracker_newchunk(&mut tdf.file_tracker, files, flags,
                                    &file_name, rd.data, offset,
                                    rd.len, 0, false, &file_id);
                        }
                        true
                    },
                    None => { false },
                };
                if !found {
                    let (tx, files, flags) = state.new_file_tx(&file_fid, &file_name, STREAM_TOCLIENT);
                    if let Some(SMBTransactionTypeData::FILE(ref mut tdf)) = tx.type_data {
                        let file_id : u32 = tx.id as u32;
                        SCLogDebug!("FID {:?} found at tx {}", file_fid, tx.id);
                        filetracker_newchunk(&mut tdf.file_tracker, files, flags,
                                &file_name, rd.data, offset,
                                rd.len, 0, false, &file_id);
                        tdf.share_name = share_name;
                    }
                    tx.vercmd.set_smb1_cmd(SMB1_COMMAND_READ_ANDX);
                }
            } else {
                SCLogDebug!("SMBv1 READ response from PIPE");
                let hdr = SMBCommonHdr::from1(r, SMBHDR_TYPE_HEADER);
                let vercmd = SMBVerCmdStat::new1(r.command);

                // hack: we store fid with ssn id mixed in, but here we want the
                // real thing instead.
                let pure_fid = if file_fid.len() > 2 { &file_fid[0..2] } else { &[] };
                smb_read_dcerpc_record(state, vercmd, hdr, &pure_fid, &rd.data);
            }

            state.file_tc_left = rd.len - rd.data.len() as u32;
            state.file_tc_guid = file_fid.to_vec();
            SCLogDebug!("SMBv1 READ RESPONSE: {} bytes left", state.file_tc_left);
        }
        _ => {
            state.set_event(SMBEvent::MalformedData);
        },
    }
}
