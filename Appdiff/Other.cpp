#include "Core/Core.h"
#include "Unreal/UnCore.h"
#include "Unreal/UnObject.h"
#include "Unreal/UnPackage.h"
#include "Unreal/PackageUtils.h"
#include "Appdiff/Index.h"

// includes for file enumeration
#if _WIN32
#	include <io.h>					// for findfirst() set
#else
#	include <dirent.h>				// for opendir() etc
#	include <sys/stat.h>			// for stat()
#endif


void LoadGears4Manifest(const CGameFileInfo* info);


void ListPackages(const TArray<UnPackage*> &Packages) {
	guard(List);

	for (int packageIndex = 0; packageIndex < Packages.Num(); packageIndex++)
	{
		UnPackage* Package = Packages[packageIndex];
		if (Packages.Num() > 1)
		{
			appPrintf("\n%s\n", *Package->GetFilename());
		}
		// dump package exports table
		for (int i = 0; i < Package->Summary.ExportCount; i++)
		{
			const FObjectExport &Exp = Package->ExportTable[i];
			appPrintf("%4d %8X %8X %s %s\n", i, Exp.SerialOffset, Exp.SerialSize, Package->GetObjectName(Exp.ClassIndex), *Exp.ObjectName);
		}
	}

	unguard;

  return;
}


void appGetGameFileInfo(TArray<const CGameFileInfo*> &Info_out)
{
  /*
  for (int i=0; i<GameFiles.Num(); ++i)
  {
    Info_out.Add(GameFiles[i]);
  }
  */
  return;
}

void appLoadPackages(TArray<UnPackage*> &Packages_out,
                     TArray<FStaticString<256>> &FilePaths,
                     const TArray<FString> &AesKeys,
                     bool bShouldLoadPackages)
{
  LoadIndex(FilePaths, AesKeys);
  //for (int i=0; i<FilePaths.Num(); ++i)
  //{
  //  LoadIndex(*FilePaths[i]);
  //}
  return;

  /*
#if GEARS4
	if (GForceGame == GAME_Gears4)
	{
		const CGameFileInfo* manifest = CGameFileInfo::Find("BundleManifest.bin");
		if (manifest)
		{
			LoadGears4Manifest(manifest);
		}
		else
		{
			appErrorNoLog("Gears of War 4: missing BundleManifest.bin file.");
		}
	}
#endif // GEARS4
#if UNREAL4
	// Count sizes of additional files. Should process .uexp and .ubulk files, register their information for .uasset.
  for (int index=0; index<GameFiles.Num(); ++index)
  {
		CGameFileInfo* info = GameFiles[index];
		if (info->IsPackage)
		{
			// Find all files with the same path/name but different extension
			TStaticArray<const CGameFileInfo*, 32> otherFiles;
			info->FindOtherFiles(otherFiles);
			for (const CGameFileInfo* other : otherFiles)
			{
				info->ExtraSizeInKb += other->SizeInKb;
			}
		}
	};
    /*
	ParallelFor(GameFiles.Num(), [](int index)
		{
			CGameFileInfo* info = GameFiles[index];
			if (info->IsPackage)
			{
				// Find all files with the same path/name but different extension
				TStaticArray<const CGameFileInfo*, 32> otherFiles;
				info->FindOtherFiles(otherFiles);
				for (const CGameFileInfo* other : otherFiles)
				{
					info->ExtraSizeInKb += other->SizeInKb;
				}
			}
		});* /
#endif // UNREAL4

  for (int i=0; i<GameFiles.Num(); ++i)
  {
  	TArray<const CGameFileInfo*> Files;
    FString filename;
    GameFiles[i]->GetCleanName(filename);
  	appFindGameFiles(*filename, Files);
    if (Files.Num() && bShouldLoadPackages)
  	{
      UnPackage* Package = UnPackage::LoadPackage(Files[0], true);
      if (Package) Packages_out.Add(Package);
		}
  }
  */

  return;
}

bool appScanRoot(TArray<FStaticString<256>> &FilePaths, const char *dir, TArray<const char*> &packagesToLoad, bool recurse)
{
	guard(ScanGameDirectory);

	char Path[MAX_PACKAGE_PATH];
	bool result = true;
	//todo: check - there's TArray<FStaticString> what's unsafe
	TArray<FStaticString<256>> Filenames;
	Filenames.Empty(256);

#if _WIN32
	appSprintf(ARRAY_ARG(Path), "%s/*.*", dir);
	_finddatai64_t found;
	intptr_t hFind = _findfirsti64(Path, &found);
	if (hFind == -1) return true;
	do
	{
		if (found.name[0] == '.') continue;			// "." or ".."
		// directory -> recurse
		if (found.attrib & _A_SUBDIR)
		{
			if (recurse)
			{
				appSprintf(ARRAY_ARG(Path), "%s/%s", dir, found.name);
				result = appScanRoot(FilePaths, Path, packagesToLoad, recurse);
			}
		}
		else
		{
      bool add = !packagesToLoad.Num();
      for (int i=0; i<packagesToLoad.Num(); ++i) {
        if (!strcmp(found.name, packagesToLoad[i])) {
          add = true;
          break;
        }
      }
      if (add) {
  			Filenames.Add(found.name);
      }
		}
	} while (result && _findnexti64(hFind, &found) != -1);
	_findclose(hFind);
#else
	DIR *find = opendir(dir);
	if (!find) return true;
	struct dirent *ent;
	while (/*result &&*/ (ent = readdir(find)))
	{
		if (ent->d_name[0] == '.') continue;			// "." or ".."
		appSprintf(ARRAY_ARG(Path), "%s/%s", dir, ent->d_name);
		// directory -> recurse
		// note: using 'stat64' here because 'stat' ignores large files
		struct stat64 buf;
		if (stat64(Path, &buf) < 0) continue;			// or break?
		if (S_ISDIR(buf.st_mode))
		{
			if (recurse)
				result = appScanRoot(FilePaths, Path, packagesToLoad, recurse);
		}
		else
		{
      bool add = !packagesToLoad.Num();
      for (int i=0; i<packagesToLoad.Num(); ++i) {
        if (!strcmp(ent->d_name, packagesToLoad[i])) {
          add = true;
          break;
        }
      }
      if (add) {
  			Filenames.Add(ent->d_name);
      }
		}
	}
	closedir(find);
#endif

	// Register files in sorted order - should be done for pak files, so patches will work.
	Filenames.Sort([](const FStaticString<256>& p1, const FStaticString<256>& p2) -> int
		{
			return stricmp(*p1, *p2) > 0;
		});

	for (int i = 0; i < Filenames.Num(); i++)
	{
		appSprintf(ARRAY_ARG(Path), "%s/%s", dir, *Filenames[i]);
    FilePaths.Add(Path);
	}

	return result;

	unguard;
}


UObject *appLoadObjects(TArray<UObject*> &Objects_out,
                        const TArray<UnPackage*> &Packages,
                        TArray<const char*> &objectsToLoad,
                        const char *argClassName,
                        const char *attachAnimName,
                        bool bShouldLoadObjects)
{
  UObject *GForceAnimSet = NULL;
	// load requested objects if any, or fully load everything
	UObject::BeginLoad();
	if (objectsToLoad.Num())
	{
		// selectively load objects
		int totalFound = 0;
		for (int objIdx = 0; objIdx < objectsToLoad.Num(); objIdx++)
		{
			const char *objName   = objectsToLoad[objIdx];
			const char *className = (objIdx == 0) ? argClassName : NULL;
			int found = 0;
			for (int pkg = 0; pkg < Packages.Num(); pkg++)
			{
				UnPackage *Package2 = Packages[pkg];
				// load specific object(s)
				int idx = -1;
				while (true)
				{
					idx = Package2->FindExport(objName, className, idx + 1);
					if (idx == INDEX_NONE) break;		// not found in this package

					found++;
					totalFound++;
					appPrintf("Export \"%s\" was found in package \"%s\"\n", objName, *Package2->GetFilename());

					// create object from package
					UObject *Obj = Package2->CreateExport(idx);
					if (Obj)
					{
						Objects_out.Add(Obj);
						if (objName == attachAnimName && (Obj->IsA("MeshAnimation") || Obj->IsA("AnimSet")))
							GForceAnimSet = Obj;
					}
				}
				if (found) break;
			}
			if (!found)
			{
				appPrintf("Export \"%s\" was not found in specified package(s)\n", objName);
				exit(1);
			}
		}
		appPrintf("Found %d object(s)\n", totalFound);
	}
	else if (bShouldLoadObjects)
	{
		// fully load all packages
		for (int pkg = 0; pkg < Packages.Num(); pkg++)
			LoadWholePackage(Packages[pkg]);
	}
	UObject::EndLoad();
  return GForceAnimSet;
}

