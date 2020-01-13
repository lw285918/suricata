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

extern crate proc_macro;
use proc_macro::TokenStream;
use quote::quote;
use syn::{self, parse_macro_input, DeriveInput};

pub fn derive_app_layer_event(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = input.ident;

    let mut fields = Vec::new();
    let mut vals = Vec::new();
    let mut cstrings = Vec::new();
    let mut names = Vec::new();

    match input.data {
        syn::Data::Enum(ref data) => {
            for (i, v) in (&data.variants).into_iter().enumerate() {
                fields.push(v.ident.clone());
                let name = transform_name(&v.ident.to_string());
                let cname = format!("{}\0", name);
                names.push(name);
                cstrings.push(cname);
                vals.push(i as i32);
            }
        }
        _ => unimplemented!(),
    }

    let expanded = quote! {
        impl crate::applayer::AppLayerEvent for #name {
            fn from_id(id: i32) -> Option<#name> {
                match id {
                    #( #vals => Some(#name::#fields) ,)*
                    _ => None,
                }
            }

            fn as_i32(&self) -> i32 {
                match *self {
                    #( #name::#fields => #vals ,)*
                }
            }

            fn to_cstring(&self) -> &str {
                match *self {
                    #( #name::#fields => #cstrings ,)*
                }
            }

            fn from_cstring(s: &std::ffi::CStr) -> Option<#name> {
                if let Ok(s) = s.to_str() {
                    match s {
                        #( #names => Some(#name::#fields) ,)*
                        _ => None
                    }
                } else {
                    None
                }
            }
        }
    };

    proc_macro::TokenStream::from(expanded)
}

pub fn transform_name(in_name: &str) -> String {
    let mut out = String::new();
    for (i, c) in in_name.chars().enumerate() {
        if i == 0 {
            out.push_str(&c.to_lowercase().to_string());
        } else if c.is_uppercase() {
            out.push('_');
            out.push_str(&c.to_lowercase().to_string());
        } else {
            out.push(c);
        }
    }
    out
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_transform_name() {
        assert_eq!(transform_name("SomeEvent"), "some_event".to_string());
        assert_eq!(
            transform_name("UnassignedMsgType"),
            "unassigned_msg_type".to_string()
        );
    }
}
