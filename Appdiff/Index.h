#ifndef __APPDIFF_INDEX_H__
#define __APPDIFF_INDEX_H__


#include "Core/Core.h"
#include "Unreal/UnCore.h"
#include "Unreal/FileSystem/GameFileSystem.h"
#include "Unreal/FileSystem/UnArchivePak.h"
#include "Unreal/FileSystem/UnArchiveObb.h"


class FPakVFS_appdiff : public FPakVFS
{
public:
	FPakVFS_appdiff(const char* InFilename, const TArray<FString> &AesKeys)
  : FPakVFS(InFilename), AesKeysRef(AesKeys), KeyIndex(-1), NumFiles(0)
  { return; }
	virtual ~FPakVFS_appdiff() {
	  if (Reader) delete Reader;
	}

protected:
  void LoadPakIndexCommon(TArray<byte> &InfoBlock, FMemReader &InfoReader, FArchive* reader, const FPakInfo& info, FString& error);
	// UE4.24 and older
	virtual bool LoadPakIndexLegacy(FArchive* reader, const FPakInfo& info, FString& error);
	// UE4.25 and newer
	virtual bool LoadPakIndex(FArchive* reader, const FPakInfo& info, FString& error);

protected:
  const TArray<FString> &AesKeysRef;
  int32 NumFiles;
  int KeyIndex;
  FPakInfo PakInfo;
  TArray<CRegisterFileInfo> RegInfo;
};


class FFileVFS_appdiff : public FVirtualFileSystem
{
public:
	FFileVFS_appdiff(const char* RelFilePath, int size);
	virtual ~FPakVFS_appdiff() {}

protected:
  TArray<CRegisterFileInfo> RegInfo;
};


void LoadIndex(const TArray<FStaticString<256>> &FilePaths, const TArray<FString> &AesKeys);


#endif // __APPDIFF_INDEX_H__
