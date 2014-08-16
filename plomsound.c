#include <errno.h> /* errno */
#include <fcntl.h> /* open() */
#include <linux/soundcard.h> /* SOUND_PCM_WRITE_RATE */
#include <stdint.h> /* uint32_t */
#include <stdio.h> /* printf(), perror() */
#include <stdlib.h> /* exit(), free(), malloc(), rand(), srand(), EXIT_FAILURE*/
#include <string.h> /* memset() */
#include <sys/ioctl.h> /* ioctl(), O_WRONLY */
#include <time.h> /* time() */
#include <unistd.h> /* close(), write() */



#define N_OCTAVES 8
#define N_STEPS_OCTAVE_TO_OCTAVE 8
#define LOUDNESS 8
#define MAX_LENGTH_DIVISOR 16
#define DSP_RATE_TARGET 192000
#define START_FREQ 32
#define PROB_OCTAVE_CHANGE 2



static char * buf;
static int dsp;
uint32_t dsp_rate;



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



/* Play beep of frequency "freq" and length of 1 / "length_div". */
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



/* Compose not quite random series of beeps. */
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



int main()
{
    /* Set up /dev/dsp devices to 192k bitrate. */
    dsp = open("/dev/dsp", O_WRONLY);
    exit_on_err(-1 == dsp, "open() failed.");
    dsp_rate = DSP_RATE_TARGET;
    exit_on_err(0>ioctl(dsp,SOUND_PCM_WRITE_RATE,&dsp_rate), "ioctl() failed.");

    /* Allocate memory for max. 1 second of playback. */
    buf = malloc(dsp_rate);
    exit_on_err(!buf, "malloc() failed.");

    /* Music composition loop. */
    compose();

    /* Clean up. (So far, is not actually called, ever.) */
    free(buf);
    exit_on_err(-1 == close(dsp), "close() failed.");
    return(0);
}
