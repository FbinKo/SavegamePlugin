#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SavableObjectInterface.generated.h"

// This class does not need to be modified.
// just implement this interface in each actor which should be saved/loaded
UINTERFACE(MinimalAPI)
class USavableObjectInterface : public UInterface
{
	GENERATED_BODY()
};

class SAVEGAMESYSTEM_API ISavableObjectInterface
{
	GENERATED_BODY()

public:

	/* Called after the Actor state was restored from a SaveGame file.
	* @param bHasGameplayStarted tells if loading was already done -> SavegameApplied wont becalled anymore and we can perform potential start logic here, because gameplay is already active
	*/
	UFUNCTION(BlueprintNativeEvent)
		void OnActorLoaded(bool bHasGameplayStarted);

	/* Called before Actor state is stored to SaveGame file. */
	UFUNCTION(BlueprintNativeEvent)
		void OnSaveActor();

};