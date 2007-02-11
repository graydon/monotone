mtn_setup()

addfile("bilbo", "this is bilbo's secret diary\n")
commit()
anc = base_revision()

addfile("charlie", "this is charlie's file\n")
commit()
other = base_revision()

-- try merging the current workspace head into the workspace
check(mtn("merge_into_workspace", other), 1, nil, false)
