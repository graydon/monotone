/* read_password.c: retrieve the password (WIN32)
 * Nico Schottelius (nico-linux-monotone AT schottelius.org)
 * 13-May-2004
 */

#include <io.h>     /* write             */
#include <stdlib.h>     /* *alloc            */
#include <string.h>     /* str*              */

#define PASS_LENGTH 128 /* who'll ever use 128 Byte Passwords? */

char *read_password(char *text) {
   char     *password = NULL;
   size_t   pass_len;
   char     *tmp;
   
   /* password prompt */
   write(0,text,strlen(text));
   
   /* get password */
   password = (char *) malloc(PASS_LENGTH);
   if(password == NULL) return (char *) 0; /* a die function should be here */
   
   pass_len = read(0,password,PASS_LENGTH-1);
   
   /* catch errors and zero reads */
   if(pass_len <= 0) {
      free(password);
      password = NULL;
   } else {
      tmp = realloc(password,pass_len);   /* shrink to used memory */
      if(tmp != NULL) password = tmp;
      password[pass_len-1] = 0;     /* replace eof with \0 */
   }

   return password;
}
