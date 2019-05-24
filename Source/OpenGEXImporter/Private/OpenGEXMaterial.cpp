#include "OpenGEXMaterial.h"
#include "OpenGEXCommons.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "OpenGEXUtility.h"
#include "FileHelper.h"

static constexpr int32 GridSnap = 16;

FLinearColor ConvertOpenGEXColorToLinearColor(const float* ColorData)
{
	return FLinearColor(ColorData[0], ColorData[1], ColorData[2]);
}

static void PlaceOnGrid(UMaterialExpression* Node, int32 X, int32 Y, const FString& Description = FString())
{
	Node->MaterialExpressionEditorX = GridSnap * X;
	Node->MaterialExpressionEditorY = GridSnap * Y;

	if (!Description.IsEmpty())
	{
		Node->Desc = Description;
		Node->bCommentBubbleVisible = true;
	}
}

static void PlaceOnGridNextTo(UMaterialExpression* Node, const UMaterialExpression* ExistingNode)
{
	int32 X = ExistingNode->MaterialExpressionEditorX / GridSnap;
	int32 Y = ExistingNode->MaterialExpressionEditorY / GridSnap;

	PlaceOnGrid(Node, X + 8, Y + 2);
}

static void ConnectInput(UMaterial* Material, FExpressionInput& MaterialInput, UMaterialExpression* Factor, UMaterialExpression* Texture, int32 TextureOutputIndex = 0)
{
	if (Texture && Factor)
	{
		// multiply these to get final input

		// [Factor]---\            |
		//             [Multiply]--| Material
		// [Texture]--/            |

		UMaterialExpressionMultiply* MultiplyNode = NewObject<UMaterialExpressionMultiply>(Material);
		PlaceOnGridNextTo(MultiplyNode, Factor);
		Material->Expressions.Add(MultiplyNode); // necessary?
		MultiplyNode->A.Connect(0, Factor);
		MultiplyNode->B.Connect(TextureOutputIndex, Texture);
		MaterialInput.Connect(0, MultiplyNode);
	}
	else if (Texture)
	{
		MaterialInput.Connect(TextureOutputIndex, Texture);
	}
	else if (Factor)
	{
		MaterialInput.Connect(0, Factor);
	}

	// Add Texture & Factor to Material->Expressions in this func? If so use AddUnique for Texture.
}

UTexture2D* ImportTextureFromOpenGEXTexture(OGEX::TextureStructure* TextureStruct, int32 Index, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags)
{
	UTexture2D* Texture = nullptr;

	FString TextureFileCleanName = FPaths::GetCleanFilename(UTF8_TO_TCHAR(TextureStruct->GetTextureName()));
	FString Extension = FPaths::GetExtension(TextureFileCleanName);

	FString TextureFilename = FPaths::GetPath(FileName) + TEXT("/") + TextureFileCleanName;

	TArray<uint8> ImageData;
	if (!FFileHelper::LoadFileToArray(ImageData, *TextureFilename))
	{
		UE_LOG(LogOpenGEXImporter, Error, TEXT("Failed to load file '%s' to array"), *TextureFilename);
		return nullptr;
	}

	if (ImageData.Num() > 0)
	{
		const uint8* ImageDataPtr = &ImageData[0];

		FString AssetName;
		UPackage* AssetPackage = RetrieveAssetPackageAndName<UTexture2D>(InParent, TextureFileCleanName, TEXT("T"), InName, Index, AssetName);

		auto Factory = NewObject<UTextureFactory>();
		Factory->AddToRoot();

		Factory->SuppressImportOverwriteDialog();

		Texture = (UTexture2D*)Factory->FactoryCreateBinary(
			UTexture2D::StaticClass(), AssetPackage, *AssetName,
			Flags, nullptr, *Extension,
			ImageDataPtr, ImageDataPtr + ImageData.Num(), GWarn);

		if (Texture != nullptr)
		{
			Texture->AssetImportData->Update(*TextureFileCleanName);

			FAssetRegistryModule::AssetCreated(Texture);

			AssetPackage->SetDirtyFlag(true);
		}

		Factory->RemoveFromRoot();
	}

	return Texture;
}

TMap<FName, UTexture2D*> ImportTexturesFromOpenGEX(OGEX::MaterialStructure* MaterialStruct, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags)
{
	TMap<FName, UTexture2D*> Textures;

	int32 Index = 0;

	Array<OGEX::AttribStructure*>& AttribuStrucrtures = MaterialStruct->GetAttribStructures();
	for (int32 i = 0; i < AttribuStrucrtures.GetElementCount(); ++i)
	{
		OGEX::AttribStructure* AttribStruct = AttribuStrucrtures[i];
		if (AttribStruct->GetStructureType() == OGEX::kStructureTexture)
		{
			Index += 1;
			OGEX::TextureStructure* TextureStruct = static_cast<OGEX::TextureStructure*>(AttribStruct);
			UTexture2D* UnTex = ImportTextureFromOpenGEXTexture(TextureStruct, Index, InParent, FileName, InName, Flags);
			if (UnTex)
			{
				Textures.Add(FName(UTF8_TO_TCHAR(TextureStruct->GetAttribString())), UnTex);
			}
		}
	}

	return Textures;
}

UMaterial* ImportMaterialFromOpenGEX(OGEX::MaterialStructure* MaterialStruct, int32 Index, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags)
{
	TMap<FName, UTexture2D*> Textures = ImportTexturesFromOpenGEX(MaterialStruct, InParent, FileName, InName, Flags);

	FString AssetName;
	UPackage* AssetPackage = RetrieveAssetPackageAndName<UMaterial>(InParent, UTF8_TO_TCHAR(MaterialStruct->GetMaterialName()), TEXT("M"), InName, Index, AssetName);

	UMaterial* Material = NewObject<UMaterial>(AssetPackage, UMaterial::StaticClass(), FName(*AssetName), Flags);

	UMaterialExpressionConstant3Vector* BaseColorFactorNode = nullptr;
	UMaterialExpressionTextureSample* BaseColorSamplerNode = nullptr;

	for (int32 i = 0; i < MaterialStruct->GetAttribStructures().GetElementCount(); ++i)
	{
		Array<OGEX::AttribStructure*>& AttribStructures = MaterialStruct->GetAttribStructures();
		if (AttribStructures[i]->GetStructureType() == OGEX::kStructureColor)
		{
			OGEX::ColorStructure* ColorStruct = static_cast<OGEX::ColorStructure*>(AttribStructures[i]);
			
			if (ColorStruct->GetAttribString() == "diffuse")
			{
				BaseColorFactorNode = BaseColorFactorNode = NewObject<UMaterialExpressionConstant3Vector>(Material);
				BaseColorFactorNode->Constant = ConvertOpenGEXColorToLinearColor(ColorStruct->GetColor());
				BaseColorFactorNode->bCollapsed = true;
				PlaceOnGrid(BaseColorFactorNode, -25, -15, "baseColorFactor.rgb");
			}
		}
		else if (AttribStructures[i]->GetStructureType() == OGEX::kStructureTexture)
		{
			OGEX::TextureStructure* TextureStruct = static_cast<OGEX::TextureStructure*>(AttribStructures[i]);
			if (TextureStruct->GetAttribString() == "diffuse")
			{
				UTexture2D** TexturePtr = Textures.Find("diffuse");
				if (TexturePtr)
				{
					UTexture2D* Texture = *TexturePtr;
					Texture->SRGB = true;
					Texture->LODGroup = TEXTUREGROUP_World;
					Texture->CompressionSettings = TC_Default;

					BaseColorSamplerNode = NewObject<UMaterialExpressionTextureSample>(Material);
					BaseColorSamplerNode->Texture = Texture;
					BaseColorSamplerNode->SamplerType = SAMPLERTYPE_Color;
					BaseColorSamplerNode->ConstCoordinate = TextureStruct->GetTexcoordIndex();
					Material->Expressions.Add(BaseColorSamplerNode);
					PlaceOnGrid(BaseColorSamplerNode, -40, -14, "baseColorTexture");
				}
			}
			else if (TextureStruct->GetAttribString() == "normal")
			{
				UTexture2D** TexturePtr = Textures.Find("normal");
				if (TexturePtr)
				{
					UTexture2D* Texture = *TexturePtr;
					Texture->SRGB = false;
					Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
					Texture->bFlipGreenChannel = false;
					Texture->CompressionSettings = TC_Normalmap;

					UMaterialExpressionTextureSample* NormalSamplerNode = NewObject<UMaterialExpressionTextureSample>(Material);
					NormalSamplerNode->Texture = Texture;
					NormalSamplerNode->SamplerType = SAMPLERTYPE_Normal;
					NormalSamplerNode->ConstCoordinate = TextureStruct->GetTexcoordIndex();

					PlaceOnGrid(NormalSamplerNode, -21, 38, "normalTexture");

					ConnectInput(Material, Material->Normal, nullptr, NormalSamplerNode);
				}
			}
		}
	}

	ConnectInput(Material, Material->BaseColor, BaseColorFactorNode, BaseColorSamplerNode);

	return Material;
}

TMap<FName, UMaterial*> ImportMaterialsFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags)
{
	TMap<FName, UMaterial*> Materials;

	int32 Index = 0;
	Structure* StructureNode = OpenGexDataDescriptionPtr->GetRootStructure()->GetFirstSubnode();
	for (; StructureNode; StructureNode = StructureNode->Next())
	{
		if (StructureNode->GetStructureType() == OGEX::kStructureMaterial)
		{
			OGEX::MaterialStructure* MaterialStruct = static_cast<OGEX::MaterialStructure*>(StructureNode);
			UMaterial* Material = ImportMaterialFromOpenGEX(MaterialStruct, Index, InParent, FileName, InName, Flags);

			Index += 1;

			if (Material)
			{
				Materials.Add(MaterialStruct->GetStructureName(), Material);
			}
		}
	}

	return Materials;
}
