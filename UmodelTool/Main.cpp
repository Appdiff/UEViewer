#include "Core.h"

#if _WIN32
#include <signal.h>					// abort handler
#endif

// Classes for registration
#include "UnCore.h"
#include "UnObject.h"
#include "UnPackage.h"
#include "Unreal/FileSystem/GameFileSystem.h"
#include "UnrealMesh/UnAnimNotify.h"

#include "UnrealMaterial/UnMaterial.h"
#include "UnrealMaterial/UnMaterial2.h"
#include "UnrealMaterial/UnMaterial3.h"
#include "UnrealMaterial/UnMaterialExpression.h"

#include "UnrealMesh/UnMesh2.h"
#include "UnrealMesh/UnMesh3.h"
#include "UnrealMesh/UnMesh4.h"

#include "UnSound.h"
#include "UnThirdParty.h"

#include "Exporters/Exporters.h"

#if DECLARE_VIEWER_PROPS
#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#endif

#include "GameDatabase.h"
#include "PackageUtils.h"

#include "UmodelApp.h"
#include "UmodelCommands.h"
#include "Version.h"
#include "MiscStrings.h"

#define APP_CAPTION					"UE Viewer"

//#define SHOW_HIDDEN_SWITCHES		1
//#define DUMP_MEM_ON_EXIT			1


// Note: declaring this variable in global scope will have side effect that
// GSettings.Reset() will be called before main() executed.
CUmodelSettings GSettings;

extern bool GEnableThreads;

/*-----------------------------------------------------------------------------
	Table of known Unreal classes
-----------------------------------------------------------------------------*/

static void RegisterCommonUnrealClasses()
{
	// classes and structures
	RegisterCoreClasses();
BEGIN_CLASS_TABLE

	REGISTER_MATERIAL_CLASSES
	REGISTER_ANIM_NOTIFY_CLASSES
#if BIOSHOCK
	REGISTER_MATERIAL_CLASSES_BIO
	REGISTER_MESH_CLASSES_BIO
#endif
#if SPLINTER_CELL
	REGISTER_MATERIAL_CLASSES_SCELL
#endif
#if UNREAL3
	REGISTER_MATERIAL_CLASSES_U3		//!! needed for Bioshock 2 too
#endif

#if DECLARE_VIEWER_PROPS
	REGISTER_SKELMESH_VCLASSES
	REGISTER_STATICMESH_VCLASSES
	REGISTER_MATERIAL_VCLASSES
#endif // DECLARE_VIEWER_PROPS

END_CLASS_TABLE
	// enumerations
	REGISTER_MATERIAL_ENUMS
#if UNREAL3
	REGISTER_MATERIAL_ENUMS_U3
	REGISTER_MESH_ENUMS_U3
#endif
}


static void RegisterUnrealClasses2()
{
BEGIN_CLASS_TABLE
	REGISTER_MESH_CLASSES_U2
#if UNREAL1
	REGISTER_MESH_CLASSES_U1
#endif
#if RUNE
	REGISTER_MESH_CLASSES_RUNE
#endif
END_CLASS_TABLE
}


static void RegisterUnrealClasses3()
{
#if UNREAL3
BEGIN_CLASS_TABLE
//	REGISTER_MATERIAL_CLASSES_U3 -- registered for Bioshock in RegisterCommonUnrealClasses()
	REGISTER_MESH_CLASSES_U3
	REGISTER_EXPRESSION_CLASSES
#if TUROK
	REGISTER_MESH_CLASSES_TUROK
#endif
#if MASSEFF
	REGISTER_MESH_CLASSES_MASSEFF
#endif
#if DCU_ONLINE
	REGISTER_MATERIAL_CLASSES_DCUO
#endif
#if TRANSFORMERS
	REGISTER_MESH_CLASSES_TRANS
#endif
#if MKVSDC
	REGISTER_MESH_CLASSES_MK
#endif
END_CLASS_TABLE
#endif // UNREAL3
	SuppressUnknownClass("UBodySetup");
}


static void RegisterUnrealClasses4()
{
#if UNREAL4
BEGIN_CLASS_TABLE
	REGISTER_MESH_CLASSES_U4
	REGISTER_MATERIAL_CLASSES_U4
	REGISTER_EXPRESSION_CLASSES
END_CLASS_TABLE
	REGISTER_MATERIAL_ENUMS_U4
	REGISTER_MESH_ENUMS_U4
#endif // UNREAL4
	SuppressUnknownClass("UMaterialExpression*"); // wildcard
	SuppressUnknownClass("UMaterialFunction");
	SuppressUnknownClass("UPhysicalMaterial");
	SuppressUnknownClass("UBodySetup");
	SuppressUnknownClass("UNavCollision");
}



static void RegisterUnrealSoundClasses()
{
BEGIN_CLASS_TABLE
	REGISTER_SOUND_CLASSES
#if UNREAL3
	REGISTER_SOUND_CLASSES_UE3
#endif
#if TRANSFORMERS
	REGISTER_SOUND_CLASSES_TRANS
#endif
#if UNREAL4
	REGISTER_SOUND_CLASSES_UE4
#endif
END_CLASS_TABLE
}


static void RegisterUnreal3rdPartyClasses()
{
#if UNREAL3
BEGIN_CLASS_TABLE
	REGISTER_3RDP_CLASSES
END_CLASS_TABLE
#endif
}

static void RegisterClasses(int game)
{
	// prepare classes
	// note: we are registering classes after loading package: in this case we can know engine version (1/2/3)
	RegisterCommonUnrealClasses();
	if (game < GAME_UE3)
	{
		RegisterUnrealClasses2();
	}
	else if (game < GAME_UE4_BASE)
	{
		RegisterUnrealClasses3();
		RegisterUnreal3rdPartyClasses();
	}
	else
	{
		RegisterUnrealClasses4();
	}
	if (GSettings.Startup.UseSound) RegisterUnrealSoundClasses();

	// remove some class loaders when requested by command line
	if (!GSettings.Startup.UseAnimation)
	{
		UnregisterClass("MeshAnimation", true);
		UnregisterClass("AnimSet", true);
		UnregisterClass("AnimSequence", true);
		UnregisterClass("AnimNotify", true);
	}
	if (!GSettings.Startup.UseSkeletalMesh)
	{
		UnregisterClass("SkeletalMesh", true);
		UnregisterClass("SkeletalMeshSocket", true);
		UnregisterClass("MorphTarget", false);
		if (!GSettings.Startup.UseAnimation)
			UnregisterClass("Skeleton", false);
	}
	if (!GSettings.Startup.UseStaticMesh) UnregisterClass("StaticMesh", true);
	if (!GSettings.Startup.UseTexture)
	{
		UnregisterClass("UnrealMaterial", true);
		UnregisterClass("MaterialExpression", true);
	}
	if (!GSettings.Startup.UseMorphTarget) UnregisterClass("MorphTarget", false);
	if (!GSettings.Startup.UseLightmapTexture) UnregisterClass("LightMapTexture2D", true);
	if (!GSettings.Startup.UseScaleForm) UnregisterClass("SwfMovie", true);
	if (!GSettings.Startup.UseFaceFx)
	{
		UnregisterClass("FaceFXAnimSet", true);
		UnregisterClass("FaceFXAsset", true);
	}
}


/*-----------------------------------------------------------------------------
	Exporters
-----------------------------------------------------------------------------*/


static void CallExportSkeletalMesh(const CSkeletalMesh* Mesh)
{
	assert(Mesh);
	switch (GSettings.Export.SkeletalMeshFormat)
	{
	case EExportMeshFormat::psk:
	default:
		ExportPsk(Mesh);
		break;
	case EExportMeshFormat::gltf:
		ExportSkeletalMeshGLTF(Mesh);
		break;
	case EExportMeshFormat::md5:
		ExportMd5Mesh(Mesh);
		break;
	}
}

static void CallExportStaticMesh(const CStaticMesh* Mesh)
{
	assert(Mesh);
	switch (GSettings.Export.StaticMeshFormat)
	{
	case EExportMeshFormat::psk:
	default:
		ExportStaticMesh(Mesh);
		break;
	case EExportMeshFormat::gltf:
		ExportStaticMeshGLTF(Mesh);
		break;
	}
}

static void CallExportAnimation(const CAnimSet* Anim)
{
	assert(Anim);
	switch (GSettings.Export.SkeletalMeshFormat)
	{
	case EExportMeshFormat::psk:
	default:
		ExportPsa(Anim);
		break;
	case EExportMeshFormat::gltf:
		appPrintf("ERROR: glTF animation could be exported from mesh viewer only.\n");
		break;
	case EExportMeshFormat::md5:
		ExportMd5Anim(Anim);
		break;
	}
}

static void RegisterExporters()
{
	RegisterExporter<USkeletalMesh>([](const USkeletalMesh* Mesh) { CallExportSkeletalMesh(Mesh->ConvertedMesh); });
	RegisterExporter<UMeshAnimation>([](const UMeshAnimation* Anim) { CallExportAnimation(Anim->ConvertedAnim); });
	RegisterExporter<UVertMesh>(Export3D);
	RegisterExporter<UStaticMesh>([](const UStaticMesh* Mesh) { CallExportStaticMesh(Mesh->ConvertedMesh); });
	RegisterExporter<USound>(ExportSound);
#if UNREAL3
	RegisterExporter<USkeletalMesh3>([](const USkeletalMesh3* Mesh) { CallExportSkeletalMesh(Mesh->ConvertedMesh); });
	RegisterExporter<UAnimSet>([](const UAnimSet* Anim) { CallExportAnimation(Anim->ConvertedAnim); });
	RegisterExporter<UStaticMesh3>([](const UStaticMesh3* Mesh) { CallExportStaticMesh(Mesh->ConvertedMesh); });
	RegisterExporter<USoundNodeWave>(ExportSoundNodeWave);
	RegisterExporter<USwfMovie>(ExportGfx);
	RegisterExporter<UFaceFXAnimSet>(ExportFaceFXAnimSet);
	RegisterExporter<UFaceFXAsset>(ExportFaceFXAsset);
#endif // UNREAL3
#if UNREAL4
	RegisterExporter<USkeletalMesh4>([](const USkeletalMesh4* Mesh) { CallExportSkeletalMesh(Mesh->ConvertedMesh); });
	RegisterExporter<UStaticMesh4>([](const UStaticMesh4* Mesh) { CallExportStaticMesh(Mesh->ConvertedMesh); });
	RegisterExporter<USkeleton>([](const USkeleton* Anim) { CallExportAnimation(Anim->ConvertedAnim); });
	RegisterExporter<USoundWave>(ExportSoundWave4);
#endif // UNREAL4
	RegisterExporter<UUnrealMaterial>(ExportMaterial);			// register this after Texture/Texture2D exporters
}


/*-----------------------------------------------------------------------------
	Initialization of class and export systems
-----------------------------------------------------------------------------*/

void InitClassAndExportSystems(int Game)
{
	static bool initialized = false;
	if (initialized) return;
	initialized = true;

	RegisterExporters();
	RegisterClasses(Game);
#if BIOSHOCK
	if (Game == GAME_Bioshock)
	{
		//!! should change this code!
		CTypeInfo::RemapProp("UShader", "Opacity", "Opacity_Bio"); //!!
	}
#endif // BIOSHOCK
}

/*-----------------------------------------------------------------------------
	Usage information
-----------------------------------------------------------------------------*/

static void PrintUsage()
{
	appPrintf(
			"UE Viewer - extractor\n"
			"Usage: umodel [command] [options] <package> [<object> [<class>]]\n"
			"       umodel @<response_file>\n"
			"\n"
			"    <package>       name of package to load - this could be a file name\n"
			"                    with or without extension, or wildcard\n"
			"    <object>        name of object to load\n"
			"    <class>         class of object to load (useful, when trying to load\n"
			"                    object with ambiguous name)\n"
			"\n"
			"Commands:\n"
			"    -view           (default) visualize object; when no <object> specified\n"
			"                    will load whole package\n"
			"    -list           list contents of package\n"
			"    -export         export specified object or whole package\n"
			"    -save           save specified packages\n"
			"\n"
			"Help information:\n"
			"    -help           display this help page\n"
			"    -version        display umodel version information\n"
			"    -taglist        list of tags to override game autodetection (for -game=nnn option)\n"
			"    -gamelist       list of supported games\n"
			"\n"
			"Developer commands:\n"
			"    -log=file       write log to the specified file\n"
			"    -dump           dump object information to console\n"
			"    -pkginfo        load package and display its information\n"
			"    -testexport     perform fake export\n"
#if SHOW_HIDDEN_SWITCHES
#	if VSTUDIO_INTEGRATION
			"    -debug          invoke system crash handler on errors\n"
#	endif
			"    -nomt           disable multithreading optimizations\n"
#endif // SHOW_HIDDEN_SWITCHES
			"\n"
			"Options:\n"
			"    -path=PATH      path to game installation directory; if not specified,\n"
			"                    program will search for packages in current directory\n"
			"    -game=tag       override game autodetection (see -taglist for variants)\n"
			"    -pkgver=nnn     override package version (advanced option!)\n"
			"    -pkg=package    load extra package (in addition to <package>)\n"
			"    -obj=object     specify object(s) to load\n"
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
			"    -all            used with -dump, will dump all objects instead of specified one\n"
			"    -uncook         use original package name as a base export directory (UE3)\n"
			"    -groups         use group names instead of class names for directories (UE1-3)\n"
			"    -uc             create unreal script when possible\n"
			"    -psk            use ActorX format for meshes (default)\n"
			"    -md5            use md5mesh/md5anim format for skeletal mesh\n"
			"    -gltf           use glTF 2.0 format for mesh\n"
			"    -lods           export all available mesh LOD levels\n"
			"    -dds            export textures in DDS format whenever possible\n"
			"    -png            export textures in PNG format instead of TGA\n"
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
			"UE Viewer (UModel)\n" "%s\n" "%s\n" "%s\n",
			GBuildString, GCopyrightString, GUmodelHomepage
	);
}


/*-----------------------------------------------------------------------------
	Main function
-----------------------------------------------------------------------------*/

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

// forward
void UISetExceptionHandler(void (*Handler)());

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

static void CheckHexAesKey()
{
#define ishex(c)		( (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') )
#define hextodigit(c)	( (c >= 'a') ? c - 'a' + 10 : c - '0' )

	if (GAesKey.Len() < 3) return;
	const char* s = *GAesKey;

	// Hex key starts with "0x"
	if (*s++ != '0') return;
	if (tolower(*s++) != 'x') return;

	FString NewKey;
	NewKey.Empty(GAesKey.Len() / 2 + 1);

	int remains = GAesKey.Len() - 2;
	if (remains & 1)
	{
		appErrorNoLog("Hexadecimal AES key contains odd number of characters");
	}
	while (remains > 0)
	{
		uint8 b = 0;
		if ((remains & 1) == 0)
		{
			// this code will not be executed only in a case of odd character count, for the first char
			char c = tolower(*s++);
			if (!ishex(c))
			{
				appErrorNoLog("Illegal character in hexadecimal AES key");
			}
			b = hextodigit(c) << 4;
			remains--;
		}
		char c = tolower(*s++);
		if (!ishex(c))
		{
			appErrorNoLog("Illegal character in hexadecimal AES key");
		}
		b |= hextodigit(c);
		remains--;

		NewKey.AppendChar((char)b);
	}

	GAesKey = NewKey;
}

bool UE4EncryptedPak()
{
	return false;
}

#endif // UNREAL4

#define OPT_BOOL(name,var)				{ name, (byte*)&var, true  },
#define OPT_NBOOL(name,var)				{ name, (byte*)&var, false },
#define OPT_VALUE(name,var,value)		{ name, (byte*)&var, value },

int main(int argc, const char **argv)
{
  bool bShouldLoadObjects = false;
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

#if _WIN32
	signal(SIGABRT, AbortHandler);
#endif

	PROFILE_IF(false);
	guard(Main);

	if (argc == 2 && argv[1][0] == '@')
	{
		// Should read command line from a file
		const char* appName = argv[0];
		appParseResponseFile(argv[1]+1, argc, argv);
		argv[0] = appName;
	}

	// display usage
	if (argc < 2)
	{
		PrintUsage();
		exit(0);
	}

	GSettings.Load();
  GSettings.Startup.SetPath("."); // default, can be overwritten by options

	// parse command line
	enum
	{
		CMD_Dump,
		CMD_PkgInfo,
		CMD_List,
		CMD_Export,
		CMD_Save,
	};

	static byte mainCmd = CMD_List;
	static bool bAll = false;
	TArray<const char*> packagesToLoad, objectsToLoad;
	TArray<const char*> params;
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
			OPT_VALUE("dump",    mainCmd, CMD_Dump)
			OPT_VALUE("export",  mainCmd, CMD_Export)
			OPT_VALUE("save",    mainCmd, CMD_Save)
			OPT_VALUE("pkginfo", mainCmd, CMD_PkgInfo)
			OPT_VALUE("list",    mainCmd, CMD_List)
#if VSTUDIO_INTEGRATION
			OPT_BOOL ("debug",   GUseDebugger)
#endif
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
			OPT_BOOL ("dds",     GSettings.Export.ExportDdsTexture)
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
		if (!stricmp(opt, "psk"))
		{
			GSettings.Export.SkeletalMeshFormat = GSettings.Export.StaticMeshFormat = EExportMeshFormat::psk;
		}
		if (!stricmp(opt, "png"))
		{
			GSettings.Export.TextureFormat = ETextureExportFormat::png;
			//!! todo: also screenshots
		}
		else if (!stricmp(opt, "md5"))
		{
			GSettings.Export.SkeletalMeshFormat = GSettings.Export.StaticMeshFormat = EExportMeshFormat::md5;
		}
		else if (!stricmp(opt, "gltf"))
		{
			GSettings.Export.SkeletalMeshFormat = GSettings.Export.StaticMeshFormat = EExportMeshFormat::gltf;
		}
		else if (!stricmp(opt, "all") && mainCmd == CMD_Dump)
		{
			// -all should be used only with -dump
			bAll = true;
		}
		// more complex options
		else if (!strnicmp(opt, "log=", 4))
		{
			appOpenLogFile(opt+4);
		}
		else if (!strnicmp(opt, "path=", 5))
		{
			GSettings.Startup.SetPath(opt+5);
		}
		else if (!strnicmp(opt, "out=", 4))
		{
			GSettings.Export.SetPath(opt+4);
			GSettings.SavePackages.SetPath(opt+4);
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
		else if (!strnicmp(opt, "pkg=", 4))
		{
			const char *pkg = opt+4;
			packagesToLoad.Add(pkg);
		}
		else if (!strnicmp(opt, "obj=", 4))
		{
			const char *obj = opt+4;
			objectsToLoad.Add(obj);
		}
		else if (!strnicmp(opt, "anim=", 5))
		{
			const char *obj = opt+5;
			objectsToLoad.Add(obj);
			attachAnimName = obj;
		}
		else if (!stricmp(opt, "3rdparty"))
		{
			GSettings.Startup.UseScaleForm = GSettings.Startup.UseFaceFx = true;
		}
		else if (!strnicmp(opt, "aes=", 4))
		{
			GAesKey = opt+4;
			GAesKey.TrimStartAndEndInline();
			CheckHexAesKey();
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
		else if (!stricmp(opt, "nomt"))
		{
			GEnableThreads = false;
		}
		else if (!stricmp(opt, "testexport"))
		{
			mainCmd = CMD_Export;
			GDummyExport = true;
		}
		else if (!stricmp(opt, "debug"))
		{
			// Do nothing if this option is not supported
		}
		else
		{
			CommandLineError("invalid option: -%s", opt);
		}
	}
	if (params.Num() > 3)
	{
		CommandLineError("too many arguments, please check your command line.\n");
	}

	// apply some of GSettings
	GForceGame = GSettings.Startup.GameOverride;	// force game fore scanning any game files
	GForcePlatform = GSettings.Startup.Platform;
	GForceCompMethod = GSettings.Startup.PackageCompression;
	GSettings.Export.Apply();

	// Parse UMODEL [package_name [obj_name [class_name]]]
	if (params.Num() >= 1)
	{
		packagesToLoad.Insert(params[0], 0);
	}
	if (params.Num() >= 2)
	{
		objectsToLoad.Insert(params[1], 0);
	}
	const char *argClassName = (params.Num() >= 3) ? params[2] : NULL;

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
  for (int i=0; i<FilePaths.Num(); ++i)
  {
    appPrintf("Path Found: %s\n", *FilePaths[i]);
  }
  bool bShouldLoadPackages = (mainCmd != CMD_Save);
  TArray<UnPackage*> Packages;
  appLoadPackages(Packages, FilePaths, bShouldLoadPackages);
  TArray<const CGameFileInfo*> GameFiles;
  appGetGameFileInfo(GameFiles);

	if (mainCmd == CMD_List)
	{
    ListPackages(Packages);
		return 0;
	}

	if (mainCmd == CMD_Save)
  {
		SavePackages(GameFiles);
		return 0;
	}

	// register exporters and classes
	InitClassAndExportSystems(Packages[0]->Game);

	if (mainCmd == CMD_PkgInfo)
	{
		DisplayPackageStats(Packages);
		return 0;					// already displayed when loaded package; extend it?
	}

  bShouldLoadObjects = (mainCmd != CMD_Export) || (objectsToLoad.Num() > 0);
	TArray<UObject*> Objects;
  GForceAnimSet = appLoadObjects(Objects,
                                 Packages,
                                 objectsToLoad,
                                 argClassName,
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

  if (mainCmd == CMD_Export)
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

#if RENDERING
  if (mainCmd == CMD_Dump)
  {
		// dump object(s)
		for (int idx = 0; idx < UObject::GObjObjects.Num(); idx++)
		{
			UObject* ExpObj = UObject::GObjObjects[idx];
			if (!bAll)
			{
				if (Packages.FindItem(ExpObj->Package) < 0)					// refine object by package
					continue;
				if (objectsToLoad.Num() && (Objects.FindItem(ExpObj) < 0))	// refine object by name
					continue;
			}

			GApplication.CreateVisualizer(ExpObj);
			if (GApplication.Viewer)
				GApplication.Viewer->Dump();								// dump info to console
		}
  }
#endif

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
