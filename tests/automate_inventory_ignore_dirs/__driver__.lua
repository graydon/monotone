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

check(mtn("automate", "inventory", "--rcfile=local_hooks.lua", "source"), 0, true, true)
canonicalize("stdout")
canonicalize("stderr")

check(get("expected.stdout"))
check (readfile("expected.stdout") == readfile("stdout"))

check(get("expected.stderr"))
check (sortContentsByLine(readfile("expected.stderr")) == sortContentsByLine(readfile("stderr")))


-- However, if we then add a file in the ignored directory, it will
-- be reported as 'missing'. So we output a warning for this.
addfile("source/ignored_dir/oops", "commited ignored file!")

check(mtn("automate", "inventory", "--rcfile=local_hooks.lua", "source"), 0, true, true)
canonicalize("stdout")
canonicalize("stderr")

check(get("expected_2.stdout"))
check (readfile("expected_2.stdout") == readfile("stdout"))

check(get("expected_2.stderr"))
check (sortContentsByLine(readfile("expected_2.stderr")) == sortContentsByLine(readfile("stderr")))

-- end of file
