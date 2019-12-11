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

#pragma once

// Defines
#define HIGH(__v) ((__v >> 8) & 0xFF)
#define LOW(__v) (__v & 0xFF)
#define WORD(__h, __l) (((__h & 0xFF) << 8) | (__l & 0xFF))
#define TRACKS_PER_DISK 35
#define SECTORS_PER_TRACK 16
#define BYTES_PER_SECTOR 256
#define VTOC_TRACK  17
#define VTOC_SECTOR 0
#define FILE_NAME_SIZE     30
#define TSL_MAX_NUMBER      122

// Structs
#pragma pack(push, 1)

struct Sts {
    unsigned char   track;
    unsigned char   sector;
};

struct Svtoc {
    char            unk1;
    struct Sts      catalog;
    char            dosRelease;
    char            unk2[2];
    unsigned char   diskVolume;
    char            unk3[32];
    char            maxTSPairs;
    char            unk4[8];
    char            lastAllocTrack;
    char            allocDirection;
    char            unk5[2];
    char            numTracks;
    char            sectorsPerTrack;
    short           bytesPerSector;
    unsigned char   bitmap[50][4];
};

struct ScatalogHeader {
    char            unk1;
    struct Sts      nextTs;
    char            unk2[8];
};

struct StslHeader {
    char            unk1;
    struct Sts      nextTs;
    char            unk2[2];
    short           offset;
    char            unk3[5];
};

struct SfileEntry {
    struct Sts      TsList;
    unsigned char   type;
    unsigned char   name[FILE_NAME_SIZE];
    short           size;
};

#pragma pack(pop)

struct ScatalogEntry {
    struct Sts          actTs;
    struct Sts          nextTs;
    int                 entryNum;
    struct SfileEntry   fileEntry;
};
