#ifndef __PATH_COMPONENT_HH__
#define __PATH_COMPONENT_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>

#include "file_io.hh"
#include "numeric_vocab.hh"
#include "vocab.hh"

typedef u32 path_component;

void
compose_path(std::vector<path_component> const & names,
             file_path & path);

void
split_path(file_path const & p,
           std::vector<path_component> & components);

void
split_path(file_path const & p,
           std::vector<path_component> & prefix,
           path_component & leaf_path);

path_component
make_null_component();

inline bool
null_name(path_component pc)
{
  return make_null_component() == pc;
}

#endif
