
// This file is derived from execution_monitor.cpp, a part of boost.
// 
// the error reporting mechanisms in that file were irritating to our
// users, so we've just copied and modified the code, cleaning up 
// parts of it in the process. 
//
// it is somewhat likely that you actually want to look in monotone.cc for
// cpp_main(), which is the function which does more interesting stuff. all
// this file does is interface with the operating system error reporting
// mechanisms to ensure that we back out of SEGV and friends using
// exceptions, and translate system exceptions to something helpful for a
// user doing debugging or error reporting.

#include <boost/cstdlib.hpp>
#include <boost/config.hpp>
#include <string>
#include <new>
#include <typeinfo>
#include <exception>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <ui.hh>
#include <signal.h>
#include <setjmp.h>

// Microsoft + other compatible compilers such as Intel
#if defined(_MSC_VER) || (defined(__MWERKS__) && __MWERKS__ >= 0x3000)
#define MS_STRUCTURED_EXCEPTION_HANDLING
#include <wtypes.h>
#include <winbase.h>
#include <excpt.h>
#include <eh.h> 
#if !defined(__MWERKS__)
#define MS_CRT_DEBUG_HOOK
#include <crtdbg.h>
#endif

#elif (defined(__BORLANDC__) && defined(_Windows))
#define MS_STRUCTURED_EXCEPTION_HANDLING
#include <windows.h>  // Borland 5.5.1 has its own way of doing things. 

#elif (defined(__GNUC__) && defined(__MINGW32__)) 
#define MS_STRUCTURED_EXCEPTION_HANDLING
#include <windows.h> 

#elif defined(__unix) || defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define UNIX_STYLE_SIGNAL_HANDLING
#include <unistd.h>
#include <csignal>
#include <csetjmp>

#else
#error "no known OS signal handling interface"
#endif


// A rough outline of what this file does:
// 
// runs main()
//   - sets up a try block to catch the_one_true_exception
//   - calls main_with_many_flavours_of_exception
//     + sets up a try block with a zillion little exception handlers
//       all of which translate into the_one_true_exception
//     + installs structured exception handler and assertion handler
//       for microsoft systems
//     + calls main_with_optional_signal_handling
//       * sets up a unix sigjump_buf if appropriate
//       * calls cpp_main

extern int 
cpp_main(int argc, char ** argv);

static const size_t 
REPORT_ERROR_BUFFER_SIZE = 512;

#ifdef BOOST_NO_STDC_NAMESPACE
namespace std { using ::strlen; using ::strncat; }
#endif

struct
the_one_true_exception
{
  static char buf[REPORT_ERROR_BUFFER_SIZE];
  explicit the_one_true_exception(char const *msg1, char const *msg2)
  {
    buf[0] = '\0';
    std::strncat(buf, msg1, sizeof(buf)-1);
    std::strncat(buf, msg2, sizeof(buf)-1-std::strlen(buf));
  }
};
char the_one_true_exception::buf[REPORT_ERROR_BUFFER_SIZE];

static void 
report_error(char const *msg1, char const *msg2 = "")
{
  throw the_one_true_exception(msg1, msg2);
}


////////////////////////////////////////////////
// windows style structured exception handling 
// (and assertions, which get their own support)
////////////////////////////////////////////////

#if defined(MS_CRT_DEBUG_HOOK)
static int
assert_reporting_function(int reportType, char* userMessage, int* retVal)
{
  switch (reportType)
    {
    case _CRT_ASSERT:
      report_error(userMessage);
      return 1;
      
    case _CRT_ERROR:
      report_error(userMessage);
      return 1;
      
    default:
      return 0;
    }
}
#endif

#if defined(MS_STRUCTURED_EXCEPTION_HANDLING)
#if !defined(__BORLANDC__) && !defined(__MINGW32__)
struct
ms_se_exception 
{
  unsigned int exception_id;
  explicit ms_se_exception(unsigned int n) 
    : exception_id(n) 
  {}
};

static void
ms_se_trans_func(unsigned int id, _EXCEPTION_POINTERS*)
{
  throw ms_se_exception(id);
}

static void
report_ms_se_error(unsigned int id)
{
  switch (id) 
    {
    case EXCEPTION_ACCESS_VIOLATION:
      report_error("memory access violation");
      break;
      
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      report_error("illegal instruction");
      break;
      
    case EXCEPTION_PRIV_INSTRUCTION:
      report_error("privilaged instruction");
      break;

    case EXCEPTION_IN_PAGE_ERROR:
      report_error("memory page error");
      break;

    case EXCEPTION_STACK_OVERFLOW:
      report_error("stack overflow");
      break;

    case EXCEPTION_DATATYPE_MISALIGNMENT:
      report_error("data misalignment");
      break;
      
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      report_error("integer divide by zero");
      break;

    case EXCEPTION_INT_OVERFLOW:
      report_error("integer overflow");
      break;

    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      report_error("array bounds exceeded");
      break;

    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      report_error("floating point divide by zero");
      break;

    case EXCEPTION_FLT_STACK_CHECK:
      report_error("floating point stack check");
      break;
      
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
      report_error("floating point error");
      break;
      
    default:
      report_error("unrecognized exception or signal");
    }
}
#endif

#if (defined(__BORLANDC__) && defined(_Windows))
// this works for Borland but not other Win32 compilers (which trap too many cases)
static int 
main_with_signal_handlers(int argc, char **argv)
{
  int result;
  __try 
    { 
      result = cpp_main(argc, argv); 
    }    
  __except (1)
    {
      throw ms_se_exception(GetExceptionCode());
    }
  return result;
}

#else
static int 
main_with_signal_handlers(int argc, char **argv)
{
  return cpp_main(argc, argv);
}
#endif


/////////////////////////////
// unix style signal handling 
/////////////////////////////

#elif defined(UNIX_STYLE_SIGNAL_HANDLING)
struct
unix_signal_exception 
{
  char const * error_message;
  explicit unix_signal_exception(char const * em)
    : error_message(em)
  {}
};

static sigjmp_buf jump_buf;

extern "C" 
{
  static void 
  unix_style_signal_handler(int sig)
  {
    siglongjmp(jump_buf, sig);
  }  
}

static int 
main_with_signal_handlers(int argc, char **argv)
{
    typedef struct sigaction* sigaction_ptr;
    static struct sigaction all_signals_action;
    static struct sigaction ignore_signals_action;
    struct sigaction old_SIGFPE_action;
    struct sigaction old_SIGTRAP_action;
    struct sigaction old_SIGSEGV_action;
    struct sigaction old_SIGBUS_action;
    struct sigaction old_SIGABRT_action;
    struct sigaction old_SIGPIPE_action;

    all_signals_action.sa_flags   = 0;
    all_signals_action.sa_handler = &unix_style_signal_handler;
    sigemptyset(&all_signals_action.sa_mask);
    
    ignore_signals_action.sa_flags   = 0;
    ignore_signals_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_signals_action.sa_mask);

    sigaction(SIGFPE , &all_signals_action, &old_SIGFPE_action);
    sigaction(SIGTRAP, &all_signals_action, &old_SIGTRAP_action);
    sigaction(SIGSEGV, &all_signals_action, &old_SIGSEGV_action);
    sigaction(SIGBUS , &all_signals_action, &old_SIGBUS_action);
    sigaction(SIGABRT, &all_signals_action, &old_SIGABRT_action);
    sigaction(SIGPIPE, &ignore_signals_action, &old_SIGPIPE_action);

    int result = 0;
    bool trapped_signal = false;
    char const *em = NULL;

    volatile int sigtype = sigsetjmp(jump_buf, 1);

    if(sigtype == 0) 
      {
        result = cpp_main(argc, argv);
      }
    
    else 
      {
        trapped_signal = true;
        switch(sigtype) 
          {
          case SIGTRAP:
            em = "signal: SIGTRAP (perhaps integer divide by zero)";
            break;
          case SIGFPE:
            em = "signal: SIGFPE (arithmetic exception)";
            break;
          case SIGABRT:
            em = "signal: SIGABRT (application abort requested)";
            break;
          case SIGSEGV:
          case SIGBUS:
            em = "signal: memory access violation";
            break;
          default:
            em = "signal: unrecognized signal";
          }
      }
    
    sigaction(SIGFPE , &old_SIGFPE_action , sigaction_ptr());
    sigaction(SIGTRAP, &old_SIGTRAP_action, sigaction_ptr());
    sigaction(SIGSEGV, &old_SIGSEGV_action, sigaction_ptr());
    sigaction(SIGBUS , &old_SIGBUS_action , sigaction_ptr());
    sigaction(SIGABRT, &old_SIGABRT_action, sigaction_ptr());
    sigaction(SIGPIPE, &old_SIGPIPE_action, sigaction_ptr());
    
    if(trapped_signal) 
      throw unix_signal_exception(em);

    return result;
}
#endif


static int 
main_with_many_flavours_of_exception(int argc, char **argv)
{
  
#if defined(MS_STRUCTURED_EXCEPTION_HANDLING) && !defined(__BORLANDC__) && !defined(__MINGW32__)
  _set_se_translator(ms_se_trans_func);
#endif
  
#if defined(MS_CRT_DEBUG_HOOK)
  _CrtSetReportHook(&assert_reporting_function);
#endif

    try 
      {
        return main_with_signal_handlers(argc, argv);
      }

    catch (char const * ex)
      { 
        report_error("C string: ", ex); 
      }

    catch (std::string const & ex)
      { 
        report_error("std::string: ", ex.c_str()); 
      }
    
    catch( std::bad_alloc const & ex )
      { 
        report_error("std::bad_alloc: ", ex.what()); 
      }
    
#if !defined(__BORLANDC__) || __BORLANDC__ > 0x0551
    catch (std::bad_cast const & ex)
      { 
        report_error("std::bad_cast: ", ex.what()); 
      }

    catch (std::bad_typeid const & ex)
      { 
        report_error("std::bad_typeid: ", ex.what()); 
      }
#else
    catch(std::bad_cast const & ex)
      { 
        report_error("std::bad_cast"); 
      }

    catch( std::bad_typeid const & ex)
      { 
        report_error("std::bad_typeid"); 
      }
#endif
    
    catch(std::bad_exception const & ex)
      { 
        report_error("std::bad_exception: ", ex.what()); 
      }

    catch( std::domain_error const& ex )
      { 
        report_error("std::domain_error: ", ex.what()); 
      }

    catch( std::invalid_argument const& ex )
      { 
        report_error("std::invalid_argument: ", ex.what()); 
      }

    catch( std::length_error const& ex )
      { 
        report_error("std::length_error: ", ex.what()); 
      }

    catch( std::out_of_range const& ex )
      { 
        report_error("std::out_of_range: ", ex.what()); 
      }

    catch( std::range_error const& ex )
      { 
        report_error("std::range_error: ", ex.what()); 
      }

    catch( std::overflow_error const& ex )
      { 
        report_error("std::overflow_error: ", ex.what()); 
      }

    catch( std::underflow_error const& ex )
      { 
        report_error("std::underflow_error: ", ex.what()); 
      }

    catch( std::logic_error const& ex )
      { 
        report_error("std::logic_error: ", ex.what()); 
      }

    catch( std::runtime_error const& ex )
      { 
        report_error("std::runtime_error: ", ex.what()); 
      }

    catch( std::exception const& ex )
      { 
        report_error("std::exception: ", ex.what()); 
      }

#if defined(MS_STRUCTURED_EXCEPTION_HANDLING) && !defined(__BORLANDC__) && !defined(__MINGW32__)
    catch(ms_se_exception const & ex)
      { 
        report_ms_se_error(ex.exception_id); 
      }

#elif defined(UNIX_STYLE_SIGNAL_HANDLING)
    catch(unix_signal_exception const & ex)
      { 
        report_error(ex.error_message); 
      }
#endif

    catch( ... )
      { 
        report_error("exception of unknown type" ); 
      }
    return 0;
}

int 
main(int argc, char **argv)
{
  try
    {
      return main_with_many_flavours_of_exception(argc, argv);
    }
  catch (the_one_true_exception const & e)
    {
      ui.fatal(std::string(e.buf) + "\n");
      // If we got here, it's because something went _really_ wrong, like an
      // invariant failure or a segfault.  So use a distinctive error code, in
      // particular so the testsuite can tell whether we detected an error
      // properly or waited until an invariant caught it...
      return 3;
    }
}
