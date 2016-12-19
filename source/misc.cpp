/*
 *  sysUpdater is an update app for the Nintendo 3DS.
 *  Copyright (C) 2015 profi200
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/
 */


#include <string>
#include <3ds.h>
#include "misc.h"
#include "fs.h"

extern "C" {
    Result svchax_init(bool patch_srv);
    extern u32 __ctr_svchax;
    extern u32 __ctr_svchax_srv;
}

// Simple std::sort() compar function for file names
bool fileNameCmp(fs::DirEntry& first, fs::DirEntry& second)
{
	if(first.isDir && (!second.isDir)) return true;
	else if((!first.isDir) && second.isDir) return false;


	return (first.name.compare(second.name)<0);
}

int getAMu() {

  Handle amHandle = 0;

  printf("Checking for am:u...\n");
  // verify am:u access
  srvGetServiceHandleDirect(&amHandle, "am:u");
  if (amHandle) {
    aptInit();
    fsInit();
    sdmcArchiveInit();
    amInit();
    printf("Initted services...\n");

    svcCloseHandle(amHandle);

    printf("\x1b[32mGot am:u handle!\x1b[0m\n\n");
    return 0;
  }

  printf("Did not get am:u handle!\n\n");
  printf("Attempting svchax...\n");

  // try to get arm11
  svchax_init(true);
  printf("Initted svchax...\n\n");

  aptInit();
	fsInit();
	sdmcArchiveInit();
	amInit();
  printf("Initted services...\n");

  printf("Checking for am:u...\n\n");
  // verify am:u access
  srvGetServiceHandleDirect(&amHandle, "am:u");
  if (amHandle) {
    svcCloseHandle(amHandle);
    printf("\x1b[32mGot am:u handle!\x1b[0m\n\n");
    return 0;
  }
  return 1;
}
