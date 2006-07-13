-- A situation where the multiple-head merge heuristic avoids a conflict:
-- This graph describes a single file, whose contents at a given revision 
-- are the two letters shown for that node, each on its own line.
--
--         oo
--        /  \
--       xx  ce
--      /  \
--     cx  xe
--
-- If "cx" and "xe" are merged first, the result can trivially be merged
-- with "ce".  Merging either "cx" or "xe" with "ce" will conflict.


mtn_setup()

function C(str)
  return (string.gsub(str, "(.)", "%1\n"))
end

addfile("file", C("oo"))
commit("branch")
grandparent = base_revision()

-- Check in ce first so that the old dumb "in whatever order
-- get_branch_heads returns" algorithm will do it wrong.

writefile_q("file", C("ce"))
commit()
ce_rev = base_revision()

revert_to(grandparent)
writefile_q("file", C("xx"))
commit()
parent = base_revision()

writefile_q("file", C("cx"))
commit()
cx_rev = base_revision()

revert_to(parent)
writefile_q("file", C("xe"))
commit()
xe_rev = base_revision()

-- Double-check that the old dumb "in lexicographic order by revision_id"
-- algorithm would get this wrong.
check(ce_rev < cx_rev or ce_rev < xe_rev)

check(mtn("merge"), 0, false, false)
