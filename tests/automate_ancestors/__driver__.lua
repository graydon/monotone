
mtn_setup()

check(mtn("automate", "ancestors", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

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
revmap("ancestors", {revs.a}, {})
revmap("ancestors", {revs.b}, {revs.a})
revmap("ancestors", {revs.e}, {revs.a, revs.c})
revmap("ancestors", {revs.f}, {revs.d, revs.a, revs.c, revs.e})
revmap("ancestors", {revs.d, revs.f}, {revs.d, revs.a, revs.c, revs.e})
revmap("ancestors", {revs.a, revs.b, revs.c}, {revs.a})
