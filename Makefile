#!/bin/bash
#Makefile for sfind
sfind: sfind.o utils.o
	gcc -Wall sfind.o utils.o -o sfind
utils.o: utils.c utils.h
	gcc -Wall -c utils.c
sfind.o: sfind.c utils.h
	gcc -Wall -c sfind.c
clean:
	rm -f sfind
	rm -f utils.o
	rm -f sfind.o
