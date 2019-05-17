#pragma once

#include "Factories/Factory.h"

#include "OpenGEXImporterFactory.generated.h"

UCLASS(Transient)
class UOpenGEXImporterFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOpenGEXImporterFactory(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	bool FactoryCanImport(const FString& Filename) override;
};