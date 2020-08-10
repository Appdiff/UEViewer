#ifndef __APPDIFF_OTHER_H__
#define __APPDIFF_OTHER_H__


template<int N> class FStaticString;
struct CGameFileInfo;
void appGetGameFileInfo(TArray<const CGameFileInfo*> &out);
UObject *appLoadObjects(TArray<UObject*> &out, const TArray<UnPackage*> &Packages, TArray<const char*> &objectsToLoad, const char *argClassName, const char *attachAnimName, bool ShouldLoad);
void appLoadPackages(TArray<UnPackage*> &out, TArray<FStaticString<256>> &FilePaths, const TArray<FString> &AesKeys, bool ShouldLoad);
bool appScanRoot(TArray<FStaticString<256>> &FilePaths, const char *dir, TArray<const char*> &packagesToLoad, bool recurse = true);
void ListPackages(const TArray<UnPackage*> &Packages);


#endif // __APPDIFF_OTHER_H__
