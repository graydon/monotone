/* read_password.c: retrieve the password
 * Nico Schottelius (nico-linux-monotone@schottelius.org)
 * 13-May-2004
 */

#include <unistd.h>     /* write             */
#include <stdlib.h>     /* *alloc            */
#include <string.h>     /* str*              */
#include <termios.h>    /* tc(set|get)attr   */

#define PASS_LENGTH 128 /* who'll ever use 128 Byte Passwords? */

struct termios save_term;

/* is successfull */
static void echo_on()
{
   save_term.c_lflag |= ECHO | ECHOE | ECHOK;
   tcsetattr(0, TCSANOW, &save_term);
}

/* return 0 if failed, return 1 if successfull */
static int echo_off() {
   struct termios temp;
   if (tcgetattr(0,&save_term)) return 0;
   temp=save_term;
   temp.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
   tcsetattr(0, TCSANOW, &temp);
   return 1;
}

char *read_password(char *keyid) {
   char     *password = NULL;
   size_t   pass_len;
   char     *tmp;
   size_t   text_len;
   char     *text_1   = "enter passphrase for key ID [";
   char     *text_2;
   char     *text_3   = "] : ";

   /* set output message */
   text_2      = keyid;
   text_len    = strlen(text_1) + strlen(text_2) + strlen(text_3);
   tmp         = alloca(text_len+1); /* alloca frees itself */

   strcpy(tmp,text_1);
   strcat(tmp,text_2);
   strcat(tmp,text_3);

   write(0,tmp,text_len);
   
   /* get password */
   password = (char *) malloc(PASS_LENGTH);
   if(password == NULL) return (char *) 0; /* a die function should be here */
   
   echo_off();
   pass_len = read(0,password,PASS_LENGTH-1);
   tcflush(0,TCIFLUSH);
   echo_on();
   
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
