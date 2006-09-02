
mtn_setup()

check(mtn("automate", "parents", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)
check(mtn("automate", "children", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

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
revmap("parents", {revs.a}, {})
revmap("children", {revs.b}, {})
revmap("children", {revs.f}, {})
revmap("parents", {revs.b}, {revs.a})
revmap("parents", {revs.c}, {revs.a})
revmap("parents", {revs.d}, {revs.c})
revmap("parents", {revs.e}, {revs.c})
revmap("parents", {revs.f}, {revs.d, revs.e})
revmap("children", {revs.d}, {revs.f})
revmap("children", {revs.e}, {revs.f})
revmap("children", {revs.c}, {revs.d, revs.e})
revmap("children", {revs.a}, {revs.b, revs.c})
