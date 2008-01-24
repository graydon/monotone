-- Test that 'automate inventory' properly ignores directories given in .mtn_ignore

function sortContentsByLine(input)
  local lines = {}
  local theStart = 1
  local delimiter = "\n"
  local theSplitStart, theSplitEnd = string.find(input, delimiter, theStart)

  while theSplitStart do
    table.insert(lines, string.sub(input, theStart, theSplitStart - 1))
    theStart = theSplitEnd + 1
    theSplitStart, theSplitEnd = string.find(input, delimiter, theStart)
  end
  table.insert(lines, string.sub(input, theStart))
  table.sort(lines)
  
  local len = table.getn(lines)
  local output = lines[1]
  for i = 2, len do
    output = output .. delimiter .. lines[i]
  end
  return output
end

mtn_setup()

check(get("local_hooks.lua"))
check(get("expected.stderr"))
check(get("expected.stdout"))

include ("common/test_utils_inventory.lua")

----------
-- The local local_hooks.lua defines ignore_file to ignore 'ignored'
-- directory, 'source/ignored_1' file. It also writes the name of each
-- file checked to stderr. So we check to see that it does _not_ write
-- the names of the files in the ignored directory.

mkdir ("source")
addfile("source/source_1", "source_1")
addfile("source/source_2", "source_2")
writefile("source/ignored_1", "source ignored_1")

mkdir ("source/ignored_dir")
writefile ("source/ignored_dir/file_1", "ignored file 1")
writefile ("source/ignored_dir/file_2", "ignored file 2")

check(mtn("automate", "inventory", "--rcfile=local_hooks.lua", "source"), 0, true, false)

canonicalize("stdout")
canonicalize("ts-stderr")

check (readfile("expected.stdout") == readfile("stdout"))

check (sortContentsByLine(readfile("expected.stderr")) == sortContentsByLine(readfile("ts-stderr")))


-- end of file
