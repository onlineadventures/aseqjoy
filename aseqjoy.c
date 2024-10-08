/**
 * aseqjoy - Tiny Joystick -> MIDI Controller Tool
 * Copyright 2003-2016 by Alexander Koenig - alex@lisas.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Note: that these sources contain a few lines of Vojtech Pavlik's jstest.c 
 * example, which is GPL'd, too and available from:
 * http://atrey.karlin.mff.cuni.cz/~vojtech/joystick/
 */

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <linux/joystick.h>
#include <alsa/asoundlib.h>

#define NAME_LENGTH 128

#define TOOL_NAME "aseqjoy"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

int joystick_no=0;

typedef struct {
	int controller;
	int last_value;
} val;

snd_seq_t *seq_handle;
snd_seq_event_t ev;
int controllers[4];
int buttons_map[32]; // Assuming a maximum of 32 buttons for mapping
int verbose=0;
int cc14=0;

int open_alsa_seq()
{
	char client_name[32];
	char port_name[48];
	snd_seq_addr_t src;
	
	/* Create the sequencer port. */
	
	sprintf(client_name, "Joystick%i", joystick_no);
	sprintf(port_name , "%s Output", client_name);

	if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
		puts("Error: Failed to access the ALSA sequencer.");
		exit(-1);
	}

	snd_seq_set_client_name(seq_handle, client_name);
	src.client = snd_seq_client_id(seq_handle);
	src.port = snd_seq_create_simple_port(seq_handle, "Joystick Output",
		SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, SND_SEQ_PORT_TYPE_APPLICATION);

	/* Init the event structure */
	
	snd_seq_ev_clear(&ev);
	snd_seq_ev_set_source(&ev, src.port);
	snd_seq_ev_set_subs(&ev);
	snd_seq_ev_set_direct(&ev);

	return 0;
}

int axes;
int joy_fd;
int buttons;

int open_joystick()
{
	char device[256];
	char name[NAME_LENGTH] = "Unknown";	
	
	sprintf(device, "/dev/js%i", joystick_no);

	if ((joy_fd = open(device, O_RDONLY)) < 0) {
		fprintf(stderr, "%s: ", TOOL_NAME); perror(device);
		sprintf(device, "/dev/input/js%i", joystick_no);
		
		if ((joy_fd = open(device, O_RDONLY)) < 0) {	
			fprintf(stderr, "%s: ", TOOL_NAME); perror(device);
			exit(-3);
		}
	}

	ioctl(joy_fd, JSIOCGAXES, &axes);
	ioctl(joy_fd, JSIOCGBUTTONS, &buttons);
	ioctl(joy_fd, JSIOCGNAME(NAME_LENGTH), name);

	printf("Using joystick (%s) through device %s with %i axes and %i buttons.\n", name, device, axes, buttons);

	return 0;
}

void loop()
{
	struct js_event js;
	int current_channel=1;
	double val_d;
	int val_i;
	int i;
	val *values;
	
	values = calloc(axes, sizeof(val));
	
	puts("Axis -> MIDI controller mapping:");
	
	for (i=0; i<axes; i++) {
		if (i<4) {
			values[i].controller=controllers[i];
		} else {
			values[i].controller=10+i;
		}
		printf("  %2i -> %3i\n", i, values[i].controller);
		values[i].last_value=0;		
	}

	puts("Button -> MIDI controller mapping:");
	for (i=0; i<buttons; i++) {
		if (buttons_map[i] != -1) {
			printf("  %2i -> %3i\n", i, buttons_map[i]);
		}
	}
	
	puts("Ready, entering loop - use Ctrl-C to exit.");	

	while (1) {
		if (read(joy_fd, &js, sizeof(struct js_event)) != sizeof(struct js_event)) {
			perror(TOOL_NAME ": error reading from joystick device");
			exit (-5);
		}

		switch(js.type & ~JS_EVENT_INIT) {		
			case JS_EVENT_BUTTON:
				if (buttons_map[js.number] != -1) {
					if (verbose) {
						printf("Button %i pressed, sending MIDI controller %i with value %i.\n", js.number, buttons_map[js.number], js.value);
					}
					
					ev.type = SND_SEQ_EVENT_CONTROLLER;
					snd_seq_ev_set_fixed(&ev);
					ev.data.control.channel = current_channel;
					ev.data.control.param = buttons_map[js.number];
					ev.data.control.value = js.value ? 127 : 0;
					snd_seq_event_output_direct(seq_handle, &ev);
				}
			break;
			
			case JS_EVENT_AXIS:
				val_d = (double) js.value;
				val_d += SHRT_MAX;
				val_d = val_d / ((double) USHRT_MAX);
				
				if (cc14) {
					val_d *= 16383.0;
				} else {
					val_d *= 127.0;
				}
			
				val_i = (int) val_d;
			
				if (values[js.number].last_value != val_i) {
					if (cc14) {
						ev.type = SND_SEQ_EVENT_CONTROL14;
					} else {					
						ev.type = SND_SEQ_EVENT_CONTROLLER;
					}

					snd_seq_ev_set_fixed(&ev);
					ev.data.control.channel = current_channel;
					ev.data.control.param = values[js.number].controller;
					ev.data.control.value = val_i;
					snd_seq_event_output_direct(seq_handle, &ev);
					
					if (verbose) {
						printf("Sent controller %i with value: %i.\n", values[js.number].controller, val_i);
					}
					
					values[js.number].last_value = val_i;
				}
			break;
		}
	}
}

int main (int argc, char **argv)
{
	int i;
        fprintf(stderr, "%s version %s - Copyright (C) 2003-2016 by Alexander Koenig\n",  TOOL_NAME, VERSION);
        fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY - for details read the license.\n", TOOL_NAME);

	for (i=0; i<4; i++) {
		controllers[i]=10+i;
	}
	
	for (i=0; i<32; i++) {
		buttons_map[i] = -1; // Initialize all buttons as unmapped
	}
	
	while (1) {
		int i=getopt(argc, argv, "vhrd:0:1:2:3:b:");
		if (i==-1) break;
		
		switch (i) {
			case '?':
			case 'h':
				printf("usage: %s [-d joystick_no] [-v] [-0 ctrl0] [-1 ctrl1] [-2 ctrl2] [-3 ctrl3] [-b button=controller]\n\n", TOOL_NAME);
				puts("\t-d select the joystick to use: 0..3");
				puts("\t-0 select the controller for axis 0 (1-127)");
				puts("\t-1 select the controller for axis 1 (1-127) etc");
				puts("\t-r use fine control change events (14 bit resolution)");
				puts("\t-v verbose mode.");
				puts("\t-b map button to controller (format: button=controller)");
				exit(-2);
			break;
			
			case '0':
				controllers[0] = atoi(optarg);
			break;

			case '1':
				controllers[1] = atoi(optarg);
			break;

			case '2':
				controllers[2] = atoi(optarg);
			break;

			case '3':
				controllers[3] = atoi(optarg);
			break;
			
			case 'b': {
				int button, controller;
				if (sscanf(optarg, "%d=%d", &button, &controller) == 2 && button >= 0 && button < 32 && controller >= 0 && controller <= 127) {
					buttons_map[button] = controller;
				} else {
					fprintf(stderr, "Invalid button mapping: %s\n", optarg);
					exit(-2);
				}
			}
			break;
			
			case 'v':
				verbose = 1;
			break;

			case 'r':
				cc14 = 1;
			break;
			
			case 'd':
				joystick_no = atoi(optarg);
			break;
		}
	}

	open_joystick();
	open_alsa_seq();
	
	loop();

	return 0;
}
