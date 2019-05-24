#include "OpenGEXStaticMesh.h"

#include "Engine/StaticMesh.h"
//#include "RawMesh.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshAttributeArray.h"
#include "Materials/Material.h"
#include "AssetRegistryModule.h"
#include "OpenGEXUtility.h"

#include <string>

template <std::size_t Size>
constexpr std::size_t GetConstCharPtrLength(const char(&)[Size])
{
	return Size - 1;
}

static FVector ConvertOpenGEXFloat3(const float* InFloat3)
{
	return { InFloat3[0], InFloat3[1], InFloat3[2] };
}

static FVector2D ConvertOpenGEXFloat2(const float* InFloat2)
{
	return { InFloat2[0], InFloat2[1] };
}

static void AssignMaterialsForOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, OGEX::GeometryNodeStructure* GeometryNode, int32 LODIndex, UStaticMesh* StaticMesh, TMap<int32, int32>& OutMaterialIndexToSlot, const TMap<FName, UMaterial*>& Materials, const TSet<int32>& MaterialIndices)
{
	TArray<int32> SortedMaterialIndices = MaterialIndices.Array();
	SortedMaterialIndices.Sort();

	const int32 N = MaterialIndices.Num();
	OutMaterialIndexToSlot.Empty(N);
	StaticMesh->StaticMaterials.Reserve(N);

	for (int32 MaterialIndex : SortedMaterialIndices)
	{
		UMaterial* Mat;
		int32 MeshSlot;

		OGEX::MaterialStructure* MaterialStruct = GeometryNode->materialStructureArray[MaterialIndex];
		Mat = Materials[MaterialStruct->GetStructureName()];
		FName MatName(MaterialStruct->GetMaterialName());
		MeshSlot = StaticMesh->StaticMaterials.Emplace(Mat, MatName, MatName);

		OutMaterialIndexToSlot.Add(MaterialIndex, MeshSlot);

		StaticMesh->SectionInfoMap.Set(LODIndex, MeshSlot, FMeshSectionInfo(MeshSlot));
	}
}

static void DecomposeVertexArraysForOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, OGEX::MeshStructure* MeshStruct,
	OGEX::VertexArrayStructure* PositionVertexArrayStruct,
	OGEX::VertexArrayStructure* NormalVertexArrayStruct,
	OGEX::VertexArrayStructure* TangentVertexArrayStruct,
	OGEX::VertexArrayStructure* BitangentVertexArrayStruct,
	OGEX::VertexArrayStructure* ColorVertexArrayStruct,
	TArray<OGEX::VertexArrayStructure*>& TexcoordVertexArrayStructs)
{
	Array<OGEX::VertexArrayStructure*>& VerteArrayStructures = MeshStruct->GetVertexArrayStructures();
	TArray<int32> ReadyForRemoveIndices;
	for (int32 i = 0; i < VerteArrayStructures.GetElementCount(); ++i)
	{
		OGEX::VertexArrayStructure* VertexArrayStruct = VerteArrayStructures[i];
		std::string AttribStr(VertexArrayStruct->GetArrayAttrib());
		size_t SubPos = std::string::npos;

		SubPos = AttribStr.find("position");
		if (SubPos != std::string::npos)
		{
			PositionVertexArrayStruct = VertexArrayStruct;
		}

		SubPos = AttribStr.find("normal");
		if (SubPos != std::string::npos)
		{
			NormalVertexArrayStruct = VertexArrayStruct;
		}

		SubPos = AttribStr.find("tangent");
		if (SubPos != std::string::npos)
		{
			TangentVertexArrayStruct = VertexArrayStruct;
		}

		SubPos = AttribStr.find("bitangent");
		if (SubPos != std::string::npos)
		{
			BitangentVertexArrayStruct = VertexArrayStruct;
		}

		SubPos = AttribStr.find("color");
		if (SubPos != std::string::npos)
		{
			ColorVertexArrayStruct = VertexArrayStruct;
		}

		SubPos = AttribStr.find("texcoord");
		if (SubPos != std::string::npos)
		{
			TexcoordVertexArrayStructs.Add(VertexArrayStruct);
		}
	}
}

UStaticMesh* ImportOneMeshFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, OGEX::GeometryNodeStructure* GeometryNode, const TMap<FName, UMaterial*>& Materials, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn, int32 Index)
{
	UStaticMesh* StaticMesh = nullptr;

	if (!GeometryNode->geometryObjectStructure)
		return StaticMesh;

	// We should warn if certain things are "fixed up" during import.
	bool bDidGenerateTexCoords = false;
	bool bDidGenerateTangents = false;
	bool bMeshUsesEmptyMaterial = false;

	OGEX::GeometryObjectStructure* GeometryObject = GeometryNode->geometryObjectStructure;

	FString AssetName;
	UPackage* AssetPackage = RetrieveAssetPackageAndName<UStaticMesh>(InParent, UTF8_TO_TCHAR(GeometryNode->GetNodeName()), TEXT("SM"), InName, Index, AssetName);

	StaticMesh = NewObject<UStaticMesh>(AssetPackage, FName(*AssetName), Flags);
	FStaticMeshSourceModel& SourceModel = StaticMesh->AddSourceModel();

	Map<OGEX::MeshStructure>& MeshMap = *GeometryObject->GetMeshMap();
	OGEX::MeshStructure* MeshStruct = MeshMap.First();
	while (MeshStruct)
	{
		int32 LODIndex = MeshStruct->GetMeshLevel();
		FMeshDescription* MeshDescription = StaticMesh->CreateOriginalMeshDescription(LODIndex);
		StaticMesh->RegisterMeshAttributes(*MeshDescription);

		TVertexAttributesRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		TEdgeAttributesRef<bool> EdgeHardnesses = MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
		TEdgeAttributesRef<float> EdgeCreaseSharpnesses = MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);
		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
		TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
		TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
		TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
		TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
		TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);


		OGEX::VertexArrayStructure* PositionVertexArrayStruct = nullptr;
		OGEX::VertexArrayStructure* NormalVertexArrayStruct = nullptr;
		OGEX::VertexArrayStructure* TangentVertexArrayStruct = nullptr;
		OGEX::VertexArrayStructure* BitangentVertexArrayStruct = nullptr;
		OGEX::VertexArrayStructure* ColorVertexArrayStruct = nullptr;
		TArray<OGEX::VertexArrayStructure*> TexcoordVertexArrayStructs;

		DecomposeVertexArraysForOpenGEX(OpenGexDataDescriptionPtr, MeshStruct, PositionVertexArrayStruct, NormalVertexArrayStruct, TangentVertexArrayStruct, BitangentVertexArrayStruct, ColorVertexArrayStruct, TexcoordVertexArrayStructs);

		int32 NumUVs = 0;
		{
			NumUVs = TexcoordVertexArrayStructs.Num();
		}

		VertexInstanceUVs.SetNumIndices(NumUVs);

		FMeshBuildSettings& Settings = SourceModel.BuildSettings;

		Settings.bRecomputeNormals = NormalVertexArrayStruct == nullptr;
		Settings.bRecomputeTangents = TangentVertexArrayStruct == nullptr;
		Settings.bUseMikkTSpace = true;

		Settings.bRemoveDegenerates = false;
		Settings.bBuildAdjacencyBuffer = false;
		Settings.bBuildReversedIndexBuffer = false;

		Settings.bUseHighPrecisionTangentBasis = false;
		Settings.bUseFullPrecisionUVs = false;

		Settings.bGenerateLightmapUVs = (NumUVs <= 1);

		TSet<int32> MaterialIndicesUsed;

		// Add the vertex
		TArray<TMap<int32, FVertexID>> PositionIndexToVertexID_PerPrim;
		PositionIndexToVertexID_PerPrim.AddDefaulted(MeshStruct->GetIndexArrayStructures().GetElementCount());
		for (int32 PrimIndex = 0; PrimIndex < MeshStruct->GetIndexArrayStructures().GetElementCount(); ++PrimIndex)
		{
			OGEX::IndexArrayStructure* IndexArrayStruct = MeshStruct->GetIndexArrayStructures()[PrimIndex];
			MaterialIndicesUsed.Add(IndexArrayStruct->GetMaterialIndex());
		}

		TMap<int32, int32> MaterialIndexToSlot;
		AssignMaterialsForOpenGEX(OpenGexDataDescriptionPtr, GeometryNode, LODIndex, StaticMesh, MaterialIndexToSlot, Materials, MaterialIndicesUsed);

		TMap<int32, FPolygonGroupID> MaterialIndexToPolygonGroupID;
		//Add the PolygonGroup
		for (int32 MaterialIndex : MaterialIndicesUsed)
		{
			const FPolygonGroupID& PolygonGroupID = MeshDescription->CreatePolygonGroup();
			MaterialIndexToPolygonGroupID.Add(MaterialIndex, PolygonGroupID);
			PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = StaticMesh->StaticMaterials[MaterialIndexToSlot[MaterialIndex]].ImportedMaterialSlotName;
		}

		for (int32 i = 0; i < PositionVertexArrayStruct->GetDataStructure()->GetDataElementCount(); ++i)
		{
			FVertexID VertexID = MeshDescription->CreateVertex();
			VertexPositions[VertexID] = ConvertOpenGEXFloat3(PositionVertexArrayStruct->GetDataStructure()->GetArrayDataElement(i));
		}

		TArray<FVector2D> UVs[MAX_MESH_TEXTURE_COORDS_MD];
		for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
		{
			for (int32 i = 0; i < TexcoordVertexArrayStructs[UVIndex]->GetDataStructure()->GetDataElementCount(); ++i)
			{
				UVs[UVIndex].Add(ConvertOpenGEXFloat2(TexcoordVertexArrayStructs[UVIndex]->GetDataStructure()->GetArrayDataElement(i)));
			}
		}

		MeshStruct = MeshStruct->Next();

		StaticMesh->CommitOriginalMeshDescription(LODIndex);
	}

	StaticMesh->PostEditChange();

	AssetPackage->SetDirtyFlag(true);
	FAssetRegistryModule::AssetCreated(StaticMesh);

	return StaticMesh;
}

TArray<UStaticMesh*> ImportMeshesFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, const TMap<FName, UMaterial*>& Materials, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn)
{
	TArray<UStaticMesh*> Result;

	int32 Index = 0;

	Structure* StructureNode = OpenGexDataDescriptionPtr->GetRootStructure()->GetFirstSubnode();
	for (; StructureNode; StructureNode = StructureNode->Next())
	{
		if (StructureNode->GetStructureType() == OGEX::kStructureGeometryNode)
		{
			OGEX::GeometryNodeStructure* GeometryNode = static_cast<OGEX::GeometryNodeStructure*>(StructureNode);
			UStaticMesh* RetStaticMesh = ImportOneMeshFromOpenGEX(OpenGexDataDescriptionPtr, GeometryNode, Materials, FileName, InParent, InName, Flags, Warn, Index);
			if (RetStaticMesh)
			{
				Result.Add(RetStaticMesh);
			}
			Index += 1;
		}
	}

	return Result;
}

bool HasTangentFromOpenGEXMeshStructure(OGEX::MeshStructure* MeshStruct)
{
	bool bHasTangentResult = false;

	Array<OGEX::VertexArrayStructure*>& VerteArrayStructures = MeshStruct->GetVertexArrayStructures();
	TArray<int32> ReadyForRemoveIndices;
	for (int32 i = 0; i < VerteArrayStructures.GetElementCount(); ++i)
	{
		OGEX::VertexArrayStructure* VertexArrayStruct = VerteArrayStructures[i];
		std::string AttribStr(VertexArrayStruct->GetArrayAttrib());
		size_t SubPos = AttribStr.find("tangent");
		if (SubPos != std::string::npos)
		{
			bHasTangentResult = true;
			break;
		}
	}

	return bHasTangentResult;
}
