all:
	g++ main.cpp -o vcs_keyboard -lSDL -Os
	strip vcs_keyboard