ROM_PATH?=roms/CUBE8.ch8

.PHONY: all

build:
	gcc main.c -o chip8 -lcsfml-audio -lcsfml-graphics -lcsfml-window -lcsfml-system -Wall

run: build
	./chip8 ${ROM_PATH}
