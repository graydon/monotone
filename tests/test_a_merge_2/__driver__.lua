
mtn_setup()

-- Another real merge error. This is the diff between the correct
-- result and the file produced by the merge:
--
-- --- correct
-- +++ testfile
-- @@ -336,10 +336,10 @@
--          {
--            L(F("found node %d, ancestor of left %s and right %s\n")
--              % anc % left % right);
-- -          return true;
--          }
--      }
--  //      dump_bitset_map("ancestors", ancestors);
-- +//      dump_bitset_map("dominators", dominators);
--  //      dump_bitset_map("parents", parents);
--    return false;
--  }

check(get("parent"))
check(get("left"))
check(get("right"))
check(get("correct"))

copy("parent", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
parent = base_revision()

copy("left", "testfile")
commit()

revert_to(parent)

copy("right", "testfile")
commit()

check(mtn("merge"), 0, false, false)

check(mtn("update"), 0, false, false)
check(samefile("testfile", "correct"))
