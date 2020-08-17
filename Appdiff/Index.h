#ifndef __APPDIFF_INDEX_H__
#define __APPDIFF_INDEX_H__


class FPakVFS_appdiff;
class FString;
class UnPackage_appdiff;
template <int N> class FStaticString;
template <typename T> class TArray;


class CIndexFileInfo
{
public:
  CIndexFileInfo(void)
  : MyIndex(-1), FileSystem(NULL), Package(NULL), Path(""), Filename(""),
    Size(0), SizeInKb(0), ExtraSizeInKb(0), IndexInArchive(-1),
    IsPackage(false), IsPackageScanned(false),
    NumSkeletalMeshes(0), NumStaticMeshes(0), NumAnimations(0), NumTextures(0)
  { return; }
  ~CIndexFileInfo(void);

  int MyIndex;

	FPakVFS_appdiff* FileSystem;
	UnPackage* Package; // non-null when corresponding package is loaded

  FString Path;
  FString Filename;
	int64		Size;
	int64		SizeInKb;
	int64		ExtraSizeInKb;
	int64		IndexInArchive;

	// content information, valid when IsPackageScanned is true
	//todo: can store index in some global Info structure, reuse Info for matching cases,
	//todo: e.g. when uasset has Skel=1+Other=0, or All=0 etc; Index=-1 = not scanned
	bool		IsPackage;
	bool		IsPackageScanned;
	uint16	NumSkeletalMeshes;
	uint16	NumStaticMeshes;
	uint16	NumAnimations;
	uint16	NumTextures;
};


class Index
{
public:
  Index(const TArray<FStaticString<256>> &FilePaths, const TArray<FString> &AesKeys);
  ~Index(void);
  void Print(void);
  TArray<CIndexFileInfo>& GetFileInfos(void) { return d_info; }

protected:
  TArray<FPakVFS_appdiff*> d_vfs;
  TArray<CIndexFileInfo> d_info;

protected:
  Index();
  Index(const Index&);
};


#endif // __APPDIFF_INDEX_H__
