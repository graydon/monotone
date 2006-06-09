
mtn_setup()

check(mtn("automate", "graph"), 0, 0, false)

--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
include("/common/automate_ancestry.lua")

revs = make_graph()

-- Now do some checks

check(mtn("automate", "graph"), 0, true, false)
canonicalize("stdout")
de = {revs.d, revs.e}
table.sort(de)
lines = {revs.a,
         revs.b.." "..revs.a,
         revs.c.." "..revs.a,
         revs.d.." "..revs.c,
         revs.e.." "..revs.c,
         revs.f.." "..table.concat(de, " ")}
table.sort(lines)
writefile("graph", table.concat(lines, "\n").."\n")

check(samefile("stdout", "graph"))
