// sets things up for a netsync.
//
// under unix, we need to disable sigpipe, otherwise monotone might get
// TERMinated ungracefully when the remote connection closes.

#include <signal.h>
#include <unistd.h>

static struct sigaction old_SIGPIPE_action;
static bool have_old_action = false;

void start_platform_netsync()
{
  struct sigaction ignore_signals_action;
  ignore_signals_action.sa_handler = SIG_IGN;
  ignore_signals_action.sa_flags   = 0;
  sigemptyset(&ignore_signals_action.sa_mask);

  sigaction(SIGPIPE, &ignore_signals_action, &old_SIGPIPE_action);
  have_old_action = true;
}

void end_platform_netsync()
{
  if (!have_old_action)
  {
    old_SIGPIPE_action.sa_handler = SIG_DFL;
    old_SIGPIPE_action.sa_flags   = 0;
    sigemptyset(&old_SIGPIPE_action.sa_mask);
  }

  sigaction(SIGPIPE, &old_SIGPIPE_action, NULL);
}
