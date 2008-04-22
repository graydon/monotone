
include("common/selectors.lua")
mtn_setup()

addfile("testfile", "blah blah")
commit()
REV1=base_revision()

writefile("testfile", "stuff stuff")
commit()
REV2=base_revision()

-- empty parent selector
selmap("p:", {REV1})
-- standard selection
selmap("p:" .. REV2, {REV1})
-- parent of a root revision
selmap("p:" .. REV1, {})

