/* os specific header file
 * includes all os_dependent functions
 * Their implementations are in unix/ and win32/
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

/* read_password.c */
extern "C" char *read_password(char *text);
