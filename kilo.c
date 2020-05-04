#include <termios.h>
#include <unistd.h>

void enableRawMode() {
 struct termios raw;
 
 // reads in terminal attributes to struct raw
 tcgetattr(STDIN_FILENO, &raw);

 // using the bitwise-NOT operator, we get a 0 in fourth bit from right
 // then bitwise-AND with flag field to force all flag fields to become 0 in 
 // fourth bit from right. 
 // c_lflag is for local flags, described as a "dumping ground for other state."
 // by doing this, we will not see what we type bc of the mod to c_lflag telling it not to ECHO. 
 raw.c_lflag &= ~(ECHO);

 // after modification of struct in tcgetattr(), it can be applied to terminal
 // using this function. TCSAFLUSH specifies when to apply the change: here we apply
 // after all pending output has been written to the terminal and discards any 
 // input that hasn't been read. 
 tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
} 

int main() {
 // causing each key typed to not be printed to terminal
 enableRawMode();

 char c;
 while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
 return 0;
}
