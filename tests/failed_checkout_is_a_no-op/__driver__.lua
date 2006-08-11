
mtn_setup()

-- If a checkout fails, no target directory should be created, and if
-- the checkout directory already exists, no _MTN/ directory should be
-- created.

check(mtn("checkout", "--revision=bogus-id", "outdir"), 1, false, false)
check(not exists("outdir"))
mkdir("outdir")
check(indir("outdir", mtn("checkout", "--revision=bogus-id", ".")), 1, false, false)
check(not exists("outdir/_MTN"))
