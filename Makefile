all : usarena.a example

usarena.a :
	(cd Src ; make)
	cp Src/usarena.a .
	cp Src/*.h .

example :
	(cd Example ; make)
	cp Example/example .

clean :
	(cd Src     ; make clean)
	(cd Example ; make clean)
	/bin/rm -f *.[ah] example
