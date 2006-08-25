
mtn_setup()

-- This was a real merge error.  A binary file happily merged by monotone
-- just because contains some strategically placed line feeds
-- now is a test for the new attribute merge_manual and its effect on merging

check(get("parent.bmp"))
check(get("left.bmp"))
check(get("right.bmp"))

-- hook forces all files binary
check(get("binary.lua"))

-- hook forces all files text
check(get("text.lua"))

-- --- first: auto add as binary 
copy("parent.bmp", "binary.bmp")
check(mtn("--rcfile=binary.lua", "add", "binary.bmp"), 0, false, false)
commit("binbranch")
parent = base_revision()

check(mtn("attr", "get", "binary.bmp", "mtn:manual_merge"), 0, false, false)

copy("left.bmp", "binary.bmp")
commit("binbranch")

revert_to(parent)

copy("right.bmp", "binary.bmp")
commit("binbranch")

-- file marked binary: merge should fail
check(mtn("--branch=binbranch", "merge"), 1, false, false)

-- --- second: auto add as text
copy("parent.bmp", "text.bmp")
check(mtn("--rcfile=text.lua", "add", "text.bmp"), 0, false, false)
commit("textbranch")
parent = base_revision()

copy("left.bmp", "text.bmp")
commit("textbranch")

revert_to(parent)

copy("right.bmp", "text.bmp")
commit("textbranch")

-- file marked text: merge should work!
check(mtn("--branch=textbranch", "merge"), 0, false, false)

-- --- third: manually make filename as binary
copy("parent.bmp", "forcebin.bmp")
check(mtn("--rcfile=text.lua", "add", "forcebin.bmp"), 0, false, false)
commit("forcebinbranch")
parent = base_revision()

copy("left.bmp", "forcebin.bmp")
commit("forcebinbranch")

revert_to(parent)

copy("right.bmp", "forcebin.bmp")

-- set bin
check(mtn("attr", "set", "forcebin.bmp", "mtn:manual_merge", "true"), 0, false, false)
commit("forcebinbranch")

-- file marked binary: merge should fail
check(mtn("--branch=forcebinbranch", "merge"), 1, false, false)

-- --- fourth: automatically make filename as binary, then force text
copy("parent.bmp", "forcetext.bmp")
check(mtn("--rcfile=binary.lua", "add", "forcetext.bmp"), 0, false, false)
check(mtn("attr", "set", "forcetext.bmp", "mtn:manual_merge", "false"), 0, false, false)
commit("forcetextbranch")
parent = base_revision()

copy("left.bmp", "forcetext.bmp")
commit("forcetextbranch")

revert_to(parent)

copy("right.bmp", "forcetext.bmp")
commit("forcetextbranch")

-- file marked text: merge should work
check(mtn("--branch=forcetextbranch", "merge"), 0, false, false)
