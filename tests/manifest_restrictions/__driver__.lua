
mtn_setup()

check(get("ignored.lua"))

mkdir("work")
mkdir("work/A")
mkdir("work/A/B")

writefile("work/foo.o", "version 1 of foo.o")

addfile("work/fileX", "version 1 of fileX which will be renamed to work/file1")
addfile("work/file2", "version 1 of file2")
addfile("work/file3", "version 1 of file3")

writefile("work/file4", "version 1 of file4")

addfile("work/A/fileA", "file in a subdirectory")
addfile("work/A/B/fileAB", "file in a deeper subdirectory")

-- initial commit

commit()

-- FIXME_RESTRICTIONS: the old code allows for --depth=N with no paths
-- and adds the "." path so that depth is interpreted against the current
-- included directory. this seems bad. how does --depth interact with --exclude?
--check(mtn("ls", "known", "--depth=0"), 0, true, false)
--check(not qgrep("fileX", "stdout"))

check(mtn("ls", "known", "--depth=1", ".") , 0, true, false)
check(not qgrep("fileX", "stdout"))

check(mtn("ls", "known", "--depth=2", ".") , 0, true, false)
check(qgrep("fileX", "stdout"))

check(mtn("ls", "known", "--depth=1", "work/A") , 0, true, false)
check(not qgrep("fileAB", "stdout"))

check(mtn("ls", "known", "--depth=2", "work/A") , 0, true, false)
check(qgrep("fileAB", "stdout"))

-- test restriction of unknown, missing, ignored files

check(mtn("ls", "unknown"), 0, true, false)
check(qgrep("work/file4", "stdout"))

check(mtn("ls", "unknown", "work"), 0, true, false)
check(qgrep("work/file4", "stdout"))

rename("work/file2", "work/filex2")

check(mtn("ls", "missing"), 0, true, false)
check(qgrep("work/file2", "stdout"))

check(mtn("ls", "missing", "work/file2"), 0, true, false)
check(qgrep("work/file2", "stdout"))

rename("work/filex2", "work/file2")

check(mtn("ls", "ignored", "--rcfile=ignored.lua"), 0, true, false)
check(qgrep("work/foo.o", "stdout"))

check(mtn("ls", "ignored", "--rcfile=ignored.lua", "work"), 0, true, false)
check(qgrep("work/foo.o", "stdout"))

-- create moved, dropped, and changed work to test status, diff, commit

rename("work/fileX", "work/file1")
remove("work/file2")

writefile("work/file3", "version 2 of file3 with some changes")
writefile("work/A/fileA", "version 2 of fileA with some changes")
writefile("work/A/B/fileAB", "version 2 of fileAB with some changes")

check(mtn("rename", "--bookkeep-only", "work/fileX", "work/file1"), 0, false, false)
check(mtn("drop", "--bookkeep-only", "work/file2"), 0, false, false)
check(mtn("add", "work/file4"), 0, false, false)

-- moved fileX to file1
-- dropped file2
-- changed file3
-- added file4

-- test for files included/excluded in various outputs

function included(...)
  local missed = {}
  local ok = true
  for _,x in ipairs(arg) do
    if not qgrep("work/file"..x, "stdout") then
      table.insert(missed, x)
      ok = false
    end
  end
  if not ok then
    L("missed: ", table.concat(missed, " "), "\n")
  end
  return ok
end

function excluded(...)
  local missed = {}
  local ok = true
  for _,x in ipairs(arg) do
    if qgrep("work/file"..x, "stdout") then
      table.insert(missed, x)
      ok = false
    end
  end
  if not ok then
    L("seen: ", table.concat(missed, " "), "\n")
  end
  return ok
end

-- status

check(mtn("status"), 0, true, false)
check(included("X", 1, 2, 3, 4), 0, false)

-- include both source and target of rename

check(mtn("status", "work/fileX", "work/file1"), 0, true, false)
check(included("X", 1))
check(excluded(2, 3, 4))

-- include drop

check(mtn("status", "work/file2"), 0, true, false)
check(included(2))
check(excluded("X", 1, 3, 4))

-- include delta

check(mtn("status", "work/file3"), 0, true, false)
check(included(3))
check(excluded("X", 1, 2, 4))

-- include add

check(mtn("status", "work/file4"), 0, true, false)
check(included(4))
check(excluded("X", 1, 2, 3))

-- diff

check(mtn("diff"), 0, true, false)
check(included("X", 1, 2, 3, 4))

check(mtn("diff", "--depth=1", "."), 0, true, false)
check(not qgrep("fileAB", "stdout"))

check(mtn("diff", "--depth=3", "."), 0, true, false)
check(qgrep("fileA", "stdout"))

check(mtn("diff", "--context", "--depth=1", "."), 0, true, false)
check(not qgrep("fileAB", "stdout"))

check(mtn("diff", "--context", "--depth=3", "."), 0, true, false)
check(qgrep("fileA", "stdout"))

-- include both source and target of rename

check(mtn("diff", "work/fileX", "work/file1"), 0, true, false)
check(included("X", 1))
check(excluded(2, 3, 4))

-- include drop

check(mtn("diff", "work/file2"), 0, true, false)
check(included(2))
check(excluded("X", 1, 3, 4))

-- include delta

check(mtn("diff", "work/file3"), 0, true, false)
check(included(3))
check(excluded("X", 1, 2, 4))

-- include add

check(mtn("diff", "work/file4"), 0, true, false)
check(included(4))
check(excluded("X", 1, 2, 3))

-- commit

check(mtn("status"), 0, true, false)
check(included("X", 1, 2, 3, 4))

-- include rename source and target

check(mtn("commit", "--message=move fileX to file1",
          "work/fileX", "work/file1"), 0, false, false)

check(mtn("status"), 0, true, false)
check(included(2, 3, 4))
check(excluded("X", 1))

-- include drop

check(mtn("commit", "--message=drop file2", "work/file2"), 0, false, false)

check(mtn("status"), 0, true, false)
check(included(3, 4))
check(excluded("X", 1, 2))

-- include delta

check(mtn("commit", "--message=change file3", "work/file3"), 0, false, false)

check(mtn("status"), 0, true, false)
check(included(4))
check(excluded("X", 1, 2, 3))

-- include add

check(mtn("commit", "--message=add file4", "work/file4"), 0, false, false)

check(mtn("status"), 0, true, false)
check(excluded("X", 1, 2, 3, 4))

-- setup for excluded commits

-- moved file1 to fileY
-- dropped file2
-- changed file3
-- added file4

-- moved file3 to file
-- dropped file1
-- changed file4
-- added file5

-- exclude rename source 
-- exclude rename target 
-- exclude drop
-- exclude delta
-- exclude add

-- test bad removal of restricted files 
-- (set/iterator/erase bug found by matt@ucc.asn.au)

nums = {[1] = "one", [2] = "two", [3] = "three",
        [4] = "four", [5] = "five", [6] = "six",
        [7] = "seven", [8] = "eight", [9] = "nine",
        [10] = "ten", [11] = "eleven", [12] = "twelve"}
for i = 1,12 do
  addfile("file."..nums[i], "file "..nums[i])
end

commit()

for i = 1,11 do
  if i ~= 2 then
    writefile("file."..nums[i], "new file "..nums[i])
  end
end

check(mtn("diff", "file.four", "file.ten"), 0, true, false)

check(qgrep("file.four", "stdout"))
check(qgrep("file.ten", "stdout"))

-- none of these should show up in the diff
-- only four and ten are included

for i = 1,12
do
  if i ~= 4 and i ~= 10 then
    check(not qgrep("file.$i", "stdout"))
  end
end
