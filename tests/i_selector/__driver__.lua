
include("common/selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit()
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit()
REV2=base_revision()

-- empty id selector
selmap("i:", {REV1, REV2})
-- standard selection
selmap("i:" .. REV1, {REV1})
selmap("i:" .. REV2, {REV2})


-- assume the generated revisions id's differ in their first character.
assert(not (string.sub(REV1, 1, 1) == string.sub(REV2, 1, 1)))

-- expanding from the first four chars
selmap("i:" .. string.sub(REV1, 1, 4), {REV1})
selmap("i:" .. string.sub(REV2, 1, 4), {REV2})

-- expanding from the first two chars
selmap("i:" .. string.sub(REV1, 1, 2), {REV1})
selmap("i:" .. string.sub(REV2, 1, 2), {REV2})

-- expanding from the first char only
selmap_xfail("i:" .. string.sub(REV1, 1, 1), {REV1})
selmap_xfail("i:" .. string.sub(REV2, 1, 1), {REV2})

