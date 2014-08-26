#define _POSIX_C_SOURCE 2
#include <errno.h> /* errno */
#include <fcntl.h> /* open(), O_WRONLY, O_CREAT */
#include <linux/soundcard.h> /* SOUND_PCM_WRITE_RATE */
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



static char * buf;
static int dsp;
static int dsp_rate;
static int dsp_channels;
static int dsp_bits;
static int wav;
static uint32_t wav_size;
static uint8_t writing_wave;



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



/* Write beep of frequency "freq" and length of 1 / "length_div". */
static void beep(uint8_t length_div, uint16_t freq)
{
    uint32_t size, size_cycle, marker_half_cycle, i, ii;
    size = dsp_rate / length_div;
    size_cycle = dsp_rate / freq;
    marker_half_cycle = size_cycle / 2;
    for (i = ii = 0; i < size; i++, ii++)
    {
        if (ii == size_cycle)
        {
            ii = 0;
        }
        buf[i] = ii < marker_half_cycle ? 0 : LOUDNESS;
    }
    exit_on_err(-1 == write(dsp, buf, size), "write() failed.");
    if (writing_wave)
    {
        exit_on_err(-1 == write(wav, buf, size), "write() failed.");
    }
    wav_size = wav_size + size;
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



/* Compose/play/record not quite random series of beeps. */
static void compose()
{
    uint16_t base_freq;
    uint8_t octave_n, freq_step;
    long double root_of_2;
    srand(time(0));
    root_of_2 = nth_root_of_2(N_STEPS_OCTAVE_TO_OCTAVE - 1);
    octave_n  = rand() % N_OCTAVES;
    freq_step = rand() % N_STEPS_OCTAVE_TO_OCTAVE;
    base_freq = get_base_octave(octave_n);
    while (1)
    {
        long double multiplier;
        uint16_t freq;
        uint8_t length_div;
        if (rand() % PROB_OCTAVE_CHANGE)
        {
            if      (0 == freq_step && octave_n)
            {
                octave_n--;
                base_freq = get_base_octave(octave_n);
            }
            else if (   N_STEPS_OCTAVE_TO_OCTAVE - 1 == freq_step
                     && N_OCTAVES - 1 > octave_n)
            {
                octave_n++;
                base_freq = get_base_octave(octave_n);
            }
        }
        freq_step = rand() % N_STEPS_OCTAVE_TO_OCTAVE;
        multiplier = to_power_of(root_of_2, freq_step);
        freq = base_freq * multiplier;
        length_div = 1 + rand() % (MAX_LENGTH_DIVISOR - 1);
        printf("freq %5d (base %5d step %3d multiply %d/100000) length 1/%3d\n",
               freq,base_freq,freq_step,(int)(multiplier*100000),length_div);
        beep(length_div, freq);
    }
}



/* Little-endian write of 16 bit "value" to wav file, "err" as error message. */
static void write_little_endian_16(uint16_t value, char * err)
{
    uint8_t byte;
    byte = value;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
    byte = value >> 8;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
}



/* Little-endian write of 32 bit "value" to wav file, "err" as error message. */
static void write_little_endian_32(uint32_t value, char * err)
{
    uint8_t byte;
    byte = value;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
    byte = value >> 8;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
    byte = value >> 16;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
    byte = value >> 24;
    exit_on_err(-1 == write(wav, &byte, 1), err);;
}



/* Write wav file header; use "err_wri" for error message. */
static void write_wav_header(char * err_wri)
{
    exit_on_err(-1 == write(wav, "RIFF", 4), err_wri);
    write_little_endian_32(UINT32_MAX, err_wri);
    exit_on_err(-1 == write(wav, "WAVE", 4), err_wri);
    exit_on_err(-1 == write(wav, "fmt ", 4), err_wri);
    write_little_endian_32(16, err_wri);
    write_little_endian_16(1, err_wri);
    write_little_endian_16(dsp_channels, err_wri);
    write_little_endian_32(dsp_rate, err_wri);
    write_little_endian_32(dsp_rate * dsp_channels * dsp_bits, err_wri);
    write_little_endian_16(dsp_channels * dsp_bits, err_wri);
    write_little_endian_16(8, err_wri);
    exit_on_err(-1 == write(wav, "data", 4), err_wri);
    write_little_endian_32(UINT32_MAX, err_wri);
}



static void setup_dsp(char * err_o)
{
    char * err_ioc;
    err_ioc = "ioctl() failed.";
    dsp = open("/dev/dsp", O_WRONLY);
    exit_on_err(-1 == dsp, err_o);
    dsp_rate = DSP_RATE_TARGET;
    exit_on_err(0>ioctl(dsp, SOUND_PCM_WRITE_RATE, &dsp_rate), err_ioc);
    printf("samples per second: %d\n", dsp_rate);
    dsp_channels = 1;
    exit_on_err(0>ioctl(dsp, SOUND_PCM_WRITE_CHANNELS, &dsp_channels), err_ioc);
    printf("channels: %d\n", dsp_channels);
    dsp_bits = 1;
    exit_on_err(0>ioctl(dsp, SOUND_PCM_WRITE_BITS, &dsp_bits), err_ioc);
    printf("bytes per frame: %d\n", dsp_channels);
}



static void obey_argv(int argc, char ** argv)
{
    int opt;
    while (-1 != (opt = getopt(argc, argv, "w")))
    {
        if ('w' == opt)
        {
            writing_wave = 1;
        }
        else
        {
            exit(EXIT_FAILURE);
        }
    }

}



int main(int argc, char ** argv)
{
    char * err_o, * err_wri;

    /* Read command line arguments for setting wave file writing. */
    writing_wave = 0;
    obey_argv(argc, argv);

    /* Set up /dev/dsp device and wav file . */
    err_o = "open() failed.";
    setup_dsp(err_o);
    if (writing_wave)
    {
        err_wri = "Trouble with write().";
        wav = open("out.wav", O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        exit_on_err(-1 == wav, err_o);
        write_wav_header(err_wri);
        wav_size = 44;
    }

    /* Allocate memory for max. 1 second of playback. */
    buf = malloc(dsp_rate);
    exit_on_err(!buf, "malloc() failed.");

    /* Music composition loop. */
    compose();

    /* Clean up. (So far, is not actually called, ever.) */
    free(buf);
    if (writing_wave)
    {
        exit_on_err(-1 == lseek(wav, 4, SEEK_SET), "lseek() failed.");
        write_little_endian_32(wav_size - 8, err_wri);
        exit_on_err(-1 == lseek(wav, 40, SEEK_SET), "lseek() failed.");
        write_little_endian_32(wav_size - 44, err_wri);
        exit_on_err(-1 == close(wav), "close() failed.");
    }
    exit_on_err(-1 == close(dsp), "close() failed.");
    return(0);
}
