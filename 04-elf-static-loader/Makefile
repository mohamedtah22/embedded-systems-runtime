all: loader

loader: loader.o start.o startup.o
	ld -o loader loader.o startup.o start.o -L/usr/lib32 -lc -T linking_script -dynamic-linker /lib32/ld-linux.so.2

loader.o: loader.c
	gcc -g -m32 -Wall -c -o loader.o loader.c

start.o: start.s
	nasm -g -f elf -w+all -o start.o start.s

clean:
	rm -f start.o loader.o loader
