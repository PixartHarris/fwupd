// Copyright {{Year}} {{Author}} <{{Email}}>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructPxiTp {
    signature: u8 == 0xDE,
    address: u16le,
}

#[derive(ToString)]
enum FuPxiTpStatus {
    Unknown,
    Failed,
}
