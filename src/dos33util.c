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
#include <unistd.h>
#include <ctype.h>    /* toupper() */
#include "dos33.h"
#include "utils.h"
#include "version.h"

// Defines

// Enums
enum {
	COMMAND_LOAD = 0,
	COMMAND_SAVE,
	COMMAND_CATALOG,
	COMMAND_DELETE,
	COMMAND_UNDELETE,
	COMMAND_LOCK,
	COMMAND_UNLOCK,
	COMMAND_RENAME,
	COMMAND_DUMP,
	COMMAND_INIT,
	COMMAND_UNKNOWN,
};

// Structs
struct command_type {
	int type;
	char name[32];
};

// Constants
const static struct command_type commands[] = {
	{COMMAND_LOAD,		"LOAD"},
	{COMMAND_SAVE,		"SAVE"},
	{COMMAND_CATALOG,	"CATALOG"},
	{COMMAND_DELETE,	"DELETE"},
	{COMMAND_UNDELETE,	"UNDELETE"},
	{COMMAND_LOCK,		"LOCK"},
	{COMMAND_UNLOCK,	"UNLOCK"},
	{COMMAND_RENAME,	"RENAME"},
	{COMMAND_DUMP,		"DUMP"},
	{COMMAND_INIT,      "INIT"},
};
const static int num_commands = sizeof(commands) / sizeof(struct command_type);
const static int onesTbl[16] = {
	/* 0x0 = 0000 */ 0,
	/* 0x1 = 0001 */ 1,
	/* 0x2 = 0010 */ 1,
	/* 0x3 = 0011 */ 2,
	/* 0x4 = 0100 */ 1,
	/* 0x5 = 0101 */ 2,
	/* 0x6 = 0110 */ 2,
	/* 0x7 = 0111 */ 3,
	/* 0x8 = 1000 */ 1,
	/* 0x9 = 1001 */ 2,
	/* 0xA = 1010 */ 2,
	/* 0xB = 1011 */ 3,
	/* 0xC = 1100 */ 2,
	/* 0xd = 1101 */ 3,
	/* 0xe = 1110 */ 3,
	/* 0xf = 1111 */ 4,
};

// Variables
char					dskFilename[FILENAME_MAX] = "";
FILE					*dskFile = NULL;
struct Svtoc			vtoc;
struct ScatalogEntry	catEntry;
int						force = 0, raw = 0, address = -1;
char					type = '?';

// Private functions

/*****************************************************************************/
static int dos33ReadVtoc() {
	int r;

	// Seek to VTOC
	fseek(dskFile, diskOffset(VTOC_TRACK, VTOC_SECTOR), SEEK_SET);
	r = fread(&vtoc, 1, sizeof(vtoc), dskFile);
	if (r < 0) {
		fprintf(stderr, "Error on I/O\n");
		return -1;
	}
	// Clear catalog entry
	memset(&catEntry, 0, sizeof(catEntry));
	return 0;
}

/*****************************************************************************/
static int dos33SaveVtoc() {
	int r;

	// Seek to VTOC
	fseek(dskFile, diskOffset(VTOC_TRACK, VTOC_SECTOR), SEEK_SET);
	// Write back
	r = fwrite(&vtoc, 1, sizeof(vtoc), dskFile);
	if (r < 0) {
		fprintf(stderr, "Error on I/O\n");
		return -1;
	}
	// Clear catalog entry
	memset(&catEntry, 0, sizeof(catEntry));
	return 0;
}

/*****************************************************************************/
static void dos33AllocTs(int track, int sector) {
	int sm;

	sm = sector % 8;
	if (sector < 8) {
		vtoc.bitmap[track][1] &= ~(1 << sm);
	} else {
		vtoc.bitmap[track][0] &= ~(1 << sm);
	}
}

/*****************************************************************************/
static void dos33ReleaseTs(int track, int sector) {
	int sm;

	sm = sector % 8;
	if (sector < 8) {
		vtoc.bitmap[track][1] |= 1 << sm;
	} else {
		vtoc.bitmap[track][0] |= 1 << sm;
	}
}

/*****************************************************************************/
static int dos33GetFreeSpace() {
	int i, sectors_free = 0;

	for(i = 0; i < TRACKS_PER_DISK; i++) {
		sectors_free += onesTbl[ vtoc.bitmap[i][0]       & 0x0F];
		sectors_free += onesTbl[(vtoc.bitmap[i][0] >> 4) & 0x0F];
		sectors_free += onesTbl[ vtoc.bitmap[i][1]       & 0x0F];
		sectors_free += onesTbl[(vtoc.bitmap[i][1] >> 4) & 0x0F];
	}

	return sectors_free * BYTES_PER_SECTOR;
}

/*****************************************************************************/
static int dos33FindAndAllocSector(struct Sts *ts) {
	int foundTrack = -1, foundSector = -1;
	int i, b, startTrack;
	int r;
	char trackDir;


	// Originally used to keep things near center of disk for speed
	// We can use to avoid fragmentation possibly
	startTrack = vtoc.lastAllocTrack % TRACKS_PER_DISK;
	trackDir = vtoc.allocDirection;

	if ((trackDir != 1) && (trackDir != -1)) {
		fprintf(stderr,"ERROR! Invalid track dir %i\n", trackDir);
		exit(1);
	}

	if (((startTrack > VTOC_TRACK) && (trackDir != 1)) ||
		((startTrack < VTOC_TRACK) && (trackDir != -1))) {
		fprintf(stderr,
			"Warning! Non-optimal values for track dir t=%i d=%i!\n",
			startTrack, trackDir);
	}

	i = startTrack;
	do {
		for (b = 1; b > -1; b--) {
			r = vtoc.bitmap[i][b];
			if (r != 0) {
				foundSector = findFirstOne(r);
				foundTrack = i;
				// clear bit indicating in use
				vtoc.bitmap[i][b] &= ~(1 << foundSector);
				foundSector += (8 * (1 - b));
				break;
			}
		}
		if (foundTrack != -1) {
			break;
		}
		// Move to next track, handling overflows
		i += trackDir;
		if (i < 0) {
			i = VTOC_TRACK;
			trackDir = 1;
		}
		if (i >= TRACKS_PER_DISK) {
			i = VTOC_TRACK;
			trackDir = -1;
		}
	} while (i != startTrack);

	if (foundTrack == -1 || foundSector == -1) {
		fprintf(stderr, "No room left!\n");
		return 0;
	}

	/* store new track/direction info */
	vtoc.lastAllocTrack = foundTrack;
	if (foundTrack > VTOC_TRACK) {
		vtoc.allocDirection = 1;
	} else {
		vtoc.allocDirection = -1;
	}
	ts->track = foundTrack;
	ts->sector = foundSector;
	return 1;
}

/*****************************************************************************/
static int dos33GetNextCatEntry() {
	int						r;
	struct ScatalogHeader	header;

	if (catEntry.nextTs.track == 0 || catEntry.entryNum == 8) {
		if (catEntry.entryNum == 8) {
			catEntry.actTs.track = catEntry.nextTs.track;
			catEntry.actTs.sector = catEntry.nextTs.sector;
		} else {
			catEntry.actTs.track = vtoc.catalog.track;
			catEntry.actTs.sector = vtoc.catalog.sector;
		}
		fseek(dskFile, 
			diskOffset(catEntry.actTs.track, catEntry.actTs.sector),
			SEEK_SET);
		r = fread(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		catEntry.nextTs.track = header.nextTs.track;
		catEntry.nextTs.sector = header.nextTs.track;
		catEntry.entryNum = 0;
	}
	r = fread(&catEntry.fileEntry, 1, sizeof(catEntry.fileEntry), dskFile);
	if (r < 0) {
		fprintf(stderr, "Error on I/O\n");
		exit(1);
	}
	++catEntry.entryNum;
	if (catEntry.fileEntry.TsList.track == 0) {
		return 0;
	}
	return 1;
}

/*****************************************************************************/
static int dos33SaveActCatEntry() {
	int r, e, t, s;

	if (catEntry.nextTs.track == 0) {
		return 0;
	}
	e = sizeof(struct ScatalogHeader);
	e += (catEntry.entryNum - 1) * sizeof(struct SfileEntry);
	t = catEntry.actTs.track;
	s = catEntry.actTs.sector;
	fseek(dskFile, diskOffset(t, s) + e, SEEK_SET);
	r = fwrite(&catEntry.fileEntry, 1, sizeof(catEntry.fileEntry), dskFile);
	if (r < 0) {
		fprintf(stderr, "Error on I/O\n");
		exit(1);
	}
	return 1;
}

/*****************************************************************************/
static int dos33CheckFileExists(char *filename, int file_deleted) {
	char	name[FILENAME_MAX];
	int		i, nl;

	dos33ReadVtoc();
	while (dos33GetNextCatEntry()) {
		nl = FILE_NAME_SIZE;
		if (catEntry.fileEntry.TsList.track == 0xFF) {
			--nl;
		}
		dos33FilenameToAscii(name, catEntry.fileEntry.name, nl);
		// convert inverse chars
		for(i = 0; i < strlen(name); i++) {
			if (name[i] < 0x20) {
				name[i] += 0x40;
			}
		}
		if (0 == strcasecmp(filename, name)) {
			if (catEntry.fileEntry.TsList.track == 0xFF) {
				if (file_deleted) {
					return 1;
				}
			} else {
				if (!file_deleted) {
					return 1;
				}
			}
		}
	}
	return 0;
}

/*****************************************************************************/
static void dos33FindEmptyEntry() {
	dos33ReadVtoc();
	while (dos33GetNextCatEntry()) {
		if (catEntry.fileEntry.TsList.track == 0xFF) {
			return;
		}
	}
}

/*****************************************************************************/
static void cmdCatalog() {
	char	name[FILENAME_MAX];
	int		i, nl;

	dos33ReadVtoc();
	printf("DISK VOLUME %d\n\n", vtoc.diskVolume);
	while (dos33GetNextCatEntry()) {
		nl = FILE_NAME_SIZE;
		if (catEntry.fileEntry.TsList.track == 0xFF) {
			--nl;
			printf("#");
		} else {
			printf(" ");
		}
		if (catEntry.fileEntry.type & 0x80) {
			printf("*");
		} else {
			printf(" ");
		}
		printf("%c", dos33TypeToLetter(catEntry.fileEntry.type));
		printf(" ");
		printf("%.3i ", catEntry.fileEntry.size);
		dos33FilenameToAscii(name, catEntry.fileEntry.name, nl);
		// convert inverse chars
		for(i = 0; i < strlen(name); i++) {
			if (name[i] < 0x20) {
				printf("^%c", name[i] + 0x40);
			} else {
				printf("%c", name[i]);
			}
		}
		printf("\n");
	}
}

/*****************************************************************************/
static void cmdLoad(char *appleFilename, char *outputFilename) {
	char				tempStr[FILENAME_MAX];
	struct StslHeader	header;
	struct Sts			dataTs[TSL_MAX_NUMBER], nextTs;
	int					i, r, tslPointer, bufPointer;
	int					fileSize, offset, aux;
	char				*buffer = NULL;
	FILE				*outputFile = NULL;

	if (!dos33CheckFileExists(appleFilename, 0)) {
		fprintf(stderr, "Apple filename not found.\n");
		return;
	}
	// Alloc data buffer
	buffer = (char *)malloc(BYTES_PER_SECTOR * catEntry.fileEntry.size);
	bufPointer = 0;
	nextTs.track = catEntry.fileEntry.TsList.track;
	nextTs.sector = catEntry.fileEntry.TsList.sector;
	while (1) {
		// Read TSL
		fseek(dskFile, diskOffset(nextTs.track, nextTs.sector), SEEK_SET);
		r = fread(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		nextTs.track = header.nextTs.track;
		nextTs.sector = header.nextTs.sector;
		tslPointer = 0;
		while(tslPointer < TSL_MAX_NUMBER) {
			r = fread(&dataTs[tslPointer], 1, sizeof(struct Sts), dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			if (dataTs[tslPointer].track == 0 && dataTs[tslPointer].sector == 0) {
				break;
			}
			++tslPointer;
		}
		// Read data
		for (i = 0; i < tslPointer; i++) {
			fseek(dskFile, diskOffset(dataTs[i].track, dataTs[i].sector), SEEK_SET);
			r = fread(&buffer[bufPointer], 1, BYTES_PER_SECTOR, dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			bufPointer += r;
		}
		if (nextTs.track == 0 && nextTs.sector == 0) {
			break;
		}
	}
	// process file
	aux = 0;
	type = dos33TypeToLetter(catEntry.fileEntry.type);
	switch(type) {
		case 'A':
		case 'I':
			aux = 0x0801;
			fileSize = WORD(buffer[1], buffer[0]);
			offset = 2;
			break;

		case 'B':
			aux = WORD(buffer[1], buffer[0]);
			fileSize = WORD(buffer[3], buffer[2]);
			offset = 4;
			break;
		
		default:
			offset = 0;
			fileSize = bufPointer;
	}
	if (raw) {
		strcpy(tempStr, outputFilename);
	} else {
		sprintf(tempStr, "%s#%02X%04X", outputFilename, 
			dos33TypeToHex(catEntry.fileEntry.type), aux);
	}
	outputFile = fopen(tempStr, "wb");
	if (NULL == outputFile) {
		fprintf(stderr,"Error opening '%s' for write.\n", tempStr);
		exit(1);
	}
	if (raw) {
		fwrite(buffer, 1, fileSize + offset, outputFile);
	} else {
		fwrite(buffer + offset, 1, fileSize, outputFile);
	}
	fclose(outputFile);
}

/*****************************************************************************/
static void dos33DeleteFile(char *appleFilename) {
    int					r, tslPointer;
	struct StslHeader	header;
	struct Sts			nextTs, dataTs;

	if (!dos33CheckFileExists(appleFilename, 0)) {
		fprintf(stderr, 
			"Error! File %s does not exist or already deleted.\n", 
			appleFilename);
		return;
	}
	if (catEntry.fileEntry.type & 0x80) {
       fprintf(stderr, "File is locked! Unlock before deleting!\n");
       return;
	}
	nextTs.track = catEntry.fileEntry.TsList.track;
	nextTs.sector = catEntry.fileEntry.TsList.sector;
	while (1) {
		// Read TSL
		fseek(dskFile, diskOffset(nextTs.track, nextTs.sector), SEEK_SET);
		r = fread(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		// Release TSL TS
		dos33ReleaseTs(nextTs.track, nextTs.sector);
		//
		nextTs.track = header.nextTs.track;
		nextTs.sector = header.nextTs.sector;
		tslPointer = 0;
		while(tslPointer < TSL_MAX_NUMBER) {
			r = fread(&dataTs, 1, sizeof(struct Sts), dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			if (dataTs.track == 0 && dataTs.sector == 0) {
				break;
			}
			// Release data TS
			dos33ReleaseTs(dataTs.track, dataTs.sector);
			++tslPointer;
		}
		if (nextTs.track == 0 && nextTs.sector == 0) {
			break;
		}
	}
	// Save track to last name char and mark as deleted
	catEntry.fileEntry.name[FILE_NAME_SIZE-1] = catEntry.fileEntry.TsList.track;
	catEntry.fileEntry.TsList.track = 0xFF;
	dos33SaveActCatEntry();
	dos33SaveVtoc();
}

/*****************************************************************************/
static void dos33UndeleteFile(char *appleFilename) {
    int					r, tslPointer;
	struct StslHeader	header;
	struct Sts			nextTs, dataTs;

	if (!dos33CheckFileExists(appleFilename, 1)) {
		fprintf(stderr, 
			"Error! File %s does not exist or not deleted.\n", 
			appleFilename);
		return;
	}
	// Restore TSL track
	catEntry.fileEntry.TsList.track = catEntry.fileEntry.name[FILE_NAME_SIZE-1];
	if (catEntry.fileEntry.TsList.track >= TRACKS_PER_DISK) {
		fprintf(stderr, "Error undeleting file, track > %d\n", TRACKS_PER_DISK);
		return;
	}
	catEntry.fileEntry.name[FILE_NAME_SIZE-1] = ' ' | 0x80;
	nextTs.track = catEntry.fileEntry.TsList.track;
	nextTs.sector = catEntry.fileEntry.TsList.sector;
	while (1) {
		// Read TSL
		fseek(dskFile, diskOffset(nextTs.track, nextTs.sector), SEEK_SET);
		r = fread(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		// Re-alloc TSL TS
		dos33AllocTs(nextTs.track, nextTs.sector);
		//
		nextTs.track = header.nextTs.track;
		nextTs.sector = header.nextTs.sector;
		tslPointer = 0;
		while(tslPointer < TSL_MAX_NUMBER) {
			r = fread(&dataTs, 1, sizeof(struct Sts), dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			if (dataTs.track == 0 && dataTs.sector == 0) {
				break;
			}
			// Re-alloc data TS
			dos33AllocTs(dataTs.track, dataTs.sector);
			++tslPointer;
		}
		if (nextTs.track == 0 && nextTs.sector == 0) {
			break;
		}
	}
	dos33SaveActCatEntry();
	dos33SaveVtoc();
}

/*****************************************************************************/
static void cmdSave(char *inputFilename, char *appleFilename) {
	FILE				*inputFile;
	int					i, r, length, fileSize, offset;
	int					freeSpace, neededSectors, sizeInSectors, sectorsUsed;
	struct Sts			oldTs, newTs, dataTs[TSL_MAX_NUMBER];
	struct StslHeader	header;
	int					tslPointer, bufPointer;
	char				*buffer;

	//printf("SAVE: file %s, applefile %s, address %d, type: %c\n", inputFilename, appleFilename, address, type);
	if (!raw && address == -1) {
		fprintf(stderr, "Error! no raw mode needs an address.\n");
		return;
	}
	if (address < 0 || address > 0xFFFF) {
		fprintf(stderr,"Error! invalid address.\n");
		return;
	}
	if (type == '?') {
		fprintf(stderr,"Error! type unknown.\n");
		return;
	}
	if (!checkAppleFilename(appleFilename)) {
		return;
	}
	inputFile = fopen(inputFilename, "rb");
	if (NULL == inputFile) {
		fprintf(stderr,"Error opening '%s' for read.\n", inputFilename);
		return;
	}
	if (dos33CheckFileExists(appleFilename, 0)) {
		fprintf(stderr, "Warning! %s exists!\n", appleFilename);
		if (!force) {
			printf("Exiting early...\n");
			fclose(inputFile);
			return;
		}
		fprintf(stderr, "Deleting previous version...\n");
		dos33DeleteFile(appleFilename);
	}
	dos33FindEmptyEntry();
	// Get input file size
	fseek(inputFile, 0, SEEK_END);
	fileSize = ftell(inputFile);
	fseek(inputFile, 0, SEEK_SET);
	offset = 0;
	length = fileSize;
	if (!raw) {
		switch(type) {
			case 'A':
			case 'I':
				offset = 2;
				break;

			case 'B':
				offset = 4;
				break;

			default:
				offset = 0;
		}
		fileSize += offset;
	}
	// We need to round up to nearest sector size
	// Add an extra sector for the T/S list
	// Then add extra sector for a T/S list every 122*256 bytes (~31k)
	neededSectors = (fileSize / BYTES_PER_SECTOR) +		// round sectors
			((fileSize % BYTES_PER_SECTOR) != 0) +		// tail if needed
			1 + 										// first T/S list
			(fileSize / (122 * BYTES_PER_SECTOR));		// extra t/s lists
	// Get free space on device
	freeSpace = dos33GetFreeSpace();
	// Check for free space
	if (neededSectors * BYTES_PER_SECTOR > freeSpace) {
		fprintf(stderr, "Error! Not enough free space "
				"on disk image (need %d, have %d)\n",
				neededSectors * BYTES_PER_SECTOR, freeSpace);
		fclose(inputFile);
		return;
	}
	// plus one because we need a sector for the tail
	sizeInSectors = (fileSize / BYTES_PER_SECTOR) +
		((fileSize % BYTES_PER_SECTOR) != 0);

	// Alloc buffer and read input file
	buffer = (char *)malloc(fileSize);
	r = fread(buffer + offset, 1, fileSize - offset, inputFile);
	fclose(inputFile);
	switch(type) {
		case 'A':
		case 'I':
			buffer[0] = LOW(length);
			buffer[1] = HIGH(length);
			break;

		case 'B':
			buffer[0] = LOW(address);
			buffer[1] = HIGH(address);
			buffer[2] = LOW(length);
			buffer[3] = HIGH(length);
			break;

		default:
			break;
	}
	// Alloc all sectors
	i = 0;
	sectorsUsed = 0;
	newTs.track = 0;
	newTs.sector = 0;
	while (i < sizeInSectors) {

		// Create new T/S list if necessary
		if (i % TSL_MAX_NUMBER == 0) {
			// Save actual TS
			oldTs.track = newTs.track;
			oldTs.sector = newTs.sector;
			// allocate a sector for the new list
			if (!dos33FindAndAllocSector(&newTs)) {
				return;
			}
			++sectorsUsed;
			if (i == 0) {
				// Is the first allocation, just save in the file entry
				catEntry.fileEntry.TsList.track = newTs.track;
				catEntry.fileEntry.TsList.sector = newTs.sector;
				// Uses oldTs to indicate actual TS
				oldTs.track = newTs.track;
				oldTs.sector = newTs.sector;
			} else {
				// New TSL sector, save actual
				memset(&header, 0, sizeof(header));
				header.nextTs.track = newTs.track;
				header.nextTs.sector = newTs.sector;
				// set offset into file
				header.offset = (i - TSL_MAX_NUMBER) * 256;
				//
				fseek(dskFile, diskOffset(oldTs.track, oldTs.sector), SEEK_SET);
				r = fwrite(&header, 1, sizeof(header), dskFile);
				if (r < 0) {
					fprintf(stderr, "Error on I/O\n");
					exit(1);
				}
				for (i = 0; i < TSL_MAX_NUMBER; i++) {
					r = fwrite(&dataTs[i], 1, sizeof(struct Sts), dskFile);
					if (r < 0) {
						fprintf(stderr, "Error on I/O\n");
						exit(1);
					}
				}
			}
			memset(&dataTs, 0, sizeof(dataTs));
			tslPointer = 0;
		}

		/* allocate a sector */
		if (!dos33FindAndAllocSector(&newTs)) {
			return;
		}
		++sectorsUsed;
		dataTs[tslPointer].track = newTs.track;
		dataTs[tslPointer].sector = newTs.sector;
		++tslPointer;
		++i;
	}
	// Save last TSL
	memset(&header, 0, sizeof(header));
	fseek(dskFile, diskOffset(oldTs.track, oldTs.sector), SEEK_SET);
	r = fwrite(&header, 1, sizeof(header), dskFile);
	if (r < 0) {
		fprintf(stderr, "Error on I/O\n");
		exit(1);
	}
	for (i = 0; i < TSL_MAX_NUMBER; i++) {
		r = fwrite(&dataTs[i], 1, sizeof(struct Sts), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
	}

	// Sectors allocated, now read TSs and save file
	newTs.track = catEntry.fileEntry.TsList.track;
	newTs.sector = catEntry.fileEntry.TsList.sector;
	bufPointer = 0;
	while (1) {
		// Read TSL
		fseek(dskFile, diskOffset(newTs.track, newTs.sector), SEEK_SET);
		r = fread(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		newTs.track = header.nextTs.track;
		newTs.sector = header.nextTs.sector;
		tslPointer = 0;
		while(tslPointer < TSL_MAX_NUMBER) {
			r = fread(&dataTs[tslPointer], 1, sizeof(struct Sts), dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			if (dataTs[tslPointer].track == 0 && dataTs[tslPointer].sector == 0) {
				break;
			}
			++tslPointer;
		}
		// Write data
		for (i = 0; i < tslPointer; i++) {
			fseek(dskFile, diskOffset(dataTs[i].track, dataTs[i].sector), SEEK_SET);
			r = fwrite(&buffer[bufPointer], 1, BYTES_PER_SECTOR, dskFile);
			if (r < 0) {
				fprintf(stderr, "Error on I/O\n");
				exit(1);
			}
			bufPointer += r;
		}
		if (newTs.track == 0 && newTs.sector == 0) {
			break;
		}
	}
	catEntry.fileEntry.type = dos33LetterToType(type, 0);
	catEntry.fileEntry.size = sectorsUsed;
	// copy over filename 
	for (i = 0; i < strlen(appleFilename); i++) {
		catEntry.fileEntry.name[i] = appleFilename[i] | 0x80;
	}
	// pad out the filename with spaces
	for(i = strlen(appleFilename); i < FILE_NAME_SIZE; i++) {
		catEntry.fileEntry.name[i] = ' ' | 0x80;
	}
	dos33SaveActCatEntry();
	dos33SaveVtoc();
}

/*****************************************************************************/
static void cmdDump() {
	int i, j, b;

	dos33ReadVtoc();
	printf("\n");
	printf("VTOC INFORMATION:\n");
	printf("\tFirst Catalog = %02X/%02X\n", vtoc.catalog.track, 
		vtoc.catalog.sector);
	printf("\tDOS RELEASE = 3.%i\n", vtoc.dosRelease);
	printf("\tDISK VOLUME = %i\n", vtoc.diskVolume);
	printf("\tT/S pairs that will fit in T/S List = %i\n", vtoc.maxTSPairs);
	printf("\tLast track where sectors were allocated = $%02X\n",
		vtoc.lastAllocTrack);
	printf("\tDirection of track allocation = %i\n", vtoc.allocDirection);
	printf("\tNumber of tracks per disk = %i\n", vtoc.numTracks);
	printf("\tNumber of sectors per track = %i\n", vtoc.sectorsPerTrack);
	printf("\tNumber of bytes per sector = %i\n", vtoc.bytesPerSector);
	printf("\nFree sector bitmap:\n\n");
	printf("\t                1111111111111111222\n");
	printf("\t0123456789ABCDEF0123456789ABCDEF012\n");
	for(j = 0; j < SECTORS_PER_TRACK; j++) {
		printf("$%01X:\t",j);
		for(i = 0; i < TRACKS_PER_DISK; i++) {
			if (j < 8) {
				b = vtoc.bitmap[i][1] >> j;
			} else {
				b = vtoc.bitmap[i][0] >> (j - 8);
			}
			if (b & 0x01) {
				printf(".");
			} else {
				printf("U");
			}
		}
		printf("\n");
	}
	printf("Key: 'U' = used, '.' = free\n\n");
}

/*****************************************************************************/
static void cmdInit(char *dosFilename) {
	int						r, i, dosSize = 0, neededSectors;
	char					buffer[BYTES_PER_SECTOR], *dosBuffer;
	struct ScatalogHeader	header;
	FILE					*dosFile;

	if (strlen(dosFilename) > 0) {
		dosFile = fopen(dosFilename, "rb");
		if (NULL == dosFilename) {
			fprintf(stderr,"Error opening '%s' for read.\n", dosFilename);
			return;
		}
		fseek(dosFile, 0, SEEK_END);
		dosSize = ftell(dosFile);
		fseek(dosFile, 0, SEEK_SET);
		if (dosSize > BYTES_PER_SECTOR * SECTORS_PER_TRACK * 3) {
			fprintf(stderr,"DOS file do not fit in the image.\n");
			fclose(dosFile);
			return;
		}
		dosBuffer = (char *)malloc(dosSize);
		r = fread(dosBuffer, 1, dosSize, dosFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
		fclose(dosFile);
	}

	dskFile = fopen(dskFilename, "wb");
	if (NULL == dskFile) {
		fprintf(stderr,"Error opening disk_image: %s\n", dskFilename);
		return;
	}
	// zero out file
	memset(buffer, 0, BYTES_PER_SECTOR);
	for(i = 0; i < TRACKS_PER_DISK * SECTORS_PER_TRACK; i++) {
		r = fwrite(buffer, 1, BYTES_PER_SECTOR, dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
	}
	if (dosSize > 0) {
		fseek(dskFile, 0, SEEK_SET);
		r = fwrite(dosBuffer, 1, dosSize, dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
	}
	// Create VTOC
	memset(&vtoc, 0, sizeof(vtoc));
	vtoc.dosRelease = 3;
	vtoc.catalog.track = VTOC_TRACK;
	vtoc.catalog.sector = SECTORS_PER_TRACK - 1;
	vtoc.diskVolume = 254;
	vtoc.maxTSPairs = TSL_MAX_NUMBER;
	vtoc.lastAllocTrack = VTOC_TRACK + 1;
	vtoc.allocDirection = 1;
	vtoc.numTracks = TRACKS_PER_DISK;
	vtoc.sectorsPerTrack = SECTORS_PER_TRACK;
	vtoc.bytesPerSector = BYTES_PER_SECTOR;
	// reserve track 0
	// No user data can be stored here as track=0 is special case
	// end of file indicator
	for (i = 1; i < TRACKS_PER_DISK; i++) {
		vtoc.bitmap[i][0] = 0xFF;
		vtoc.bitmap[i][1] = 0xFF;
	}
	// if copying dos reserve anothers tracks/sectors
	if (dosSize > 0) {
		neededSectors = dosSize / BYTES_PER_SECTOR;
		neededSectors -= 1 * SECTORS_PER_TRACK;		// Exclude track 0
		i = 1;
		r = 0;
		while(neededSectors-- > 0) {
			dos33AllocTs(i, r++);
			if (r == SECTORS_PER_TRACK) {
				r = 0;
				++i;
			}
		}
	}
	// reserve VTOC track
	// reserved for vtoc and catalog stuff
	vtoc.bitmap[VTOC_TRACK][0] = 0;
	vtoc.bitmap[VTOC_TRACK][1] = 0;
	dos33SaveVtoc();
	memset(&header, 0, sizeof(header));
	// Set catalog next pointers
	for (i = SECTORS_PER_TRACK - 1; i > 1; i--) {
		header.nextTs.track = VTOC_TRACK;
		header.nextTs.sector = i - 1;
		fseek(dskFile, diskOffset(VTOC_TRACK, i), SEEK_SET);
		r = fwrite(&header, 1, sizeof(header), dskFile);
		if (r < 0) {
			fprintf(stderr, "Error on I/O\n");
			exit(1);
		}
	}
	fclose(dskFile);
}

/*****************************************************************************/
static void openRw() {
	dskFile = fopen(dskFilename, "r+b");
	if (NULL == dskFile) {
		fprintf(stderr,"Error opening disk_image: %s\n", dskFilename);
		exit(1);
	}
}

/*****************************************************************************/
static int lookupCommand(char *name) {
	int which = COMMAND_UNKNOWN, i;

	for(i = 0; i < num_commands; i++) {
		if(!strncmp(name, commands[i].name, strlen(commands[i].name))) {
			which = commands[i].type;
			break;
		}
	}
	return which;
}

/*****************************************************************************/
static void usage(char *name, int only_version) {
	printf("dos33util %s - by FBLabs\n\n", VERSION);
	if (0 != only_version) {
		return;
	}
	printf("Usage: %s [options] <filename> <command>\n\n", name);
	printf("Generic options:\n");
	printf("\t-h  display this help\n");
	printf("\t-V  show version and exit\n");
	printf("\t-f  force operation\n");
	printf("Command options:\n");
	printf("\t-r      : raw mode\n");
	printf("\t-t type : char file type (T|I|A|B|S|R|N|L)\n");
	printf("\t-a aux  : set auxiliary value (address)\n");
	printf("\n");
	printf("List of valid commands:\n");
	printf("\tCATALOG\n");
	printf("\tLOAD     [-r] <apple_file> [local_file]\n");
	printf("\tSAVE     [-r] [-a aux] [-t type] <local_file> [apple_file]\n");
	printf("\tDELETE   <apple_file>\n");
	printf("\tUNDELETE <apple_file>\n");
	printf("\tLOCK     <apple_file>\n");
	printf("\tUNLOCK   <apple_file>\n");
	printf("\tRENAME   <apple_file_old> <apple_file_new>\n");
	printf("\tDUMP\n");
	printf("\tINIT     [dos_file]\n");
	printf("\n");
	return;
}

/*****************************************************************************/
int main(int argc, char **argv) {
	char	commandStr[FILENAME_MAX] = "";
	char	commandArgs[10][FILENAME_MAX];
	char	appleFilename[FILENAME_MAX] = "";
	char	newAppleFilename[FILENAME_MAX] = "";
	char	inputFilename[FILENAME_MAX] = "";
	char	outputFilename[FILENAME_MAX] = "";
	char	*endptr, *p;
	int		command, typeHex, removeSuffix;
	int		i, c = 1, cac = 0;

	/* Check command line arguments */
	while (c < argc) {
		// Check if is a option
		if (argv[c][0] == '-' || argv[c][0] == '/') {
			// Check options w/o parameter
			switch(argv[c][1]) {
				case 'h':
					usage(argv[0], 0);
					return 0;

				case 'V':
					usage(argv[0], 1);
					return 0;
				
				case 'f':
					force = 1;
					break;

				case 'r':
					raw = 1;
					break;

				default:
					// Check options with parameters
					if (c+1 == (int)argc) {
						fprintf(stderr, 
							"ERROR! Missing parameter for option %s",
							argv[c]);
						return 1;
					}
					switch(argv[c][1]) {
						case 'a':
							++c;
							address = strtol(argv[c], &endptr, 0);
							break;

						case 't':
							++c;
							type = argv[c][0];
							break;

					}
			}
		} else {
			// Check image and command
			if (strlen(dskFilename) == 0) {
				strcpy(dskFilename, argv[c]);
			} else if (strlen(commandStr) == 0) {
				strcpy(commandStr, argv[c]);
			} else {
				if (cac == 10) {
					break;
				}
				strcpy(commandArgs[cac++], argv[c]);
			}
		}
		++c;
	}
	if (strlen(dskFilename) == 0) {
		fprintf(stderr,"Must specify disk image file!\n\n");
		usage(argv[0], 0);
		return 1;
	}
	if (strlen(commandStr) == 0) {
		fprintf(stderr,"Must specify command!\n\n");
		usage(argv[0], 0);
		return 1;
	}
	/* Make command be uppercase */
	for(i = 0; i < strlen(commandStr); i++) {
		commandStr[i] = toupper(commandStr[i]);
	}
	command = lookupCommand(commandStr);
	memset(&vtoc, 0, sizeof(vtoc));
	switch(command) {

		case COMMAND_CATALOG:
			openRw();
			cmdCatalog();
			break;

		case COMMAND_LOAD:
			if (cac == 0) {
				fprintf(stderr,"Error! Need apple filename\n");
				return 1;
			}
			openRw();
			truncateFilename(appleFilename, commandArgs[0]);
			if (cac > 1) {
				strcpy(outputFilename, commandArgs[1]);
			} else {
				strcpy(outputFilename, appleFilename);
			}
			cmdLoad(appleFilename, outputFilename);
			break;

		case COMMAND_SAVE:
			if (cac == 0) {
				fprintf(stderr,"Error! Need filename\n");
				return 1;
			}
			strcpy(inputFilename, commandArgs[0]);
			// Try get type and auxiliary value from filename
			removeSuffix = 0;
			if (!raw) {
				p = strrchr(inputFilename, '#');
				if (p) {			
					sscanf(p+1,"%2X%4X", &typeHex, &address);
					removeSuffix = 1;
					type = dos33TypeToLetter(dos33HexToType(typeHex));
				}
			}
			if (cac > 1) {
				truncateFilename(appleFilename, commandArgs[1]);
			} else {
				p = inputFilename + (strlen(inputFilename) - 1);
				while(p != inputFilename) {
					if (*p == '/' || *p == '\\') {
						++p;
						break;
					}
					--p;
				}
				truncateFilename(appleFilename, p);
				if (removeSuffix) {
					appleFilename[strlen(appleFilename) - 7] = '\0';
				}
			}
			openRw();
			cmdSave(inputFilename, appleFilename);
			break;

		case COMMAND_DELETE:
			if (cac == 0) {
				fprintf(stderr,"Error! Need apple filename\n");
				return 1;
			}
			truncateFilename(appleFilename, commandArgs[0]);
			openRw();
			dos33DeleteFile(appleFilename);
			break;

		case COMMAND_UNDELETE:
			if (cac == 0) {
				fprintf(stderr,"Error! Need apple filename\n");
				return 1;
			}
			truncateFilename(appleFilename, commandArgs[0]);
			openRw();
			dos33UndeleteFile(appleFilename);
			break;

		case COMMAND_LOCK:
			if (cac == 0) {
				fprintf(stderr,"Error! Need apple filename\n");
				return 1;
			}
			truncateFilename(appleFilename, commandArgs[0]);
			openRw();
			if (!dos33CheckFileExists(appleFilename, 0)) {
				fprintf(stderr, 
					"Error! File %s does not exist or has been deleted.\n", 
					appleFilename);
				return 1;
			}
			catEntry.fileEntry.type |= 0x80;
			dos33SaveActCatEntry();
			break;

		case COMMAND_UNLOCK:
			if (cac == 0) {
				fprintf(stderr,"Error! Need apple filename\n");
				return 1;
			}
			truncateFilename(appleFilename, commandArgs[0]);
			openRw();
			if (!dos33CheckFileExists(appleFilename, 0)) {
				fprintf(stderr, 
					"Error! File %s does not exist or has been deleted.\n", 
					appleFilename);
				return 1;
			}
			catEntry.fileEntry.type &= ~0x80;
			dos33SaveActCatEntry();
			break;

		case COMMAND_RENAME:
			if (cac < 2) {
				fprintf(stderr,"Error! Need two apple filename\n");
				return 1;
			}
			truncateFilename(appleFilename, commandArgs[0]);
			truncateFilename(newAppleFilename, commandArgs[1]);
			openRw();
			if (!dos33CheckFileExists(appleFilename, 0)) {
				fprintf(stderr, 
					"Error! File %s does not exist or has been deleted.\n", 
					appleFilename);
				return 1;
			}
			for (i = 0; i < strlen(newAppleFilename); i++) {
				catEntry.fileEntry.name[i] = newAppleFilename[i] | 0x80;
			}
			for(i = strlen(newAppleFilename); i < FILE_NAME_SIZE; i++) {
				catEntry.fileEntry.name[i] = ' ' | 0x80;
			}
			dos33SaveActCatEntry();
			break;

		case COMMAND_DUMP:
			openRw();
			cmdDump();
			break;

		case COMMAND_INIT:
			if (cac > 0) {
				strcpy(inputFilename, commandArgs[0]);
			}
			cmdInit(inputFilename);
			break;

		default:
			fclose(dskFile);
			fprintf(stderr,"Unknown command '%s'\n", commandStr);
			usage(argv[0], 0);
			return 1;
	}

	if (dskFile) {
		fclose(dskFile);
	}

	return 0;
}
