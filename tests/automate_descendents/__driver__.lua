
mtn_setup()

check(mtn("automate", "descendents", "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

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
revmap("descendents", {revs.b}, {})
revmap("descendents", {revs.f}, {})
revmap("descendents", {revs.d}, {revs.f})
revmap("descendents", {revs.e}, {revs.f})
revmap("descendents", {revs.c}, {revs.d, revs.e, revs.f})
revmap("descendents", {revs.a}, {revs.d, revs.e, revs.f, revs.c, revs.b})
revmap("descendents", {revs.d, revs.f}, {revs.f})
revmap("descendents", {revs.d, revs.e, revs.f}, {revs.f})
revmap("descendents", {revs.b, revs.d, revs.e, revs.f}, {revs.f})
