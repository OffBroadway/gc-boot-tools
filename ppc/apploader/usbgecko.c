/* 
 * Copyright (c) 2019-2021, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

// #include "tinyprintf.h"
#include "usbgecko.h"

#include "config.h"

#ifdef DEBUG

// u32 *uart_init = (u32*)0x81481c70;
// u32 (*InitializeUART)(u32) = (u32 (*)(u32))0x81360920;

u32 *orig_uart_enabled = (u32*)0x81481a40;
s32 (*orig_WriteUARTN)(const void *buf, u32 len) = (s32 (*)(const void *, u32))0x81360970;

s32 custom_WriteUARTN(const void *buf, u32 len)
{
  *orig_uart_enabled = 0xa5ff005a;
	return orig_WriteUARTN(buf, len);
}

typedef void* (*WriteProc_t)(void*, const char*, size_t);

int (*__pformatter)(WriteProc_t WriteProc, void* WriteProcArg, const char* format_str, va_list arg) = (void*)0x81378cf8;
WriteProc_t __StringWrite = (void*)0x81378c34;

typedef struct {
	char* CharStr;
	size_t MaxCharCount;
	size_t CharsWritten;
} __OutStrCtrl;

// from https://github.com/projectPiki/pikmin2/blob/0285984b81a1c837063ae1852d94607fdb21d64c/src/Dolphin/MSL_C/MSL_Common/printf.c#L1267-L1282
int vsnprintf(char* s, size_t n, const char* format, va_list arg) {
	int end;
	__OutStrCtrl osc;
	osc.CharStr      = s;
	osc.MaxCharCount = n;
	osc.CharsWritten = 0;

	end = __pformatter(__StringWrite, &osc, format, arg);

	if (s) {
		s[(end < n) ? end : n - 1] = '\0';
	}

	return end;
}

void gprintf(const char *fmt, ...) {
  va_list args;
  static char buf[256];

  va_start(args, fmt);
  int length = vsnprintf((char *)buf, 256, (char *)fmt, args);

  custom_WriteUARTN(buf, length);

  va_end(args);
}
#endif
