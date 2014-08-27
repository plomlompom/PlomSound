#define _POSIX_C_SOURCE 2 /* getopt(), sigaction() */
#include <errno.h> /* errno */
#include <fcntl.h> /* open(), O_WRONLY, O_CREAT */
#include <linux/soundcard.h> /* SOUND_PCM_WRITE_RATE */
#include <signal.h> /* sigaction(), SIGINT */
#include <stdint.h> /* uint8_t, uint16_t, uint32_t, UINT32_MAX */
#include <stdio.h> /* printf(), perror() */
#include <stdlib.h> /* exit(), free(), malloc(), rand(), srand(), EXIT_FAILURE*/
#include <string.h> /* memset() */
#include <sys/ioctl.h> /* ioctl() */
#include <sys/stat.h> /* S_IRWXU, S_IRWXG, S_IRWXO*/
#include <time.h> /* time() */
#include <unistd.h> /* close(), write(), lseek(), optarg, getopt() */



#define N_OCTAVES 8
#define N_STEPS_OCTAVE_TO_OCTAVE 8
#define LOUDNESS 8
#define MAX_LENGTH_DIVISOR 16
#define DSP_RATE_TARGET 48000
#define START_FREQ 32
#define PROB_OCTAVE_CHANGE 2



struct mem_sound
{
    struct mem_sound * next;
    uint8_t length_div;
    uint8_t octave_n;
    uint8_t freq_step;
};

static struct dsp {
    int file;
    uint32_t rate;
    uint8_t channels;
    uint8_t bits;
} dsp;

static struct wav {
    int file;
    uint32_t size;
    uint8_t active;
} wav;

static uint8_t sigint_called;

static uint8_t compose_select;



/* If "err" == 0, do nothing. Else, exit with an error message that consists of
 * "msg" and of errno's content (if it is non-zero).
 */
static void exit_on_err(int err, const char * msg)
{
    if (0 == err)
    {
        return;
    }
    printf("Aborted program due to error. %s\n", msg);
    if (0 != errno)
    {
        perror("errno states");
    }
    exit(EXIT_FAILURE);
}



/* Write beep of frequency "freq" and length of 1 / "length_div" via "buf". */
static void beep(char * buf, uint8_t length_div, uint16_t freq)
{
    uint32_t size, size_cycle, marker_half_cycle, i, ii;
    size = dsp.rate / length_div;
    size_cycle = dsp.rate / freq;
    marker_half_cycle = size_cycle / 2;
    for (i = ii = 0; i < size; i++, ii++)
    {
        if (ii == size_cycle)
        {
            ii = 0;
        }
        buf[i] = ii < marker_half_cycle ? 0 : LOUDNESS;
    }
    exit_on_err(-1 == write(dsp.file, buf, size), "write() failed.");
    if (wav.active)
    {
        exit_on_err(-1 == write(wav.file, buf, size), "write() failed.");
    }
    wav.size = wav.size + size;
}



/* Return "octave_n"-th octave from start frequency (START_FREQ). */
static uint32_t get_base_octave(uint8_t octave_n)
{
        uint16_t multiplier;
        uint8_t  i;
        multiplier = 1;
        for (i = 0; i < octave_n; i++)
        {
            multiplier = multiplier * 2;
        }
        return START_FREQ * multiplier;
}



/* Return power of "base" to "exponent". */
static long double to_power_of(long double base, uint8_t exponent)
{
    uint8_t i;
    long double result = 1;
    for (i = 0; i < exponent; i++)
    {
        result = result * base;
    }
    return result;
}



/* Return "degree"-th root of 2. Needs a "degree" > 0 to work properly. */
static long double nth_root_of_2(uint8_t degree)
{
    long double guess_old, guess_new;
    guess_old = 0;
    guess_new = 1;
    while (guess_old != guess_new)
    {
        guess_old = guess_new;
        guess_new = guess_old -   (to_power_of(guess_old, degree) - 2)
                                / (degree * (to_power_of(guess_old, degree-1)));
    }
    return guess_new;
}




/* Add at "pp_mem_sound" sound of random octavem step and length. */
static void add_mem_sound(struct mem_sound ** pp_mem_sound)
{
    struct mem_sound * new_mem_sound = malloc(sizeof(struct mem_sound));
    exit_on_err(!new_mem_sound, "malloc() failed.");
    new_mem_sound->freq_step = rand() % N_STEPS_OCTAVE_TO_OCTAVE;
    new_mem_sound->length_div = 1 + rand() % (MAX_LENGTH_DIVISOR - 1);
    new_mem_sound->octave_n = rand() % N_OCTAVES;
    new_mem_sound->next = NULL;
    *pp_mem_sound = new_mem_sound;
}



/* Iterate over growing sound series for new sound, add to it at the end. */
static void bar(uint8_t * length_div, uint8_t * octave_n, uint8_t * freq_step)
{
    static struct mem_sound * mem_sounds = NULL;
    static struct mem_sound * selected_mem_sound = NULL;
    if (!mem_sounds)
    {
        add_mem_sound(&mem_sounds);
        selected_mem_sound = mem_sounds;
    }
    *freq_step  = selected_mem_sound->freq_step;
    *length_div = selected_mem_sound->length_div;
    *octave_n   = selected_mem_sound->octave_n;
    if (!selected_mem_sound->next)
    {
        add_mem_sound(&(selected_mem_sound->next));
        selected_mem_sound = mem_sounds;
    }
    else
    {
        selected_mem_sound = selected_mem_sound->next;
    }
}



/* Set sound of random length and in-octave step, change octave at extremes. */
static void foo(uint8_t * length_div, uint8_t * octave_n, uint8_t * freq_step)
{
    if (!(*length_div))
    {
        *octave_n = rand() % N_OCTAVES;
    }
    if (rand() % PROB_OCTAVE_CHANGE)
    {
        if      (0 == *freq_step && *octave_n)
        {
            (*octave_n)--;
        }
        else if (   N_STEPS_OCTAVE_TO_OCTAVE - 1 == *freq_step
                 && N_OCTAVES - 1 > *octave_n)
        {
            (*octave_n)++;
        }
    }
    *freq_step = rand() % N_STEPS_OCTAVE_TO_OCTAVE;
    *length_div = 1 + rand() % (MAX_LENGTH_DIVISOR - 1);
}



/* Compose/play/record not quite random series of beeps. */
static void compose()
{
    static char * buf;
    uint8_t octave_n, freq_step, length_div;
    long double root_of_2;
    srand(time(0));
    root_of_2 = nth_root_of_2(N_STEPS_OCTAVE_TO_OCTAVE - 1);
    length_div = octave_n = freq_step = 0;
    buf = malloc(dsp.rate);
    exit_on_err(!buf, "malloc() failed.");
    for (sigint_called = 0; !sigint_called; )
    {
        long double multiplier;
        uint16_t freq, base_freq;
        if (!compose_select)
        {
            foo(&length_div, &octave_n, &freq_step);
        }
        else
        {
            bar(&length_div, &octave_n, &freq_step);
        }
        base_freq = get_base_octave(octave_n);
        multiplier = to_power_of(root_of_2, freq_step);
        freq = base_freq * multiplier;
        printf("freq %5d (base %5d step %3d multiply %d/100000) length 1/%3d\n",
               freq,base_freq,freq_step,(int)(multiplier*100000),length_div);
        beep(buf, length_div, freq);
    }
    free(buf);
}



/* Little-endian write of 16 bit "value" to wav file, "err" as error message. */
static void write_little_endian_16(uint16_t value, char * err)
{
    uint8_t byte;
    byte = value;
    exit_on_err(-1 == write(wav.file, &byte, 1), err);;
    byte = value >> 8;
    exit_on_err(-1 == write(wav.file, &byte, 1), err);;
}



/* Little-endian write of 32 bit "value" to wav file, "err" as error message. */
static void write_little_endian_32(uint32_t value, char * err)
{
    uint16_t double_byte;
    double_byte = value;
    write_little_endian_16(double_byte, err);
    double_byte = value >> 16;
    write_little_endian_16(double_byte, err);
}



/* Write wav file header; use "err_wri" for error message. */
static void write_wav_header(char * err_wri)
{
    exit_on_err(-1 == write(wav.file, "RIFF", 4), err_wri);
    write_little_endian_32(UINT32_MAX, err_wri);
    exit_on_err(-1 == write(wav.file, "WAVE", 4), err_wri);
    exit_on_err(-1 == write(wav.file, "fmt ", 4), err_wri);
    write_little_endian_32(16, err_wri);
    write_little_endian_16(1, err_wri);
    write_little_endian_16(dsp.channels, err_wri);
    write_little_endian_32(dsp.rate, err_wri);
    write_little_endian_32(dsp.rate * dsp.channels * dsp.bits, err_wri);
    write_little_endian_16(dsp.channels * dsp.bits, err_wri);
    write_little_endian_16(8, err_wri);
    exit_on_err(-1 == write(wav.file, "data", 4), err_wri);
    write_little_endian_32(UINT32_MAX, err_wri);
}



/* Set up /dev/dsp device. */
static void setup_dsp(char * err_o)
{
    char * err;
    err = "ioctl() failed.";
    dsp.file = open("/dev/dsp", O_WRONLY);
    exit_on_err(-1 == dsp.file, err_o);
    dsp.rate = DSP_RATE_TARGET;
    exit_on_err(0>ioctl(dsp.file, SOUND_PCM_WRITE_RATE, &dsp.rate), err);
    printf("samples per second: %d\n", dsp.rate);
    dsp.channels = 1;
    exit_on_err(0>ioctl(dsp.file, SOUND_PCM_WRITE_CHANNELS, &dsp.channels), err);
    printf("channels: %d\n", dsp.channels);
    dsp.bits = 1;
    exit_on_err(0>ioctl(dsp.file, SOUND_PCM_WRITE_BITS, &dsp.bits), err);
    printf("bytes per frame: %d\n", dsp.channels);
}



/* Read command line options. */
static void obey_argv(int argc, char ** argv)
{
    int opt;
    while (-1 != (opt = getopt(argc, argv, "wn")))
    {
        if      ('w' == opt)
        {
            wav.active = 1;
        }
        else if ('n' == opt)
        {
            compose_select = 1;
        }
        else
        {
            exit(EXIT_FAILURE);
        }
    }

}



/* Simply set sigint_called to 1. */
static void sigint_handler()
{
    sigint_called = 1;
}



int main(int argc, char ** argv)
{
    char * err_o, * err_wri;
    struct sigaction act;

    /* Default and read command line options. */
    wav.active = 0;
    compose_select = 0;
    obey_argv(argc, argv);

    /* Set up /dev/dsp device and wav file . */
    err_o = "open() failed.";
    setup_dsp(err_o);
    if (wav.active)
    {
        err_wri = "Trouble with write().";
        wav.file = open("out.wav", O_WRONLY | O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);
        exit_on_err(-1 == wav.file, err_o);
        write_wav_header(err_wri);
        wav.size = 44;
    }

    /* Set handler for SIGINT signal. */
    memset(&act, 0, sizeof(act));
    act.sa_handler = &sigint_handler;
    exit_on_err(sigaction(SIGINT, &act, NULL), "Trouble with sigaction().");

    /* Music composition loop. */
    compose();

    /* Clean up. */
    if (wav.active)
    {
        exit_on_err(-1 == lseek(wav.file, 4, SEEK_SET), "lseek() failed.");
        write_little_endian_32(wav.size - 8, err_wri);
        exit_on_err(-1 == lseek(wav.file, 40, SEEK_SET), "lseek() failed.");
        write_little_endian_32(wav.size - 44, err_wri);
        exit_on_err(-1 == close(wav.file), "close() failed.");
    }
    exit_on_err(-1 == close(dsp.file), "close() failed.");
    return(0);
}
