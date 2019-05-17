#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * The public interface of the OpenGEXImporter module
 */
class IOpenGEXImporter : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to IOpenGEXImporter
	 *
	 * @return Returns IOpenGEXImporter singleton instance, loading the module on demand if needed
	 */
	static IOpenGEXImporter& Get()
	{
		return FModuleManager::LoadModuleChecked<IOpenGEXImporter>("OpenGEXImporter");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static bool IsAvailable()
	{
		// static const FName ModuleName("GLTFImporter");
		return FModuleManager::Get().IsModuleLoaded("OpenGEXImporter");
	}
};
