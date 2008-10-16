// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "dates.hh"

#include <ctime>
#include <climits>

using std::string;

// Writing a 64-bit constant is tricky.  We cannot use the macros that
// <stdint.h> provides in C99 (UINT64_C, or even UINT64_MAX) because those
// macros are not in C++'s version of <stdint.h>.  std::numeric_limits<u64>
// cannot be used directly, so we have to resort to #ifdef chains on the old
// skool C limits macros.  BOOST_STATIC_ASSERT is defined in a way that
// doesn't let us use std::numeric_limits<u64>::max(), so we have to
// postpone checking it until runtime (date_t::from_unix_epoch), bleah.
// However, the check will be optimized out, and the unit tests exercise it.
#if defined ULONG_MAX && ULONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULONG_MAX
  #define u64_C(x) x##UL
#elif defined ULLONG_MAX && ULLONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULLONG_MAX
  #define u64_C(x) x##ULL
#elif defined ULONG_LONG_MAX && ULONG_LONG_MAX > UINT_MAX
  #define PROBABLE_U64_MAX ULONG_LONG_MAX
  #define u64_C(x) x##ULL
#else
  #error "How do I write a constant of type u64?"
#endif

const string &
date_t::as_iso_8601_extended() const
{
  I(this->valid());
  return d;
}

std::ostream &
operator<< (std::ostream & o, date_t const & d)
{
  return o << d.as_iso_8601_extended();
}

template <> void
dump(date_t const & d, std::string & s)
{
  s = d.as_iso_8601_extended();
}

date_t
date_t::now()
{
  using std::time_t;
  using std::time;
  using std::tm;
  using std::gmtime;
  using std::strftime;

  time_t t = time(0);
  struct tm b = *gmtime(&t);

  // in CE 10000, you will need to increase the size of 'buf'.
  I(b.tm_year <= 9999);

  char buf[20];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &b);
  return date_t(string(buf));
}

// The Unix epoch is 1970-01-01T00:00:00 (in UTC).  As we cannot safely
//  assume that the system's epoch is the Unix epoch, we implement the
//  conversion to broken-down time by hand instead of relying on gmtime().
//  The algorithm below has been tested on one value from every day in the
//  range [1970-01-01T00:00:00, 36812-02-20T00:36:16) -- that is, [0, 2**40).
//
// Unix time_t values are a linear count of seconds since the epoch,
// and should be interpreted according to the Gregorian calendar:
//
//  - There are 60 seconds in a minute, 3600 seconds in an hour,
//    86400 seconds in a day.
//  - Years not divisible by 4 have 365 days, or 31536000 seconds.
//  - Years divisible by 4 have 366 days, or 31622400 seconds, except ...
//  - Years divisible by 100 have only 365 days, except ...
//  - Years divisible by 400 have 366 days.
//
//  The last two rules are the Gregorian correction to the Julian calendar.
//  We make no attempt to handle leap seconds.

unsigned int const MIN = 60;
unsigned int const HOUR = MIN * 60;
unsigned int const DAY = HOUR * 24;
unsigned int const YEAR = DAY * 365;
unsigned int const LEAP = DAY * 366;

unsigned char const MONTHS[] = {
  31, // jan
  28, // feb (non-leap)
  31, // mar
  30, // apr
  31, // may
  30, // jun
  31, // jul
  31, // aug
  30, // sep
  31, // oct
  30, // nov
  31, // dec
};


inline bool
is_leap_year(unsigned int year)
{
  return (year % 4 == 0
    && (year % 100 != 0 || year % 400 == 0));
}
inline u32
secs_in_year(unsigned int year)
{
  return is_leap_year(year) ? LEAP : YEAR;
}

date_t
date_t::from_unix_epoch(u64 t)
{
  // these types hint to the compiler that narrowing divides are safe
  u64 yearbeg;
  u32 year;
  u32 month;
  u32 day;
  u32 secofday;
  u16 hour;
  u16 secofhour;
  u8 min;
  u8 sec;

  // validate our assumptions about which basic type is u64 (see above).
  I(PROBABLE_U64_MAX == std::numeric_limits<u64>::max());

  // time_t values after this point will overflow a signed 32-bit year
  // counter.  'year' above is unsigned, but the system's struct tm almost
  // certainly uses a signed tm_year; it is best to be consistent.
  I(t <= u64_C(67767976233532799));

  // There are 31556952 seconds (365d 5h 43m 12s) in the average Gregorian
  // year.  This will therefore approximate the correct year (minus 1970).
  // It may be off in either direction, but by no more than one year
  // (empirically tested for every year from 1970 to 2**32 - 1).
  year = t / 31556952;

  // Given the above approximation, recalculate the _exact_ number of
  // seconds to the beginning of that year.  For this to work correctly
  // (i.e. for the year/4, year/100, year/400 terms to increment exactly
  // when they ought to) it is necessary to count years from 1601 (as if the
  // Gregorian calendar had been in effect at that time) and then correct
  // the final number of seconds back to the 1970 epoch.
  year += 369;

  yearbeg = (widen<u64,u32>(year)*365 + year/4 - year/100 + year/400)*DAY;
  yearbeg -= (widen<u64,u32>(369)*365 + 369/4 - 369/100 + 369/400)*DAY;

  // *now* we want year to have its true value.
  year += 1601;

  // Linear search for the range of seconds that really contains t.
  // At most one of these loops should iterate, and only once.
  while (yearbeg > t)
    yearbeg -= secs_in_year(--year);
  while (yearbeg + secs_in_year(year) <= t)
    yearbeg += secs_in_year(year++);

  t -= yearbeg;

  // <yakko> Now, the months digit!
  month = 0;
  for (;;)
    {
      unsigned int this_month = MONTHS[month] * DAY;
      if (month == 1 && is_leap_year(year))
        this_month += DAY;
      if (t < this_month)
        break;

      t -= this_month;
      month++;
      L(FL("from_unix_epoch: month >= %u, t now %llu") % month % t);
      I(month < 12);
    }

  // the rest is straightforward.
  day = t / DAY;
  secofday = t % DAY;

  hour = secofday / HOUR;
  secofhour = secofday % HOUR;

  min = secofhour / MIN;
  sec = secofhour % MIN;

  // the widen<>s here are necessary because boost::format *ignores the
  // format specification* and prints u8s as characters.
  return date_t((FL("%u-%02u-%02uT%02u:%02u:%02u")
                 % year % (month + 1) % (day + 1)
                 % hour % widen<u32,u8>(min) % widen<u32,u8>(sec)).str());
}

// We might want to consider teaching this routine more time formats.
// gnulib has a rather nice date parser, except that it requires Bison
// (not even just yacc).

date_t
date_t::from_string(string const & s)
{
  try
    {
      string d = s;
      size_t i = d.size() - 1;  // last character of the array

      // seconds
      u8 sec;
      N(d.at(i) >= '0' && d.at(i) <= '9'
        && d.at(i-1) >= '0' && d.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      sec = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(sec < 60,
        F("seconds out of range"));

      // optional colon
      if (d.at(i) == ':')
        i--;
      else
        d.insert(i+1, 1, ':');

      // minutes
      u8 min;
      N(d.at(i) >= '0' && d.at(i) <= '9'
        && d.at(i-1) >= '0' && d.at(i-1) <= '5',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      min = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(min < 60,
        F("minutes out of range"));

      // optional colon
      if (d.at(i) == ':')
        i--;
      else
        d.insert(i+1, 1, ':');

      // hours
      u8 hour;
      N((d.at(i-1) >= '0' && d.at(i-1) <= '1'
         && d.at(i) >= '0' && d.at(i) <= '9')
        || (d.at(i-1) == '2' && d.at(i) >= '0' && d.at(i) <= '3'),
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      hour = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;
      N(hour < 24,
        F("hour out of range"));

      // 'T' is required at this point; we also accept a space
      N(d.at(i) == 'T' || d.at(i) == ' ',
        F("unrecognized date (monotone only understands ISO 8601 format)"));

      if (d.at(i) == ' ')
        d.at(i) = 'T';
      i--;

      // day
      u8 day;
      N(d.at(i-1) >= '0' && d.at(i-1) <= '3'
        && d.at(i) >= '0' && d.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      day = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      i -= 2;

      // optional dash
      if (d.at(i) == '-')
        i--;
      else
        d.insert(i+1, 1, '-');

      // month
      u8 month;
      N(d.at(i-1) >= '0' && d.at(i-1) <= '1'
        && d.at(i) >= '0' && d.at(i) <= '9',
        F("unrecognized date (monotone only understands ISO 8601 format)"));
      month = (d.at(i-1) - '0')*10 + (d.at(i) - '0');
      N(month >= 1 && month <= 12,
        F("month out of range in '%s'") % d);
      i -= 2;

      // optional dash
      if (d.at(i) == '-')
        i--;
      else
        d.insert(i+1, 1, '-');

      // year
      N(i >= 3,
        F("unrecognized date (monotone only understands ISO 8601 format)"));

      // this counts down through zero and stops when it wraps around
      // (size_t being unsigned)
      u32 year = 0;
      u32 digit = 1;
      while (i < d.size())
        {
          N(d.at(i) >= '0' && d.at(i) <= '9',
            F("unrecognized date (monotone only understands ISO 8601 format)"));
          year += (d.at(i) - '0')*digit;
          i--;
          digit *= 10;
        }

      N(year >= 1970,
        F("date too early (monotone only goes back to 1970-01-01T00:00:00)"));

      u8 mdays;
      if (month == 2 && is_leap_year(year))
        mdays = MONTHS[month-1] + 1;
      else
        mdays = MONTHS[month-1];

      N(day >= 1 && day <= mdays,
        F("day out of range for its month in '%s'") % d);

      return date_t(d);
    }
  catch (std::out_of_range)
    {
      N(false,
        F("unrecognized date (monotone only understands ISO 8601 format)"));
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(date, from_string)
{
#define OK(x,y) UNIT_TEST_CHECK(date_t::from_string(x).as_iso_8601_extended() \
                            == (y))
#define NO(x) UNIT_TEST_CHECK_THROW(date_t::from_string(x), informative_failure)

  // canonical format
  OK("2007-03-01T18:41:13", "2007-03-01T18:41:13");
  // squashed format
  OK("20070301T184113", "2007-03-01T18:41:13");
  // space between date and time
  OK("2007-03-01 18:41:13", "2007-03-01T18:41:13");
  // squashed, space
  OK("20070301 184113", "2007-03-01T18:41:13");
  // more than four digits in the year
  OK("120070301T184113", "12007-03-01T18:41:13");

  // inappropriate character at every possible position
  NO("x007-03-01T18:41:13");
  NO("2x07-03-01T18:41:13");
  NO("20x7-03-01T18:41:13");
  NO("200x-03-01T18:41:13");
  NO("2007x03-01T18:41:13");
  NO("2007-x3-01T18:41:13");
  NO("2007-0x-01T18:41:13");
  NO("2007-03x01T18:41:13");
  NO("2007-03-x1T18:41:13");
  NO("2007-03-0xT18:41:13");
  NO("2007-03-01x18:41:13");
  NO("2007-03-01Tx8:41:13");
  NO("2007-03-01T1x:41:13");
  NO("2007-03-01T18x41:13");
  NO("2007-03-01T18:x1:13");
  NO("2007-03-01T18:4x:13");
  NO("2007-03-01T18:41x13");
  NO("2007-03-01T18:41:x3");
  NO("2007-03-01T18:41:1x");

  NO("x0070301T184113");
  NO("2x070301T184113");
  NO("20x70301T184113");
  NO("200x0301T184113");
  NO("2007x301T184113");
  NO("20070x01T184113");
  NO("200703x1T184113");
  NO("2007030xT184113");
  NO("20070301x184113");
  NO("20070301Tx84113");
  NO("20070301T1x4113");
  NO("20070301T18x113");
  NO("20070301T184x13");
  NO("20070301T1841x3");
  NO("20070301T18411x");

  // two digit years are not accepted
  NO("07-03-01T18:41:13");

  // components out of range
  NO("1969-03-01T18:41:13");

  NO("2007-00-01T18:41:13");
  NO("2007-13-01T18:41:13");

  NO("2007-01-00T18:41:13");
  NO("2007-01-32T18:41:13");
  NO("2007-02-29T18:41:13");
  NO("2007-03-32T18:41:13");
  NO("2007-04-31T18:41:13");
  NO("2007-05-32T18:41:13");
  NO("2007-06-31T18:41:13");
  NO("2007-07-32T18:41:13");
  NO("2007-08-32T18:41:13");
  NO("2007-09-31T18:41:13");
  NO("2007-10-32T18:41:13");
  NO("2007-11-31T18:41:13");
  NO("2007-03-32T18:41:13");

  NO("2007-03-01T24:41:13");
  NO("2007-03-01T18:60:13");
  NO("2007-03-01T18:41:60");

  // leap year February
  OK("2008-02-29T18:41:13", "2008-02-29T18:41:13");
  NO("2008-02-30T18:41:13");

  // maybe we should support these, but we don't
  NO("2007-03-01");
  NO("18:41");
  NO("18:41:13");
  NO("Thu Mar 1 18:41:13 PST 2007");
  NO("Thu, 01 Mar 2007 18:47:22");
  NO("Thu, 01 Mar 2007 18:47:22 -0800");
  NO("torsdag, mars 01, 2007, 18.50.10");
  // et cetera
#undef OK
#undef NO
}

UNIT_TEST(date, from_unix_epoch)
{
#define OK(x,y) do {                                                    \
    string s_ = date_t::from_unix_epoch(x).as_iso_8601_extended();      \
    L(FL("from_unix_epoch: %lu -> %s") % (x) % s_);                     \
    UNIT_TEST_CHECK(s_ == (y));                                             \
  } while (0)

  // every month boundary in 1970
  OK(0,       "1970-01-01T00:00:00");
  OK(2678399, "1970-01-31T23:59:59");
  OK(2678400, "1970-02-01T00:00:00");
  OK(5097599, "1970-02-28T23:59:59");
  OK(5097600, "1970-03-01T00:00:00");
  OK(7775999, "1970-03-31T23:59:59");
  OK(7776000, "1970-04-01T00:00:00");
  OK(10367999, "1970-04-30T23:59:59");
  OK(10368000, "1970-05-01T00:00:00");
  OK(13046399, "1970-05-31T23:59:59");
  OK(13046400, "1970-06-01T00:00:00");
  OK(15638399, "1970-06-30T23:59:59");
  OK(15638400, "1970-07-01T00:00:00");
  OK(18316799, "1970-07-31T23:59:59");
  OK(18316800, "1970-08-01T00:00:00");
  OK(20995199, "1970-08-31T23:59:59");
  OK(20995200, "1970-09-01T00:00:00");
  OK(23587199, "1970-09-30T23:59:59");
  OK(23587200, "1970-10-01T00:00:00");
  OK(26265599, "1970-10-31T23:59:59");
  OK(26265600, "1970-11-01T00:00:00");
  OK(28857599, "1970-11-30T23:59:59");
  OK(28857600, "1970-12-01T00:00:00");
  OK(31535999, "1970-12-31T23:59:59");
  OK(31536000, "1971-01-01T00:00:00");

  // every month boundary in 1972 (an ordinary leap year)
  OK(63071999, "1971-12-31T23:59:59");
  OK(63072000, "1972-01-01T00:00:00");
  OK(65750399, "1972-01-31T23:59:59");
  OK(65750400, "1972-02-01T00:00:00");
  OK(68255999, "1972-02-29T23:59:59");
  OK(68256000, "1972-03-01T00:00:00");
  OK(70934399, "1972-03-31T23:59:59");
  OK(70934400, "1972-04-01T00:00:00");
  OK(73526399, "1972-04-30T23:59:59");
  OK(73526400, "1972-05-01T00:00:00");
  OK(76204799, "1972-05-31T23:59:59");
  OK(76204800, "1972-06-01T00:00:00");
  OK(78796799, "1972-06-30T23:59:59");
  OK(78796800, "1972-07-01T00:00:00");
  OK(81475199, "1972-07-31T23:59:59");
  OK(81475200, "1972-08-01T00:00:00");
  OK(84153599, "1972-08-31T23:59:59");
  OK(84153600, "1972-09-01T00:00:00");
  OK(86745599, "1972-09-30T23:59:59");
  OK(86745600, "1972-10-01T00:00:00");
  OK(89423999, "1972-10-31T23:59:59");
  OK(89424000, "1972-11-01T00:00:00");
  OK(92015999, "1972-11-30T23:59:59");
  OK(92016000, "1972-12-01T00:00:00");
  OK(94694399, "1972-12-31T23:59:59");
  OK(94694400, "1973-01-01T00:00:00");

  // every month boundary in 2000 (a leap year per rule 5)
  OK(946684799, "1999-12-31T23:59:59");
  OK(946684800, "2000-01-01T00:00:00");
  OK(949363199, "2000-01-31T23:59:59");
  OK(949363200, "2000-02-01T00:00:00");
  OK(951868799, "2000-02-29T23:59:59");
  OK(951868800, "2000-03-01T00:00:00");
  OK(954547199, "2000-03-31T23:59:59");
  OK(954547200, "2000-04-01T00:00:00");
  OK(957139199, "2000-04-30T23:59:59");
  OK(957139200, "2000-05-01T00:00:00");
  OK(959817599, "2000-05-31T23:59:59");
  OK(959817600, "2000-06-01T00:00:00");
  OK(962409599, "2000-06-30T23:59:59");
  OK(962409600, "2000-07-01T00:00:00");
  OK(965087999, "2000-07-31T23:59:59");
  OK(965088000, "2000-08-01T00:00:00");
  OK(967766399, "2000-08-31T23:59:59");
  OK(967766400, "2000-09-01T00:00:00");
  OK(970358399, "2000-09-30T23:59:59");
  OK(970358400, "2000-10-01T00:00:00");
  OK(973036799, "2000-10-31T23:59:59");
  OK(973036800, "2000-11-01T00:00:00");
  OK(975628799, "2000-11-30T23:59:59");
  OK(975628800, "2000-12-01T00:00:00");
  OK(978307199, "2000-12-31T23:59:59");
  OK(978307200, "2001-01-01T00:00:00");

  // every month boundary in 2100 (a normal year per rule 4)
  OK(u64_C(4102444800), "2100-01-01T00:00:00");
  OK(u64_C(4105123199), "2100-01-31T23:59:59");
  OK(u64_C(4105123200), "2100-02-01T00:00:00");
  OK(u64_C(4107542399), "2100-02-28T23:59:59");
  OK(u64_C(4107542400), "2100-03-01T00:00:00");
  OK(u64_C(4110220799), "2100-03-31T23:59:59");
  OK(u64_C(4110220800), "2100-04-01T00:00:00");
  OK(u64_C(4112812799), "2100-04-30T23:59:59");
  OK(u64_C(4112812800), "2100-05-01T00:00:00");
  OK(u64_C(4115491199), "2100-05-31T23:59:59");
  OK(u64_C(4115491200), "2100-06-01T00:00:00");
  OK(u64_C(4118083199), "2100-06-30T23:59:59");
  OK(u64_C(4118083200), "2100-07-01T00:00:00");
  OK(u64_C(4120761599), "2100-07-31T23:59:59");
  OK(u64_C(4120761600), "2100-08-01T00:00:00");
  OK(u64_C(4123439999), "2100-08-31T23:59:59");
  OK(u64_C(4123440000), "2100-09-01T00:00:00");
  OK(u64_C(4126031999), "2100-09-30T23:59:59");
  OK(u64_C(4126032000), "2100-10-01T00:00:00");
  OK(u64_C(4128710399), "2100-10-31T23:59:59");
  OK(u64_C(4128710400), "2100-11-01T00:00:00");
  OK(u64_C(4131302399), "2100-11-30T23:59:59");
  OK(u64_C(4131302400), "2100-12-01T00:00:00");
  OK(u64_C(4133980799), "2100-12-31T23:59:59");

  // limit of a (signed) 32-bit year counter
  OK(u64_C(67767976233532799), "2147483647-12-31T23:59:59");
  UNIT_TEST_CHECK_THROW(date_t::from_unix_epoch(u64_C(67768036191676800)),
                    std::logic_error);

#undef OK
}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
