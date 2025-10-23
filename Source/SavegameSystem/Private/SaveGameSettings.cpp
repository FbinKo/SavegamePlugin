#include "SaveGameSettings.h"
#include "CustomSaveGame.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SaveGameSettings)

USaveGameSettings::USaveGameSettings()
{
	// Default value while nothing is specified in the DefaultGame.ini
	SaveSlotName = "SaveGame";
	savegameClass = UCustomSaveGame::StaticClass();
	PersistentSaveSlotName = "PersistentSaveGame";
	persistentSavegameClass = UPersistentSaveGame::StaticClass();
	SaveGameSlotNamesToFilter = TArray<FString>();

	bAutoSave = false;
	autoSaveInterval = 300.f;
	bAutoSaveAppliesTimeDilation = true;
	bSaveOnLevelChange = true;
	bUseSaveSlotAsNameForSaveOnLevelChange = true;
	bOverrideExistingSaveWhenSaveOnLevelChange = false;

	bSortSavegamesByTimestamp = true;
	bAutoSaveIsAlwaysFirst = true;

	bManuallySpawnPlayer = false;

	minLoadingScreenDuration = 2.0f;
	LoadingScreenZOrder = 1000;
	bWaitForUserInputToEndLoadingScreen = false;
}