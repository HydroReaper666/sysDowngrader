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
#include <3ds.h>
#include "error.h"
#include "fs.h"
#include "misc.h"
#include "title.h"
#include "hashes.h"
#include "sha256.h"

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

	bool is_n3ds = 0;
	APT_CheckNew3DS(&is_n3ds);

	bool verified = false;
	Buffer<char> tmpStr(256);
	Result res;
	TitleInstallInfo installInfo;
	AM_TitleEntry ciaFileInfo;
	fs::File f;

	printf("Getting firmware files information...\n\n");

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
				printf("(A) continue\n(a) cancel\n\n");
				while(aptMainLoop())
				{
					hidScanInput();

					if(hidKeysDown() & KEY_A)
						break;

					if(hidKeysDown() & KEY_B)
						throw titleException(_FILE_, __LINE__, res, "Canceled!");
				}
			}

			printf("Getting firmware files version...\n\n");
			printf("NATIVE_FIRM (");

			tmpStr.clear();
			utf16_to_utf8((u8*) &tmpStr, (u16*) it.name.c_str(), 255);
			printf("%s", &tmpStr);

			printf(") is v");
			printf("%i\n\n", ciaFileInfo.version);

			printf("Verifying firmware files...\n\n");

			for(auto const &firmVersionMap : firmVersions) {

				if(firmVersionMap.first == ciaFileInfo.version) {

					for(auto it2 : filesDirs) {
						for(auto const &devicesVersionMap : firmVersionMap.second) {

							tmpStr.clear();
							utf16_to_utf8((u8*) &tmpStr, (u16*) it2.name.c_str(), 255);

							if(&tmpStr == devicesVersionMap.first) {

								for(auto it3 : filesDirs) {
									for(auto const &regionVersionMap : devicesVersionMap.second) {

										tmpStr.clear();
										utf16_to_utf8((u8*) &tmpStr, (u16*) it3.name.c_str(), 255);

										if(&tmpStr == regionVersionMap.first) {

											if(filesDirs.size() > regionVersionMap.second.size()) throw titleException(_FILE_, __LINE__, res, "Too many titles in /updates/ found!\n");
											if(filesDirs.size() < regionVersionMap.second.size()) throw titleException(_FILE_, __LINE__, res, "Too few titles in /updates/ found!\n");

											for(auto it4 : filesDirs) {

												tmpStr.clear();
												utf16_to_utf8((u8*) &tmpStr, (u16*) it4.name.c_str(), 255);

                        									fs::File ciaFile(u"/updates/" + it4.name, FS_OPEN_READ);
                      										Buffer<u8> shaBuffer(MAX_BUF_SIZE, false);
												u32 blockSize;
                      										u64 ciaSize, offset = 0;
                      										ciaSize = ciaFile.size();
												SHA256 sha256stream;

												sha256stream.reset();

												for(u32 i=0; i<=ciaSize / MAX_BUF_SIZE; i++)
												{
													blockSize = ((ciaSize - offset<MAX_BUF_SIZE) ? ciaSize - offset : MAX_BUF_SIZE);

													if(blockSize>0)
													{
														try
														{
															ciaFile.read(&shaBuffer, blockSize);
														} catch(fsException& e)
														{
															throw titleException(_FILE_, __LINE__, res, "Could not read file!");
														}

														sha256stream.add(&shaBuffer, blockSize);
														offset += blockSize;
													}
												}

												printf("%s", &tmpStr);

												if(sha256stream.getHash() != regionVersionMap.second.find(&tmpStr)->second) {
													throw titleException(_FILE_, __LINE__, res, "\x1b[31mHash mismatch! File is corrupt or incorrect!\x1b[0m\n\n");
												} else {
													printf("\x1b[32m  Verified\x1b[0m\n");
												}

											}
											verified = true;

										}

									}
								}

							}

						}
					}
		 		}
			}
		}

	}

	if (!verified) {
		throw titleException(_FILE_, __LINE__, res, "\x1b[31mNot found valid update set! File is corrupt or incorrect!\x1b[0m\n\n");
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
			printf("NATIVE_FIRM         ");
		} else {
			tmpStr.clear();
			utf16_to_utf8((u8*) &tmpStr, (u16*) it.name.c_str(), 255);

			printf("%s", &tmpStr);
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
						while (aptMainLoop()) {
							svcSleepThread(10000000000LL);
						}
      		}

					if (mode == 0) {
						printf("Beginning downgrade...\n\n");
						installUpdates(true);
						printf("\n\nUpdates installed; rebooting in 10 seconds...\n");
					} else if (mode == 1) {
						printf("Beginning update...\n\n");
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
					printf("Please reboot.");
					once = true;
				}
				catch(titleException& e)
				{
					printf("\n%s\n", e.what());
					printf("Please reboot.");
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
