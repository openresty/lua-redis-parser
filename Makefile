version=0.01
name=lua-redis-parser
dist=$(name)-$(version)

.PHONE: all clean dist test

#CC = gcc
RM = rm -f

# Gives a nice speedup, but also spoils debugging on x86. Comment out this
# line when debugging.
OMIT_FRAME_POINTER = -fomit-frame-pointer

# Name of .pc file. "lua5.1" on Debian/Ubuntu
#LUAPKG = lua5.1
#CFLAGS = `pkg-config $(LUAPKG) --cflags` -fPIC -O3 -Wall
#LFLAGS = -shared $(OMIT_FRAME_POINTER)
#INSTALL_PATH = `pkg-config $(LUAPKG) --variable=INSTALL_CMOD`

## If your system doesn't have pkg-config, comment out the previous lines and
## uncomment and change the following ones according to your building
## enviroment.

#CFLAGS=-I/usr/include/lua5.1/ -O0 -g -fPIC -Wall -Werror
CFLAGS=-I/usr/include/lua5.1/ -O2 -fPIC -Wall -Werror
LFLAGS=-shared $(OMIT_FRAME_POINTER)
INSTALL_PATH=/usr/lib/lua/5.1
CC=gcc

all: parser.so

parser.lo: redis-parser.c ddebug.h
	$(CC) $(CFLAGS) -o parser.lo -c $<

parser.so: parser.lo
	$(CC) -o parser.so $(LFLAGS) $(LIBS) $<

install: parser.so
	install -D -s parser.so $(DESTDIR)$(INSTALL_PATH)/lz/parser.so

clean:
	$(RM) *.so *.lo lz/*.so

test: parser.so
	if [ ! -d redis ]; then mkdir redis; fi
	cp parser.so redis/
	prove -r t

valtest: parser.so
	if [ ! -d lz ]; then mkdir lz; fi
	cp parser.so lz/
	TEST_LUA_USE_VALGRIND=1 prove -r t

dist:
	if [ -d $(dist) ]; then rm -r $(dist); fi
	mkdir $(dist)
	cp *.c *.h Makefile $(dist)/
	tar czvf $(dist).tar.gz $(dist)/

