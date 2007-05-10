local index = 1

function checkexp (label, computed, expected)
   if computed ~= expected then
      err (label .. " Expected '" .. expected .. "' got '" .. computed .. "'")
   end
end

function checkvalue (label, computed, name, value)
   checkexp(label .. ".name", computed.name, name)

   if type(value) == "table" then
      checkexp(label .. ".length", #computed.values, #value)
      for i = 1, #value do
         checkexp(label .. i, computed.values[i], value[i])
      end

   else
      checkexp(label .. ".length", #computed.values, 1)
      checkexp(label .. "." .. name, computed.values[1], value)
   end
end

function find_line (parsed, line)
-- return index in parsed matching line.name, line.values
   for I = 1, #parsed do
       if parsed[I].name == line.name then
          if type (line.values) ~= "table" then
             if parsed[I].values[1] == line.values then
                return I
             end
          else
             err ("searching for line with table of values not yet supported")
          end
       end
   end

   err ("line '" .. line.name .. " " .. line.values .. "' not found")
end

function check_inventory (parsed, parsed_index, stanza)
-- 'stanza' is a table for one stanza
-- 'parsed_index' gives the first index for this stanza in 'parsed'
-- (which should be the output of parse_basic_io).
-- Returns parsed_index incremented to the last index checked.

   checkvalue (parsed_index, parsed[parsed_index], "path", stanza.path)
   parsed_index = parsed_index + 1

   if stanza.old_type then
      checkvalue (parsed_index, parsed[parsed_index], "old_type", stanza.old_type)
      parsed_index = parsed_index + 1
   end

   if stanza.new_path then
      checkvalue (parsed_index, parsed[parsed_index], "new_path", stanza.new_path)
      parsed_index = parsed_index + 1
   end

   if stanza.new_type then
      checkvalue (parsed_index, parsed[parsed_index], "new_type", stanza.new_type)
      parsed_index = parsed_index + 1
   end

   if stanza.old_path then
      checkvalue (parsed_index, parsed[parsed_index], "old_path", stanza.old_path)
      parsed_index = parsed_index + 1
   end

   if stanza.fs_type then
      checkvalue (parsed_index, parsed[parsed_index], "fs_type", stanza.fs_type)
      parsed_index = parsed_index + 1
   end

   if stanza.status then
      checkvalue (parsed_index, parsed[parsed_index], "status", stanza.status)
      parsed_index = parsed_index + 1
   end

   if stanza.changes then
      checkvalue (parsed_index, parsed[parsed_index], "changes", stanza.changes)
      parsed_index = parsed_index + 1
   end

   return parsed_index
end -- check_inventory

----------
--  main process

mtn_setup()

check(get("inventory_hooks.lua"))

-- create a basic file history; add some files, then operate on each
-- of them in some way.

addfile("missing", "missing")
addfile("dropped", "dropped")
addfile("original", "original")
addfile("unchanged", "unchanged")
addfile("patched", "patched")
commit()

addfile("added", "added")
writefile("unknown", "unknown")
writefile("ignored~", "ignored~")

remove("missing")
remove("dropped")
rename("original", "renamed")
writefile("patched", "something has changed")

check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "dropped"), 0, false, false)

-- Now see what 'automate inventory' has to say

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

--  not in previous output
--  FIXME: null path is root directory. need to keep for 'pivot_root'; test that
index = check_inventory (parsed, index,
{path = "",
 old_type = "directory",
 new_type = "directory",
 fs_type = "none",
 status = {"missing"}})

--  AP 0 0 added
index = check_inventory (parsed, index,
{path = "added",
 new_type = "file",
 fs_type = "file",
 status = {"added", "known"}})

-- D   0 0 dropped
index = check_inventory (parsed, index,
{path = "dropped",
 old_type = "file",
 fs_type = "none",
  status = "dropped"})

--   I 0 0 ignored
index = check_inventory (parsed, index,
{   path = "ignored~",
 fs_type = "file",
  status = "ignored"})

-- skip inventory_hooks.lua, keys, keys/tester@test.net, min_hooks.lua
index = index + 3 * 4

--   M 0 0 missing
index = check_inventory (parsed, index,
{   path = "missing",
old_type = "file",
new_type = "file",
 fs_type = "none",
  status = "missing"})

-- R   1 0 original
index = check_inventory (parsed, index,
{   path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "none",
  status = "rename_source"})

--   P 0 0 patched
index = check_inventory (parsed, index,
{   path = "patched",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = "known",
 changes = "content"})

--  R  0 1 renamed
index = check_inventory (parsed, index,
{   path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_target", "known"}})

-- skip test.db, test_hooks.lua, tester.log, ts-stderr, ts-stdin, ts-stdout
index = find_line (parsed, {name = "path", values = "unchanged"})

--     0 0 unchanged
index = check_inventory (parsed, index,
{   path = "unchanged",
old_type = "file",
new_type = "file",
 fs_type = "file",
  status = "known"})

--   U 0 0 unknown
index = check_inventory (parsed, index,
{  path = "unknown",
fs_type = "file",
 status = "unknown"})

-- swapped but not moved

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "--bookkeep-only", "unchanged", "temporary"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "original", "unchanged"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "temporary", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

-- Only check the stanzas for the renamed files
index = find_line (parsed, {name = "path", values = "original"})

-- FIXME: this output is confusing. And this file's contents are _not_ changed.
--
-- tommyd: since the changes were bookkeep-only, the node to which inventory
-- refers to with 'path' actually has different contents than what has been
-- recorded in the old roster, so its more the question: do we want to guess
-- if a file rename was bookkeep-only by comparing fileids or not? If a commit
-- happens at this stage, no missing files hinder that, and both file's 
-- contents are really changed and not swapped as they should, so I vote for
-- keeping this flag on here.
check_inventory (parsed, index,
{path     = "original",
 old_type = "file",
 new_path = "unchanged",
 new_type = "file",
 old_path = "unchanged",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"},
 changes  = "content"})

index = find_line (parsed, {name = "path", values = "unchanged"})

-- FIXME: this output is confusing.
check_inventory (parsed, index,
{path     = "unchanged",
 old_type = "file",
 new_path = "original",
 new_type = "file",
 old_path = "original",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"},
 changes  = "content"})

-- swapped and moved

rename("unchanged", "temporary")
rename("original", "unchanged")
rename("temporary", "original")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

-- Only check the stanzas for the renamed files
index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{path     = "original",
 old_type = "file",
 new_path = "unchanged",
 new_type = "file",
 old_path = "unchanged",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"}})

index = find_line (parsed, {name = "path", values = "unchanged"})

-- FIXME: this output is confusing.
check_inventory (parsed, index,
{path     = "unchanged",
 old_type = "file",
 new_path = "original",
 new_type = "file",
 old_path = "original",
 fs_type  = "file",
 status   = {"rename_source", "rename_target", "known"}})

-- rename foo bar; add foo

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("add", "original"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{path     = "original",
 old_type = "file",
 new_path = "renamed",
 new_type = "file",
 fs_type  = "file",
 status   = {"rename_source", "added", "known"}})

-- rotated but not moved
--   dropped -> missing -> original -> dropped

check(mtn("revert", "."), 0, false, false)

check(mtn("rename", "--bookkeep-only", "original", "temporary"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "missing", "original"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "dropped", "missing"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "temporary", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "dropped"})

check_inventory (parsed, index,
{path = "dropped",
old_type = "file",
new_path = "missing",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

index = find_line (parsed, {name = "path", values = "missing"})

check_inventory (parsed, index,
{   path = "missing",
old_type = "file",
new_path = "original",
new_type = "file",
old_path = "dropped",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{   path = "original",
old_type = "file",
new_path = "dropped",
new_type = "file",
old_path = "missing",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"},
 changes = "content"})

-- rotated and moved

rename("original", "temporary")
rename("missing", "original")
rename("dropped", "missing")
rename("temporary", "dropped")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "dropped"})

--  FIXME: the only difference between this and the previous case is the absense of the erroneous 'changes'.
check_inventory (parsed, index,
{path = "dropped",
old_type = "file",
new_path = "missing",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

index = find_line (parsed, {name = "path", values = "missing"})

check_inventory (parsed, index,
{   path = "missing",
old_type = "file",
new_path = "original",
new_type = "file",
old_path = "dropped",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{   path = "original",
old_type = "file",
new_path = "dropped",
new_type = "file",
old_path = "missing",
 fs_type = "file",
  status = {"rename_source", "rename_target", "known"}})

-- dropped but not removed and thus unknown

check(mtn("revert", "."), 0, false, false)

check(mtn("drop", "--bookkeep-only", "dropped"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "dropped"})

check_inventory (parsed, index,
{path = "dropped",
old_type = "file",
 fs_type = "file",
  status = {"dropped", "unknown"}})

-- added but removed and thus missing

check(mtn("revert", "."), 0, false, false)

check(mtn("add", "added"), 0, false, false)
remove("added")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "added"})

check_inventory (parsed, index,
{path = "added",
new_type = "file",
 fs_type = "none",
  status = {"added", "missing"}})

-- renamed but not moved and thus unknown source and  missing target

check(mtn("revert", "."), 0, false, false)

remove("renamed")
check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "file",
  status = {"rename_source", "unknown"}})

index = find_line (parsed, {name = "path", values = "renamed"})

check_inventory (parsed, index,
{path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "none",
  status = {"rename_target", "missing"}})

-- moved but not renamed and thus missing source and unknown target

check(mtn("revert", "."), 0, false, false)

rename("original", "renamed")

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{path = "original",
old_type = "file",
new_type = "file",
 fs_type = "none",
  status = {"missing"}})

index = find_line (parsed, {name = "path", values = "renamed"})

check_inventory (parsed, index,
{path = "renamed",
 fs_type = "file",
  status = {"unknown"}})

-- renamed and patched

check(mtn("revert", "."), 0, false, false)

writefile("renamed", "renamed and patched")
remove("original")

check(mtn("rename", "--bookkeep-only", "original", "renamed"), 0, false, false)
check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "original"})

check_inventory (parsed, index,
{path = "original",
old_type = "file",
new_path = "renamed",
 fs_type = "none",
  status = {"rename_source"}})

index = find_line (parsed, {name = "path", values = "renamed"})

check_inventory (parsed, index,
{path = "renamed",
new_type = "file",
old_path = "original",
 fs_type = "file",
  status = {"rename_target", "known"},
  changes = "content"})

-- check if unknown/missing/dropped directories are recognized as such

mkdir("new_dir")
check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)

parsed = parse_basic_io(readfile("stdout"))

index = find_line (parsed, {name = "path", values = "new_dir"})

check_inventory (parsed, index,
{path = "new_dir",
 fs_type = "directory",
  status = {"unknown"}})

check(mtn("add", "new_dir"), 0, false, false)
remove("new_dir");

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_line (parsed, {name = "path", values = "new_dir"}),
{    path = "new_dir",
 new_type = "directory",
  fs_type = "none",
   status = {"added", "missing"}})

mkdir("new_dir")
commit()

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_line (parsed, {name = "path", values = "new_dir"}),
{path = "new_dir",
old_type = "directory",
new_type = "directory",
 fs_type = "directory",
  status = {"known"}})

remove("new_dir")
check(mtn("drop", "--bookkeep-only", "new_dir"), 0, false, false)

check(mtn("automate", "inventory", "--rcfile=inventory_hooks.lua"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check_inventory (parsed, find_line (parsed, {name = "path", values = "new_dir"}),
{   path = "new_dir",
old_type = "directory",
 fs_type = "none",
  status = {"dropped"}})

-- TODO: tests for renaming directories and restricted inventory output
