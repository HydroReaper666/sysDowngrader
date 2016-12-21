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
#include <stdio.h>
#include <stdarg.h>
#include "misc.h"
#include "fs.h"

extern "C" {
  Result svchax_init(bool patch_srv);
  extern u32 __ctr_svchax;
  extern u32 __ctr_svchax_srv;
}

Logging::Logging(void) {
  lgf = fopen("/sysDowngrader.log", "a");
  if (nullptr != lgf) {
    fprintf(lgf, "\n------------------------------------------------------------\n\n");
    fflush(lgf);
  }
}

Logging::~Logging(void) {
  if (nullptr != lgf) {
    fclose(lgf);
  }
}

void Logging::logprintf(const char *fmt, ...) {
  if (nullptr != lgf) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    vfprintf(lgf, fmt, ap);
    fflush(lgf);
    va_end(ap);
  }
}

void Logging::logsnprintf(char *str, size_t sz, const char *fmt, ...) {
  if (nullptr != lgf) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    vfprintf(lgf, fmt, ap);
    fflush(lgf);
    va_end(ap);
  }
}

Logging *logging = new Logging();

int getAMu() {

  Handle amHandle = 0;

  logging->logprintf("Checking for am:u...\n");
  // verify am:u access
  srvGetServiceHandleDirect(&amHandle, "am:u");
  if (amHandle) {
    aptInit();
    fsInit();
    sdmcArchiveInit();
    amInit();
    cfguInit();
    logging->logprintf("Initted services...\n");

    svcCloseHandle(amHandle);

    logging->logprintf("\x1b[32mGot am:u handle!\x1b[0m\n\n");
    return 0;
  }

  logging->logprintf("Did not get am:u handle!\n\n");
  logging->logprintf("Attempting svchax...\n");

  // try to get arm11
  svchax_init(true);
  logging->logprintf("Initted svchax...\n\n");

  aptInit();
	fsInit();
	sdmcArchiveInit();
	amInit();
  cfguInit();
  logging->logprintf("Initted services...\n");

  logging->logprintf("Checking for am:u...\n\n");
  // verify am:u access
  srvGetServiceHandleDirect(&amHandle, "am:u");
  if (amHandle) {
    svcCloseHandle(amHandle);
    logging->logprintf("\x1b[32mGot am:u handle!\x1b[0m\n\n");
    return 0;
  }
  return 1;
}
