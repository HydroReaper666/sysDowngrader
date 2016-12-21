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
#include <functional>
#include <string>
#include <vector>
#include <ctime>
#include <3ds.h>
#include "fs.h"
#include "misc.h"

#define _FILE_ "fs.cpp" // Replacement for __FILE__ without the path



FS_Archive sdmcArchive;


namespace fs
{
	// Simple std::sort() compar function for file names
	bool fileNameCmp(fs::DirEntry& first, fs::DirEntry& second)
	{
		if(first.isDir && (!second.isDir)) return true;
		else if((!first.isDir) && second.isDir) return false;


		return (first.name.compare(second.name)<0);
	}


	//===============================================
	// class File                                  ||
	//===============================================

	void File::open(const std::u16string& path, u32 openFlags, FS_Archive& archive)
	{
		FS_Path filePath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Result  res;

		// Save args for when we want to move the file or other uses
		_path_      = path;
		_openFlags_ = openFlags;
		_archive_   = &archive;



		close(); // Close file handle before we open a new one
		seek(0, FS_SEEK_SET); // Reset current offset
		if(FSUSER_OpenFile(&_fileHandle_, archive, filePath, openFlags & 3, 0))
		{
			if((res = FSUSER_OpenFile(&_fileHandle_, archive, filePath, openFlags, 0)))
				throw fsException(_FILE_, __LINE__, res, "Failed to open file!");
		}
	}


	void File::open(const FS_Path& lowPath, u32 openFlags, FS_Archive& archive)
	{
		Result  res;


		close(); // Close file handle before we open a new one
		seek(0, FS_SEEK_SET); // Reset current offset
		if(FSUSER_OpenFile(&_fileHandle_, archive, lowPath, openFlags & 3, 0))
		{
			if((res = FSUSER_OpenFile(&_fileHandle_, archive, lowPath, openFlags, 0)))
				throw fsException(_FILE_, __LINE__, res, "Failed to open file!");
		}
	}


	u32 File::read(void *buf, u32 size)
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		u32 bytesRead;
		Result res;


		if((res = FSFILE_Read(_fileHandle_, &bytesRead, _offset_, buf, size)))
			throw fsException(_FILE_, __LINE__, res, "Failed to read from file!");

		_offset_ += bytesRead;
		return bytesRead;
	}


	u32 File::write(const void *buf, u32 size)
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		u32 bytesWritten;
		Result res;


		if((res = FSFILE_Write(_fileHandle_, &bytesWritten, _offset_, buf, size, FS_WRITE_FLUSH)))
			throw fsException(_FILE_, __LINE__, res, "Failed to write to file!");

		_offset_ += bytesWritten;
		return bytesWritten;
	}


	void File::flush()
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

    Result res;


		if((res = FSFILE_Flush(_fileHandle_))) throw fsException(_FILE_, __LINE__, res, "Failed to flush file!");
	}


	void File::seek(const u64 offset, fsSeekMode mode)
	{
		switch(mode)
		{
			case FS_SEEK_SET:
				_offset_ = offset;
				break;
			case FS_SEEK_CUR:
				_offset_ += offset;
				break;
			case FS_SEEK_END:
				_offset_ = size() - offset;
		}
	}


	u64 File::size()
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		u64 tmp;
		Result res;


		if((res = FSFILE_GetSize(_fileHandle_, &tmp))) throw fsException(_FILE_, __LINE__, res, "Failed to get file size!");

		return tmp;
	}


	void File::setSize(const u64 size)
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		Result res;


		if((res = FSFILE_SetSize(_fileHandle_, size))) throw fsException(_FILE_, __LINE__, res, "Failed to set file size!");
	}


	// This can also be used to rename files
	void File::move(const std::u16string& dst, FS_Archive& dstArchive)
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		u64 tmp = tell();


		close(); // Close file handle before we open a new one
		moveFile(_path_, dst, *_archive_, dstArchive);
		open(dst, _openFlags_ & 3, dstArchive); // Open moved file
		seek(tmp, FS_SEEK_SET);
	}


	u64 File::copy(const std::u16string& dst, std::function<void (const std::u16string& file, u32 percent)> statusCallback, FS_Archive& dstArchive)
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		return copyFile(_path_, dst, statusCallback, *_archive_, dstArchive);
	}


	void File::del()
	{
		if(!_fileHandle_) throw fsException(_FILE_, __LINE__, 0xDEADBEEF, "No file opened!");

		close(); // Close file handle before we can delete this file
		deleteFile(_path_, *_archive_);
	}


	//===============================================
	// Other file functions                        ||
	//===============================================

	bool fileExist(const std::u16string& path, FS_Archive& archive)
	{
		FS_Path filePath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Handle fileHandle;
		Result res;


		if(!FSUSER_OpenFile(&fileHandle, archive, filePath, FS_OPEN_READ, 0))
		{
			if((res = FSFILE_Close(fileHandle))) throw fsException(_FILE_, __LINE__, res, "Failed to close file!");
			return true;
		}

		return false;
	}


	void moveFile(const std::u16string& src, const std::u16string& dst, FS_Archive& srcArchive, FS_Archive& dstArchive)
	{
		FS_Path srcPath = {PATH_UTF16, (src.length()*2)+2, (const u8*)src.c_str()};
		FS_Path dstPath = {PATH_UTF16, (dst.length()*2)+2, (const u8*)dst.c_str()};
		Result res;


		if((res = FSUSER_RenameFile(srcArchive, srcPath, dstArchive, dstPath)))
			throw fsException(_FILE_, __LINE__, res, "Failed to move file!");
	}


	u64 copyFile(const std::u16string& src, const std::u16string& dst, std::function<void (const std::u16string& file, u32 percent)> callback, FS_Archive& srcArchive, FS_Archive& dstArchive)
	{
		File inFile(src, FS_OPEN_READ, srcArchive), outFile(dst, FS_OPEN_WRITE|FS_OPEN_CREATE, dstArchive);
		u32 blockSize;
		u64 inFileSize, offset = 0;



		inFileSize = inFile.size();
		outFile.setSize(inFileSize);


		Buffer<u8> buffer(MAX_BUF_SIZE, false);


		for(u32 i=0; i<=inFileSize / MAX_BUF_SIZE; i++)
		{
			blockSize = ((inFileSize - offset<MAX_BUF_SIZE) ? inFileSize - offset : MAX_BUF_SIZE);

			if(blockSize>0)
			{
				inFile.read(&buffer, blockSize);
				outFile.write(&buffer, blockSize);

				offset += blockSize;
				if(callback) callback(src, offset * 100 / inFileSize);
			}
		}

		return offset;
	}


	void deleteFile(const std::u16string& path, FS_Archive& archive)
	{
		FS_Path srcPath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Result res;


		if((res = FSUSER_DeleteFile(archive, srcPath))) throw fsException(_FILE_, __LINE__, res, "Failed to delete file!");
	}


	//===============================================
	// Directory related functions                 ||
	//===============================================

	bool dirExist(const std::u16string& path, FS_Archive& archive)
	{
		FS_Path dirPath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Handle dirHandle;
		Result res;


		if(!FSUSER_OpenDirectory(&dirHandle, archive, dirPath))
		{
			if((res = FSDIR_Close(dirHandle))) throw fsException(_FILE_, __LINE__, res, "Failed to close directory!");
			return true;
		}

		return false;
	}


	void makeDir(const std::u16string& path, FS_Archive& archive)
	{
		FS_Path dirPath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Handle dirHandle;
		Result res;


		if(!FSUSER_OpenDirectory(&dirHandle, archive, dirPath))
		{
			if((res = FSDIR_Close(dirHandle))) throw fsException(_FILE_, __LINE__, res, "Failed to close directory!");
			return;
		}
		if((res = FSUSER_CreateDirectory(archive, dirPath, 0)))
			throw fsException(_FILE_, __LINE__, res, "Failed to create directory!");
	}


	void makePath(const std::u16string& path, FS_Archive& archive)
	{
		std::u16string tmp;
		size_t found = 0;


		if(path.length() < 2 || path.find_first_of(u"/") == std::u16string::npos) return;
		while(found != std::u16string::npos)
		{
			found = path.find_first_of(u"/", found+1);
			tmp.assign(path, 0, found);
			makeDir(tmp, archive);
		}
	}


	DirInfo getDirInfo(const std::u16string& path, FS_Archive& archive)
	{
		u32 depth = 0;
		u16 helper[128]; // Anyone uses higher dir depths?
		DirInfo dirInfo = {0};

		std::u16string tmpPath(path);



		std::vector<DirEntry> entries = listDirContents(tmpPath, u"", archive);
		helper[0] = 0; // We are in the root at file/folder 0


		while(1)
		{
			// Prevent non-existent member access
			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir)
			{
				addToPath(tmpPath, entries[helper[depth]].name);
				dirInfo.dirCount++;

				helper[++depth] = 0; // Go 1 up in the fs tree and reset position
				entries = listDirContents(tmpPath, u"", archive);
			}

			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir) continue;

			for(auto it : entries)
			{
				if(!it.isDir)
				{
					dirInfo.fileCount++;
					dirInfo.size += it.size;
				}
			}

			if(!depth) break;

			removeFromPath(tmpPath);

			helper[--depth]++; // Go 1 down in the fs tree and increase position
			entries = listDirContents(tmpPath, u"", archive);
		}

		return dirInfo;
	}


	// Filter format is "entry1;entry2;..." for example ".txt;.png;". "" means list everything
	std::vector<DirEntry> listDirContents(const std::u16string& path, const std::u16string filter, FS_Archive& archive)
	{
		bool useFilter = false;
		Handle dirHandle;
		u32 entriesRead;
		Result res;

		FS_Path dirPath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		std::vector<DirEntry> filesFolders;
		if(filter.length() > 0) useFilter = true;



		if((res = FSUSER_OpenDirectory(&dirHandle, archive, dirPath)))
			throw fsException(_FILE_, __LINE__, res, "Failed to open directory!");


		Buffer<FS_DirectoryEntry> entries(32, false);


		do
		{
			entriesRead = 0;
			filesFolders.reserve(filesFolders.size()+32); // Save time by reserving enough mem
			if((res = FSDIR_Read(dirHandle, &entriesRead, 32, &entries))) throw fsException(_FILE_, __LINE__, res, "Failed to read directory!");

			if(useFilter)
			{
				for(u32 i=0; i<entriesRead; i++)
				{
					if(!(entries[i].attributes & FS_ATTRIBUTE_DIRECTORY))
					{
						size_t foundOld = 0;
						size_t found = 0;
						const std::u16string file((char16_t*)entries[i].name);

						while(1)
						{
							found = filter.find_first_of(u";", found+1);
							if(found == std::u16string::npos) break;
							if(foundOld > 0) foundOld++; // Skip the separator
							if(file.length() < found-foundOld) continue;
              if(file.compare(file.length()-(found-foundOld), found-foundOld, filter, foundOld, found-foundOld) == 0 && file[0] != u'.') {
								filesFolders.push_back(DirEntry((char16_t*)entries[i].name, entries[i].attributes & FS_ATTRIBUTE_DIRECTORY, entries[i].fileSize));
							}
							foundOld = found;
						}
					}
					else
					{
						filesFolders.push_back(DirEntry((char16_t*)entries[i].name, entries[i].attributes & FS_ATTRIBUTE_DIRECTORY, entries[i].fileSize));
					}
				}
			}
			else
			{
				for(u32 i=0; i<entriesRead; i++)
				{
					filesFolders.push_back(DirEntry((char16_t*)entries[i].name, entries[i].attributes & FS_ATTRIBUTE_DIRECTORY, entries[i].fileSize));
				}
			}
		} while(entriesRead == 32);



		// If we reserved too much mem shrink it
		filesFolders.shrink_to_fit();

		// Sort folders and files
		std::sort(filesFolders.begin(), filesFolders.end(), fileNameCmp);



		if((res = FSDIR_Close(dirHandle))) throw fsException(_FILE_, __LINE__, res, "Failed to close directory!");

		return filesFolders;
	}


	void moveDir(const std::u16string& src, const std::u16string& dst, FS_Archive& srcArchive, FS_Archive& dstArchive)
	{
		FS_Path srcPath = {PATH_UTF16, (src.length()*2)+2, (const u8*)src.c_str()};
		FS_Path dstPath = {PATH_UTF16, (dst.length()*2)+2, (const u8*)dst.c_str()};
		Result res;


		if((res = FSUSER_RenameDirectory(srcArchive, srcPath, dstArchive, dstPath))) throw fsException(_FILE_, __LINE__, res, "Failed to move directory!");
	}


	void copyDir(const std::u16string& src, const std::u16string& dst, std::function<void (const std::u16string& fsObject, u32 totalPercent, u32 filePercent)> callback, FS_Archive& srcArchive, FS_Archive& dstArchive)
	{
		u32 depth = 0, fileCount = 0, dirCount = 0;
		u16 helper[128]; // Anyone uses higher dir depths?

		DirInfo inDirInfo = getDirInfo(src, srcArchive);
		std::u16string tmpInPath(src);
		std::u16string tmpOutPath(dst);



		// Create the specified path if it doesn't exist
		makePath(tmpOutPath, dstArchive);
		std::vector<DirEntry> entries = listDirContents(tmpInPath, u"", srcArchive);
		helper[0] = 0; // We are in the root at file/folder 0


		while(1)
		{
			// Prevent non-existent member access
			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir)
			{
				addToPath(tmpInPath, entries[helper[depth]].name);
				addToPath(tmpOutPath, entries[helper[depth]].name);
				if(callback) callback(tmpInPath, (fileCount + dirCount) * 100 / (inDirInfo.fileCount + inDirInfo.dirCount), 0);
				makeDir(tmpOutPath, dstArchive);
				dirCount++;

				helper[++depth] = 0; // Go 1 up in the fs tree and reset position
				entries = listDirContents(tmpInPath, u"", srcArchive);
			}

			if((helper[depth]>=entries.size()) ? 0 : entries[helper[depth]].isDir) continue;

			for(auto it : entries)
			{
				if(!it.isDir)
				{
					addToPath(tmpInPath, it.name);
					addToPath(tmpOutPath, it.name);
					if(callback) copyFile(tmpInPath, tmpOutPath, [&](const std::u16string& file, u32 percent)
																												{
																													callback(file, (fileCount + dirCount) * 100 / (inDirInfo.fileCount + inDirInfo.dirCount), percent);
																												}, srcArchive, dstArchive);
					else copyFile(tmpInPath, tmpOutPath, nullptr, srcArchive, dstArchive);
					fileCount++;
					removeFromPath(tmpInPath);
					removeFromPath(tmpOutPath);
				}
			}

			if(!depth) break;

			removeFromPath(tmpInPath);
			removeFromPath(tmpOutPath);

			helper[--depth]++; // Go 1 down in the fs tree and increase position
			entries = listDirContents(tmpInPath, u"", srcArchive);
		}

		if(callback) callback(tmpInPath, (fileCount + dirCount) * 100 / (inDirInfo.fileCount + inDirInfo.dirCount), 0);
	}


	void deleteDir(const std::u16string& path, FS_Archive& archive)
	{
		FS_Path dirPath = {PATH_UTF16, (path.length()*2)+2, (const u8*)path.c_str()};
		Result res;


		if(path.compare(u"/") != 0)
		{
			if((res = FSUSER_DeleteDirectoryRecursively(archive, dirPath)))
				throw fsException(_FILE_, __LINE__, res, "Failed to delete directory!");
		}
		else // We can't delete "/" itself so delete everything in root
		{
			std::vector<DirEntry> list = listDirContents(path, u"", archive);

			for(auto it : list)
			{
				if(it.isDir) deleteDir(u"/" + it.name, archive);
				else deleteFile(u"/" + it.name, archive);
			}
		}
	}

	//===============================================
	// Misc functions                              ||
	//===============================================

	void addToPath(std::u16string& path, const std::u16string& dirOrFile)
	{
		if(path.length()>1) path += (u"/" + dirOrFile);
		else path += dirOrFile;
	}


	void removeFromPath(std::u16string& path)
	{
		size_t lastSlash = path.find_last_of(u"/");

		if(lastSlash>1) path.erase(lastSlash);
		else path.erase(lastSlash+1);
	}
} // namespace fs


void sdmcArchiveInit()
{
	sdmcArchive = 0;
	FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
}

void sdmcArchiveExit()
{
	FSUSER_CloseArchive(sdmcArchive);
}
