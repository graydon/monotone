// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "app_state.hh"
#include "file_io.hh"
#include "lua.hh"
#include "manifest.hh"
#include "nonce.hh"
#include "transforms.hh"
#include "vocab.hh"

#include "cryptopp/osrng.h"

#include <string>

// the idea with nonces is that sometimes users will want to differentiate
// versions which are "essentially identical", for example to differentiate
// time-lines which "return" to previous versions, cyclically. to handle
// such circumstances, we make a file called .mt-nonce which has a little
// bit of random noise in it. any time you want to make a version "unique"
// in history, you "bump" the nonce, generating a new one. the file is
// stored like any other in the monotone database, with once exception: it
// is merged using a special 2-way merge strategy which just XORs the bytes
// of each nonce together when they conflict.

std::string const nonce_file_name(".mt-nonce");

void
make_nonce(app_state & app, 
	   std::string & nonce)
{
  if (! app.lua.hook_get_nonce(nonce))
    {
      size_t const sz = 4096;
      CryptoPP::AutoSeededRandomPool nonce_urandom;
      byte buf[sz];
      nonce_urandom.GenerateBlock(buf, sz);
      nonce = std::string(reinterpret_cast<char *>(buf), 
			  reinterpret_cast<char *>(buf+sz));
    }  
}

void
merge_nonces(file_data const & left,
	     file_data const & right,
	     file_data & merged)
{
  data ldat, rdat;
  base64< gzip<data> > mdat;
  std::string lstr, rstr, mstr;
  unpack(left.inner(), ldat);
  unpack(right.inner(), rdat);
  lstr = ldat();
  rstr = rdat();
  for (size_t i = 0; i < std::min(lstr.size(), rstr.size()); ++i)
    {
      mstr += (lstr[i] ^ rstr[i]);
    }
  pack(data(mstr), mdat);
  merged = file_data(mdat);
}

void
merge_nonces(file_data const & ancestor,
	     file_data const & left,
	     file_data const & right,
	     file_data & merged)
{
  if (ancestor == left)
    merged = right;
  else if (ancestor == right)
    merged = left;
  else
    merge_nonces(left, right, merged);
}
	     
