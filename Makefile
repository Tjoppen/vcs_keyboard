all:
	gcc main.c -o vcs_keyboard -lSDL -Os
	strip vcs_keyboard