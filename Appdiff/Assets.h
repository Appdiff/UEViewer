class CAnimSet;
class CSkeletalMesh;
class CStaticMesh;

void RegisterCommonUnrealClasses();
void RegisterUnrealClasses2();
void RegisterUnrealClasses3();
void RegisterUnrealClasses4();
void RegisterUnrealSoundClasses();
void RegisterUnreal3rdPartyClasses();
void RegisterClasses(int game);

void CallExportSkeletalMesh(const CSkeletalMesh* Mesh);
void CallExportStaticMesh(const CStaticMesh* Mesh);
void CallExportAnimation(const CAnimSet* Anim);
void RegisterExporters();

void InitClassAndExportSystems(int Game);
