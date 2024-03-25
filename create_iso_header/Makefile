
DEBUG=1

CROSS=
CC=$(CROSS)gcc

HEXDUMP = hexdump

CFLAGS := -g


create_iso_header_C_SRCS = create_iso_header.c
create_iso_header_C_OBJS = $(patsubst %.c, %.o, $(create_iso_header_C_SRCS))

create_iso_header_SRCS = $(create_iso_header_C_SRCS)
create_iso_header_OBJS = $(create_iso_header_C_OBJS) ../common/lib.o

all: disc_header.iso

disc_header.iso: create_iso_header ../ppc/apploader/apploader.bin
	./create_iso_header ../ppc/apploader/apploader.bin ../icons/opening.bnr

create_iso_header: $(create_iso_header_OBJS)
	$(CC) -o $@ $+

$(create_iso_header_C_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f \
		*~ \
		create_iso_header \ $(create_iso_header_C_OBJS)

dist-clean: clean
	rm -f \
		disc_header.iso

dummy:

