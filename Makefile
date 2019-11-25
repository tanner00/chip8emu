ROM_PATH?=roms/cube8.ch8

.PHONY: all

build:
	gcc -g main.c -o chip8 -Lminifb -lminifb -lX11 -Iminifb/include -Wall

run: build
	./chip8 ${ROM_PATH}
