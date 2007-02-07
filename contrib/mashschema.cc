/* This program helps take a sequence of CREATE TABLE statements as
   written in schema.sql and mash them into the format used when
   computing schema hashes.  Use it when you need to add an entry to
   the temporarily_allowed_tables array in schema_migration.cc.

   Here's how to use it: Cut the relevant CREATE TABLE statements out
   of schema.sql.  Paste them into a scratch file.  Make sure they are
   in alphabetical order by table name. Remove all terminating
   semicolons.  Then run this program as follows:

    g++ mashschema.cc
    ./a.out < scratchfile | fmt |
         sed -e 's/\\/\\\\/g
                 s/"/\\"/g
                 s/^/  "/
                 s/$/ "/
                 $s/ "$/",/' > scratchfile2

   Insert the text in scratchfile2 *verbatim* into the array, just before
   the 0-terminator. */

#include <iostream>
#include <string>
#include <boost/tokenizer.hpp>

using std::cin;
using std::cout;
using std::string;
using boost::char_separator;
typedef boost::tokenizer<char_separator<char> > tokenizer;

int
main(void)
{
  string schema;
  string line;
  char_separator<char> sep(" \r\n\t", "(),;");

  while (!cin.eof())
    {
      std::getline(cin, line);
      tokenizer tokens(line, sep);
      for (tokenizer::iterator i = tokens.begin(); i != tokens.end(); i++)
        {
          if (schema.size() != 0)
            schema += " ";
          schema += *i;
        }
    }

  cout << schema << "\n";

  return 0;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
