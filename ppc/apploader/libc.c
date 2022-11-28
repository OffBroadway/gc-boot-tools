#include "libc.h"

void _memcpy(void* dest, const void* src, int count) {
	char* tmp = (char*)dest,* s = (char*)src;
	while (count--)
		*tmp++ = *s++;
}

void _memset(void* s, int c, int count) {
	char* xs = (char*)s;
	while (count--)
		*xs++ = c;
}
