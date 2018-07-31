/**************************************************************
 Name        : am2301.c
 Version     : 0.1
 Info        : http://picoboard.pl/raspberry-pi-obsluga-czujnika-temperatury-wilgotnosci-dht21-am2301/

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
***************************************************************************/

#include <wiringPi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>

/* Connect DATA to GPIO24 (pin 18). In wiringPi pin number is 5 */
static const int _pin_am2301 = 5;

static void do_init(void);

typedef struct __sensor_data {
    float rh;
    float t;
} sensor_data;

static void quit_handler(int sig)
{
    signal(sig, SIG_IGN);
    exit(0);
}

static void do_init(void)
{
    if (wiringPiSetup() == -1) {
        printf("wiringPi-Error\n");
        exit(1);
    }

    signal (SIGTERM, quit_handler);
    signal (SIGHUP, quit_handler);

    piHiPri(20);
}

static int wait_change(int mode, unsigned int tmo)
{
    int v1, v2, v3;
    unsigned int now = micros();

    do {
	/* Primitive low-pass filter */ 
	v1 = digitalRead(_pin_am2301);
	v2 = digitalRead(_pin_am2301);
	v3 = digitalRead(_pin_am2301);
	if (v1 == v2 && v2 == v3 && v3 == mode) {
	    return (micros() - now);
	}
    } while ((micros() - now) < tmo);
    return -1;
}

static int read_am2301(sensor_data *s, int mode)
{
    int  exponent, read_counter;
    int time_til_change;
    int read_digit;
    unsigned char read;
    unsigned char reads[5];

    /* Leave it high for a while */
    pinMode(_pin_am2301, OUTPUT);
    digitalWrite(_pin_am2301, HIGH);
    delayMicroseconds(100);

    /* Set it low to give the start signal */
    digitalWrite(_pin_am2301, LOW);
    delayMicroseconds(1000);

    /* Now set the pin high to let the sensor start communicating */ 
    digitalWrite(_pin_am2301, HIGH);
    pinMode(_pin_am2301, INPUT);
    if (wait_change(HIGH, 100) == -1) {
      return -1;
    }

    /* Wait for ACK */
    if (wait_change(LOW, 100) == -1) {
      return -2;
    }

    if (wait_change(HIGH, 100) == -1) {
      return -3;
    }

    /* When restarting, it looks like this lookfor start bit is not needed */
    if (mode != 0) {
      /* Wait for the start bit */
      if (wait_change(LOW, 200) == -1) {
          return -4;
      }

      if (wait_change(HIGH, 200) == -1) {
          return -5;
      }
    }

    for (read_counter = 0; read_counter < 5; read_counter++) {
      for (exponent = 7, read = 0; exponent >= 0; exponent--) {
        time_til_change = wait_change(LOW, 500);
        if (time_til_change == -1) {
            return -6;
        }

        read_digit = (time_til_change >= 50) ? 1 : 0;
        // read = read OR (read_digit * 2^exponent)
        read = read | (read_digit << exponent);
        if (wait_change(HIGH, 500) == -1) {
            return -7;
        }
      }
      reads[read_counter] = read;
    }

    pinMode(_pin_am2301, OUTPUT);
    digitalWrite(_pin_am2301, HIGH);

    /* Verify checksum */
    read = reads[0] + reads[1] + reads[2] + reads[3];
    if (read != reads[4]) {
      return -8;
    }

    s->rh = (float) (((uint16_t) reads[0] << 8) | (uint16_t) reads[1]);
    s->rh /= 10.0f;
    s->t = (float) (((uint16_t) reads[2] << 8) | (uint16_t) reads[3]);
    s->t /= 10.0f;

    if (s->rh > 100.0 || s->rh < 0.0 ||
          s->t > 80.0 || s->t < -40.0 )
    {
      return -9;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int i = 0;
    int ret;
    sensor_data s;

    do_init();

    /* Try 10 times, then bail out.
     */
    while (i < 10) {
	ret = read_am2301(&s, 1);
	if (ret == 0) {
	    printf("t=%.1f\nrh=%.1f\n", s.t, s.rh);
	    break;
	}
	delay(2000);
	i++;
    }

    if (i > 10) {
	return -1;
    }
    return 0;
}
