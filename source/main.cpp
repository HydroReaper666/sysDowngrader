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

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <inttypes.h>
#include <3ds.h>
#include "error.h"
#include "fs.h"
#include "misc.h"
#include "title.h"
#include "hashes.h"

#define _FILE_ "main.cpp" // Replacement for __FILE__ without the path

typedef struct
{
	std::u16string name;
	AM_TitleEntry entry;
	bool requiresDelete;
} TitleInstallInfo;

// Ordered from highest to lowest priority.
static const u32 titleTypes[7] = {
		0x00040138, // System Firmware
		0x00040130, // System Modules
		0x00040030, // Applets
		0x00040010, // System Applications
		0x0004001B, // System Data Archives
		0x0004009B, // System Data Archives (Shared Archives)
		0x000400DB, // System Data Archives
};

u32 getTitlePriority(u64 id) {
	u32 type = (u32) (id >> 32);
	for(u32 i = 0; i < 7; i++) {
		if(type == titleTypes[i]) {
			return i;
		}
	}

	return 0;
}

bool sortTitlesHighToLow(const TitleInstallInfo &a, const TitleInstallInfo &b) {
	bool aSafe = (a.entry.titleID & 0xFF) == 0x03;
	bool bSafe = (b.entry.titleID & 0xFF) == 0x03;
	if(aSafe != bSafe) {
		return aSafe;
	}

	return getTitlePriority(a.entry.titleID) < getTitlePriority(b.entry.titleID);
}

bool sortTitlesLowToHigh(const TitleInstallInfo &a, const TitleInstallInfo &b) {
        bool aSafe = (a.entry.titleID & 0xFF) == 0x03;
        bool bSafe = (b.entry.titleID & 0xFF) == 0x03;
        if(aSafe != bSafe) {
                return aSafe;
        }

	return getTitlePriority(a.entry.titleID) > getTitlePriority(b.entry.titleID);
}

// Fix compile error. This should be properly initialized if you fiddle with the title stuff!
u8 sysLang = 0;

// Override the default service exit functions
extern "C"
{
	void __appExit()
	{
		// Exit services
		amExit();
		sdmcArchiveExit();
		fsExit();
		hidExit();
		gfxExit();
		aptExit();
		srvExit();
	}
}

// Find title and compare versions. Returns CIA file version - installed title version
int versionCmp(std::vector<TitleInfo>& installedTitles, u64& titleID, u16 version)
{
	for(auto it : installedTitles)
	{
		if(it.titleID == titleID)
		{
			return (version - it.version);
		}
	}

	return 1; // The title is not installed
}


// If downgrade is true we don't care about versions (except equal versions) and uninstall newer versions
void installUpdates(bool downgrade)
{
	std::vector<fs::DirEntry> filesDirs = fs::listDirContents(u"/updates", u".cia;"); // Filter for .cia files
	std::vector<TitleInfo> installedTitles = getTitleInfos(MEDIATYPE_NAND);
	std::vector<TitleInstallInfo> titles;

	std::unordered_map<u64, std::unordered_map<u64, std::unordered_map<u64, std::array<uint8_t,32>>>> devices;
	std::unordered_map<u64, std::unordered_map<u64, std::array<uint8_t,32>>> regions;
	std::unordered_map<u64, std::array<uint8_t,32>> hashes;
	std::array<uint8_t,32> cmphash;

	u8 calchash[32];
	u64 ciaSize, offset = 0;
	u32 bytesRead;

	bool is_n3ds = 0;
	APT_CheckNew3DS(&is_n3ds);

	Buffer<char> tmpStr(256);
	Result res;
	TitleInstallInfo installInfo;
	AM_TitleEntry ciaFileInfo;
	fs::File f;

	printf("Getting firmware files information...\n\n");

	// determine firm cia version
	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle())))
				throw titleException(_FILE_, __LINE__, res, "Failed to get CIA file info!");

			if(ciaFileInfo.titleID != 0x0004013800000002LL && ciaFileInfo.titleID != 0x0004013820000002L)
				continue;

			if(ciaFileInfo.titleID == 0x0004013820000002LL && is_n3ds == 0)
				throw titleException(_FILE_, __LINE__, res, "Installing N3DS pack on O3DS will always brick!");
			if(ciaFileInfo.titleID == 0x0004013800000002LL && is_n3ds == 1 && ciaFileInfo.version > 11872)
				throw titleException(_FILE_, __LINE__, res, "Installing O3DS pack >6.0 on N3DS will always brick!");

			if(ciaFileInfo.titleID == 0x0004013800000002LL && is_n3ds == 1 && ciaFileInfo.version < 11872){
				printf("Installing O3DS pack on N3DS will brick unless you swap the NCSD and crypto slot!\n");
				printf("!! DO NOT CONTINUE UNLESS !!\n!! YOU ARE ON A9LH OR REDNAND !!\n\n");
				printf("(A) continue\n(B) cancel\n\n");
				while(aptMainLoop())
				{
					hidScanInput();

					if(hidKeysDown() & KEY_A)
						break;

					if(hidKeysDown() & KEY_B)
						throw titleException(_FILE_, __LINE__, res, "Canceled!");
				}
			}

			printf("Verifying firmware files...\n");

			if (firms.find(ciaFileInfo.version) == firms.end()) {
				throw titleException(_FILE_, __LINE__, res, "\x1b[31mDid not find known firmware files!\x1b[0m\n");
			} else {
				devices = firms.find(ciaFileInfo.version)->second;
			}
		}
	}

	printf("Getting region map...\n");

	// determine firm cia device (n3ds/o3ds)
	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle())))
				throw titleException(_FILE_, __LINE__, res, "Failed to get CIA file info!");

			if (devices.find(ciaFileInfo.titleID) == devices.end()) {
				continue;
			} else {
				regions = devices.find(ciaFileInfo.titleID)->second;
			}
		}
	}

	if (regions.empty()){
		throw titleException(_FILE_, __LINE__, res, "\x1b[31mDid not find known firmware files!\x1b[0m\n");
	}

	printf("Getting hash map...\n");

	//determine home menu cia for region
	//also do region checking
	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle())))
				throw titleException(_FILE_, __LINE__, res, "Failed to get CIA file info!");

			if (regions.find(ciaFileInfo.titleID) == regions.end()) {
				continue;
			} else {

				u64 home = regions.find(ciaFileInfo.titleID)->first;
				u8 region;

				if((res = CFGU_SecureInfoGetRegion(&region)))
					throw titleException(_FILE_, __LINE__, res, "CFGU_SecureInfoGetRegion() failed!");

				if ( (( home == 0x0004003000008202LL ) && ( region != CFG_REGION_JPN )) ||
					 (( home == 0x0004003000008F02LL ) && ( region != CFG_REGION_USA )) ||
					 (( home == 0x0004003000009802LL ) && ( region != CFG_REGION_EUR || CFG_REGION_AUS )) ||
					 (( home == 0x000400300000A102LL ) && ( region != CFG_REGION_CHN )) ||
					 (( home == 0x000400300000A902LL ) && ( region != CFG_REGION_KOR )) ||
					 (( home == 0x000400300000B102LL ) && ( region != CFG_REGION_TWN )) ) {
						 throw titleException(_FILE_, __LINE__, res, "\x1b[31mFirmware files are not for this device region!\x1b[0m\n");
				}

			 	hashes = regions.find(ciaFileInfo.titleID)->second;
				if(filesDirs.size() > hashes.size()) throw titleException(_FILE_, __LINE__, res, "Too many titles in /updates/ found!\n");
				if(filesDirs.size() < hashes.size()) throw titleException(_FILE_, __LINE__, res, "Too few titles in /updates/ found!\n");
			}
		}
	}

	if (hashes.empty()){
		throw titleException(_FILE_, __LINE__, res, "\x1b[31mDid not find known firmware files!\x1b[0m\n");
	}

	printf("Checking hashes...\n\n");

	//check hashmap
	for(auto it : filesDirs)
	{
		if(!it.isDir){

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle())))
				throw titleException(_FILE_, __LINE__, res, "Failed to get CIA file info!");

			printf("0x%016" PRIx64, ciaFileInfo.titleID);

			cmphash = hashes.find(ciaFileInfo.titleID)->second;
			ciaSize = f.size();

			Buffer<u8> shaBuffer(ciaSize, false);

			if((res = FSFILE_Read(f.getFileHandle(), &bytesRead, offset, &shaBuffer, ciaSize)))
				throw fsException(_FILE_, __LINE__, res, "Failed to read from file!");

			if((res = FSUSER_UpdateSha256Context(&shaBuffer, ciaSize, calchash)))
				throw titleException(_FILE_, __LINE__, res, "FSUSER_UpdateSha256Context() failed!");

			if(memcmp(cmphash.data(), calchash, 32)==0){
				printf("\x1b[32m  Verified\x1b[0m\n");
			} else {
				throw titleException(_FILE_, __LINE__, res, "\x1b[31mHash mismatch! File is corrupt or incorrect!\x1b[0m\n\n");
			}

		}
	}

	printf("\n\n\x1b[32mVerified firmware files successfully!\n\n\x1b[0m\n\n");
	printf("Installing firmware files...\n");
	for(auto it : filesDirs)
	{
		if(!it.isDir)
		{
			// Quick and dirty hack to detect these pesky
			// attribute files OSX creates.
			// This should rather be added to the
			// filter rules later.
			if(it.name[0] == u'.') continue;

			f.open(u"/updates/" + it.name, FS_OPEN_READ);
			if((res = AM_GetCiaFileInfo(MEDIATYPE_NAND, &ciaFileInfo, f.getFileHandle()))) throw titleException(_FILE_, __LINE__, res, "Failed to get CIA file info!");

			int cmpResult = versionCmp(installedTitles, ciaFileInfo.titleID, ciaFileInfo.version);
			if((downgrade && cmpResult != 0) || (cmpResult > 0))
			{
				installInfo.name = it.name;
				installInfo.entry = ciaFileInfo;
				installInfo.requiresDelete = downgrade && cmpResult < 0;

				titles.push_back(installInfo);
			}
		}
	}

	std::sort(titles.begin(), titles.end(), downgrade ? sortTitlesLowToHigh : sortTitlesHighToLow);

	for(auto it : titles)
	{
		bool nativeFirm = it.entry.titleID == 0x0004013800000002LL || it.entry.titleID == 0x0004013820000002LL;
		if(nativeFirm)
		{
			printf("\nNATIVE_FIRM (0x%016" PRIx64 ")", it.entry.titleID);
		} else {
			printf("0x%016" PRIx64, it.entry.titleID);
		}

		if(it.requiresDelete) deleteTitle(MEDIATYPE_NAND, it.entry.titleID);
		installCia(u"/updates/" + it.name, MEDIATYPE_NAND);
		if(nativeFirm && (res = AM_InstallFirm(it.entry.titleID))) throw titleException(_FILE_, __LINE__, res, "Failed to install NATIVE_FIRM!");
		printf("\x1b[32m  Installed\x1b[0m\n");
	}
}

int main()
{
	gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);

	bool once = false;
	int mode;

	consoleInit(GFX_TOP, NULL);

	printf("sysDowngrader\n\n");
	printf("(A) update\n(Y) downgrade\n(X) test svchax\n(B) exit\n\n");
	printf("Use the (HOME) button to exit the CIA version.\n");
	printf("The installation cannot be aborted once started!\n\n\n");
	printf("Credits:\n");
	printf(" + profi200\n");
	printf(" + aliaspider\n");
	printf(" + AngelSL\n");
	printf(" + Plailect\n\n");


	while(aptMainLoop())
	{
		hidScanInput();


		if(hidKeysDown() & KEY_B)
			break;
		if(!once)
		{
			if(hidKeysDown() & (KEY_A | KEY_Y | KEY_X))
			{
				try
				{

					if ((bool)(hidKeysDown() & KEY_Y)) {
						mode = 0;
					} else if ((bool)(hidKeysDown() & KEY_A)) {
						mode = 1;
					} else {
						mode = 2;
					}

					consoleClear();

					if (getAMu() != 0) {
						printf("\x1b[31mDid not get am:u handle, please reboot\x1b[0m\n\n");
						return 0;
					}

					if (mode == 0) {
						printf("Beginning downgrade...\n");
						installUpdates(true);
						printf("\n\nUpdates installed; rebooting in 10 seconds...\n");
					} else if (mode == 1) {
						printf("Beginning update...\n");
						installUpdates(false);
						printf("\n\nUpdates installed; rebooting in 10 seconds...\n");
					} else {
						printf("Tested svchax; rebooting in 10 seconds...\n");
					}

					svcSleepThread(10000000000LL);

					APT_HardwareResetAsync();

					once = true;
				}
				catch(fsException& e)
				{
					printf("\n%s\n", e.what());
					printf("Did you store the update files in '/updates'?\n");
					printf("Press (B) to exit.");
					once = true;
				}
				catch(titleException& e)
				{
					printf("\n%s\n", e.what());
					printf("Press (B) to exit.");
					once = true;
				}
			}
		}
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	return 0;
}
