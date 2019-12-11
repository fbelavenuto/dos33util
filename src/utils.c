/* dos33util - Apple D.O.S. 3.3 utility
 *
 * Copyright (C) 2019-2020  Fabio Belavenuto
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * 
 * This code is based on dos33fsutils from:
 * https://github.com/deater/dos33fsprogs
 * Copyright Vince Weaver <vince@deater.net>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "dos33.h"

// Functions

/*****************************************************************************/
int diskOffset(unsigned char track, unsigned char sector) {
	if (track >= TRACKS_PER_DISK) {
		fprintf(stderr, "Error: track > %d\n", TRACKS_PER_DISK);
		exit(1);
	}
	if (sector >= SECTORS_PER_TRACK) {
		fprintf(stderr, "Error: sector > %d\n", SECTORS_PER_TRACK);
		exit(1);
	}
	return (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR;
}

/*****************************************************************************/
int checkAppleFilename(char *filename) {
	int i;

	if (filename[0] < 64) {
		fprintf(stderr,"Error! First char of filename "
				"must be ASCII 64 or above!\n");
		return 0;
	}

	// Check for comma in filename
	for(i = 0; i < strlen(filename); i++) {
		if (filename[i] == ',') {
			fprintf(stderr,"Error! "
				"Cannot have ',' in a filename!\n");
			return 0;
		}
	}
	return 1;
}

/*****************************************************************************/
int truncateFilename(char *out, char *in) {
	int truncated = 0;

	/* Truncate filename if too long */
	if (strlen(in) > 30) {
		fprintf(stderr, "Warning!  Truncating %s to 30 chars\n", in);
		truncated = 1;
	}
	strncpy(out, in, 30);
	out[30] = '\0';
	return truncated;
}

/*****************************************************************************/
char *dos33FilenameToAscii(char *dest, unsigned char *src, int len) {
	int i, last_nonspace = 0;

	for(i = 0; i < len; i++) {
		if (src[i] != 0xA0) {
			last_nonspace=i;
		}
	}

	for(i = 0; i < last_nonspace + 1; i++) {
		dest[i] = src[i] & 0x7F;
	}
	dest[i] = '\0';
	return dest;
}

/*****************************************************************************/
char dos33TypeToLetter(int value) {

	switch(value & 0x7F) {
		case 0x0:
			return 'T';

		case 0x1:
			return 'I';

		case 0x2:
			return 'A';

		case 0x4:
			return 'B';

		case 0x8:
			return 'S';

		case 0x10:
			return 'R';

		case 0x20:
			return 'N';

		case 0x40:
			return 'L';

		default:
			return '?';

	}
	return '?';
}

/*****************************************************************************/
int dos33LetterToType(char type, int lock) {

	int r;

	/* Covert to upper case */
	if (type >= 'a') {
		type = type - 0x20;
	}
	switch(type) {
		case 'T':
			r = 0x0;
			break;

		case 'I':
			r = 0x1;
			break;

		case 'A':
			r = 0x2;
			break;

		case 'B':
			r = 0x4;
			break;

		case 'S':
			r = 0x8;
			break;

		case 'R':
			r = 0x10;
			break;

		case 'N':
			r = 0x20;
			break;

		case 'L':
			r = 0x40;
			break;

		default:
			r = 0x0;
	}
	if (lock) {
		r |= 0x80;
	}
	return r;
}

/*****************************************************************************/
int dos33TypeToHex(int value) {
	switch(value & 0x7F) {
		case 0x0:
			return 0x04;

		case 0x1:
			return 0xFA;

		case 0x2:
			return 0xFC;

		case 0x4:
			return 0x06;

		case 0x8:
			return 0xFF;

		case 0x10:
			return 0xFE;

		case 0x20:
			return 0xB8;

		case 0x40:
			return 0xBC;

		default:
			return 0x00;
	}
}

/*****************************************************************************/
int dos33HexToType(int value) {
	switch(value) {
		case 0x04:
			return 0x00;

		case 0xFA:
			return 0x01;

		case 0xFC:
			return 0x02;

		case 0x06:
			return 0x04;

		case 0xFF:
			return 0x08;

		case 0xFE:
			return 0x10;

		case 0xB8:
			return 0x20;

		case 0xBC:
			return 0x40;

		default:
			return 0x00;
	}
}

/*****************************************************************************/
int findFirstOne(unsigned char byte) {
	int i = 0;

	if (byte == 0) {
		return -1;
	}
	while((byte & (1 << i)) == 0) {
		++i;
	}
	return i;
}
