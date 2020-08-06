#include "Core/Core.h"

#include "Unreal/UnCore.h"
#include "Unreal/TypeInfo.h"

#include "UnObject.h"
#include "Unreal/UnTypeinfo.h"

#include "Exporters/Exporters.h"
#include "Unreal/Mesh/SkeletalMesh.h"
#include "Unreal/Mesh/StaticMesh.h"
#include "Unreal/UnrealMesh/UnAnimNotify.h"
#include "Unreal/UnrealMesh/UnMesh2.h"
#include "Unreal/UnrealMesh/UnMesh3.h"
#include "Unreal/UnrealMesh/UnMesh4.h"
#include "Unreal/UnrealMaterial/UnMaterial.h"
#include "Unreal/UnrealMaterial/UnMaterial2.h"
#include "Unreal/UnrealMaterial/UnMaterial3.h"
#include "Unreal/UnrealMaterial/UnMaterialExpression.h"
#include "Unreal/UnSound.h"
#include "Unreal/UnThirdParty.h"

#include "UmodelTool/UmodelSettings.h"


/*-----------------------------------------------------------------------------
	Table of known Unreal classes
-----------------------------------------------------------------------------*/

void RegisterCommonUnrealClasses()
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


void RegisterUnrealClasses2()
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


void RegisterUnrealClasses3()
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


void RegisterUnrealClasses4()
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


void RegisterUnrealSoundClasses()
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


void RegisterUnreal3rdPartyClasses()
{
#if UNREAL3
BEGIN_CLASS_TABLE
	REGISTER_3RDP_CLASSES
END_CLASS_TABLE
#endif
}


void RegisterClasses(int game)
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


void CallExportSkeletalMesh(const CSkeletalMesh* Mesh)
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


void CallExportStaticMesh(const CStaticMesh* Mesh)
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


void CallExportAnimation(const CAnimSet* Anim)
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


void RegisterExporters()
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
	bool initialized = false;
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
