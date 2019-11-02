CC=gcc
CFLAGS=-g -Wall -std=gnu99

all: smtpd popd

smtpd: smtpd.o socketbuffer.o user.o server.o
popd: popd.o socketbuffer.o user.o server.o

smtpd.o: smtpd.c socketbuffer.h user.h server.h
popd.o: popd.c socketbuffer.h user.h server.h

socketbuffer.o: socketbuffer.c socketbuffer.h
user.o: user.c user.h
server.o: server.c server.h

clean:
	-rm -rf smtpd popd smtpd.o popd.o socketbuffer.o user.o server.o
cleanall: clean
	-rm -rf *~
