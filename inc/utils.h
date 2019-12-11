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

#include <stdlib.h>

// Prototipes
int diskOffset(unsigned char track, unsigned char sector);
int checkAppleFilename(char *filename);
int truncateFilename(char *out, char *in);
char *dos33FilenameToAscii(char *dest, unsigned char *src, int len);
char dos33TypeToLetter(int value);
int dos33LetterToType(char type, int lock);
int dos33TypeToHex(int value);
int dos33HexToType(int value);
int findFirstOne(unsigned char byte);
