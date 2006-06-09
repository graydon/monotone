
mtn_setup()

-- This was a real merge error.  A binary file happily merged by monotone
-- just because contains some strategically placed line feeds
-- now is a test for the new attribute merge_manual and its effect on merging

getfile("parent.bmp")
getfile("left.bmp")
getfile("right.bmp")

-- hook forces all files binary
getfile("binary.lua")

-- hook forces all files text
getfile("text.lua")

-- --- first: auto add as binary 
copyfile("parent.bmp", "binary.bmp")
check(mtn("--rcfile=binary.lua", "add", "binary.bmp"), 0, false, false)
commit("binbranch")
parent = base_revision()

check(mtn("attr", "get", "binary.bmp", "mtn:manual_merge"), 0, false, false)

copyfile("left.bmp", "binary.bmp")
commit("binbranch")

revert_to(parent)

copyfile("right.bmp", "binary.bmp")
commit("binbranch")

-- file marked binary: merge should fail
check(mtn("--branch=binbranch", "merge"), 1, false, false)

-- --- second: auto add as text
copyfile("parent.bmp", "text.bmp")
check(mtn("--rcfile=text.lua", "add", "text.bmp"), 0, false, false)
commit("textbranch")
parent = base_revision()

copyfile("left.bmp", "text.bmp")
commit("textbranch")

revert_to(parent)

copyfile("right.bmp", "text.bmp")
commit("textbranch")

-- file marked text: merge should work!
check(mtn("--branch=textbranch", "merge"), 0, false, false)

-- --- third: manually make filename as binary
copyfile("parent.bmp", "forcebin.bmp")
check(mtn("--rcfile=text.lua", "add", "forcebin.bmp"), 0, false, false)
commit("forcebinbranch")
parent = base_revision()

copyfile("left.bmp", "forcebin.bmp")
commit("forcebinbranch")

revert_to(parent)

copyfile("right.bmp", "forcebin.bmp")

-- set bin
check(mtn("attr", "set", "forcebin.bmp", "mtn:manual_merge", "true"), 0, false, false)
commit("forcebinbranch")

-- file marked binary: merge should fail
check(mtn("--branch=forcebinbranch", "merge"), 1, false, false)

-- --- fourth: automatically make filename as binary, then force text
copyfile("parent.bmp", "forcetext.bmp")
check(mtn("--rcfile=binary.lua", "add", "forcetext.bmp"), 0, false, false)
check(mtn("attr", "set", "forcetext.bmp", "mtn:manual_merge", "false"), 0, false, false)
commit("forcetextbranch")
parent = base_revision()

copyfile("left.bmp", "forcetext.bmp")
commit("forcetextbranch")

revert_to(parent)

copyfile("right.bmp", "forcetext.bmp")
commit("forcetextbranch")

-- file marked text: merge should work
check(mtn("--branch=forcetextbranch", "merge"), 0, false, false)
