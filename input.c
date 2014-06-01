/*
Copyright (c) 2013-2014 René Ladan. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include "input.h"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <sys/param.h>

#ifdef __FreeBSD__
#  if __FreeBSD_version >= 900022
#    include <sys/gpio.h>
#  else
#    define NOLIVE 1
#  endif
#elif defined(__NetBSD_Version__)
#  define NOLIVE 1
#elif defined(__linux__)
#  include <sys/types.h>
#else
#  error Unsupported operating system, please send a patch to the author
#endif

uint8_t bitpos; /* second */
uint8_t buffer[60]; /* wrap after 60 positions */
uint16_t state; /* any errors, or high bit */
FILE *datafile = NULL; /* input file (recorded data) */
FILE *logfile = NULL; /* auto-appended in live mode */
int fd = 0; /* gpio file */
struct hardware hw;
struct bitinfo bit;

int
init_hardware(int pin_nr)
{
#ifdef __FreeBSD__
#ifndef NOLIVE
	struct gpio_pin pin;

	fd = open("/dev/gpioc0", O_RDONLY);
	if (fd < 0) {
		perror("open (/dev/gpioc0)");
		return -errno;
	}

	pin.gp_pin = pin_nr;
	if (ioctl(fd, GPIOGETCONFIG, &pin) < 0) {
		perror("ioctl(GPIOGETCONFIG)");
		return -errno;
	}
#endif
#elif defined(__linux__)
	char buf[64];
	int res;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) {
		perror("open (/sys/class/gpio/export)");
		return -errno;
	}
	res = snprintf(buf, sizeof(buf), "%d", pin_nr);
	if (res < 0 || res > sizeof(buf)-1) {
		printf("pin_nr too high? (%i)\n", res);
		return -1;
	}
	if (write(fd, buf, res) < 0) {
		perror("write(export)");
		if (errno != EBUSY)
			return -errno; /* EBUSY -> pin already exported ? */
	}
	if (close(fd) == -1) {
		perror("close(export)");
		return -errno;
	}
	res = snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction",
	    pin_nr);
	if (res < 0 || res > sizeof(buf)-1) {
		printf("pin_nr too high? (%i)\n", res);
		return -1;
	}
	fd = open(buf, O_WRONLY);
	if (fd < 0) {
		perror("open (direction)");
		return -errno;
	}
	if (write(fd, "in", 3) < 0) {
		perror("write(in)");
		return -errno;
	}
	if (close(fd) == -1) {
		perror("close(direction)");
		return -errno;
	}
	res = snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value",
	    pin_nr);
	if (res < 0 || res > sizeof(buf)-1) {
		printf("pin_nr too high? (%i)\n", res);
		return -1;
	}
	fd = open(buf, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open (value)");
		return -errno;
	}
#endif
	return fd;
}

void
set_state_vars(void)
{
	bitpos = 0;
	state = 0;
	bzero(buffer, sizeof(buffer));
}

int
set_mode_file(char *infilename)
{
	set_state_vars();
	datafile = fopen(infilename, "r");
	if (datafile == NULL) {
		perror("fopen (datafile)");
		return errno;
	}
	return 0;
}

int
set_mode_live(void)
{
	int res;

	set_state_vars();
#ifdef NOLIVE
	printf("No GPIO interface available, disabling live decoding\n");
	cleanup();
	return -1;
#else
	/* fill hardware structure and initialize hardware */
	hw.pin = strtol(get_config_value("pin"), NULL, 10);
	res = init_hardware(hw.pin);
	if (res < 0) {
		cleanup();
		return res;
	}
	hw.active_high = strtol(get_config_value("activehigh"), NULL, 10);
	hw.freq = strtol(get_config_value("freq"), NULL, 10);
#endif
	return 0;
}

void
cleanup(void)
{
	if (fd > 0 && close(fd) == -1)
#ifdef __FreeBSD__
		perror("close (/dev/gpioc0)");
#elif defined(__linux__)
		perror("close (/sys/class/gpio/*)");
#endif
	fd = 0;
	if (logfile != NULL && fclose(logfile) == EOF)
		perror("fclose (logfile)");
	logfile = NULL;
	if (datafile != NULL && fclose(datafile) == EOF)
		perror("fclose (datafile)");
	datafile = NULL;
}

uint8_t
get_pulse(void)
{
	uint8_t tmpch = 0;
	int count = 0;
#ifdef __FreeBSD__
#ifndef NOLIVE
	struct gpio_req req;

	req.gp_pin = hw.pin;
	count = ioctl(fd, GPIOGET, &req);
	tmpch = (req.gp_value == GPIO_PIN_HIGH) ? 1 : 0;
	if (count < 0)
#endif
#elif defined(__linux__)
	count = read(fd, &tmpch, sizeof(tmpch));
	tmpch -= '0';
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
		return GETBIT_IO; /* rewind to prevent EBUSY/no read */
	if (count != sizeof(tmpch))
#endif
		return GETBIT_IO; /* hardware failure? */

	if (!hw.active_high)
		tmpch = 1 - tmpch;
	return tmpch;
}

uint16_t
get_bit_live(void)
{
	/*
	 * The bits are decoded from the signal using an exponential low-pass
	 * filter in conjunction with a Schmitt trigger. The idea and the
	 * initial implementation for this come from Udo Klein, with permission.
	 * http://blog.blinkenlight.net/experiments/dcf77/binary-clock/#comment-5916
	 */

	char outch;
	int newminute;
	uint8_t p, stv;
	struct timespec slp;
	float y;
	static int init = 1;

	bit.freq_reset = 0;
	bit.frac = bit.maxone = -0.01;

	/*
	 * Clear previous flags, except GETBIT_TOOLONG to be able
	 * to determine if this flag can be cleared again.
	 */
	state = (state & GETBIT_TOOLONG) ? GETBIT_TOOLONG : 0;

	/*
	 * One period is either 1000 ms or 2000 ms long (normal or
	 * padding for last). The active part is either 100 ms ('0')
	 * or 200 ms ('1') long, the maximum allowed values as
	 * percentage of the second length are specified with maxone/2
	 * and maxone respectively.
	 *
	 *  ~A > 1.5 * realfreq: value |= GETBIT_EOM
	 *  ~A > 2.5 * realfreq: timeout
	 */

	if (init == 1) {
		bit.realfreq = hw.freq;
		bit.bit0 = 0.1 * bit.realfreq;
		bit.bit20 = 0.2 * bit.realfreq;
	}
	/*
	 * Set up filter, reach 50% after realfreq/20 samples (i.e. 50 ms)
	 */
	bit.a = 1.0 - exp2(-1.0 / (bit.realfreq / 20.0));
	y = -1;
	bit.tlow = 0;
	stv = 2;

	for (bit.t = 0; ; bit.t++) {
		p = get_pulse();
		if (p == GETBIT_IO) {
			state |= GETBIT_IO;
			outch = '*';
			goto report;
		}

		y = y < 0 ? (float)p : y + bit.a * (p - y);
		if (stv == 2)
			stv = p;

		/*
		 * Prevent algorithm collapse during thunderstorms
		 * or scheduler abuse
		 */
		if (bit.realfreq < hw.freq / 2 ||
		    bit.realfreq > hw.freq * 3/2) {
			if (logfile != NULL)
				fprintf(logfile, bit.realfreq < hw.freq / 2 ?
				    "<" : ">");
			bit.realfreq = hw.freq;
			bit.freq_reset = 1;
		}

		if (bit.t > bit.realfreq * 5/2) {
			bit.realfreq = bit.realfreq + 0.05 *
			    ((bit.t / 2.5) - bit.realfreq);
			bit.a = 1.0 - exp2(-1.0 / (bit.realfreq / 20.0));
			if (bit.tlow * 100 / bit.t < 1) {
				state |= GETBIT_RECV;
				outch = 'r';
			} else if (bit.tlow * 100 / bit.t >= 99) {
				state |= GETBIT_XMIT;
				outch = 'x';
			} else {
				state |= GETBIT_RND;
				outch = '#';
			}
			goto report; /* timeout */
		}

		/*
		 * Schmitt trigger, maximize value to introduce
		 * hysteresis and to avoid infinite memory.
		 */
		if (y < 0.5 && stv == 1) {
			y = 0.0;
			stv = 0;
			bit.tlow = bit.t; /* end of high part of second */
		}
		if (y > 0.5 && stv == 0) {
			y = 1.0;
			stv = 1;

			newminute = bit.t > bit.realfreq * 3/2;
			if (init == 1)
				init = 2;
			else {
				if (newminute)
					bit.realfreq = bit.realfreq + 0.05 *
					    ((bit.t/2) - bit.realfreq);
				else
					bit.realfreq = bit.realfreq + 0.05 *
					    (bit.t - bit.realfreq);
				bit.a = 1.0 - exp2(-1.0 / (bit.realfreq / 20.0));
			}

			bit.frac = (float)bit.tlow / (float)bit.t;
			if (newminute) {
				bit.frac *= 2;
				state |= GETBIT_EOM;
			}
			break; /* start of new second */
		}
		slp.tv_sec = 0;
		slp.tv_nsec = 1e9 / hw.freq / hw.freq * bit.realfreq;
		while (nanosleep(&slp, &slp))
			;
	}

	bit.maxone = (bit.bit0 + bit.bit20) / bit.realfreq;
	if (bit.frac < 0) {
		/* radio error, results already set */
	} else if (bit.frac <= bit.maxone / 2.0) {
		/* zero bit, ~100 ms active signal */
		outch = '0';
		buffer[bitpos] = 0;
	} else if (bit.frac <= bit.maxone) {
		/* one bit, ~200 ms active signal */
		state |= GETBIT_ONE;
		outch = '1';
		buffer[bitpos] = 1;
	} else {
		/* bad radio signal, retain old value */
		state |= GETBIT_READ;
		outch = '_';
	}
	if (init == 2)
		init = 0;
	else {
		if (bitpos == 0 && buffer[0] == 0)
			bit.bit0 = bit.bit0 + 0.5 * (bit.tlow - bit.bit0);
		if (bitpos == 20 && buffer[20] == 1)
			bit.bit20 = bit.bit20 + 0.5 * (bit.tlow - bit.bit20);
	}
report:
	if (logfile != NULL)
		fprintf(logfile, "%c%s", outch, state & GETBIT_EOM ? "\n" : "");
	return state;
}

#define TRYCHAR \
	oldinch = inch; \
	inch = getc(datafile); \
	if (feof(datafile)) \
		return state; \
	if ((inch != '\r' && inch != '\n') || inch == oldinch) { \
		if (ungetc(inch, datafile) == EOF) /* EOF remains, IO error */\
			state |= GETBIT_EOD; \
	}

uint16_t
get_bit_file(void)
{
	int oldinch, inch, valid = 0;

	/*
	 * Clear previous flags, except GETBIT_TOOLONG to be able
	 * to determine if this flag can be cleared again.
	 */
	state = (state & GETBIT_TOOLONG) ? GETBIT_TOOLONG : 0;

	while (valid == 0) {
		inch = getc(datafile);
		switch (inch) {
		case EOF:
			state |= GETBIT_EOD;
			return state;
		case '0':
		case '1':
			buffer[bitpos] = (uint8_t)(inch - '0');
			if (inch == '1')
				state |= GETBIT_ONE;
			valid = 1;
			break;
		case '\r':
		case '\n':
			/* handle multiple consecutive EOM markers */
			state |= GETBIT_EOM; /* otherwise empty bit */
			valid = 1;
			break;
		case 'x':
			state |= GETBIT_XMIT;
			valid = 1;
			break;
		case 'r':
			state |= GETBIT_RECV;
			valid = 1;
			break;
		case '#':
			state |= GETBIT_RND;
			valid = 1;
			break;
		case '*':
			state |= GETBIT_IO;
			valid = 1;
			break;
		case '_':
			/* retain old value in buffer[bitpos] */
			state |= GETBIT_READ;
			valid = 1;
			break;
		default:
			break;
		}
	}
	/* Only allow \r , \n , \r\n , and \n\r as single EOM markers */
	TRYCHAR else {
		state |= GETBIT_EOM;
		/* Check for B\r\n or B\n\r */
		TRYCHAR
	}
	return state;
}

int
is_space_bit(int bitpos)
{
	return (bitpos == 1 || bitpos == 15 || bitpos == 16 || bitpos == 19 ||
	    bitpos == 20 || bitpos == 21 || bitpos == 28 || bitpos == 29 ||
	    bitpos == 35 || bitpos == 36 || bitpos == 42 || bitpos == 45 ||
	    bitpos == 50 || bitpos == 58 || bitpos == 59 || bitpos == 60);
}

uint16_t
next_bit(void)
{
	bitpos = (state & GETBIT_EOM) ? 0 : bitpos + 1;
	if (bitpos == sizeof(buffer)) {
		state |= GETBIT_TOOLONG;
		bitpos = 0;
		return state;
	}
	state &= ~GETBIT_TOOLONG; /* fits again */
	return state;
}

uint8_t
get_bitpos(void)
{
	return bitpos;
}

uint8_t *
get_buffer(void)
{
	return buffer;
}

struct hardware *
get_hardware_parameters(void)
{
	return &hw;
}

int
write_new_logfile(char *logfilename)
{
	logfile = fopen(logfilename, "a");
	if (logfile == NULL)
		return errno;
	fprintf(logfile, "\n--new log--\n\n");
	return 0;
}

int
close_logfile(void)
{
	int f;
	f = fclose(logfile);
	return (f == EOF) ? errno : 0;
}

struct bitinfo *
get_bitinfo(void)
{
	return &bit;
}
