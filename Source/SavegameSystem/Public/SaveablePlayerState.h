#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SaveablePlayerState.generated.h"

class UCustomSaveGame;
struct FPlayerSaveData;

UCLASS()
class SAVEGAMESYSTEM_API ASaveablePlayerState : public APlayerState
{
	GENERATED_UCLASS_BODY()

protected:
	//bool bIsStartingAtTransform;
	FTransform StartTransform;
	TSubclassOf<APawn> PlayerClass;
	FPlayerSaveData* loadedSaveData;

public:
	void ResetPlayerState();

	/* called when saving */
	UFUNCTION(BlueprintNativeEvent)
		void SavePlayerState(UCustomSaveGame* SaveObject);

	/* called from UGameMode::HandleStartingNewPlayer when savegame data gets applied to the playerstate */
	UFUNCTION(BlueprintNativeEvent)
		void LoadPlayerState(UCustomSaveGame* SaveObject);

	UFUNCTION(BlueprintPure)
		FTransform GetRestartTransform();

	UFUNCTION(BlueprintPure)
		TSubclassOf<APawn> GetPlayerClass();

	/* called when apply savegame finished for sublevels */
	UFUNCTION(BlueprintNativeEvent)
		void ApplySaveGameData(UCustomSaveGame* SaveObject);

	/* use this function to apply the savegame data to the controlled pawn (only applies it if same class)
	* mainly relevant for manually spawned pawn
	*/
	UFUNCTION(BlueprintCallable)
		void BP_ApplySaveGameDataToPawn();

};