
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
check(mtn("automate", "ancestry_difference", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)
check(mtn("automate", "ancestry_difference", revs.a, "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

revmap("ancestry_difference", {revs.a}, {revs.a})
revmap("ancestry_difference", {revs.a, revs.a}, {})
revmap("ancestry_difference", {revs.a, revs.b}, {})
revmap("ancestry_difference", {revs.a, revs.f}, {})
revmap("ancestry_difference", {revs.f, revs.f}, {})
revmap("ancestry_difference", {revs.b, revs.a}, {revs.b})
revmap("ancestry_difference", {revs.b}, {revs.a, revs.b}, false)
revmap("ancestry_difference", {revs.f, revs.d, revs.e}, {revs.f}, false)
revmap("ancestry_difference", {revs.f, revs.e}, {revs.d, revs.f}, false)
revmap("ancestry_difference", {revs.b, revs.f}, {revs.b})
acdef = {revs.a, revs.c, revs.d, revs.e, revs.f}
acedf = {revs.a, revs.c, revs.e, revs.d, revs.f}
x = pcall(revmap, "ancestry_difference", {revs.f}, acdef, false)
y = pcall(revmap, "ancestry_difference", {revs.f}, acedf, false)
check(x or y)
