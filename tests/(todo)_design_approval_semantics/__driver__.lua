
-- This test is a bug report.
xfail_if(true, false)

-- There's a somewhat subtle issue about approval, branch membership,
-- etc.  The way I (njs) had been thinking about things originally, a
-- revision R is in a branch B iff there's a valid trusted cert cert(R,
-- "branch", B).  So, currently e.g. 'propagate' will skip performing a
-- merge in some cases and instead just stick a new branch cert on the
-- head that's being propagated, and 'update' will skip past non-branch
-- nodes to reach branch nodes.
--
-- graydon points out, though, that 'update's original semantics, of
-- _not_ skipping past non-branch revisions, was intentional.  because
-- branch certs show approval, and in real life people always look at
-- and approve diffs, not tree-states.  so update should only follow
-- continuous paths of approval.
--
-- currently, 'update' still will skip past non-branch revisions, since
-- other parts of the code assume that this is how branches work, and I
-- (njs again) figured it would be better to let things be consistent
-- while we figure out what should really happen.  but, that approval
-- inherently happens on edges is a critical point, and we should
-- address it somehow.
--
-- there are some subtleties to it, though.  some things came up on IRC:

-- --monotone, Jan 23:
-- <njs>     graydon: here's a question about branch-as-approval
-- <njs>     graydon: should 'heads' be changed to be 'greatest descendent of a privileged root'?
-- <njs>     (s/descendent/descendent with a continuous chain of branch certs from that root/)
-- <graydon> possibly.
-- <graydon> possibly once we know what "priviledged" means :)
-- <graydon> or privileged, depending on whether I learn to spell
-- <njs>     one could do it with the lookaside trust branch model, have a table for each branch specifying which revision is considered the root

-- <njs> if we have A -> B -> C, B -> D -> E, where everything except D has appropriate approval, should "monotone merge" cherrypick D -> E onto C? :-)
-- <njs> (on the theory that merge is supposed to gather up all approved revisions into one head)
-- <njs> hmm, and if someone does say "disapprove D", they have to also say "approve D" for it to work!
-- <graydon> heh
-- <graydon> both interesting issues
-- <graydon> don't know. I'm not sure about either.


-- TODO: figure out how all this should work.
-- solution should support projects with different sorts of
-- workflows/approval requirements...
