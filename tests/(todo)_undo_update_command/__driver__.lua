
-- This test is a bug report.
xfail_if(true, false)

-- "update" is the only command that modifies the workspace, i.e.,
-- it is the only command that may destroy data that cannot be easily
-- recovered from the database.  So it should be undo-able.
--
-- This wouldn't be that hard to do -- before starting an update, make
-- a note of all file modifications being applied, and save copies of
-- them somewhere under _MTN/.  The only tricky part is making sure we
-- can undo tree rearrangements.
--
-- For bonus points, use this to implement "workspace rollback" --
-- right now, we can't modify the workspace atomically.  But if we
-- always saved this information before touching any files, then we
-- could also save a marker file when we start munging the filesystem,
-- that we delete when finished.  When monotone starts up, it can check
-- for this marker file, and either rollback automatically or demand
-- the user do so or whatever.
--
-- Making this work requires some careful thought, of course -- one has
-- to make sure that rollback is idempotent, it'd be nice if rollback
-- information didn't immediately overwrite undo information (so an
-- interrupted update didn't kill undo information after a rollback),
-- etc.
--
-- It'd also be nice if there was a "redo_update" to un-undo an update,
-- I suppose...

-- Are there any other operations that mutate the workspace?  They
-- should all be reversible somehow...
