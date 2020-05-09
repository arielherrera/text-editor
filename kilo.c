#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode() {
 // this allows for the terminal to exit Raw Mode and allow displaying of text
 tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
 // reads in terminal attributes to struct raw
 tcgetattr(STDIN_FILENO, &orig_termios);

 /*

 */
 atexit(disableRawMode);

 struct termios raw = orig_termios;


 /*
 I (in this case) stands for 'input flag' and XON comes from the names of the two cntrl chars
 that ctrl-s and q produce: XOFF to pause transmission and XON to resume transmission, respectively
 ctrl-s and q are read as 19 byte and 17 byte, respectively.
 I stands for 'input flag,' CR for 'carriage return,' and NL for 'new line'
 this ^ disables crtl-m which makes the terminal translate any carraige returns into new lines.
 when BRKINt is enabled, a 'break condition' will cause a SIGINT signal to be sent to the program (like pressing ctrl-c)
 INPCK enables parity checking, doesn't really apply to modern terminal emulators
 ISTRIP causes the 8th bit in an input byte to be set to 0, this is prob already turned off.
 */
 raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

 // OPOST flag formats each "\n" we enter in editor as "\r\n" where "\r" returns curson to start of line.
 raw.c_oflag &= ~(OPOST);
 /*
 CS8 isn't a flag, its a bit mask with multiple bits, which we can set using biwise OR, it sets
 the character size (CS) to 8 bits per byte. 
 */
 raw.c_cflag |= (CS8);
 /*
 using the bitwise-NOT operator, we get a 0 in fourth bit from right
 then bitwise-AND with flag field to force all flag fields to become 0 in
 fourth bit from right.
 c_lflag is for local flags, described as a "dumping ground for other state."
 by doing this, we will not see what we type bc of the mod to c_lflag telling it not to ECHO.
 ICANON is a local flag in the c_lflag field, it allows us to turn off canonical mode
 this means we'll be reading input byte by byte, intead of line by line.
 ISIG means ctrl-c and z can be read as 3 byte and 26 byte, respectively.
 this also disables ctrl-y on macOS
 IEXTEN (belonging to the c_lflag field), disables ctrl-v and ctrl-o (discards ctrl char) in macOS
 in which the terminal would've waited for use to type another char and then send that
 char literally, eg., before we displayed ctrl-c, we might've been able to type ctrl-v
 and then ctrl-c to input a 3 byte.
 */
 raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

 /*
 after modification of struct in tcgetattr(), it can be applied to terminal
 using this function. TCSAFLUSH specifies when to apply the change: here we apply
 after all pending output has been written to the terminal and discards any
 input that hasn't been read.
 */
 tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
 // causing each key typed to not be printed to terminal
 enableRawMode();

 char c;
 while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {

  /*
  iscntrl() tests to see if c is a control character
  cntrl chars are nonprintable chars that we don't want to print to screen
  ASCII codes 0-31 and 127 are cntrl chars.
  */
  if (iscntrl(c)) {
   // formatted byte to ASCII code decimal number
   printf("%d\r\n", c);
  } else {
   // %c tells us to write out the byte directly as a char
   printf("%d ('%c')\r\n", c, c);
  }
 }
return 0;
}
