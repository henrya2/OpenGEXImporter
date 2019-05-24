// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "OpenGEX.h"

class UStaticMesh;
class UMaterial;

UStaticMesh* ImportOneMeshFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, OGEX::GeometryNodeStructure* GeometryNode, const TMap<FName, UMaterial*>& Materials, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn, int32 Index);

TArray<UStaticMesh*> ImportMeshesFromOpenGEX(OGEX::OpenGexDataDescription* OpenGexDataDescriptionPtr, const TMap<FName, UMaterial*>& Materials, const FString& FileName, UObject* InParent, FName InName, EObjectFlags Flags, FFeedbackContext* Warn);

