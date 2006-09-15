GOPT(args, "", std::vector<utf8>, true, "")
#ifdef option_bodies
{
  args.push_back(utf8(arg));
}
#endif

GOPT(dbname, "db,d", system_path, true, gettext_noop("set name of database"))
#ifdef option_bodies
{
  dbname = system_path(arg);
}
#endif
