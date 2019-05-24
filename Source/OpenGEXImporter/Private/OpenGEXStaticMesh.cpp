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
	return { InFloat3[0], -InFloat3[1], InFloat3[2] };
}

static FVector2D ConvertOpenGEXFloat2(const float* InFloat2)
{
	return { InFloat2[0], InFloat2[1] };
}

template <typename T>
TArray<T> ReIndexForOpenGEX(OGEX::VertexArrayStructure* Source, const Array<uint32>& Indices);

template <>
TArray<FVector> ReIndexForOpenGEX<FVector>(OGEX::VertexArrayStructure* Source, const Array<uint32>& Indices)
{
	TArray<FVector> Result;
	Result.Reserve(Indices.GetElementCount());
	for (int32 i = 0; i < Indices.GetElementCount(); ++i)
	{
		Result.Add(ConvertOpenGEXFloat3(Source->GetDataStructure()->GetArrayDataElement(Indices[i])));
	}
	return Result;
}

template <>
TArray<FVector2D> ReIndexForOpenGEX<FVector2D>(OGEX::VertexArrayStructure* Source, const Array<uint32>& Indices)
{
	TArray<FVector2D> Result;
	Result.Reserve(Indices.GetElementCount());
	for (int32 i = 0; i < Indices.GetElementCount(); ++i)
	{
		Result.Add(ConvertOpenGEXFloat2(Source->GetDataStructure()->GetArrayDataElement(Indices[i])));
	}
	return Result;
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
	OGEX::VertexArrayStructure* & PositionVertexArrayStruct,
	OGEX::VertexArrayStructure* & NormalVertexArrayStruct,
	OGEX::VertexArrayStructure* & TangentVertexArrayStruct,
	OGEX::VertexArrayStructure* & BitangentVertexArrayStruct,
	OGEX::VertexArrayStructure* & ColorVertexArrayStruct,
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
		TMap<int32, FVertexID> PositionIndexToVertexID;
		PositionIndexToVertexID;
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
			PositionIndexToVertexID.Add(i, VertexID);
		}

		//TArray<FVector2D> UVs[MAX_MESH_TEXTURE_COORDS_MD];
		//for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
		//{
		//	for (int32 i = 0; i < TexcoordVertexArrayStructs[UVIndex]->GetDataStructure()->GetDataElementCount(); ++i)
		//	{
		//		UVs[UVIndex].Add(ConvertOpenGEXFloat2(TexcoordVertexArrayStructs[UVIndex]->GetDataStructure()->GetArrayDataElement(i)));
		//	}
		//}

		Array<OGEX::IndexArrayStructure*>& Primitives = MeshStruct->GetIndexArrayStructures();
		for (int32 PrimIndex = 0; PrimIndex < Primitives.GetElementCount(); ++PrimIndex)
		{
			OGEX::IndexArrayStructure* Prim = Primitives[PrimIndex];
			FPolygonGroupID CurrentPolygonGroupID = MaterialIndexToPolygonGroupID[Prim->GetMaterialIndex()];
			uint32 TriCount = Prim->GetIndicesArray().GetElementCount() / 3;

			//TSet<uint32> UniqueIndices;
			//for (int32 i = 0; i < Prim->GetIndicesArray().GetElementCount(); ++i)
			//{
			//	UniqueIndices.Add(Prim->GetIndicesArray()[i]);
			//}
			
			Array<uint32>& Indices = Prim->GetIndicesArray();
			TArray<FVector> Normals;

			if (NormalVertexArrayStruct)
			{
				Normals = ReIndexForOpenGEX<FVector>(NormalVertexArrayStruct, Indices);
			}

			TArray<FVector> Tangents;
			if (TangentVertexArrayStruct)
			{
				Tangents = ReIndexForOpenGEX<FVector>(TangentVertexArrayStruct, Indices);
			}

			TArray<FVector2D> UVs[MAX_MESH_TEXTURE_COORDS_MD];

			if (NumUVs <= 0)
			{
				UVs[0].AddZeroed(Indices.GetElementCount());
				bDidGenerateTexCoords = true;
				NumUVs = 1;
			}
			else
			{
				for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
				{
					UVs[UVIndex] = ReIndexForOpenGEX<FVector2D>(TexcoordVertexArrayStructs[UVIndex], Indices);
				}
			}

			for (uint32 TriangleIndex = 0; TriangleIndex < TriCount; ++TriangleIndex)
			{
				FVertexInstanceID CornerVertexInstanceIDs[3];
				FVertexID CornerVertexIDs[3];
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					uint32 IndicesIndex = TriangleIndex * 3 + Corner;
					int32 VertexIndex = Indices[IndicesIndex];

					FVertexID VertexID = PositionIndexToVertexID[VertexIndex];
					const FVertexInstanceID& VertexInstanceID = MeshDescription->CreateVertexInstance(VertexID);

					if (Tangents.Num() > 0)
					{
						VertexInstanceTangents[VertexInstanceID] = Tangents[IndicesIndex];
					}
					if (Normals.Num() > 0)
					{
						VertexInstanceNormals[VertexInstanceID] = Normals[IndicesIndex];
					}

					if (Tangents.Num() > 0 && Normals.Num() > 0)
					{
						VertexInstanceBinormalSigns[VertexInstanceID] = GetBasisDeterminantSign(VertexInstanceTangents[VertexInstanceID].GetSafeNormal(),
							(VertexInstanceNormals[VertexInstanceID] ^ VertexInstanceTangents[VertexInstanceID]).GetSafeNormal(),
							VertexInstanceNormals[VertexInstanceID].GetSafeNormal());
					}

					for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
					{
						VertexInstanceUVs.Set(VertexInstanceID, UVIndex, UVs[UVIndex][IndicesIndex]);
					}

					CornerVertexInstanceIDs[Corner] = VertexInstanceID;
					CornerVertexIDs[Corner] = VertexID;
				}

				TArray<FMeshDescription::FContourPoint> Contours;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					int32 ContourPointIndex = Contours.AddDefaulted();
					FMeshDescription::FContourPoint& ContourPoint = Contours[ContourPointIndex];
					// Find the matching edge ID
					uint32 CornerIndices[2];
					CornerIndices[0] = (Corner + 0) % 3;
					CornerIndices[1] = (Corner + 1) % 3;

					FVertexID EdgeVertexIDs[2];
					EdgeVertexIDs[0] = CornerVertexIDs[CornerIndices[0]];
					EdgeVertexIDs[1] = CornerVertexIDs[CornerIndices[1]];

					FEdgeID MatchEdgeID = MeshDescription->GetVertexPairEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
					if (MatchEdgeID == FEdgeID::Invalid)
					{
						MatchEdgeID = MeshDescription->CreateEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
						EdgeHardnesses[MatchEdgeID] = false;
						EdgeCreaseSharpnesses[MatchEdgeID] = 0.0f;
					}
					ContourPoint.EdgeID = MatchEdgeID;
					ContourPoint.VertexInstanceID = CornerVertexInstanceIDs[CornerIndices[0]];
				}

				const FPolygonID NewPolygonID = MeshDescription->CreatePolygon(CurrentPolygonGroupID, Contours);
				FMeshPolygon& Polygon = MeshDescription->GetPolygon(NewPolygonID);
				MeshDescription->ComputePolygonTriangulation(NewPolygonID, Polygon.Triangles);
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
