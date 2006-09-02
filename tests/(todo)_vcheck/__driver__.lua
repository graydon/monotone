
mtn_setup()

-- This test is a bug report (feature request).
xfail_if(true, false)

-- There used to be a command 'vcheck', that defended against the
-- chance of (accidental or malicious) SHA1 collision.  The way it did
-- this was by putting certs named "vcheck" on manifests, and these
-- certs contained a re-hash of that manifest with a nonce attached.
-- (So a MAC, basically.)  The idea being that even if two manifests
-- had the same SHA1, they wouldn't have the same salted SHA1.

-- This functionality is mostly useful for reassuring people's
-- irrational fears, but hey, that's a useful thing to do.  (This does,
-- though, probably mean that any replacement should have 0 overhead
-- for people who _aren't_ worried about SHA1 collision.)

-- The original 'vcheck' was ripped out when manifest and file certs
-- were removed, and never quite did the right thing anyway.  (It only
-- applied to manifests, in particular.)  It may be useful to reference
-- the code, though: see t:monotone-0.16.  In particular, mac.hh should
-- be useful.  Note also the section "Accidental collision" in
-- monotone.texi.

-- There are a few ways to re-add this.  The simplest is probably to
-- have a cert on revisions that contains
--   - a salt/nonce
--   - a MAC of the revision
--   - a MAC of the revision's manifest
--   - a MAC of each file within that revision.
-- possibly the last should just be "a MAC of every file mentioned in
-- that revision's list of diff's", so people creating multiple vcheck
-- certs aren't checking the same unchanging files over and over again.
-- This reduces space overhead, too, since certs's space usage adds up,
-- and does so for project members who aren't worried about SHA1
-- collisions too...
--
-- an alternative approach would be to contain:
--   - a salt/nonce
--   - a MAC of (length-prefixed revision) + (length prefixed manifest)
--     + (length prefixed versions of each file in the manifest, in manifest order)
-- this is small, and just as safe.  it is rather expensive to create
-- or check, though, since you have to load all that data, so maybe the
-- optimization above where you only hash mentioned files would be
-- good.  OTOH, if you hash everything, then you can use them
-- sparingly, and be sure that the versions so certed really are safe;
-- if you only hash some pieces, you have to cert your entire history
-- in order to "trust" any one snapshot at all.
