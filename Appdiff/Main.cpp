#include <map>
#include "Core.h"

#if _WIN32
#include <signal.h>					// abort handler
#endif


#include "Unreal/UnCore.h"
#include "Appdiff/Assets.h"
#include "Appdiff/Index.h"
#include "Appdiff/Other.h"
#include "Appdiff/aes_keys.h"
#include "Exporters/Exporters.h"
#include "UmodelTool/UmodelCommands.h"
#include "UmodelTool/UmodelSettings.h"
#include "UmodelTool/Version.h"
#include "UmodelTool/MiscStrings.h"
#include "Unreal/GameDatabase.h"
#include "Unreal/UnObject.h"
#include "Unreal/UnPackage.h"
#include "Unreal/PackageUtils.h"


//#define SHOW_HIDDEN_SWITCHES		1
//#define DUMP_MEM_ON_EXIT			1


// Note: declaring this variable in global scope will have side effect that
// GSettings.Reset() will be called before main() executed.
CUmodelSettings GSettings;

#if THREADING
extern bool GEnableThreads;
#endif

/*-----------------------------------------------------------------------------
	Usage information
-----------------------------------------------------------------------------*/

static void PrintUsage()
{
	appPrintf(
			"UE Viewer - extractor\n"
			"Usage: umodel [command] [options] <package>\n"
			"       umodel @<response_file>\n"
			"\n"
			"    <package>       name of package to load - this could be a file name\n"
			"                    with or without extension, or wildcard\n"
			"    <object>        name of object to load\n"
			"    <class>         class of object to load (useful, when trying to load\n"
			"                    object with ambiguous name)\n"
			"\n"
			"Help information:\n"
			"    -help           display this help page\n"
			"    -version        display umodel version information\n"
			"    -taglist        list of tags to override game autodetection (for -game=nnn option)\n"
			"    -gamelist       list of supported games\n"
			"\n"
			"Developer commands:\n"
			"    -log=file       write log to the specified file\n"
#if SHOW_HIDDEN_SWITCHES

#	if THREADING
			"    -nomt           disable multithreading optimizations\n"
#	endif
#endif // SHOW_HIDDEN_SWITCHES
			"\n"
			"Options:\n"
			"    -path=PATH      path to game installation directory; if not specified,\n"
			"                    program will search for packages in current directory\n"
			"    -game=tag       override game autodetection (see -taglist for variants)\n"
			"    -pkgver=nnn     override package version (advanced option!)\n"
			"    -aes=key        provide AES decryption key for encrypted pak files,\n"
			"                    key is ASCII or hex string (hex format is 0xAABBCCDD)\n"
			"\n"
			"Compatibility options:\n"
			"    -nomesh         disable loading of SkeletalMesh classes in a case of\n"
			"                    unsupported data format\n"
			"    -noanim         disable loading of MeshAnimation classes\n"
			"    -nostat         disable loading of StaticMesh class\n"
			"    -notex          disable loading of Material classes\n"
			"    -nomorph        disable loading of MorphTarget class\n"
			"    -nolightmap     disable loading of Lightmap textures\n"
			"    -sounds         allow export of sounds\n"
			"    -3rdparty       allow 3rd party asset export (ScaleForm, FaceFX)\n"
			"    -lzo|lzx|zlib   force compression method for UE3 fully-compressed packages\n"
			"\n"
			"Platform selection:\n"
			"    -ps3            Playstation 3\n"
			"    -ps4            Playstation 4\n"
			"    -nsw            Nintendo Switch\n"
			"    -ios            iOS (iPhone/iPad)\n"
			"    -android        Android\n"
			"\n");

	appPrintf(
			"Export options:\n"
			"    -out=PATH       export everything into PATH instead of the current directory\n"
			"    -uncook         use original package name as a base export directory (UE3)\n"
			"    -groups         use group names instead of class names for directories (UE1-3)\n"
			"    -uc             create unreal script when possible\n"
			"    -lods           export all available mesh LOD levels\n"
			"    -notgacomp      disable TGA compression\n"
			"    -nooverwrite    prevent existing files from being overwritten (better\n"
			"                    performance)\n"
			"\n"
			"Supported resources for export:\n"
			"    SkeletalMesh    exported as ActorX psk file, MD5Mesh or glTF\n"
			"    MeshAnimation   exported as ActorX psa file or MD5Anim\n"
			"    VertMesh        exported as Unreal 3d file\n"
			"    StaticMesh      exported as psk file with no skeleton (pskx) or glTF\n"
			"    Texture         exported in tga or dds format\n"
			"    Sounds          file extension depends on object contents\n"
			"    ScaleForm       gfx\n"
			"    FaceFX          fxa\n"
			"    Sound           exported \"as is\"\n"
			"\n"
			"For list of supported games please use -gamelist option.\n"
	);

	appPrintf(
			"\n"
			"For details and updates please visit %s\n", GUmodelHomepage
	);
}


static void PrintVersionInfo()
{
	appPrintf(
			"umodel.appdiff\n" "%s\n" "%s\n" "%s\n",
			GBuildString, GCopyrightString, GUmodelHomepage
	);
}


/*-----------------------------------------------------------------------------
	Main function
-----------------------------------------------------------------------------*/

#define OPT_BOOL(name,var)				{ name, (byte*)&var, true  },
#define OPT_NBOOL(name,var)				{ name, (byte*)&var, false },
#define OPT_VALUE(name,var,value)		{ name, (byte*)&var, value },

struct OptionInfo
{
	const char	*name;
	byte		*variable;
	byte		value;
};

static bool ProcessOption(const OptionInfo *Info, int Count, const char *Option)
{
	for (int i = 0; i < Count; i++)
	{
		const OptionInfo& c = Info[i];
		if (stricmp(c.name, Option) != 0) continue;
		*c.variable = c.value;
		return true;
	}
	return false;
}

// Display error message about wrong command line and then exit.
static void CommandLineError(const char *fmt, ...)
{
	va_list	argptr;
	va_start(argptr, fmt);
	char buf[4096];
	int len = vsnprintf(ARRAY_ARG(buf), fmt, argptr);
	va_end(argptr);
	if (len < 0 || len >= sizeof(buf) - 1) exit(1);

	appPrintf("UModel: bad command line: %s\nTry \"umodel -help\" for more information.\n", buf);
	exit(1);
}

static void ExceptionHandler()
{
	FFileWriter::CleanupOnError();
#if DO_GUARD
	GError.StandardHandler();
#endif // DO_GUARD
	exit(1);
}

#if _WIN32
// AbortHandler on linux will cause infinite recurse, but works well on Windows
static void AbortHandler(int signal)
{
	if (GError.History[0])
	{
		appPrintf("abort called during error handling\n", signal);
#if VSTUDIO_INTEGRATION
		__debugbreak();
#endif
		exit(1);
	}
	appError("abort() called");
}
#endif

#if UNREAL4

int UE4UnversionedPackage(int verMin, int verMax)
{
	appErrorNoLog("Unversioned UE4 packages are not supported. Please restart UModel and select UE4 version in range %d-%d using UI or command line.", verMin, verMax);
	return -1;
}

bool UE4EncryptedPak()
{
	return true;
}

#endif // UNREAL4

int main(int argc, const char **argv)
{
	appInitPlatform();

#if PRIVATE_BUILD
	appPrintf("PRIVATE BUILD\n");
#endif
#if MAX_DEBUG
	appPrintf("DEBUG BUILD\n");
#endif

#if DO_GUARD
	TRY {
#endif

	PROFILE_IF(false);
	guard(Main);

	GSettings.Load();
  GSettings.Startup.SetPath("."); // default, can be overwritten by options
  GSettings.Export.TextureFormat = ETextureExportFormat::png;
	GSettings.Export.SkeletalMeshFormat = EExportMeshFormat::psk;
  GSettings.Export.StaticMeshFormat = EExportMeshFormat::psk;

	// display usage
	if (argc < 2)
	{
		PrintUsage();
		exit(0);
	}

  FString TempPath;
	TArray<const char*> packagesToLoad;
	TArray<const char*> params;
  TArray<FString> AesKeys;
	const char *attachAnimName = NULL;
	for (int arg = 1; arg < argc; arg++)
	{
		const char *opt = argv[arg];
		if (opt[0] != '-')
		{
			params.Add(opt);
			continue;
		}

		opt++;			// skip '-'
		// simple options
		static const OptionInfo options[] =
		{
			OPT_BOOL ("uncook",  GSettings.Export.SaveUncooked)
			OPT_BOOL ("groups",  GSettings.Export.SaveGroups)
			OPT_BOOL ("lods",    GExportLods)
			OPT_BOOL ("uc",      GExportScripts)
			// disable classes
			OPT_NBOOL("nomesh",  GSettings.Startup.UseSkeletalMesh)
			OPT_NBOOL("nostat",  GSettings.Startup.UseStaticMesh)
			OPT_NBOOL("noanim",  GSettings.Startup.UseAnimation)
			OPT_NBOOL("notex",   GSettings.Startup.UseTexture)
			OPT_NBOOL("nomorph", GSettings.Startup.UseMorphTarget)
			OPT_NBOOL("nolightmap", GSettings.Startup.UseLightmapTexture)
			OPT_BOOL ("sounds",  GSettings.Startup.UseSound)
			OPT_BOOL ("notgacomp", GNoTgaCompress)
			OPT_BOOL ("nooverwrite", GDontOverwriteFiles)
			// platform
			OPT_VALUE("ps3",     GSettings.Startup.Platform, PLATFORM_PS3)
			OPT_VALUE("ps4",     GSettings.Startup.Platform, PLATFORM_PS4)
			OPT_VALUE("nsw",     GSettings.Startup.Platform, PLATFORM_SWITCH)
			OPT_VALUE("ios",     GSettings.Startup.Platform, PLATFORM_IOS)
			OPT_VALUE("android", GSettings.Startup.Platform, PLATFORM_ANDROID)
			// UE3 compression method
			OPT_VALUE("lzo",     GSettings.Startup.PackageCompression, COMPRESS_LZO )
			OPT_VALUE("zlib",    GSettings.Startup.PackageCompression, COMPRESS_ZLIB)
			OPT_VALUE("lzx",     GSettings.Startup.PackageCompression, COMPRESS_LZX )
		};
		if (ProcessOption(ARRAY_ARG(options), opt))
			continue;
		// more complex options
		else if (!strnicmp(opt, "log=", 4))
		{
			appOpenLogFile(opt+4);
		}
		else if (!strnicmp(opt, "path=", 5))
		{
      TempPath = opt+5;
      if (TempPath.EndsWith("/")) {
        TempPath.RemoveFromEnd("/");
      }
			GSettings.Startup.SetPath(*TempPath);
		}
		else if (!strnicmp(opt, "out=", 4))
		{
      TempPath = opt+4;
      if (TempPath.EndsWith("/")) {
        TempPath.RemoveFromEnd("/");
      }
			GSettings.Export.SetPath(*TempPath);
			GSettings.SavePackages.SetPath(*TempPath);
		}
		else if (!strnicmp(opt, "game=", 5))
		{
			int tag = FindGameTag(opt+5);
			if (tag == -1)
			{
				appPrintf("ERROR: unknown game tag \"%s\". Use -taglist option to display available tags.\n", opt+5);
				exit(0);
			}
			GSettings.Startup.GameOverride = tag;
		}
		else if (!strnicmp(opt, "pkgver=", 7))
		{
			int ver = atoi(opt+7);
			if (ver < 1)
			{
				appPrintf("ERROR: pkgver number is not valid: %s\n", opt+7);
				exit(0);
			}
			GForcePackageVersion = ver;
		}
		else if (!stricmp(opt, "3rdparty"))
		{
			GSettings.Startup.UseScaleForm = GSettings.Startup.UseFaceFx = true;
		}
		else if (!strnicmp(opt, "aes=", 4))
		{
      ParseKeys(AesKeys, opt+4);
		}
		// information commands
		else if (!stricmp(opt, "taglist"))
		{
			PrintGameList(true);
			return 0;
		}
		else if (!stricmp(opt, "gamelist"))
		{
			appPrintf("List of supported games:\n\n");
			PrintGameList();
			return 0;
		}
		else if (!stricmp(opt, "help"))
		{
			PrintUsage();
			return 0;
		}
		else if (!stricmp(opt, "version"))
		{
			PrintVersionInfo();
			return 0;
		}
#if THREADING
		else if (!stricmp(opt, "nomt"))
		{
			GEnableThreads = false;
		}
#endif
		else
		{
			CommandLineError("invalid option: -%s", opt);
		}
	}
	if (params.Num() > 1)
	{
		CommandLineError("too many arguments, please check your command line.\n");
	}

	// apply some of GSettings
	GForceGame = GSettings.Startup.GameOverride;	// force game fore scanning any game files
	GForcePlatform = GSettings.Startup.Platform;
	GForceCompMethod = GSettings.Startup.PackageCompression;
	GSettings.Export.Apply();

	// Parse UMODEL [package_name]
	if (params.Num() >= 1)
	{
		packagesToLoad.Add(params[0]);
	}
#if GEARS4
	if (GForceGame == GAME_Gears4)
	{
    bool FoundGears4 = false;
    for (int i=0; i<packagesToLoad.Num(); ++i)
    {
      if (!strcmp("BundleManifest.bin", packagesToLoad[i]))
      {
        FoundGears4 = true;
        break;
      }
    }
    if (!FoundGears4)
    {
  		packagesToLoad.Add("BundleManifest.bin");
    }
  }
#endif

#if PROFILE
	appResetProfiler();
#endif

  TArray<FStaticString<256>> FilePaths;
  appScanRoot(FilePaths, *GSettings.Startup.GamePath, packagesToLoad, true);
  Index index(FilePaths, AesKeys);
  //index.Print();
  TArray<CIndexFileInfo>& FileInfos = index.GetFileInfos();
  bool IsInitialized = false;
  for (int i=0; i<FileInfos.Num(); ++i)
  {
    CIndexFileInfo &FileInfo = FileInfos[i];
    if (FileInfo.IsPackage)
    {
      if (!IsInitialized)
      {
        InitClassAndExportSystems(FileInfo.Package->Game);
        IsInitialized = true;
      }
    	BeginExport(true);
  		LoadWholePackage(FileInfo.Package, NULL);
  		ExportObjects(NULL, NULL);
      ReleaseAllObjects();
    	EndExport(true);
    }
    else
    {
      // Do nothing for now.  Need to figure out how encryption impacts it.
      // Also need to figure out if there are any special serialization steps
      // that are required for exporting.
      ;
    }
  }
  return 0;


  bool bShouldLoadPackages = true;
  TArray<UnPackage*> Packages;
  appLoadPackages(Packages, FilePaths, AesKeys, bShouldLoadPackages);
  return 0;
  TArray<const CGameFileInfo*> GameFiles;
  appGetGameFileInfo(GameFiles);

  ListPackages(Packages);
	return 0;
	//SavePackages(GameFiles);
	//return 0;

	// register exporters and classes
	InitClassAndExportSystems(Packages[0]->Game);

	//DisplayPackageStats(Packages);
	//return 0;					// already displayed when loaded package; extend it?

	TArray<const char*> objectsToLoad;
  bool bShouldLoadObjects = false;
	TArray<UObject*> Objects;
  appLoadObjects(Objects,
                 Packages,
                 objectsToLoad,
                 NULL,
                 attachAnimName,
                 bShouldLoadObjects);
	if (!UObject::GObjObjects.Num() && bShouldLoadObjects)
	{
		appPrintf("\nThe specified package(s) has no supported objects.\n\n");
		appPrintf("Selected package(s):\n");
		for (int i = 0; i < Packages.Num(); i++)
			appPrintf("  %s\n", *Packages[i]->GetFilename());
		appPrintf("\n");
		// display list of classes
		DisplayPackageStats(Packages);
    ReleaseAllObjects();
		return 0;
	}

#if PROFILE
	appPrintProfiler();
#endif

  if (false)  //(mainCmd == CMD_Export)
  {
		// If we have list of objects, the process only those ones. Otherwise, process full packages.
		if (Objects.Num())
		{
			BeginExport(true);
      // will export everything if "Objects" array is empty, however we're calling ExportPackages() in this case
      ExportObjects(&Objects);
			EndExport();
		}
		else
		{
			ExportPackages(Packages);
		}
  }

  ReleaseAllObjects();

#if DUMP_MEM_ON_EXIT
	//!! note: CUmodelApp is not destroyed here
	appPrintf("Memory: allocated " FORMAT_SIZE("d") " bytes in %d blocks\n", GTotalAllocationSize, GTotalAllocationCount);
	appDumpMemoryAllocations();
#endif

	unguardf("umodel_build=%s", STR(GIT_REVISION));	// using string constant to allow non-git builds (with GIT_REVISION 'unknown')

#if DO_GUARD
	} CATCH_CRASH {
		ExceptionHandler();
	}
#endif

	return 0;
}
