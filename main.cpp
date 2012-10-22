#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <stdio.h>
#include <time.h>
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <vector>
#include <string>
#include <set>
#include <sstream>

using namespace std;

#define FREQ 31400
#define C 32

#include "tables.c"

static int counters[C] = {0};

//code mostly ripped from Stella
static uint8_t myAUDC[C] = {0};
static uint8_t myAUDF[C] = {0};
static int myAUDV[C] = {0};
static uint8_t myP4[C];           // 4-bit register LFSR (lower 4 bits used)
static uint8_t myP5[C];           // 5-bit register LFSR (lower 5 bits used)
static set<int> audcSet;          //AUDC values present in the current recording

#include "tiasnd.c"

struct mark {
    float t;
    string binary, note;
    int type, freq;
};

static vector<mark> notes;
static vector<int16_t> samples;
static int T;                   /* when the program was started */
static int number = 0;
#define FPS 50
static int frame = 0;

static void sprint_note(int type, int freq, char *out) {
    //TODO: we need a lengthy table for this..
    int t = audcnotesnamemap[typetab[type]];

    if (t < 0 || !notedesc[t][freq].name[0]) strcpy(out, "");
    else       sprintf(out, "%-3s %-+3i", notedesc[t][freq].name, notedesc[t][freq].tuning);
}

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

static void write_wav(string base) {
    char name[256];

    sprintf(name, "%s%i-%i.wav", base.c_str(), T, number);
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

static void write_audacity(string base) {
    char name[256];

    sprintf(name, "%s%i-%i.txt", base.c_str(), T, number);
    printf("Writing Audacity notes to %s\n", name);

    FILE *aud = fopen(name, "w");

    for (size_t x = 0; x < notes.size(); x++)
        fprintf(aud, "%f %f %s\n", notes[x].t, notes[x].t, notes[x].binary.c_str());

    fclose(aud);
}

static void write_asm(string base) {
    char name[256];

    sprintf(name, "%s%i-%i.asm", base.c_str(), T, number);
    printf("Writing ASM data to %s\n", name);

    FILE *as = fopen(name, "w");

    for (size_t x = 0; x < notes.size(); x++)
        fprintf(as, "\t.byte %s\t; %s %.2f\n", notes[x].binary.c_str(), notes[x].note.c_str(), notes[x].t);

    fclose(as);
}

static void print_help() {
    printf(
        "Keys 8-Z in a normal QWERTY matrix = AUDF 0..31\n"
        "Keypad 0-9 and page up/down changes sound type\n"
        "Press 'enter' to save what you've played (WAV, Audacity labels and ASM data)\n"
        "Press 'space' to clear the current recording\n"
        "\n"
    );
}

void setCurtype(int value, int *curtype) {
    //wrap around
    if (value < 0) value = 9;
    if (value > 9) value = 0;

    *curtype = value;
    int type = typetab[*curtype];
    printf("Switching to AUDC %i\n", type);
    setAUDC(type);
}

int main(int argc, char **argv) {
    int x;
    char name[256];
    int curtype = 3;

    SDL_AudioSpec fmt;
    SDL_Event event;

    print_help();
    T = time(NULL);

    /* need a window for the keyboard to work */
    SDL_SetVideoMode(320, 240, 0, 0);

    setAUDC(typetab[curtype]);

    for (x = 0; x < C; x++)
        myAUDF[x] = x;

    fmt.freq = FREQ;
    fmt.format = AUDIO_S16;
    fmt.channels = 1;
#ifdef WIN32
    fmt.samples = 512;
#else
    fmt.samples = 128;
#endif
    fmt.callback = synth;
    fmt.userdata = NULL;

    if (SDL_OpenAudio(&fmt, NULL) < 0)
        return 1;

    SDL_PauseAudio(0);

    for(;;) {
        float t = samples.size() / (float)FREQ;
        int f = t * FPS;

        if (f != frame) {
            frame = f;

            for (x = 0; x < C; x++) {
                if (myAUDV[x] <= 0 || myAUDV[x] >= 8000)
                    continue;

                if ((myAUDV[x] -= 1000) < 0)
                    myAUDV[x] = 0;
            }
        }

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                int x;
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    goto die;

                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_SPACE) {
                        /* clear */
                        printf("Recording cleared\n");
                        samples.clear();
                        notes.clear();
                        audcSet.clear();
                    } else if (event.key.keysym.sym == SDLK_RETURN) {
                        if (audcSet.size()) {
                            ostringstream oss;
                            for (set<int>::iterator it = audcSet.begin(); it != audcSet.end(); it++)
                                oss << *it << "-";

                            write_wav(oss.str());
                            write_audacity(oss.str());
                            write_asm(oss.str());
                        }

                        samples.clear();
                        notes.clear();
                        number++;
                    } else if (event.key.keysym.sym >= SDLK_KP0 && event.key.keysym.sym <= SDLK_KP9) {
                        setCurtype(event.key.keysym.sym - SDLK_KP0, &curtype);
                    } else if (event.key.keysym.sym == SDLK_PAGEUP)
                       setCurtype(curtype+1, &curtype);
                    else if (event.key.keysym.sym == SDLK_PAGEDOWN)
                        setCurtype(curtype-1, &curtype);
                }

                for (x = 0; x < sizeof(keymap)/sizeof(keymap[0]); x++)
                    if (event.key.keysym.sym == keymap[x].key) {
                        if (event.type == SDL_KEYDOWN) {
                            char temp[32];
                            mark m;
                            m.freq = keymap[x].freq_inv ^ 31;
                            m.type = curtype;
                            m.t = t;
                            audcSet.insert(typetab[curtype]);

                            myAUDV[m.freq] = 8000;
                            sprint_binary(m.freq, temp);

                            printf("%s ", temp);
                            m.binary = temp;

                            sprint_note(m.type, m.freq, temp);
                            printf("%s\n", temp);
                            m.note = temp;

                            notes.push_back(m);
                        } else
                            myAUDV[keymap[x].freq_inv ^ 31] = 7000;
                    }
            } else if (event.type == SDL_QUIT)
                goto die;
        }

#ifdef WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
die:
    return 0;
}
