// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this fragment is included into packet.hh and packet.cc at various
// places, to define the parts necessary to construct the family of
// packet types.

PACKET_TYPE(file_data, fdata);
PACKET_TYPE(file_delta, fdelta);
PACKET_TYPE(file_cert, file<cert>);
PACKET_TYPE(manifest_data, mdata);
PACKET_TYPE(manifest_delta, mdelta);
PACKET_TYPE(manifest_cert, manifest<cert>);
