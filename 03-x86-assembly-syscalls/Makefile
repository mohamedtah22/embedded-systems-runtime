all: start

start: start.o util.o
	gcc -m32 util.o start.o -o start

start.o: start.s
	nasm -f elf32 start.s -o start.o
util.o:
	gcc -m32 -Wall -ansi -c -nostdlib -fno-stack-protector util.c -o util.o

clean:
	rm -f *.o start
