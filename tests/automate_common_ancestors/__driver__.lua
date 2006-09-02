
include("common/automate_ancestry.lua")
mtn_setup()

--   A
--  / \
-- B   C
--     |\
--     D E
--     \/
--      F
revs = make_graph()

-- Now do some checks
check(mtn("automate", "common_ancestors",
          "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)
check(mtn("automate", "common_ancestors", revs.a,
          "c7539264e83c5d6af4c792f079b5d46e9c128665"), 1, false, false)

revmap("common_ancestors", {revs.a}, {revs.a})
revmap("common_ancestors", {revs.a, revs.a}, {revs.a})
revmap("common_ancestors", {revs.a, revs.b}, {revs.a})
revmap("common_ancestors", {revs.a, revs.f}, {revs.a})
revmap("common_ancestors", {revs.f, revs.f}, {revs.a, revs.c, revs.d, revs.e, revs.f})
revmap("common_ancestors", {revs.b, revs.f}, {revs.a})
revmap("common_ancestors", {revs.f, revs.d, revs.e}, {revs.a, revs.c})
revmap("common_ancestors", {revs.b, revs.e}, {revs.a})
