#include "OpenGEXImporterFactory.h"

#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/StaticMesh.h"
#include "RawMesh.h"
#include "Serialization/ArrayReader.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "OpenGEX.h"

UStaticMesh* ImportOneMeshFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, OGEX::GeometryNodeStructure* GeometryNode, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn)
{
	UStaticMesh* StaticMesh = nullptr;

	if (!GeometryNode->geometryObjectStructure)
		return StaticMesh;

	OGEX::GeometryObjectStructure* GeometryObject = GeometryNode->geometryObjectStructure;

	return StaticMesh;
}

TArray<UStaticMesh*> ImportMeshesFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn)
{
	TArray<UStaticMesh*> Result;

	Structure* StructureNode = OpenGexDataDescriptionPtr->GetRootStructure()->GetFirstSubnode();
	for (; StructureNode; StructureNode = StructureNode->Next())
	{
		if (StructureNode->GetStructureType() == OGEX::kStructureGeometryNode)
		{
			OGEX::GeometryNodeStructure* GeometryNode = static_cast<OGEX::GeometryNodeStructure*>(StructureNode);
			UStaticMesh* RetStaticMesh = ImportOneMeshFromOpenGEX(OpenGexDataDescriptionPtr, GeometryNode, FileName, InParent, InName, Flags, Warn);
			if (RetStaticMesh)
			{
				Result.Add(RetStaticMesh);
			}
		}
	}

	return Result;
}

UStaticMesh* ImportMeshesAndMaterialsFromOpenGEXFile(const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn)
{
	UStaticMesh* StaticMesh = nullptr;

	IPlatformFile& PlatformPhysicalFile = IPlatformFile::GetPlatformPhysical();
	IFileHandle* FileHandle = PlatformPhysicalFile.OpenRead(*FileName);
	if (FileHandle)
	{
		int32 FileSize = FileHandle->Size();
		TArray<char> Buffer;
		Buffer.SetNumZeroed(FileSize + 1);

		if (FileHandle->Read((uint8*)Buffer.GetData(), FileSize))
		{
			OGEX::OpenGexDataDescription openGexDataDescription;
			openGexDataDescription.ProcessText(Buffer.GetData());
			TArray<UStaticMesh*> StaticMeshes = ImportMeshesFromOpenGEX(&openGexDataDescription, FileName, InParent, InName, Flags, Warn);
			if (StaticMeshes.Num() > 0)
			{
				StaticMesh = StaticMeshes[0];
			}
		}
	}

	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}

	return StaticMesh;
}

UOpenGEXImporterFactory::UOpenGEXImporterFactory(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = false;
	bEditorImport = true;
	bText = false;

	SupportedClass = UStaticMesh::StaticClass();

	Formats.Add(TEXT("ogex;OpenGEX Format"));
}

UObject* UOpenGEXImporterFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UStaticMesh* StaticMesh = nullptr;

	FEditorDelegates::OnAssetPreImport.Broadcast(this, InClass, InParent, InName, Parms);

	Warn->Log(Filename);
	StaticMesh = ImportMeshesAndMaterialsFromOpenGEXFile(Filename, InParent, InName, Flags, Warn);

	FEditorDelegates::OnAssetPostImport.Broadcast(this, StaticMesh);

	return StaticMesh;
}

bool UOpenGEXImporterFactory::FactoryCanImport(const FString& Filename)
{
	bool bCanImport = false;

	const FString Extention = FPaths::GetExtension(Filename);
	if (Extention == TEXT("ogex"))
	{
		bCanImport = true;
	}

	return bCanImport;
}
