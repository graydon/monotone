
mtn_setup()

-- this test demonstrates a tricky case in which two parties, bob and
-- alice, merge a fork, left and right, differently. bob chooses the
-- changes in the left node, alice chooses the changes in the right
-- node.
--
-- the result of merging their merges incorrectly succeeds, considering
-- the LCA(bob,alice) as either foo or bar, and thereby seeing one of
-- the edges (left->bob or right->alice) as having "no changes", and
-- letting the edge "with changes" (right->bob, or left->alice) clobber
-- it.
--
-- this should be fixed so the merge-of-merges conflicts.

writefile("shared.anc", "base version data")
writefile("shared.left", "conflicting data on left side")
writefile("shared.right", "conflicting data on right side")
writefile("specific.left", "non-conflicting mergeable data on left side")
writefile("specific.right", "non-conflicting mergeable data on right side")
writefile("specific.alice", "non-conflicting mergeable data in bob")
writefile("specific.bob", "non-conflicting mergeable data in alice")

-- this case is somewhat tricky to set up too... we use two different
-- keys (bob and alice) that don't trust each other so that they can
-- produce two different merge results

check(get("bob.lua"))
check(get("alice.lua"))

function bob (...)
  return raw_mtn("--rcfile=test_hooks.lua", "--rcfile=bob.lua",
                 "--nostd", "--norc", "--db=test.db", "--key=bob",
                 "--keydir=keys", unpack(arg))
end
function alice (...)
  return raw_mtn("--rcfile=test_hooks.lua", "--rcfile=alice.lua",
                 "--nostd", "--norc", "--db=test.db", "--key=alice",
                 "--keydir=keys", unpack(arg))
end

check(bob("genkey", "bob"), 0, false, false, "bob\nbob\n")
check(alice("genkey", "alice"), 0, false, false, "alice\nalice\n")


-- construct ancestor
copy("shared.anc", "shared")
addfile("shared")
commit()
root_r_sha = base_revision()
root_f_sha = sha1("shared")

-- construct left node
copy("shared.left", "shared")
addfile("specific.left")
commit()
left_r_sha = base_revision()
left_f_sha = sha1("shared")
check(left_r_sha ~= root_r_sha)
check(left_f_sha ~= root_f_sha)

-- revert to root
revert_to(root_r_sha)

-- construct right node
copy("shared.right", "shared")
addfile("specific.right")
commit()
right_r_sha = base_revision()
right_f_sha = sha1("shared")
check(right_r_sha ~= root_r_sha)
check(right_f_sha ~= root_f_sha)
check(right_r_sha ~= left_r_sha)
check(right_f_sha ~= left_f_sha)

-- construct alice, a merge choosing the right side to win
check(alice("merge"), 0, false, false)

-- construct bob, a merge choosing the left side to win
check(bob("merge"), 0, false, false)

-- now merge the merges. this *should* fail.
-- because there are conflicting changes and
-- we have no merge3 hook to fall back on

check(mtn("merge"), 1, false, false)
