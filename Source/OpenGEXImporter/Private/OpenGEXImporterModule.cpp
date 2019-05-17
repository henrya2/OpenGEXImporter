// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IOpenGEXImporter.h"

/**
 * OpenGEX Importer module implementation (private)
 */
class FOpenGEXImporterModule : public IOpenGEXImporter
{
public:
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FOpenGEXImporterModule, OpenGEXImporter);
