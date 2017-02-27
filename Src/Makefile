HDR= arena.h  ulocks.h
SRC= ulocks.c usarena.c  usinfo.c  usinit.c  usmalloc.c
OBJ= ulocks.o usarena.o  usinfo.o  usinit.o  usmalloc.o

.c.o : ${HDR} $*.o
	cc -c $<

usarena.a : ${OBJ}
	ar r usarena.a ${OBJ}

clean :
	/bin/rm -f *.[oa]
