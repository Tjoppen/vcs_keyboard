#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>

#define FREQ 31400

static uint32_t keymask = 0;
static int counters[32] = {0};

static const struct {
    SDLKey key;
    int freq_inv;   //they're XOR 31 because I messed up and I'm too lazy to change all the numbers
} keymap[] = {
    /* SDL handily uses ASCII key codes */
    {'z',0},    {'x',1},    {'c',2},    {'v',3},
    {'b',4},    {'n',5},    {'m',6},    {',',7},
    {'a',8},    {'s',9},    {'d',10},   {'f',11},
    {'g',12},   {'h',13},   {'j',14},   {'k',15},
    {'q',16},   {'w',17},   {'e',18},   {'r',19},
    {'t',20},   {'y',21},   {'u',22},   {'i',23},
    {'1',24},   {'2',25},   {'3',26},   {'4',27},
    {'5',28},   {'6',29},   {'7',30},   {'8',31},
};

static void synth(void *unused, Uint8 *stream, int len) {
    int16_t *s16 = (int16_t*)stream;
    int x, f;

    memset(stream, 0, len);

    for (x = 0; x < len/2; x++) {
        for (f = 0; f < 32; f++) {
            if (++counters[f] >= 2*f + 2)
                counters[f] = 0;

            if (keymask & (1 << f))
                s16[x] += counters[f] > f ? -1000 : 1000;
        }
    }
}

int main() {
    SDL_AudioSpec fmt;
    SDL_Event event;

    /* need a window for the keyboard to work */
    SDL_SetVideoMode(320, 240, 0, 0);

    fmt.freq = FREQ;
    fmt.format = AUDIO_S16;
    fmt.channels = 1;
    fmt.samples = 512;          /* 256 samples, like Stella */
    fmt.callback = synth;
    fmt.userdata = NULL;

    if (SDL_OpenAudio(&fmt, NULL) < 0)
        return 1;

    SDL_PauseAudio(0);

    for(;;) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                int x;

                if (event.key.keysym.sym == SDLK_ESCAPE)
                    goto die;

                for (x = 0; x < sizeof(keymap)/sizeof(keymap[0]); x++)
                    if (event.key.keysym.sym == keymap[x].key) {
                        if (event.type == SDL_KEYDOWN)
                            keymask |= 1 << (keymap[x].freq_inv ^ 31);
                        else
                            keymask &= ~(1 << (keymap[x].freq_inv ^ 31));
                    }
            } else if (event.type == SDL_QUIT)
                goto die;
        }

        usleep(10000);
    }
die:

    return 0;
}
