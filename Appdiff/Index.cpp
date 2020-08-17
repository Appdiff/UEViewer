#include <map>

#include "Core/Core.h"
#include "Unreal/UnCore.h"
#include "Unreal/FileSystem/GameFileSystem.h"
#include "Unreal/FileSystem/UnArchivePak.h"
#include "Unreal/FileSystem/UnArchiveObb.h"
#include "Unreal/Unpackage.h"

#include "Appdiff/Index.h"


class FPakVFS_appdiff;
class UnPackage_appdiff;
bool ValidateString(FArchive& Ar);

bool FindUE4Extension(const char *Ext);


class CMapInfo
{
public:
  CMapInfo(void) : d_vfs(NULL), d_index(-1), d_pkg(NULL), d_used(0) { return; }
  CMapInfo(FPakVFS_appdiff* vfs, int index)
  : d_vfs(vfs), d_index(index), d_pkg(NULL), d_used(0)
  { return; }
  CMapInfo(const CMapInfo& MapInfo)
  : d_vfs(MapInfo.d_vfs), d_index(MapInfo.d_index), d_pkg(MapInfo.d_pkg), d_used(MapInfo.d_used)
  { return; }
  FPakVFS_appdiff* d_vfs;
  int d_index;
  UnPackage_appdiff* d_pkg;
  int d_used;
};
struct FStringCmp
{ 
public:
  bool operator()(const FString& a, const FString& b) const 
  {
    return strcmp(*a, *b) < 0;
  } 
};
typedef std::map<FString, CMapInfo, FStringCmp> t_filemap;
typedef std::map<FString, t_filemap, FStringCmp> t_dirmap;


class CRegInfo
{
public:
	CRegInfo() : Filename(""), Path(""), Size(0), IndexInArchive(-1) { return; }
  FString Filename;
  FString Path;
	int64 Size;
	int IndexInArchive;
};


class FPakVFS_appdiff : public FPakVFS
{
public:
	FPakVFS_appdiff(const char* InFilename, const TArray<FString> &AesKeys)
  : FPakVFS(InFilename), AesKeysRef(AesKeys), KeyIndex(-1), NumFiles(0)
  { return; }
	virtual ~FPakVFS_appdiff() {
		if (Reader) {
      delete Reader;
      Reader = NULL;
    }
	}
  const FString& GetFilename(void) const { return Filename; }

protected:
  void LoadPakIndexCommon(TArray<byte> &InfoBlock, FMemReader &InfoReader, FArchive* reader, const FPakInfo& info, FString& error);
	// UE4.24 and older
	virtual bool LoadPakIndexLegacy(FArchive* reader, const FPakInfo& info, FString& error);
	// UE4.25 and newer
	virtual bool LoadPakIndex(FArchive* reader, const FPakInfo& info, FString& error);

public:
  const TArray<FString> &AesKeysRef;
  int32 NumFiles;
  int KeyIndex;
  FPakInfo PakInfo;
  TArray<CRegInfo> RegInfo;
};


class UnPackage_appdiff : public UnPackage
{
	DECLARE_ARCHIVE(UnPackage_appdiff, UnPackage);
protected:
	UnPackage_appdiff(FPakVFS_appdiff* vfs, int IndexInArchive, const FString &Filename, t_filemap &FileMap);
public:
	static UnPackage_appdiff* LoadPackage(FPakVFS_appdiff* vfs, int IndexInArchive, const FString &Filename, t_filemap &FileMap);
};


CIndexFileInfo::~CIndexFileInfo(void)
{
  UnPackage::UnloadPackage(Package);
  return;
}


static FPakVFS_appdiff* LoadFile(const char* FilePath, const TArray<FString> &AesKeys)
{
  FPakVFS_appdiff *vfs = NULL;

	guard(LoadFile);

	const char* ext = strrchr(FilePath, '.');
	if (ext == NULL) return NULL;
	ext++;
	if (stricmp(ext, "pak")) return NULL;
	vfs = new FPakVFS_appdiff(FilePath, AesKeys);
	if (!vfs) return NULL;
	FArchive* reader = new FFileReader(FilePath);
	if (!reader) return NULL;
	reader->Game = GAME_UE4_BASE;
  // ignore non-UE4 extensions for speedup file registration
  //GIsUE4PackageMode = true;
	// read VF directory
	FString error;
	if (!vfs->AttachReader(reader, error))
	{
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
    vfs = NULL;
	}
	// Reset GIsUE4PackageMode back in a case if .pak file appeared in directory
	// by accident.
	//GIsUE4PackageMode = false;

	unguardf("%s", FilePath);

  return vfs;
}


#define TRACK_UNKNOWN_CLASS 0
Index::Index(const TArray<FStaticString<256>> &FilePaths, const TArray<FString> &AesKeys)
{
  for (int i=0; i<FilePaths.Num(); ++i)
  {
    FPakVFS_appdiff* vfs_i = LoadFile(*FilePaths[i], AesKeys);
    if (vfs_i == NULL) continue;
    d_vfs.Add(vfs_i);
  }

  // Map files and look for duplicates; removing dups without further checking
  t_dirmap DirMap;
  for (int i=0; i<d_vfs.Num(); ++i)
  {
    for (int j=0; j<d_vfs[i]->RegInfo.Num(); ++j)
    {
      CRegInfo &E = d_vfs[i]->RegInfo[j];
      t_filemap& Files = DirMap[E.Path];
      if (Files.count(E.Filename))
      {
        appPrintf("FOUND DUP: %s/%s\n", *(E.Path), *(E.Filename));
      }
      else
      {
        CMapInfo MapInfo(d_vfs[i], j);
        Files[E.Filename] = MapInfo;
      }
    }
  }

  // Merge/expand packages
  for (t_dirmap::iterator it = DirMap.begin(); it != DirMap.end(); it++)
  {
    for (t_filemap::iterator it2 = (it->second).begin(); it2 != (it->second).end(); it2++)
    {
      CRegInfo &E = it2->second.d_vfs->RegInfo[it2->second.d_index];
    	const char *ext = strrchr(*(E.Filename), '.');
    	if (!ext) continue;
    	// to know if file is a package or not. Note: this will also make pak loading a bit faster.
  		// Faster case for UE4 files - it has small list of extensions
    	if (!FindUE4Extension(ext+1)) continue;
      it2->second.d_pkg = UnPackage_appdiff::LoadPackage(it2->second.d_vfs,
                                                         E.IndexInArchive,
                                                         it2->first,
                                                         it->second);
    }
  }

  // Figure out number of files that are not bundled with others.
  int Total = 0;
  for (t_dirmap::iterator it = DirMap.begin(); it != DirMap.end(); it++)
    for (t_filemap::iterator it2 = (it->second).begin(); it2 != (it->second).end(); it2++)
      if (!it2->second.d_used)
        Total++;

  // Create list of file information; sorted due to iteration.
  d_info.SetNum(Total);
  int Counter = 0;
#if TRACK_UNKNOWN_CLASS
  std::map<FString, int, FStringCmp> UnknownClass;
#endif
  for (t_dirmap::iterator it = DirMap.begin(); it != DirMap.end(); it++)
  {
    for (t_filemap::iterator it2 = (it->second).begin(); it2 != (it->second).end(); it2++)
    {
      if (it2->second.d_used) continue;
      CRegInfo &E = it2->second.d_vfs->RegInfo[it2->second.d_index];
      CIndexFileInfo &I = d_info[Counter];
      I.MyIndex = Counter;
      I.FileSystem = it2->second.d_vfs;
      I.Path = E.Path;
      I.Filename = E.Filename;
      I.Size = E.Size;
      I.SizeInKb = (E.Size + 512) / 1024;;
      I.IndexInArchive = E.IndexInArchive;
      I.Package = it2->second.d_pkg;
      I.IsPackage = I.Package != NULL;
      I.IsPackageScanned = true;
      if (I.IsPackage)
      {
        UnPackage* package = I.Package;
      	for (int idx=0; idx<package->Summary.ExportCount; idx++)
      	{
      		const char* ObjectClass = package->GetObjectName(package->GetExport(idx).ClassIndex);
      		if (!stricmp(ObjectClass, "SkeletalMesh") || !stricmp(ObjectClass, "DestructibleMesh"))
      			I.NumSkeletalMeshes++;
      		else if (!stricmp(ObjectClass, "StaticMesh"))
      			I.NumStaticMeshes++;
          // whole AnimSet count for UE2 and number of sequences for UE3+
      		else if (!stricmp(ObjectClass, "Animation") ||
                   !stricmp(ObjectClass, "MeshAnimation") ||
                   !stricmp(ObjectClass, "AnimSequence"))
      			I.NumAnimations++;
      		else if (!strnicmp(ObjectClass, "Texture", 7))
      			I.NumTextures++;
#if TRACK_UNKNOWN_CLASS
          else
            UnknownClass[ObjectClass] = 1;
#endif
      	}
      }
      Counter++;
    }
  }
  assert(Counter == Total);

#if TRACK_UNKNOWN_CLASS
  for (std::map<FString, int, FStringCmp>::iterator it=UnknownClass.begin(); it != UnknownClass.end(); it++)
  {
    appPrintf("WARNING: Unknown OBJECTCLASS: %s\n", *(it->first));
  }
#endif

  return;
}

Index::~Index(void)
{
  for (int i=0; i<d_vfs.Num(); ++i) if (d_vfs[i]) delete d_vfs[i];
  return;
}

void Index::Print(void)
{
  for (int i=0; i<d_info.Num(); ++i)
  {
    appPrintf("%s/%s\n", *(d_info[i].Path), *(d_info[i].Filename));
  }
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
		CRegInfo &reg = RegInfo[i];
		reg.Filename = s + 1;
    reg.Path = *Folder;
		reg.Size = E.UncompressedSize;
		reg.IndexInArchive = i;

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
      const char *t1 = *DirectoryFileName;
      const char *t2 = *DirectoryPath;
			CRegInfo &reg = RegInfo[FileIndex];
			reg.Filename = t1;
			reg.Path = t2;
			reg.Size = E.UncompressedSize;
			reg.IndexInArchive = FileIndex;

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

	return true;

	unguard;
}


UnPackage_appdiff::UnPackage_appdiff(FPakVFS_appdiff* vfs, int IndexInArchive, const FString &Filename, t_filemap &FileMap)
{
	guard(UnPackage_appdiff::UnPackage_appdiff);

	IsLoading = true;

  GAesKey = vfs->AesKeysRef[vfs->KeyIndex];
  FArchive* baseLoader = vfs->CreateReader(IndexInArchive);
	Loader = CreateLoader(*Filename, baseLoader);
  if (!Loader) return; // deleted baseLoader in CreateLoader already
	SetupFrom(*Loader);
	if (!Summary.Serialize(*this)) return; // Probably not a package
	Loader->SetupFrom(*this);	// serialization of FPackageFileSummary could change some FArchive properties
	ReplaceLoader();

	LoadNameTable();
	LoadImportTable();
	LoadExportTable();

	// Process Event Driven Loader packages: such packages are split into 2 pieces: .uasset with headers
	// and .uexp with object's data. At this moment we already have FPackageFileSummary fully loaded,
	// so we can replace loader with .uexp file - with providing correct position offset.
	if (Game >= GAME_UE4_BASE && Summary.HeadersSize == Loader->GetFileSize())
	{
		guard(FindUexp);
		char buf[MAX_PACKAGE_PATH];
		appStrncpyz(buf, *Filename, ARRAY_COUNT(buf));
		char* s = strrchr(buf, '.');
		if (!s) s = strchr(buf, 0);
		strcpy(s, ".uexp");
    FString ExpFilename = buf;
    if (FileMap.count(ExpFilename))
		{
      CMapInfo &MapInfo = FileMap[ExpFilename];
      MapInfo.d_used = 1;
      CRegInfo &E = MapInfo.d_vfs->RegInfo[MapInfo.d_index];
      FArchive* expLoader = MapInfo.d_vfs->CreateReader(E.IndexInArchive);
			// Replace loader with this file, but add offset so it will work like it is part of original uasset
			delete Loader;
			Loader = new FReaderWrapper(expLoader, -Summary.HeadersSize);
		}
		else
		{
			appPrintf("WARNING: it seems package %s has missing .uexp file\n", *Filename);
		}
		unguard;
	}

	//PackageMap.Add(this);

	// Release package file handle
	CloseReader();

	unguard;
}


UnPackage_appdiff* UnPackage_appdiff::LoadPackage(FPakVFS_appdiff* vfs, int IndexInArchive, const FString &Filename, t_filemap &FileMap)
{
	guard(UnPackage_appdiff::LoadPackage);

	UnPackage_appdiff* package = new UnPackage_appdiff(vfs, IndexInArchive, Filename, FileMap);
	if (!package->IsValid()) {
    delete package;
    return NULL;
  }
  return package;

	unguard;
}
