#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "SaveGameSettings.generated.h"

UCLASS(Config = Game, defaultconfig) // 'defaultconfig' = "Save object config only to Default INIs, never to local INIs."
class SAVEGAMESYSTEM_API USaveGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	FSoftClassPath LoadingScreenWidget;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	int32 LoadingScreenZOrder;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	float minLoadingScreenDuration;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Loading Screen")
	bool bWaitForUserInputToEndLoadingScreen;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	bool bAutoSave;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (EditCondition = "bAutoSave"))
	float autoSaveInterval;

	/* if the autosave interval update is dependant on timedilation/pause */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (EditCondition = "bAutoSave"))
	bool bAutoSaveAppliesTimeDilation;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Sort and Filter")
	bool bSortSavegamesByTimestamp;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Sort and Filter", meta = (EditCondition = "bSortSavegamesByTimestamp"))
	bool bAutoSaveIsAlwaysFirst;

	/* slot names which will get filtered out when checking for slots (can still be loaded, but usually theese slots are not actual savegames, eg. settings or saves which are not applied in the game flow */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Sort and Filter")
		TArray<FString> SaveGameSlotNamesToFilter;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Level Change")
	bool bSaveOnLevelChange;

	/* when saving in level change, use the set saveslot name if true or use the level name if false */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Level Change", meta = (EditCondition = "bSaveOnLevelChange"))
	bool bUseSaveSlotAsNameForSaveOnLevelChange;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Level Change", meta = (EditCondition = "bSaveOnLevelChange"))
	bool bOverrideExistingSaveWhenSaveOnLevelChange;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Save Data", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	FSoftObjectPath defaultSaveGameScreenshot;

	/* Default slot name if UI doesn't specify any */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Save Data")
	FString SaveSlotName;

	/* slot name for meta progress save game */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General|Save Data")
	FString PersistentSaveSlotName;

	UPROPERTY(Config, EditAnywhere, NoClear, BlueprintReadOnly, Category = "General|Save Data")
	TSubclassOf<class UCustomSaveGame> savegameClass;

	UPROPERTY(Config, EditAnywhere, NoClear, BlueprintReadOnly, Category = "General|Save Data")
	TSubclassOf<class UPersistentSaveGame> persistentSavegameClass;

	/* if true, player will spawn default pawn with default logic and not automatically with saved transform (in case there is a savegame)
	* -> player savegame data application will have to be handled manually and potentially also the spawn of the correct player class (eg. player has different pawn like a vehicle possessed or the default pawn is not the actual pawn used)
	*/
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	bool bManuallySpawnPlayer;

	/* if this is a valid level, USavegameSubsystem::EndGame() will automatically load back to this level */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	TSoftObjectPtr<UWorld> mainMenuLevel;

	USaveGameSettings();
};