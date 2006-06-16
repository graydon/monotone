
mtn_setup()

check(mtn("automate", "packet_for_fdelta", "73070030f7b0d0f3d4ee02545d45ca4bbe5e189f", "6c704fbd4ef58f2447fd1a3e76911b2ebe97dc77"), 1, false, false)
check(mtn("automate", "packet_for_fdata", "73070030f7b0d0f3d4ee02545d45ca4bbe5e189f"), 1, false, false)
check(mtn("automate", "packet_for_rdata", "73070030f7b0d0f3d4ee02545d45ca4bbe5e189f"), 1, false, false)
check(mtn("pubkey", "foo@bar"), 1, false, false)
check(mtn("privkey", "foo@bar"), 1, false, false)

check(mtn("automate", "packets_for_certs", "73070030f7b0d0f3d4ee02545d45ca4bbe5e189f"), 1, false, false)
check(mtn("db", "check"), 0, false, false)
