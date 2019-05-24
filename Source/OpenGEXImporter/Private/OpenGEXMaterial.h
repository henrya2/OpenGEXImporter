#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OpenGEX.h"

class UTexture2D;
class UMaterial;

TMap<FName, UTexture2D*> ImportTexturesFromOpenGEX(OGEX::MaterialStructure* MaterialStruct, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags);
TMap<FName, UMaterial*> ImportMaterialsFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, UObject* InParent, const FString& FileName, FName InName, EObjectFlags Flags);