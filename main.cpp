#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <string>

using namespace std;

#define FREQ 31400
#define C 32

static int counters[C] = {0};

static const struct {
    SDLKey key;
    int freq_inv;   //they're XOR 31 because I messed up and I'm too lazy to change all the numbers
} keymap[] = {
    /* SDL handily uses ASCII key codes */
    {(SDLKey)'z',0},    {(SDLKey)'x',1},    {(SDLKey)'c',2},    {(SDLKey)'v',3},
    {(SDLKey)'b',4},    {(SDLKey)'n',5},    {(SDLKey)'m',6},    {(SDLKey)',',7},
    {(SDLKey)'a',8},    {(SDLKey)'s',9},    {(SDLKey)'d',10},   {(SDLKey)'f',11},
    {(SDLKey)'g',12},   {(SDLKey)'h',13},   {(SDLKey)'j',14},   {(SDLKey)'k',15},
    {(SDLKey)'q',16},   {(SDLKey)'w',17},   {(SDLKey)'e',18},   {(SDLKey)'r',19},
    {(SDLKey)'t',20},   {(SDLKey)'y',21},   {(SDLKey)'u',22},   {(SDLKey)'i',23},
    {(SDLKey)'1',24},   {(SDLKey)'2',25},   {(SDLKey)'3',26},   {(SDLKey)'4',27},
    {(SDLKey)'5',28},   {(SDLKey)'6',29},   {(SDLKey)'7',30},   {(SDLKey)'8',31},
};

static const int typetab[] = {
    1,2,3,4,6,7,8,12,14,15,
};

/* what the sounds are called in Slocum's player */
static const int slocumtab[] = {
    6,-1,7,0,1,2,3,5,-1,4,
};

//code mostly ripped from Stella
static uint8_t myAUDC[C] = {0};
static uint8_t myAUDF[C] = {0};
static int myAUDV[C] = {0};
static uint8_t myP4[C];           // 4-bit register LFSR (lower 4 bits used)
static uint8_t myP5[C];           // 5-bit register LFSR (lower 5 bits used)

typedef pair<float,string> mark;

static vector<mark> notes;
static vector<int16_t> samples;
static int T;                   /* when the program was started */
static int number = 0;

static void sprint_binary(int freq, char *out) {
    int c = myAUDC[freq];
    int bits = slocumtab[c];
    char temp[5];

    if (bits < 0)
        sprintf(temp, "%%xxx");
    else
        sprintf(temp, "%%%i%i%i", (bits >> 2) & 1, (bits >> 1) & 1, bits & 1);

    sprintf(out, "%s%i%i%i%i%i", temp, (freq >> 4) & 1, (freq >> 3) & 1, (freq >> 2) & 1, (freq >> 1) & 1, freq & 1);
}

static int next_tia_sample() {
    int c, ret = 0;

    // Process both sound channels
    for (c = 0; c < C; c++)
    {
      // Update P4 & P5 registers for channel if freq divider outputs a pulse
      if (++counters[c] >= myAUDF[c]*2+2)
          counters[c] = 0;

      if (counters[c] == 0 || counters[c] == myAUDF[c]+1)
      {
        switch(myAUDC[c])
        {
          case 0x00:    // Set to 1
          {
            // Shift a 1 into the 4-bit register each clock
            myP4[c] = (myP4[c] << 1) | 0x01;
            break;
          }

          case 0x01:    // 4 bit poly
          {
            // Clock P4 as a standard 4-bit LSFR taps at bits 3 & 2
            myP4[c] = (myP4[c] & 0x0f) ? 
                ((myP4[c] << 1) | (((myP4[c] & 0x08) ? 1 : 0) ^
                ((myP4[c] & 0x04) ? 1 : 0))) : 1;
            break;
          }

          case 0x02:    // div 31 -> 4 bit poly
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // This does the divide-by 31 with length 13:18
            if((myP5[c] & 0x0f) == 0x08)
            {
              // Clock P4 as a standard 4-bit LSFR taps at bits 3 & 2
              myP4[c] = (myP4[c] & 0x0f) ? 
                  ((myP4[c] << 1) | (((myP4[c] & 0x08) ? 1 : 0) ^
                  ((myP4[c] & 0x04) ? 1 : 0))) : 1;
            }
            break;
          }

          case 0x03:    // 5 bit poly -> 4 bit poly
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // P5 clocks the 4 bit poly
            if(myP5[c] & 0x10)
            {
              // Clock P4 as a standard 4-bit LSFR taps at bits 3 & 2
              myP4[c] = (myP4[c] & 0x0f) ? 
                  ((myP4[c] << 1) | (((myP4[c] & 0x08) ? 1 : 0) ^
                  ((myP4[c] & 0x04) ? 1 : 0))) : 1;
            }
            break;
          }

          case 0x04:    // div 2
          {
            // Clock P4 toggling the lower bit (divide by 2) 
            myP4[c] = (myP4[c] << 1) | ((myP4[c] & 0x01) ? 0 : 1);
            break;
          }

          case 0x05:    // div 2
          {
            // Clock P4 toggling the lower bit (divide by 2) 
            myP4[c] = (myP4[c] << 1) | ((myP4[c] & 0x01) ? 0 : 1);
            break;
          }

          case 0x06:    // div 31 -> div 2
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // This does the divide-by 31 with length 13:18
            if((myP5[c] & 0x0f) == 0x08)
            {
              // Clock P4 toggling the lower bit (divide by 2) 
              myP4[c] = (myP4[c] << 1) | ((myP4[c] & 0x01) ? 0 : 1);
            }
            break;
          }

          case 0x07:    // 5 bit poly -> div 2
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // P5 clocks the 4 bit register
            if(myP5[c] & 0x10)
            {
              // Clock P4 toggling the lower bit (divide by 2) 
              myP4[c] = (myP4[c] << 1) | ((myP4[c] & 0x01) ? 0 : 1);
            }
            break;
          }

          case 0x08:    // 9 bit poly
          {
            // Clock P5 & P4 as a standard 9-bit LSFR taps at 8 & 4
            myP5[c] = ((myP5[c] & 0x1f) || (myP4[c] & 0x0f)) ?
              ((myP5[c] << 1) | (((myP4[c] & 0x08) ? 1 : 0) ^
              ((myP5[c] & 0x10) ? 1 : 0))) : 1;
            myP4[c] = (myP4[c] << 1) | ((myP5[c] & 0x20) ? 1 : 0);
            break;
          }

          case 0x09:    // 5 bit poly
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // Clock value out of P5 into P4 with no modification
            myP4[c] = (myP4[c] << 1) | ((myP5[c] & 0x20) ? 1 : 0);
            break;
          }

          case 0x0a:    // div 31
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // This does the divide-by 31 with length 13:18
            if((myP5[c] & 0x0f) == 0x08)
            {
              // Feed bit 4 of P5 into P4 (this will toggle back and forth)
              myP4[c] = (myP4[c] << 1) | ((myP5[c] & 0x10) ? 1 : 0);
            }
            break;
          }

          case 0x0b:    // Set last 4 bits to 1
          {
            // A 1 is shifted into the 4-bit register each clock
            myP4[c] = (myP4[c] << 1) | 0x01;
            break;
          }

          case 0x0c:    // div 6
          {
            // Use 4-bit register to generate sequence 000111000111
            myP4[c] = (~myP4[c] << 1) |
                ((!(!(myP4[c] & 4) && ((myP4[c] & 7)))) ? 0 : 1);
            break;
          }

          case 0x0d:    // div 6
          {
            // Use 4-bit register to generate sequence 000111000111
            myP4[c] = (~myP4[c] << 1) |
                ((!(!(myP4[c] & 4) && ((myP4[c] & 7)))) ? 0 : 1);
            break;
          }

          case 0x0e:    // div 31 -> div 6
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // This does the divide-by 31 with length 13:18
            if((myP5[c] & 0x0f) == 0x08)
            {
              // Use 4-bit register to generate sequence 000111000111
              myP4[c] = (~myP4[c] << 1) |
                  ((!(!(myP4[c] & 4) && ((myP4[c] & 7)))) ? 0 : 1);
            }
            break;
          }

          case 0x0f:    // poly 5 -> div 6
          {
            // Clock P5 as a standard 5-bit LSFR taps at bits 4 & 2
            myP5[c] = (myP5[c] & 0x1f) ?
              ((myP5[c] << 1) | (((myP5[c] & 0x10) ? 1 : 0) ^
              ((myP5[c] & 0x04) ? 1 : 0))) : 1;

            // Use poly 5 to clock 4-bit div register
            if(myP5[c] & 0x10)
            {
              // Use 4-bit register to generate sequence 000111000111
              myP4[c] = (~myP4[c] << 1) |
                  ((!(!(myP4[c] & 4) && ((myP4[c] & 7)))) ? 0 : 1);
            }
            break;
          }
        }
      }

      ret += (myP4[c] & 8) ? myAUDV[c] : 0;
    }

    return ret;
}

static void synth(void *unused, Uint8 *stream, int len) {
    int16_t *s16 = (int16_t*)stream;
    int x;

    for (x = 0; x < len/2; x++)
        samples.push_back(s16[x] = next_tia_sample());
}

static void setAUDC(int c) {
    int x;
    for (x = 0; x < C; x++)
        myAUDC[x] = c;
}

static void setAUDV(int v) {
    int x;
    for (x = 0; x < C; x++)
        myAUDV[x] = v;
}

static void write_l32(FILE *f, uint32_t a) {
    putc(a, f);
    putc(a>>8, f);
    putc(a>>16, f);
    putc(a>>24, f);
}

static void write_wav() {
    char name[256];

    sprintf(name, "%i-%i.wav", T, number);
    printf("Writing %li sample WAV to %s\n", samples.size(), name);

    FILE *wav = fopen(name, "wb");
    fprintf(wav, "RIFF");
    write_l32(wav, samples.size()*2 + 36);
    fprintf(wav, "WAVEfmt ");
    write_l32(wav, 16);
    write_l32(wav, 0x00010001);
    write_l32(wav, FREQ);
    write_l32(wav, FREQ*2);
    write_l32(wav, 0x00100002);
    fprintf(wav, "data");
    write_l32(wav, samples.size()*2);
    fwrite(&samples[0], samples.size()*2, 1, wav);
    fclose(wav);
}

static void write_audacity() {
    char name[256];

    sprintf(name, "%i-%i.txt", T, number);
    printf("Writing Audacity notes to %s\n", name);

    FILE *aud = fopen(name, "w");

    for (size_t x = 0; x < notes.size(); x++)
        fprintf(aud, "%f %f %s\n", notes[x].first, notes[x].first, notes[x].second.c_str());

    fclose(aud);
}

static void write_asm() {
    char name[256];

    sprintf(name, "%i-%i.asm", T, number);
    printf("Writing ASM data to %s\n", name);

    FILE *as = fopen(name, "w");

    for (size_t x = 0; x < notes.size(); x++)
        fprintf(as, "\t.byte %s\t;%f\n", notes[x].second.c_str(), notes[x].first);

    fclose(as);
}

static void print_help() {
    printf(
        "Keys 8-Z in a normal QWERTY matrix = AUDF 0..31\n"
        "Keypad 0-9 changes sound type\n"
        "Press 'enter' to save what you've played (WAV, Audacity labels and ASM data)\n"
        "Press 'space' to clear the current recording\n"
        "The current recording is also saved when the program is terminated (ESC)\n"
        "\n"
    );
}

int main() {
    int x;
    char name[256];

    SDL_AudioSpec fmt;
    SDL_Event event;

    print_help();
    T = time(NULL);

    /* need a window for the keyboard to work */
    SDL_SetVideoMode(320, 240, 0, 0);

    setAUDC(4);

    for (x = 0; x < C; x++)
        myAUDF[x] = x;

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
                float t = samples.size() / (float)FREQ;

                if (event.key.keysym.sym == SDLK_ESCAPE)
                    goto die;

                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_SPACE) {
                        /* clear */
                        printf("Recording cleared\n");
                        samples.clear();
                        notes.clear();
                    } else if (event.key.keysym.sym == SDLK_RETURN) {
                        write_wav();
                        write_audacity();
                        write_asm();
                        samples.clear();
                        notes.clear();
                        number++;
                    } else if (event.key.keysym.sym >= SDLK_KP0 && event.key.keysym.sym <= SDLK_KP9) {
                        int type = typetab[event.key.keysym.sym - SDLK_KP0];
                        printf("Switching to AUDC %i\n", type);
                        setAUDC(type);
                    }
                }

                for (x = 0; x < sizeof(keymap)/sizeof(keymap[0]); x++)
                    if (event.key.keysym.sym == keymap[x].key) {
                        if (event.type == SDL_KEYDOWN) {
                            char temp[32];
                            myAUDV[keymap[x].freq_inv ^ 31] = 1000;
                            sprint_binary(keymap[x].freq_inv ^ 31, temp);
                            printf("%s\n", temp);
                            notes.push_back(make_pair(t, temp));
                        } else
                            myAUDV[keymap[x].freq_inv ^ 31] = 0;
                    }
            } else if (event.type == SDL_QUIT)
                goto die;
        }

        usleep(10000);
    }
die:
    write_wav();
    write_audacity();
    write_asm();

    return 0;
}
