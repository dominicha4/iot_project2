// UART0 Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL
// Target uC:       TM4C123GH6PM
// System Clock:    -

// Hardware configuration:
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef UART0_H_
#define UART0_H_

#include <stdint.h>
#include <stdbool.h>

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initUart0();
void setUart0BaudRate(uint32_t baudRate, uint32_t fcyc);
void putcUart0(char c);
void putsUart0(char* str);
char getcUart0();
bool kbhitUart0();
void PRINT_COLOR(char* message, char* color);
void PRINT_EFFECT(char* message, char* effect);
void PRINT_COLOR_FONT(char* message, char* color, char* effect);

//-----------------------------------------------------------------------------
// ANSI CODES for changing text
//-----------------------------------------------------------------------------
#define RESET_TEXT      "\033[0m"

#define BOLD_FONT       "\033[1m"
#define ITALIC_FONT     "\033[3m"
#define UNDERLINE_FONT  "\033[4m"

// Foreground Color Codes
#define BLACK_FG    "\x1B[30m"
#define RED_FG      "\x1B[31m"
#define GREEN_FG    "\x1B[32m"
#define YELLOW_FG   "\x1B[33m"
#define BLUE_FG     "\x1B[34m"
#define MAGENTA_FG  "\x1B[35m"
#define CYAN_FG     "\x1B[36m"
#define WHITE_FG    "\x1B[37m"

// Background Color Codes
#define BLACK_BG    "\x1B[40m"
#define RED_BG      "\x1B[41m"
#define GREEN_BG    "\x1B[42m"
#define YELLOW_BG   "\x1B[43m"
#define BLUE_BG     "\x1B[44m"
#define MAGENTA_BG  "\x1B[45m"
#define CYAN_BG     "\x1B[46m"
#define WHITE_BG    "\x1B[47m"



#endif
