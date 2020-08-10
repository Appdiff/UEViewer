#include "Appdiff/Index.h"


static FVirtualFileSystem* LoadFile(const char* FilePath, const TArray<FString> &AesKeys);
bool ValidateString(FArchive& Ar);


void LoadIndex(const TArray<FStaticString<256>> &FilePaths, const TArray<FString> &AesKeys)
{
  TArray<FVirtualFileSystem*> vfs;
  for (int i=0; i<FilePaths.Num(); ++i)
  {
    vfs.Add(LoadFile(*FilePaths[i], AesKeys));
  }
  for (int i=0; i<vfs; ++i)
  {
    if (vfs[i]) delete vfs[i];
  }
  return;
}


static FVirtualFileSystem* LoadFile(const char* FilePath, const TArray<FString> &AesKeys)
{
	guard(LoadFile);

	const char* ext = strrchr(FilePath, '.');
	if (ext == NULL) return NULL;
	ext++;

	// Find if this file in an archive with VFS inside
	FVirtualFileSystem* vfs = NULL;
	FArchive* reader = NULL;

#if SUPPORT_ANDROID
	if (!stricmp(ext, "obb"))
	{
		GForcePlatform = PLATFORM_ANDROID;
		reader = new FFileReader(FilePath);
		if (!reader) return NULL;
		reader->Game = GAME_UE3;
		vfs = new FObbVFS(FilePath);
	}
#endif // SUPPORT_ANDROID
#if UNREAL4
	if (!stricmp(ext, "pak"))
	{
		reader = new FFileReader(FilePath);
		if (!reader) return NULL;
		reader->Game = GAME_UE4_BASE;
		vfs = new FPakVFS_appdiff(FilePath, AesKeys);
		//GIsUE4PackageMode = true; // ignore non-UE4 extensions for speedup file registration
	}
#endif // UNREAL4

	//!! note: VFS pointer is not stored in any global list, and not released upon program exit
	if (vfs)
	{
		assert(reader);
		// read VF directory
		FString error;
		if (!vfs->AttachReader(reader, error))
		{
#if UNREAL4
			// Reset GIsUE4PackageMode back in a case if .pak file appeared in directory
			// by accident.
			//GIsUE4PackageMode = false;
#endif
			// something goes wrong
			if (error.Len())
			{
				appPrintf("%s\n", *error);
			}
			else
			{
				appPrintf("File %s has an unknown format\n", FilePath);
			}
			delete vfs;
			delete reader;
			return NULL;
		}
#if UNREAL4
		//GIsUE4PackageMode = false;
#endif
	}
	else
	{
    int size = 0;
		FILE* f = fopen(FilePath, "rb");
		if (f)
		{
			fseek(f, 0, SEEK_END);
			size = ftell(f);
			fclose(f);
		}
		else
		{
			// File is not accessible
			return NULL;
		}
    vfs = FFileVFS_appdiff(FilePath, size);
	}

	unguardf("%s", FilePath);

  return vfs;
}


FFileVFS_appdiff::FFileVFS_appdiff(const char* RelFilePath, int size)
{
	RegInfo.AddZeroed(1);

	// Split file name and register/find folder
	FStaticString<MAX_PACKAGE_PATH> Folder;
  Folder = RelFilePath;
	const char* s = strrchr(*Folder, '/');
  Folder[s - *Folder] = 0;

	CRegisterFileInfo &reg = RegInfo[0];
	reg.Filename = s + 1;
  reg.Path = *Folder;
	reg.Size = size;
	reg.IndexInArchive = 0;
  return;
}


void FPakVFS_appdiff::LoadPakIndexCommon(TArray<byte> &InfoBlock, FMemReader &InfoReader, FArchive* reader, const FPakInfo& info, FString& error)
{
	guard(FPakVFS_appdiff::LoadPakIndexCommon);

  // Save PakInfo
  PakInfo = info;

	// Manage pak files with encrypted index
	if (info.bEncryptedIndex)
	{
		guard(CheckEncryptedIndex);

    const byte *src = InfoBlock.GetData();
    int srclen = InfoBlock.Num();
    byte copy[srclen];
  	FMemReader CopyReader(copy, info.IndexSize);
  	CopyReader.SetupFrom(*reader);
    for (int i=0; i<AesKeysRef.Num(); ++i)
    {
      memcpy(copy, src, srclen);
      appDecryptAES(&copy[0], srclen, *AesKeysRef[i], AesKeysRef[i].Len());
  		if (ValidateString(CopyReader))
  		{
        KeyIndex = i;
        break;
  		}
    }

		if (KeyIndex < 0)
		{
			char buf[1024];
			appSprintf(ARRAY_ARG(buf), "WARNING: The provided encryption key doesn't work with \"%s\". Skipping.", *Filename);
			error = buf;
			return;
		}
    appDecryptAES(InfoBlock.GetData(), InfoBlock.Num(), *AesKeysRef[KeyIndex], AesKeysRef[KeyIndex].Len());

		// Data is ok, seek to data start.
		InfoReader.Seek(0);

		unguard;
	}

	// this file looks correct, store 'reader'
	Reader = reader;

	// Read pak index

	TRY {
		// Read MountPoint with catching error, to override error message.
		InfoReader << MountPoint;
	} CATCH {
		if (info.bEncryptedIndex)
		{
			// Display nice error message
			appErrorNoLog("Error during loading of encrypted pak file index. Probably the provided AES key is not correct.");
		}
		else
		{
			THROW_AGAIN;
		}
	}
	// Process MountPoint
	ValidateMountPoint(MountPoint);

	// Read number of files
	InfoReader << NumFiles;
	if (!NumFiles)
	{
		appPrintf("Empty pak file \"%s\"\n", *Filename);
		return;
	}
	// Now InfoReader points to the full index data, either with use of 'reader' or 'InfoReaderProxy'.
	// Build "legacy" FileInfos array from new data format
	FileInfos.AddZeroed(NumFiles);
	RegInfo.AddZeroed(NumFiles);

	//Reserve(NumFiles); // reserves global file tracking

	unguardf("LoadPakIndexCommon");

  return;
}


bool FPakVFS_appdiff::LoadPakIndexLegacy(FArchive* reader, const FPakInfo& info, FString& error)
{
	guard(FPakVFS_appdiff::LoadPakIndexLegacy);

	// Always read index to memory block for faster serialization
	TArray<byte> InfoBlock;
	InfoBlock.SetNumUninitialized(info.IndexSize);
	reader->Serialize(InfoBlock.GetData(), info.IndexSize);
	FMemReader InfoReader(InfoBlock.GetData(), info.IndexSize);
	InfoReader.SetupFrom(*reader);

  LoadPakIndexCommon(InfoBlock, InfoReader, reader, info, error);
  if (KeyIndex < 0) return false;
  if (!NumFiles) return true;

	for (int i = 0; i < NumFiles; i++)
	{
		guard(ReadInfo);

		FPakEntry& E = FileInfos[i];
		// serialize name, combine with MountPoint
		FStaticString<MAX_PACKAGE_PATH> Filename;
		InfoReader << Filename;
		FStaticString<MAX_PACKAGE_PATH> CombinedPath;
		CombinedPath = MountPoint;
		CombinedPath += Filename;
		// compact file name
		//CompactFilePath(CombinedPath);
		// serialize other fields
		E.Serialize(InfoReader);
		if (E.bEncrypted) NumEncryptedFiles++;
		if (info.Version >= PakFile_Version_FNameBasedCompressionMethod)
		{
			int32 CompressionMethodIndex = E.CompressionMethod;
			assert(CompressionMethodIndex >= 0 && CompressionMethodIndex <= 4);
			E.CompressionMethod = CompressionMethodIndex > 0 ? info.CompressionMethods[CompressionMethodIndex-1] : 0;
		}
		else if (E.CompressionMethod == COMPRESS_Custom)
		{
			// Custom compression method for UE4.20-UE4.21, use detection code.
			E.CompressionMethod = COMPRESS_FIND;
		}

		// Split file name and register/find folder
		FStaticString<MAX_PACKAGE_PATH> Folder;
    Folder = CombinedPath;
		const char* s = strrchr(*Folder, '/');
    Folder[s - *Folder] = 0;

		// Register the file
		CRegisterFileInfo &reg = RegInfo[i];
		reg.Filename = s + 1;
    reg.Path = *Folder;
		reg.Size = E.UncompressedSize;
		reg.IndexInArchive = i;
		//E.FileInfo = RegisterFile(reg);

		unguardf("Index=%d/%d", i, NumFiles);
	}

	return true;

	unguard;
}


bool FPakVFS_appdiff::LoadPakIndex(FArchive* reader, const FPakInfo& info, FString& error)
{
	guard(FPakVFS_appdiff::LoadPakIndex);

	// Always read index to memory block for faster serialization
	TArray<byte> InfoBlock;
	InfoBlock.SetNumUninitialized(info.IndexSize);
	reader->Serialize(InfoBlock.GetData(), info.IndexSize);
	FMemReader InfoReader(InfoBlock.GetData(), info.IndexSize);
	InfoReader.SetupFrom(*reader);

  LoadPakIndexCommon(InfoBlock, InfoReader, reader, info, error);
  if (KeyIndex < 0) return false;
  if (!NumFiles) return true;

	uint64 PathHashSeed;
	InfoReader << PathHashSeed;

	// Read directory information
	bool bReaderHasPathHashIndex;
	int64 PathHashIndexOffset = -1;
	int64 PathHashIndexSize = 0;
	InfoReader << bReaderHasPathHashIndex;
	if (bReaderHasPathHashIndex)
	{
		InfoReader << PathHashIndexOffset << PathHashIndexSize;
		// Skip PathHashIndexHash
		InfoReader.Seek(InfoReader.Tell() + 20);
	}

	bool bReaderHasFullDirectoryIndex = false;
	int64 FullDirectoryIndexOffset = -1;
	int64 FullDirectoryIndexSize = 0;
	InfoReader << bReaderHasFullDirectoryIndex;
	if (bReaderHasFullDirectoryIndex)
	{
		InfoReader << FullDirectoryIndexOffset << FullDirectoryIndexSize;
		// Skip FullDirectoryIndexHash
		InfoReader.Seek(InfoReader.Tell() + 20);
	}

	if (!bReaderHasFullDirectoryIndex)
	{
		// todo: read PathHashIndex: PathHashIndexOffset + PathHashIndexSize
		// todo: structure: TMap<uint64, FPakEntryLocation> (i.e. not array), FPakEntryLocation = int32
		// todo: seems it maps hash to file index.
		char buf[1024];
		appSprintf(ARRAY_ARG(buf), "WARNING: Pak file \"%s\" doesn't have a full index. Skipping.", *Filename);
		error = buf;
		return false;
	}

	TArray<uint8> EncodedPakEntries;
	InfoReader << EncodedPakEntries;

	// Read 'Files' array. This one holds decoded file entries, without file names.
	TArray<FPakEntry> Files;
	InfoReader << Files;

	// Read the full index via the same InfoReader object
	assert(bReaderHasFullDirectoryIndex);

	guard(ReadFullDirectory);
	reader->Seek64(FullDirectoryIndexOffset);
	InfoBlock.Empty(); // avoid reallocation with memcpy
	InfoBlock.SetNumUninitialized(FullDirectoryIndexSize);
	reader->Serialize(InfoBlock.GetData(), FullDirectoryIndexSize);
	InfoReader = FMemReader(InfoBlock.GetData(), InfoBlock.Num());
	InfoReader.SetupFrom(*reader);

	if (info.bEncryptedIndex)
	{
		// Read encrypted data and decrypt
    appDecryptAES(InfoBlock.GetData(), InfoBlock.Num(), *AesKeysRef[KeyIndex], AesKeysRef[KeyIndex].Len());
	}
	unguard;

	guard(BuildFullDirectory);
	int FileIndex = 0;

	/*
		// We're unwrapping this complex structure serializer for faster performance (much less allocations)
		using FPakEntryLocation = int32;
		typedef TMap<FString, FPakEntryLocation> FPakDirectory;
		// Each directory has files, it maps clean filename to index
		TMap<FString, FPakDirectory> DirectoryIndex;
		InfoReader << DirectoryIndex;
	*/

	int32 DirectoryCount;
	InfoReader << DirectoryCount;

	for (int DirectoryIndex = 0; DirectoryIndex < DirectoryCount; DirectoryIndex++)
	{
		guard(Directory);
		// Read DirectoryIndex::Key
		FStaticString<MAX_PACKAGE_PATH> DirectoryName;
		InfoReader << DirectoryName;

		// Build folder name. MountPoint ends with '/', directory name too.
		FStaticString<MAX_PACKAGE_PATH> DirectoryPath;
		DirectoryPath = MountPoint;
    if (DirectoryPath.EndsWith("/") && DirectoryName.Len() && DirectoryName[0] == '/')
      DirectoryName.RemoveFromStart("/");
		DirectoryPath += DirectoryName;
		//CompactFilePath(DirectoryPath);
		if (DirectoryPath[DirectoryPath.Len()-1] == '/')
			DirectoryPath.RemoveAt(DirectoryPath.Len()-1, 1);

		//int FolderIndex = RegisterGameFolder(*DirectoryPath);

		// Read size of FPakDirectory (DirectoryIndex::Value)
		int32 NumFilesInDirectory;
		InfoReader << NumFilesInDirectory;

		for (int DirectoryFileIndex = 0; DirectoryFileIndex < NumFilesInDirectory; DirectoryFileIndex++)
		{
			guard(File);

			FPakEntry& E = FileInfos[FileIndex];

			// Read FPakDirectory entry Key
			FStaticString<MAX_PACKAGE_PATH> DirectoryFileName;
			InfoReader << DirectoryFileName;

			// Read FPakDirectory entry Value
			int32 PakEntryLocation;
			InfoReader << PakEntryLocation;
			// PakEntryLocation is positive (offset in 'EncodedPakEntries') or negative (index in 'Files')
			// References in UE4:
			// FPakFile::DecodePakEntry <- FPakFile::GetPakEntry (decode or pick from 'Files') <- FPakFile::Find (name to index/location)
			if (PakEntryLocation < 0)
			{
				// Index in 'Files' array
				E.CopyFrom(Files[-(PakEntryLocation + 1)]);
			}
			else
			{
				// Pointer in 'EncodedPakEntries'
				E.DecodeFrom(&EncodedPakEntries[PakEntryLocation]);
			}

			if (E.bEncrypted) NumEncryptedFiles++;

			// Convert compression method
			int32 CompressionMethodIndex = E.CompressionMethod;
			assert(CompressionMethodIndex >= 0 && CompressionMethodIndex <= 4);
			E.CompressionMethod = CompressionMethodIndex > 0 ? info.CompressionMethods[CompressionMethodIndex-1] : 0;

			// Register the file
			CRegisterFileInfo &reg = RegInfo[FileIndex];
			reg.Filename = *DirectoryFileName;
			reg.Path = *DirectoryPath;
			reg.Size = E.UncompressedSize;
			reg.IndexInArchive = FileIndex;

      /*
			// Register the file
			CRegisterFileInfo reg;
			reg.Filename = *DirectoryFileName;
			reg.FolderIndex = FolderIndex;
			reg.Size = E.UncompressedSize;
			reg.IndexInArchive = FileIndex;
			E.FileInfo = RegisterFile(reg);
      */

			FileIndex++;
			unguard;
		}
		unguard;
	}
	if (FileIndex != FileInfos.Num())
	{
		appError("Wrong pak file directory?");
	}
	unguard;

#if 0
	if (count >= MIN_PAK_SIZE_FOR_HASHING)
	{
		// Hash everything
		for (FPakEntry& E : FileInfos)
		{
			AddFileToHash(&E);
		}
	}
#endif

	return true;

	unguard;
}


CGameFileInfo* CGameFileInfo::Register(FVirtualFileSystem* parentVfs, const CRegisterFileInfo& RegisterInfo)
{
	guard(CGameFileInfo::Register);

	// Verify file extension
	const char *ext = strrchr(RegisterInfo.Filename, '.');
	if (!ext)
  {
    appPrintf("***********SKIPPING UNKNOWN EXTENSION.\n");
    return NULL; // unknown type
  }
	ext++;

	// to know if file is a package or not. Note: this will also make pak loading a but faster.
	bool IsPackage = false;
#if UNREAL4
	if (GIsUE4PackageMode)
	{
		// Faster case for UE4 files - it has small list of extensions
		IsPackage = FindExtension(ext, ARRAY_ARG(UE4PackageExtensions));
	}
	else
#endif
	{
		// Longer list for games older than UE4. Processed slower, however we never have such a long list of files as for UE4.
		IsPackage = FindExtension(ext, ARRAY_ARG(PackageExtensions));
	}

	if (!parentVfs && !IsPackage)
	{
#if HAS_SUPPORT_FILES
		// Check for suppressed extensions
		if (!FindExtension(ext, ARRAY_ARG(KnownExtensions)))
#endif
		{
			// Unknown extension. Check if we should count it or not.
			// ignore unknown files inside "cooked" or "content" directories
			if (appStristr(RegisterInfo.Filename, "cooked") || appStristr(RegisterInfo.Filename, "content")) return NULL;
			// perhaps this file was exported by our tool - skip it
			if (!FindExtension(ext, ARRAY_ARG(SkipExtensions)))
			{
				// Unknown file type
				if (++GNumForeignFiles >= MAX_FOREIGN_FILES)
					appErrorNoLog("Too many unknown files - bad root directory (%s)?", GRootDirectory);
			}
			return NULL;
		}
	}

	// A known file type is here.

	// Use RegisterInfo.Path if not empty
	const char* ShortFilename = RegisterInfo.Filename;
	int FolderIndex = 0;

	if (RegisterInfo.FolderIndex)
	{
		FolderIndex = RegisterInfo.FolderIndex;
	}
	else if (!RegisterInfo.Path)
	{
		// Split file name and register/find folder
		FStaticString<MAX_PACKAGE_PATH> Folder;
		if (const char* s = strrchr(RegisterInfo.Filename, '/'))
		{
			// Have a path part inside a filename
			Folder = RegisterInfo.Filename;
			// Cut path at '/' location
			Folder[s - RegisterInfo.Filename] = 0;
			ShortFilename = s + 1;
		}
		FolderIndex = RegisterGameFolder(*Folder);
	}
	else
	{
		FolderIndex = RegisterGameFolder(RegisterInfo.Path);
	}

	int extOffset = ext - ShortFilename;
	assert(extOffset < 256); // restriction of CGameFileInfo::ExtensionOffset

	// Create CGameFileInfo entry
	CGameFileInfo* info = AllocFileInfo();
	info->IsPackage = IsPackage;
	info->FileSystem = parentVfs;
	info->IndexInVfs = RegisterInfo.IndexInArchive;
	info->ShortFilename = appStrdupPool(ShortFilename);
	info->ExtensionOffset = extOffset;
	info->FolderIndex = FolderIndex;
	info->Size = RegisterInfo.Size;
	info->SizeInKb = (info->Size + 512) / 1024;

	if (info->Size < 16) info->IsPackage = false;

#if UNREAL3
	if (info->IsPackage && (strnicmp(info->ShortFilename, "startup", 7) == 0))
	{
		// Register a startup package
		// possible name variants:
		// - startup
		// - startup_int
		// - startup_*
		int startupWeight = 0;
		if (info->ShortFilename[7] == '.')
			startupWeight = 30;							// "startup.upk"
		else if (strnicmp(info->ShortFilename+7, "_int.", 5) == 0)
			startupWeight = 20;							// "startup_int.upk"
		else if (strnicmp(info->ShortFilename+7, "_loc_int.", 9) == 0)
			startupWeight = 20;							// "startup_int.upk"
		else if (info->ShortFilename[7] == '_')
			startupWeight = 1;							// non-int locale, lower priority - use if when other is not detected
		if (startupWeight > GStartupPackageInfoWeight)
		{
			GStartupPackageInfoWeight = startupWeight;
			GStartupPackageInfo = info;
		}
	}
#endif // UNREAL3

	int hash = GetHashForFileName<true>(info->ShortFilename);

	// find if we have previously registered file with the same name
	FastNameComparer FilenameCmp(info->ShortFilename);
	for (CGameFileInfo* prevInfo = GameFileHash[hash]; prevInfo; prevInfo = prevInfo->HashNext)
	{
		if ((prevInfo->FolderIndex == FolderIndex) && FilenameCmp(prevInfo->ShortFilename))
		{
			// this is a duplicate of the file (patch), use new information
			prevInfo->UpdateFrom(info);
			// return allocated info back to pool, so it will be reused next time
			DeallocFileInfo(info);
#if DEBUG_HASH
			appPrintf("--> dup(%s) pkg=%d hash=%X\n", prevInfo->ShortFilename, prevInfo->IsPackage, hash);
#endif
			return prevInfo;
		}
	}

	// Insert new CGameFileInfo into hash table
	if (GameFiles.Num() + 1 >= GameFiles.Max())
	{
		// Resize GameFiles array with large steps
		GameFiles.Reserve(GameFiles.Num() + 1024);
	}
	GameFiles.Add(info);
	if (IsPackage) GNumPackageFiles++;
	GameFolders[FolderIndex].NumFiles++;

	info->HashNext = GameFileHash[hash];
	GameFileHash[hash] = info;

#if DEBUG_HASH
	appPrintf("--> add(%s) pkg=%d hash=%X\n", info->ShortFilename, info->IsPackage, hash);
#endif

	return info;

	unguardf("%s", RegisterInfo.Filename);
}
