
mtn_setup()

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
check(mtn("automate", "toposort", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)
check(mtn("automate", "toposort", ""), 1, false, false)

revmap("toposort", {}, {}, false)
for _,x in pairs(revs) do
  revmap("toposort", {x}, {x}, false)
end
revmap("toposort", {revs.a, revs.a}, {revs.a}, false)
revmap("toposort", {revs.a, revs.b}, {revs.a, revs.b}, false)
revmap("toposort", {revs.b, revs.a}, {revs.a, revs.b}, false)
revmap("toposort", {revs.a, revs.f}, {revs.a, revs.f}, false)
revmap("toposort", {revs.f, revs.a}, {revs.a, revs.f}, false)
acef = {revs.a, revs.c, revs.e, revs.f}
revmap("toposort", {revs.a, revs.c, revs.e, revs.f}, acef, false)
revmap("toposort", {revs.c, revs.a, revs.e, revs.f}, acef, false)
revmap("toposort", {revs.f, revs.c, revs.a, revs.e}, acef, false)
revmap("toposort", {revs.f, revs.e, revs.c, revs.a}, acef, false)
revmap("toposort", {}, {}, false)
cdef = {revs.c, revs.d, revs.e, revs.f}
cedf = {revs.c, revs.e, revs.d, revs.f}
x = pcall(revmap, "toposort", cdef, cdef, false)
y = pcall(revmap, "toposort", cdef, cedf, false)
check(x or y)
