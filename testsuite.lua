#!./tester

ostype = string.sub(get_ostype(), 1, string.find(get_ostype(), " ")-1)

-- maybe this should go in tester.lua instead?
function getpathof(exe, ext)
  local function gotit(now)
    if test.log == nil then
      logfile:write(exe, " found at ", now, "\n")
    else
      test.log:write(exe, " found at ", now, "\n")
    end
    return now
  end
  local path = os.getenv("PATH")
  local char
  if ostype == "Windows" then
    char = ';'
  else
    char = ':'
  end
  if ostype == "Windows" then
    if ext == nil then ext = ".exe" end
  else
    if ext == nil then ext = "" end
  end
  local now = initial_dir.."/"..exe..ext
  if exists(now) then return gotit(now) end
  for x in string.gmatch(path, "[^"..char.."]*"..char) do
    local dir = string.sub(x, 0, -2)
    if string.find(dir, "[\\/]$") then
      dir = string.sub(dir, 0, -2)
    end
    local now = dir.."/"..exe..ext
    if exists(now) then return gotit(now) end
  end
  if test.log == nil then
    logfile:write("Cannot find ", exe, "\n")
  else
    test.log:write("Cannot find ", exe, "\n")
  end
  return nil
end

monotone_path = getpathof("mtn")
if monotone_path == nil then monotone_path = "mtn" end
set_env("mtn", monotone_path)

writefile_q("in", nil)
prepare_redirect("in", "out", "err")
execute(monotone_path, "--full-version")
logfile:write(readfile_q("out"))
unlogged_remove("in")
unlogged_remove("out")
unlogged_remove("err")

-- NLS nuisances.
for _,name in pairs({  "LANG",
                       "LANGUAGE",
                       "LC_ADDRESS",
                       "LC_ALL",
                       "LC_COLLATE",
                       "LC_CTYPE",
                       "LC_IDENTIFICATION",
                       "LC_MEASUREMENT",
                       "LC_MESSAGES",
                       "LC_MONETARY",
                       "LC_NAME",
                       "LC_NUMERIC",
                       "LC_PAPER",
                       "LC_TELEPHONE",
                       "LC_TIME"  }) do
   set_env(name,"C")
end
       

function safe_mtn(...)
  return {monotone_path, "--norc", "--root=" .. test.root,
          "--confdir="..test.root, unpack(arg)}
end

-- function preexecute(x)
--   return {"valgrind", "--tool=memcheck", unpack(x)}
-- end

function raw_mtn(...)
  if preexecute ~= nil then
    return preexecute(safe_mtn(unpack(arg)))
  else
    return safe_mtn(unpack(arg))
  end
end

function mtn(...)
  return raw_mtn("--rcfile", test.root .. "/test_hooks.lua",
         "--nostd", "--db=" .. test.root .. "/test.db",
         "--keydir", test.root .. "/keys",
         "--key=tester@test.net", unpack(arg))
end

function minhooks_mtn(...)
  return raw_mtn("--db=" .. test.root .. "/test.db",
                 "--keydir", test.root .. "/keys",
                 "--rcfile", test.root .. "/min_hooks.lua",
                 "--key=tester@test.net", unpack(arg))
end

function commit(branch, message, mt)
  if branch == nil then branch = "testbranch" end
  if message == nil then message = "blah-blah" end
  if mt == nil then mt = mtn end
  check(mt("commit", "--message", message, "--branch", branch), 0, false, false)
end

function sha1(what)
  check(safe_mtn("identify", what), 0, false, false)
  return trim(readfile("ts-stdout"))
end

function probe_node(filename, rsha, fsha)
  remove("_MTN.old")
  rename("_MTN", "_MTN.old")
  remove(filename)
  check(mtn("checkout", "--revision", rsha, "."), 0, false)
  rename("_MTN.old/options", "_MTN")
  check(base_revision() == rsha)
  check(sha1(filename) == fsha)
end

function mtn_setup()
  check(getstd("test_keys"))
  check(getstd("test_hooks.lua"))
  check(getstd("min_hooks.lua"))
  
  check(mtn("db", "init"), 0, false, false)
  check(mtn("read", "test_keys"), 0, false, false)
  check(mtn("setup", "--branch=testbranch", "."), 0, false, false)
  remove("test_keys")
end

function base_revision()
  local workrev = readfile("_MTN/revision")
  local extract = string.gsub(workrev, "^.*old_revision %[(%x*)%].*$", "%1")
  if extract == workrev then
    err("failed to extract base revision from _MTN/revision")
  end
  return extract
end

function base_manifest()
  check(safe_mtn("automate", "get_manifest_of", base_revision()), 0, false)
  check(copy("ts-stdout", "base_manifest_temp"))
  return sha1("base_manifest_temp")
end

function certvalue(rev, name)
  check(safe_mtn("automate", "certs", rev), 0, false)
  local parsed = parse_basic_io(readfile("ts-stdout"))
  local cname
  for _,l in pairs(parsed) do
    if l.name == "name" then cname = l.values[1] end
    if cname == name and l.name == "value" then return l.values[1] end
  end
  return nil
end

function qgrep(what, where)
  local ok,res = pcall(unpack(grep("-q", what, where)))
  if not ok then err(res) end
  return res == 0
end

function addfile(filename, contents, mt)
  if contents ~= nil then writefile(filename, contents) end
  if mt == nil then mt = mtn end
  check(mt("add", filename), 0, false, false)
end

function revert_to(rev, branch, mt)
  if type(branch) == "function" then
    mt = branch
    branch = nil
  end
  if mt == nil then mt = mtn end
  remove("_MTN.old")
  rename("_MTN", "_MTN.old")
  
  if branch == nil then
    check(mt("checkout", "--revision", rev, "."), 0, false)
  else
    check(mt("checkout", "--branch", branch, "--revision", rev, "."), 0, false)
  end
  check(base_revision() == rev)
end

function canonicalize(filename)
  if ostype == "Windows" then
    L("Canonicalizing ", filename, "\n")
    local f = io.open(filename, "rb")
    local indat = f:read("*a")
    f:close()
    local outdat = string.gsub(indat, "\r\n", "\n")
    f = io.open(filename, "wb")
    f:write(outdat)
    f:close()
  else
    L("Canonicalization not needed (", filename, ")\n")
  end
end

function check_same_db_contents(db1, db2)
  check_same_stdout(mtn("--db", db1, "ls", "keys"),
                    mtn("--db", db2, "ls", "keys"))
  
  check(mtn("--db", db1, "complete", "revision", ""), 0, true, false)
  rename("stdout", "revs")
  check(mtn("--db", db2, "complete", "revision", ""), 0, true, false)
  check(samefile("stdout", "revs"))
  for rev in io.lines("revs") do
    rev = trim(rev)
    check_same_stdout(mtn("--db", db1, "automate", "certs", rev),
                      mtn("--db", db2, "automate", "certs", rev))
    check_same_stdout(mtn("--db", db1, "automate", "get_revision", rev),
                      mtn("--db", db2, "automate", "get_revision", rev))
    check_same_stdout(mtn("--db", db1, "automate", "get_manifest_of", rev),
                      mtn("--db", db2, "automate", "get_manifest_of", rev))
  end
  
  check(mtn("--db", db1, "complete", "file", ""), 0, true, false)
  rename("stdout", "files")
  check(mtn("--db", db2, "complete", "file", ""), 0, true, false)
  check(samefile("stdout", "files"))
  for file in io.lines("files") do
    file = trim(file)
    check_same_stdout(mtn("--db", db1, "automate", "get_file", file),
                      mtn("--db", db2, "automate", "get_file", file))
  end
end

-- maybe these should go in tester.lua?
function do_check_same_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  rename("stdout", "stdout-second")
  check(samefile("stdout-first", "stdout-second"))
end
function do_check_different_stdout(cmd1, cmd2)
  check(cmd1, 0, true, false)
  rename("stdout", "stdout-first")
  check(cmd2, 0, true, false)
  rename("stdout", "stdout-second")
  check(not samefile("stdout-first", "stdout-second"))
end
function check_same_stdout(a, b, c)
  if type(a) == "table" and type(b) == "table" then
    return do_check_same_stdout(a, b)
  elseif type(a) == "table" and type(b) == "function" and type(c) == "function" then
    return do_check_same_stdout(b(unpack(a)), c(unpack(a)))
  elseif type(a) == "table" and type(b) == "nil" and type(c) == "nil" then
    return do_check_same_stdout(mtn(unpack(a)), mtn2(unpack(a)))
  else
    err("bad arguments ("..type(a)..", "..type(b)..", "..type(c)..") to check_same_stdout")
  end
end
function check_different_stdout(a, b, c)
  if type(a) == "table" and type(b) == "table" then
    return do_check_different_stdout(a, b)
  elseif type(a) == "table" and type(b) == "function" and type(c) == "function" then
    return do_check_different_stdout(b(unpack(a)), c(unpack(a)))
  elseif type(a) == "table" and type(b) == "nil" and type(c) == "nil" then
    return do_check_different_stdout(mtn(unpack(a)), mtn2(unpack(a)))
  else
    err("bad arguments ("..type(a)..", "..type(b)..", "..type(c)..") to check_different_stdout")
  end
end

function write_large_file(name, size)
  local file = io.open(name, "wb")
  for i = 1,size do
    for j = 1,128 do -- write 1MB
      local str8k = ""
      for k = 1,256 do
        -- 32
        str8k = str8k .. string.char(math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255),
                                     math.random(255), math.random(255))
      end
      file:write(str8k)
    end
  end
  file:close()
end

------------------------------------------------------------------------
--====================================================================--
------------------------------------------------------------------------
testdir = srcdir.."/tests"

table.insert(tests, "basic_invocation_and_options")
table.insert(tests, "scanning_trees")
table.insert(tests, "importing_a_file")
table.insert(tests, "generating_and_extracting_keys_and_certs")
table.insert(tests, "calculation_of_unidiffs")
table.insert(tests, "persistence_of_passphrase")
table.insert(tests, "multiple_version_committing")
table.insert(tests, "creating_a_fork")
table.insert(tests, "creating_a_fork_and_updating")
table.insert(tests, "creating_a_fork_and_merging")
table.insert(tests, "merging_adds")
table.insert(tests, "merging_data_in_unrelated_files")
table.insert(tests, "merging_adds_in_unrelated_revisions")
table.insert(tests, "merging_data_in_unrelated_revisions")
table.insert(tests, "calculation_of_other_unidiffs")
table.insert(tests, "delete_work_file_on_checkout")
table.insert(tests, "revert_file_to_base_revision")
table.insert(tests, "addition_of_files_and_directories")
table.insert(tests, "add_and_then_drop_file_does_nothing")
table.insert(tests, "drop_missing_and_unknown_files")
table.insert(tests, "creating_a_bad_criss-cross_merge")
table.insert(tests, "renaming_a_file")
table.insert(tests, "renaming_a_directory")
table.insert(tests, "renaming_and_editing_a_file")
table.insert(tests, "importing_cvs_files")
table.insert(tests, "importing_files_with_non-english_names")
table.insert(tests, "external_unit_test_of_the_line_merger")
table.insert(tests, "exchanging_work_via_netsync")
table.insert(tests, "single_manifest_netsync")
table.insert(tests, "netsync_transfers_public_keys")
table.insert(tests, "repeatedly_exchanging_work_via_netsync")
table.insert(tests, "(normal)_netsync_on_partially_unrelated_revisions")
table.insert(tests, "disapproving_of_a_revision")
table.insert(tests, "creating_a_good_and_bad_test_result")
table.insert(tests, "importing_a_cvs_file_with_one_version")
table.insert(tests, "list_missing_files")
table.insert(tests, "attr_set_get_commands")
table.insert(tests, "single_character_filename_support")
table.insert(tests, "manifest_restrictions")
table.insert(tests, "subdirectory_restrictions")
table.insert(tests, "renaming_a_patched_file")
table.insert(tests, "renaming_a_deleted_file")
table.insert(tests, "merging_a_rename_twice")
table.insert(tests, "updating_from_a_merge_which_adds_a_file")
table.insert(tests, "changing_passphrase_of_a_private_key")
table.insert(tests, "diffing_a_revision_with_an_added_file")
table.insert(tests, "updating_to_a_given_revision")
table.insert(tests, "heads")
table.insert(tests, "heads_with_discontinuous_branches")
table.insert(tests, "test_a_merge")
table.insert(tests, "test_a_merge_2")
table.insert(tests, "tags_and_tagging_of_revisions")
table.insert(tests, "mtn_add_dot")
table.insert(tests, "(minor)_update_cleans_emptied_directories")
table.insert(tests, "merging_(add_a)_with_(add_a,_drop_a)")
table.insert(tests, "merging_an_add_edge")
table.insert(tests, "merge((),_(patch_a,_drop_a,_add_a))")
table.insert(tests, "merge((),_(add_a,_drop_a,_add_a))")
table.insert(tests, "merge((add_a),_(add_a,_drop_a,_add_a))")
table.insert(tests, "merge((),_(add_a,_patch_a,_drop_a,_add_a))")
table.insert(tests, "merge((patch_a),_(drop_a,_add_a))")
table.insert(tests, "merge_multiple_heads_1")
table.insert(tests, "explicit_merge")
table.insert(tests, "update_with_multiple_candidates")
table.insert(tests, "checkout_validates_target_directory")
table.insert(tests, "checkout_creates_right__MTN_options")
table.insert(tests, "trust_hooks_and_trusted_command")
table.insert(tests, "attr_set_attr_get")
table.insert(tests, "_--rcfile_requires_extant_file")
table.insert(tests, "persistent_netsync_server_-_revs_&_certs")
table.insert(tests, "persistent_netsync_server_-_keys")
table.insert(tests, "first_extent_normalization_pass")
table.insert(tests, "(imp)_deleting_directories")
table.insert(tests, "schema_migration")
table.insert(tests, "database_dump_load")
table.insert(tests, "no-change_deltas_disappear")
table.insert(tests, "merge((),_(drop_a,_rename_b_a,_patch_a))")
table.insert(tests, "verification_of_command_line_options")
table.insert(tests, "log_hides_deleted_renamed_files")
table.insert(tests, "CRLF_line_normalization")
table.insert(tests, "pull_a_netsync_branch_which_has_a_parent_from_another_branch")
table.insert(tests, "(normal)_netsync_revision_with_no_certs")
table.insert(tests, "check_same_db_contents_macro")
table.insert(tests, "merge_rev_with_ancestor")
table.insert(tests, "propagate_a_descendent")
table.insert(tests, "propagate_an_ancestor")
table.insert(tests, "status_with_missing_files")
table.insert(tests, "(imp)_persistent_netsync_server_-_keys_2")
table.insert(tests, "update_1")
table.insert(tests, "(todo)_vcheck")
table.insert(tests, "_--db_with_parent_dir")
table.insert(tests, "add_in_subdir")
table.insert(tests, "(minor)_drop_in_subdir")
table.insert(tests, "revert_in_subdirs")
table.insert(tests, "rename_in_subdir")
table.insert(tests, "attr_command_in_subdirs")
table.insert(tests, "(normal)_update_across_discontinuity")
table.insert(tests, "rename_dir_to_non-sibling")
table.insert(tests, "merge_with_add,_rename_file,_and_rename_dir")
table.insert(tests, "merge((rename_a_b),_(rename_a_c))")
table.insert(tests, "merge((patch_foo_a),_(rename_foo__bar_))")
table.insert(tests, "(imp)_merge((patch_foo_a),_(delete_foo_))")
table.insert(tests, "revert_directories")
table.insert(tests, "revert_renames")
table.insert(tests, "revert_unchanged_file_preserves_mtime")
table.insert(tests, "rename_cannot_overwrite_files")
table.insert(tests, "failed_checkout_is_a_no-op")
table.insert(tests, "(todo)_write_monotone-agent")
table.insert(tests, "(todo)_design_approval_semantics")
table.insert(tests, "committing_with_a_non-english_message")
table.insert(tests, "warn_on_bad_restriction")
table.insert(tests, "_MTN_revision_is_required")
table.insert(tests, "update_no-ops_when_no_parent_revision")
table.insert(tests, "branch-based_checkout")
table.insert(tests, "db_load_must_create_a_new_db")
table.insert(tests, "automate_automate_version")
table.insert(tests, "automate_heads")
table.insert(tests, "merge_normalization_edge_case")
table.insert(tests, "(todo)_undo_update_command")
table.insert(tests, "modification_of_an_empty_file")
table.insert(tests, "largish_file")
table.insert(tests, "files_with_intermediate__MTN_path_elements")
table.insert(tests, "(minor)_test_a_merge_3")
table.insert(tests, "(minor)_test_a_merge_4")
table.insert(tests, "db_missing")
table.insert(tests, "database_check")
table.insert(tests, "(minor)_add_own_db")
table.insert(tests, "can_execute_things")
table.insert(tests, "diff_a_binary_file")
table.insert(tests, "command_completion")
table.insert(tests, "merge_rename_file_and_rename_dir")
table.insert(tests, "diff_respects_restrictions")
table.insert(tests, "cat_-r_REV_PATH")
table.insert(tests, "netsync_client_absorbs_and_checks_epochs")
table.insert(tests, "netsync_server_absorbs_and_checks_epochs")
table.insert(tests, "netsync_epochs_are_not_sent_upstream_by_pull")
table.insert(tests, "vars")
table.insert(tests, "netsync_default_server_pattern")
table.insert(tests, "netsync_default_server_pattern_setting")
table.insert(tests, "netsync_client_absorbs_server_key")
table.insert(tests, "netsync_verifies_server_keys")
table.insert(tests, "test_a_merge_5")
table.insert(tests, "empty_id_completion")
table.insert(tests, "empty_string_as_a_path_name")
table.insert(tests, "empty_environment")
table.insert(tests, "short_options_work_correctly")
table.insert(tests, "netsync_is_not_interrupted_by_SIGPIPE")
table.insert(tests, "setup_creates__MTN_log")
table.insert(tests, "checkout_creates__MTN_log")
table.insert(tests, "commit_using__MTN_log")
table.insert(tests, "commit_w_o__MTN_log_being_present")
table.insert(tests, "commit_validation_lua_hook")
table.insert(tests, "drop_a_public_key")
table.insert(tests, "drop_a_public_and_private_key")
table.insert(tests, "rename_moves_attributes")
table.insert(tests, "automate_ancestors")
table.insert(tests, "automate_descendents")
table.insert(tests, "automate_erase_ancestors")
table.insert(tests, "automate_toposort")
table.insert(tests, "diff_in_a_never-committed_project")
table.insert(tests, "automate_ancestry_difference")
table.insert(tests, "automate_leaves")
table.insert(tests, "log_--last=N_--next=N")
table.insert(tests, "commit_using__MTN_log_and_--message")
table.insert(tests, "check_that_--xargs_and_-(at)_behave_correctly")
table.insert(tests, "db_execute")
table.insert(tests, "sql_function_gunzip_(which_replaced_unpack)")
table.insert(tests, "files_with_spaces_at_the_end")
table.insert(tests, "inodeprints")
table.insert(tests, "update_updates_inodeprints")
table.insert(tests, "listing_workspace_manifests")
table.insert(tests, "importing_cvs_files_with_identical_logs")
table.insert(tests, "sticky_branches")
table.insert(tests, "checkout_without_--branch_sets_branch")
table.insert(tests, "netsync_largish_file")
table.insert(tests, "update_to_off-branch_rev")
table.insert(tests, "setup_checkout_touch_new__MTN_options_only")
table.insert(tests, "renaming_a_directory_and_then_adding_a_new_with_the_old_name")
table.insert(tests, "test_problematic_cvs_import")
table.insert(tests, "cvs_import_with_file_added_on_a_branch")
table.insert(tests, "use_get_linesep_conv_hook")
table.insert(tests, "add_workspace_commit_in_another")
table.insert(tests, "update_to_non-existent_rev")
table.insert(tests, "_--author,_--date")
table.insert(tests, "update_does_not_stomp_non-monotone_files")
table.insert(tests, "db_check_and_non-serious_errors")
table.insert(tests, "db_kill_rev_locally_command")
table.insert(tests, "drop_removes_attributes")
table.insert(tests, "attr_drop")
table.insert(tests, "log_--last=N_FILENAME")
table.insert(tests, "attr_init_functions")
table.insert(tests, "add_executable")
table.insert(tests, "use_inodeprints_hook")
table.insert(tests, "bad_packet_args")
table.insert(tests, "commit_update_multiple_heads_message")
table.insert(tests, "diffing_with_explicit_rev_same_as_wc_rev")
table.insert(tests, "normalized_filenames")
table.insert(tests, "automate_inventory")
table.insert(tests, "rename_file_to_dir")
table.insert(tests, "replace_file_with_dir")
table.insert(tests, "replace_dir_with_file")
table.insert(tests, "automate_parents,_automate_children")
table.insert(tests, "automate_graph")
table.insert(tests, "files_with_non-utf8_data")
table.insert(tests, "cvs_import,_file_dead_on_head_and_branch")
table.insert(tests, "selecting_arbitrary_certs")
table.insert(tests, "check_automate_select")
table.insert(tests, "refresh_inodeprints")
table.insert(tests, "test_a_merge_6")
table.insert(tests, "test_annotate_command")
table.insert(tests, "annotate_file_added_on_different_forks")
table.insert(tests, "annotate_file_on_multirooted_branch")
table.insert(tests, "netsync_badhost_gives_nice_error")
table.insert(tests, "checking_a_few_command_specific_options")
table.insert(tests, "annotate_where_one_parent_is_full_copy")
table.insert(tests, "cvs_import,_deleted_file_invariant")
table.insert(tests, "_--rcfile=-")
table.insert(tests, "mtn_up")
table.insert(tests, "merge((drop_a),_(rename_a_b,_patch_b))")
table.insert(tests, "fail_cleanly_on_unreadable__MTN_options")
table.insert(tests, "importing_cvs_with_vendor_imports_and_branches")
table.insert(tests, "commit_with_--message-file")
table.insert(tests, "automate_attributes")
table.insert(tests, "diff_against_empty_file")
table.insert(tests, "netsync_permissions")
table.insert(tests, "update_with_blocked_rename")
table.insert(tests, "merge((drop_a),_(drop_a,_add_a))")
table.insert(tests, "annotate_where_lineage_depends_on_traversal")
table.insert(tests, "annotate_where_line_splits")
table.insert(tests, "automate_certs")
table.insert(tests, "check_later_and_earlier_selectors")
table.insert(tests, "automate_stdio")
table.insert(tests, "importing_a_small,_real_cvs_repository")
table.insert(tests, "update_with_pending_drop")
table.insert(tests, "update_with_pending_add")
table.insert(tests, "update_with_pending_rename")
table.insert(tests, "restricted_commit_with_inodeprints")
table.insert(tests, "merge_manual_file")
table.insert(tests, "revert_works_with_restrictions")
table.insert(tests, "status")
table.insert(tests, "a_tricky_cvs_repository_with_tags")
table.insert(tests, "_--rcfile=directory")
table.insert(tests, "include()_and_includedir()_lua_functions")
table.insert(tests, "lua_function_existsonpath")
table.insert(tests, "db_kill_branch_certs_locally_command")
table.insert(tests, "netsync_with_globs")
table.insert(tests, "netsync,--set-default")
table.insert(tests, "get_netsync_read_permitted")
table.insert(tests, "serve_pull_with_--exclude")
table.insert(tests, "netsync,--exclude,defaults")
table.insert(tests, "ls_tags_with_ambiguous_tags")
table.insert(tests, "db_kill_tag_locally")
table.insert(tests, "diff_-rREV1_-rREV2_UNCHANGED-FILE")
table.insert(tests, "b_and_t_selector_globbing")
table.insert(tests, "diff_--external")
table.insert(tests, "db_migrate_on_bad_schema")
table.insert(tests, "list_branches")
table.insert(tests, "unnormalized_paths_in_database")
table.insert(tests, "annotate_with_no_revs")
table.insert(tests, "merging_(add_a,_rename_a_b)_with_(add_b)")
table.insert(tests, "update_-b_foo_updates__MTN_options_correctly")
table.insert(tests, "_MTN_files_handled_correctly_in_aborted_commit")
table.insert(tests, "test_a_merge_7")
table.insert(tests, "commit_writes_message_back_to__MTN_log")
table.insert(tests, "log_--brief")
table.insert(tests, "explicit_merge_LEFT_RIGHT_ANC_BRANCH")
table.insert(tests, "drop_with_actual_removal")
table.insert(tests, "rename_with_actual_file_rename")
table.insert(tests, "mtn_read_FILE")
table.insert(tests, "setup_on_existing_path")
table.insert(tests, "things_in_.mtn-ignore_get_ignored")
table.insert(tests, "automate_get_file")
table.insert(tests, "automate_get_manifest_of")
table.insert(tests, "automate_get_revision")
table.insert(tests, "fail_cleanly_on_unreadable_db")
table.insert(tests, "use_restrictions_with_--exclude")
table.insert(tests, "use_restrictions_with_--exclude_and_inodeprints")
table.insert(tests, "filenames_in_diff_after_rename")
table.insert(tests, "key_management_without_a_database")
table.insert(tests, "automate_keys")
table.insert(tests, "diffing_a_file_within_revision_outside_a_workspace")
table.insert(tests, "logging_a_file_within_revision_outside_a_workspace")
table.insert(tests, "add_inside__MTN_")
table.insert(tests, "annotate_file_whose_name_changed")
table.insert(tests, "_--confdir_option_and_get_confdir_lua_function_work")
table.insert(tests, "database_is_closed_on_signal_exit")
table.insert(tests, "update_-b_switches_branches_even_when_noop")
table.insert(tests, "migrate_with_rosterify")
table.insert(tests, "rosterify_migrates_file_dir_attrs")
table.insert(tests, "db_rosterify_preserves_renames")
table.insert(tests, "restrictions_when_pwd_is_mixed_case")
table.insert(tests, "read_and_convert_old_privkey_packet")
table.insert(tests, "restricted_commands_are_consistent")
table.insert(tests, "rosterify_--drop-attr")
table.insert(tests, "rosterify_on_a_db_with_1_rev")
table.insert(tests, "b_and_h_selectors")
table.insert(tests, "revert_ignored_files")
table.insert(tests, "disallowing_persistence_of_passphrase")
table.insert(tests, "db_data_format_checking")
table.insert(tests, "rename_files_into_a_directory")
table.insert(tests, "listing_changed_files")
table.insert(tests, "revert_file_in_new_project")
table.insert(tests, "db_kill_rev_locally_command_2")
table.insert(tests, "log_--no-files_and_--merges")
table.insert(tests, "importing_cvs_branches_with_correct_ancestory")
table.insert(tests, "check_--log")
table.insert(tests, "log_and_selectors_returning_multiple_rids")
table.insert(tests, "pivot_root")
table.insert(tests, "reverting_a_pivot_root")
table.insert(tests, "updating_through_a_pivot_root")
table.insert(tests, "db_rosterify_on_a_db_with_a_root_suture")
table.insert(tests, "log_dir")
table.insert(tests, "show_conflicts")
table.insert(tests, "merge_a_project_into_a_subdirectory_of_an_unrelated_project")
table.insert(tests, "restriction_excludes_parent_dir")
table.insert(tests, "revert_moving_a_file_to_a_renamed_directory")
table.insert(tests, "one-way_netsync_where_the_sink_has_more_epochs")
table.insert(tests, "branch_handling_in_disapprove")
table.insert(tests, "checking_that_certain_commands_ignores_the_contents_of__MTN_options")
table.insert(tests, "exchanging_work_via_netsync,_with_notes")
table.insert(tests, "db_rosterify_twice_gives_an_error_second_time")
table.insert(tests, "_MTN_case-folding_security_patch")
table.insert(tests, "rosterify_handles_.mt-ignore_files")
table.insert(tests, "revert_file_blocked_by_unversioned_directory")
table.insert(tests, "pid_file_cleanup")
table.insert(tests, "rosterify_on_a_db_with_an_empty_manifest")
table.insert(tests, "sync_server_--exclude_foo")
table.insert(tests, "pid_file_and_log_handles_open_failures")
table.insert(tests, "mtn_execute_attr_respects_umask")
table.insert(tests, "setup_in_subdirectory")
table.insert(tests, "test_the_help_command")
table.insert(tests, "test_the_approve_command")
table.insert(tests, "checkout_fails_with_multiple_heads")
table.insert(tests, "ls_epochs")
table.insert(tests, "test_some_hook_helper_functions")
table.insert(tests, "do_not_log_the_result_of_hook_get_passphrase")
table.insert(tests, "quiet_turns_off_tickers_but_not_warnings")
table.insert(tests, "reallyquiet_turns_off_tickers_and_warnings")
table.insert(tests, "escaped_selectors")
table.insert(tests, "automate_get_base_revision_id")
table.insert(tests, "automate_get_current_revision_id")
table.insert(tests, "log_--diffs")
table.insert(tests, "db_info_of_new_database")
table.insert(tests, "automate_common_ancestors")
table.insert(tests, "invalid_--root_settings")
table.insert(tests, "netsync_over_pipes")
table.insert(tests, "ls_unknown_of_unknown_subdir")
table.insert(tests, "automate_branches")
table.insert(tests, "merge_conflict_with_no_lca")
table.insert(tests, "pluck_basics")
table.insert(tests, "diff_output_formats")
table.insert(tests, "pluck_lifecycle")
table.insert(tests, "pluck_restricted")
table.insert(tests, "revert_--missing_in_subdir")
table.insert(tests, "restrictions_with_renames_and_adds")
table.insert(tests, "diff_shows_renames")
table.insert(tests, "dump_on_crash")
table.insert(tests, "automate_tags")
table.insert(tests, "restrictions_with_deletes")
table.insert(tests, "log_with_restriction")
table.insert(tests, "log_quits_on_SIGPIPE")
table.insert(tests, "drop_directory_with_unversioned_files_and_merge")
table.insert(tests, "checkout_-r_no_dir")
table.insert(tests, "annotate_with_human_output")
table.insert(tests, "automate_genkey")
table.insert(tests, "migrate_workspace")
table.insert(tests, "workspace_migration")
table.insert(tests, "usage_output_streams")
table.insert(tests, "automate_get_content_changed")
table.insert(tests, "i18n_commit_messages")
table.insert(tests, "ws_ops_with_wrong_node_type")
table.insert(tests, "pivot_root_to_new_dir")
table.insert(tests, "multiple_message_commit")
table.insert(tests, "db_check_(heights)")
