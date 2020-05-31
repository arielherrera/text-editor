/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

/*
bitwise ANDs a char with value 00011111, setting the upper three bits of the char
to 0s, this mirrors what the Ctrl key does in the terminal: it strips bits 5 and 6 from whatever
key you press in combination with Ctrl, and send that
*/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

/*
global struct that contains our editor state, used to store width and height of the terminal
*/
struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) {
  /*
  we are writing an escape sequence; the 4 in write() means we are writing 4 bytes out to terminal
  the first byte is '\x1b' which is the escape character, or 27 in decimal.
  the other three bytes are [2J
  escape sequences always start with an escape char followed by the '[' char
  J is used to clear the screen, and the argument 2 tells us that we want to clear the entire screen
  <esc>[1J would clear the screen up to the cursor, <esc>[0J would clear from the cursor to end of screen
  */
  write(STDOUT_FILENO, "\x1b[2J", 4);
  /*
  H command repositions the cursor, it takes 2 args: the row and column num for where to put the cursor
  both default args for H are 1, so we leave them as is (r and c numbered from 1, not 0)
  */
  write(STDOUT_FILENO, "\x1b[H", 3);

  /*
  looks at global errno var and prints descriptive error message for it, also prints string given to provide context
  */
  perror(s);
  exit(1);
}

void disableRawMode() {
 // this allows for the terminal to exit Raw Mode and allow displaying of text
 if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
 // after printing error message and passed in string, program will exit(1)
 die("tcsetattr");
}

void enableRawMode() {
 // reads in terminal attributes to struct raw
 if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

 /*

 */
 atexit(disableRawMode);

 struct termios raw = E.orig_termios;


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
 VMIN and VTIME are indices into the c_cc field, which stands for "control characters," an array of bytes
 that control various terminal settings.
 VMIN is the minimum number of bytes of input needed before read() can returns
 VTIME sets the max amount of time to wait before read() returns. It is in tenths of a second (100ms)
 if read() times out it will return 0, which makes sense bc its usual return value is the number of bytes read
 */
 raw.c_cc[VMIN] = 0;
 raw.c_cc[VTIME] = 1;

 /*
 after modification of struct in tcgetattr(), it can be applied to terminal
 using this function. TCSAFLUSH specifies when to apply the change: here we apply
 after all pending output has been written to the terminal and discards any
 input that hasn't been read.
 */
 if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*
waits for one keypress and returns it
*/
char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

/*

*/
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    ++i;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

/*
I opted to get the window size the hard way, since ioctl() isn't guaranteed to be able to request the window size on all systems
using the 999C and 999B commands, we, respectively, move the cursor to the right then down to ensure the cursor
reaches the right and bottom edges of the screen
the C and B commands are specifically documented to stop the cursor from going off the screen
*/
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    /*
    if the write doesn't consist of 12 bytes of info, then return -1... but what would prompt the write NOT to output the full 12?
    */
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    /*
    since in our first if (currently, we are using 1, and are always returning -1), we use editorReadKey() to observe the
    results of our escape sequences before the program exits by calling die().
    */
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/
/*
since C doesnt have dynamic strings, we need to create our own dynamic string type
that supports an appending operation
it consists of a pointer to our buffer in memory and a length
we define an ABUF_INIT constant which represents an empty buffer, this acts as a constructor to our abuf type
*/
struct abuf {
  char *b; // b is the buffer of chars
  int len;
};

#define ABUF_INIT {NULL, 0}
/*
abuf is our buffer, s is the string we're appending, and len is the len of the string s
We need to allocate enough mem to hold the new string, we ask realloc() to give us a block of mem
that is the size of the current string plus the size of the sting we're appending
realloc() will either extend the size of the block of mem we currently have
allocated to make due, or it will use free() to free the current mem space
and will find another space that is big enough to fit the buffer and the
string being appended.
*/
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len); // attemps to resize mem block pointed to by ab

  if (new == NULL) return; // NULL returns if request for mem fails
  memcpy(&new[ab->len], s, len); // after space has been made, s is appended where the previous string in ab ended
  ab->b = new; // b points to new now
  ab->len += len; // new len is added to total from previous len
  // since new is allocated statically, exit from this function will return that created to memory
  // since ab got updated it is saved
}

// dtor
void abFree(struct abuf *ab) {
  free(ab->b);
}
/*** output ***/

/*

*/
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; ++y) {
    abAppend(ab, "~", 1);

    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // corrects for terminal cursor flicker in later VT models
    /*
    we are writing an escape sequence; the 4 in write() means we are writing 4 bytes out to terminal
      the first byte is '\x1b' which is the escape character, or 27 in decimal.
      the other three bytes are [2J
      escape sequences always start with an escape char followed by the '[' char
      J is used to clear the screen, and the argument 2 tells us that we want to clear the entire screen
      <esc>[1J would clear the screen up to the cursor, <esc>[0J would clear from the cursor to end of screen
      */
  abAppend(&ab, "\x1b[2J", 4); // clears whole screen
  /*
  H command repositions the cursor, it takes 2 args: the row and column num for where to put the cursor
  both default args for H are 1, so we leave them as is (r and c numbered from 1, not 0)
  */
  abAppend(&ab, "\x1b[H", 3); // repositions cursor

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3); // after drawing, repositions cursor
    abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len); // .b gives us buffer, while ->b accesses buffer
  abFree(&ab);
}

/*** input ***/

/*
waits for a keypress and handles it
*/
void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      /*
      we are writing an escape sequence; the 4 in write() means we are writing 4 bytes out to terminal
      the first byte is '\x1b' which is the escape character, or 27 in decimal.
      the other three bytes are [2J
      escape sequences always start with an escape char followed by the '[' char
      J is used to clear the screen, and the argument 2 tells us that we want to clear the entire screen
      <esc>[1J would clear the screen up to the cursor, <esc>[0J would clear from the cursor to end of screen
      */
      write(STDOUT_FILENO, "\x1b[2J", 4);
      /*
      H command repositions the cursor, it takes 2 args: the row and column num for where to put the cursor
      both default args for H are 1, so we leave them as is (r and c numbered from 1, not 0)
      */
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/*** init ***/

void initEditor() {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
  /*
 causing each key typed to not be printed to terminal
 */
 enableRawMode();

 initEditor();

 while (1) {
   editorRefreshScreen();
   editorProcessKeypress();
 }

return 0;
}
