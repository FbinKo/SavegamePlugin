// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CustomSaveGame.generated.h"

struct SAVEGAMESYSTEM_API FSaveGameObjectVersion
{
	FSaveGameObjectVersion() = delete;

	/** List of versions, native code will handle fixups for any old versions */
	enum ESaveGameVersion
	{
		// Initial version
		Initial,

		// -----<new versions must be added before this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
};

USTRUCT()
struct FComponentSaveData
{
	GENERATED_BODY()
public:
	UPROPERTY()
		FName ComponentName;

	/* only relevant if it is a scenecomponent */
	UPROPERTY()
		FTransform Transform;

	UPROPERTY()
		TArray<uint8> ByteData;

	UPROPERTY()
		FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
		FName AttachParentName;

	/* which socket on parent this component is attached to */
	UPROPERTY()
		FName AttachSocketName = NAME_None;

	UPROPERTY()
		bool bSimulatesPhysics = false;
};

USTRUCT()
struct FActorSaveData
{
	GENERATED_BODY()

public:
	FActorSaveData() {
		ActorClass = nullptr;
		AttachedActorClass = nullptr;
	}

	/* Identifier for which Actor this belongs to */
	UPROPERTY()
		FName ActorName;

	UPROPERTY()
		UClass* ActorClass;

	/* Contains all 'SaveGame' marked variables of the Actor */
	UPROPERTY()
		TArray<uint8> ByteData;

	UPROPERTY()
		TArray<FComponentSaveData> SavedComponents;

	/* name of the actor this actor is attached to */
	UPROPERTY()
		FName AttachedActorName;

	UPROPERTY()
		UClass* AttachedActorClass;

	/* to what socket specifically this actor is attached to */
	UPROPERTY()
		FName AttachedSocketName;

	/* to what component this actor is attached to */
	UPROPERTY()
		FName AttachedComponentName;

	inline bool operator==(const FActorSaveData& other) const
	{
		return ActorName == other.ActorName && ActorClass == other.ActorClass;
	}
};

//USTRUCT()
//struct FSaveData
//{
//	GENERATED_BODY()
//
//public:
//	UPROPERTY()
//		FActorSaveData ActorData;
//
//	UPROPERTY()
//		TArray<FActorSaveData> AttachedActors;
//
//	inline bool operator==(const FSaveData& other) const
//	{
//		return ActorData == other.ActorData && AttachedActors == other.AttachedActors;
//	}
//
//};

USTRUCT()
struct FSubLevelActorSaveData {
	GENERATED_BODY()

public:
	UPROPERTY()
		TArray<FActorSaveData> SavedActors;
};

USTRUCT()
struct FMapActorSaveData
{
	GENERATED_BODY()

public:
	/* key is name of sublevel */
	UPROPERTY()
		TMap<FString, FSubLevelActorSaveData> ActorsInSublevels;

	UPROPERTY()
		TArray<FName> DestroyedActors;
};

USTRUCT()
struct SAVEGAMESYSTEM_API FPlayerSaveData
{
	GENERATED_BODY()

public:
	FPlayerSaveData();

	/* Player Id defined by the online sub system (such as Steam) converted to FString for simplicity  */
	UPROPERTY()
		FString PlayerID;

	UPROPERTY()
		TMap<FPrimaryAssetId, int> Inventory;

	/* Location if player was alive during save */
	UPROPERTY()
		FVector Location;

	/* Orientation if player was alive during save */
	UPROPERTY()
		FRotator Rotation;

	/* We don't always want to restore location, and may just resume player at specific respawn point in world. */
	UPROPERTY()
		bool bResumeAtTransform;

	UPROPERTY()
		TSubclassOf<APawn> PlayerClass;

	UPROPERTY()
		TArray<uint8> PawnByteData;

	/* Contains all 'SaveGame' marked variables of the player state */
	UPROPERTY()
		TArray<uint8> ByteData;

	UPROPERTY()
		TArray<FComponentSaveData> SavedComponents;
};

USTRUCT()
struct FSubLevelInfo
{
	GENERATED_BODY()

public:
	FSubLevelInfo() {

	}

	FSubLevelInfo(FString inName) : name(inName), bIsLoaded(true), bIsVisible(true) {}

	FSubLevelInfo(ULevelStreaming* subLevel) {
		name = subLevel->GetWorldAssetPackageName();
		bIsLoaded = subLevel->IsLevelLoaded();
		bIsVisible = subLevel->IsLevelVisible();
		StreamingPriority = subLevel->GetPriority();
	}

	UPROPERTY()
		FString name = FString();

	UPROPERTY()
		bool bIsLoaded = false;

	UPROPERTY()
		bool bIsVisible = false;

	UPROPERTY()
		int StreamingPriority = 0;
};

USTRUCT(BlueprintType)
struct FLevelInfo
{
	GENERATED_BODY()

public:
	FLevelInfo() {}

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		FText name = FText();
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		TSoftObjectPtr<UWorld> level;

	UPROPERTY(EditDefaultsOnly)
		bool bIsUnlocked = false;
};


DECLARE_DYNAMIC_DELEGATE_OneParam(FOnScreenshotLoaded, UTexture2D*, screenshot);
UCLASS()
class SAVEGAMESYSTEM_API UCustomSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
		FDateTime saveTime;

	UPROPERTY()
		TArray<FPlayerSaveData> SavedPlayers;

	UPROPERTY()
		TMap<FString, FMapActorSaveData> MapSavedActors;

	/* what level we are currently in */
	UPROPERTY(BlueprintReadOnly)
		FString currentMap;

	/* loading and visibility informations of all sublevels of currentMap */
	UPROPERTY()
		TArray<FSubLevelInfo> sublevelInfos;

	/* should only be true when saved in a level and reset from SavegameSubsystem::ChangeLevel */
	UPROPERTY()
		bool bRestartWithSavedPlayerData;

	UPROPERTY()
		FString screenshotPath = "";

protected:
	UPROPERTY()
		int32 SavedDataVersion;

	static UE::Tasks::FPipe AsyncTaskPipe;

public:
	UCustomSaveGame() {
		// Set to current version, this will get overwritten during serialization when loading
		SavedDataVersion = FSaveGameObjectVersion::LatestVersion;
	}
	FPlayerSaveData* GetPlayerData(APlayerState* PlayerState);
	
	typedef TFunction<void(UTexture2D*)> FScreenshotLoaded;
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
		void GetScreenshot(const UObject* WorldContextObject, const FOnScreenshotLoaded& Callback);

protected:
	/** Overridden to allow version fixups */
	virtual void Serialize(FArchive& Ar) override;
	void OnAsyncComplete(TFunction<void()> Callback);

};

USTRUCT(BlueprintType)
struct FBaseRecord
{
	GENERATED_BODY()

public:
	FBaseRecord() {}
	FBaseRecord(FString inName) : name(inName) {}

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
		FString name = "AAA";
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
		float score = 0.f;

	// decending sort instead of ascending, name determines lexicographically the order and keeps older records in the front
	FORCEINLINE bool operator< (const FBaseRecord& other) const {
		if (score > other.score) return true;
		else if (score == other.score) {
			return name.Compare(other.name) <= 0;
		}
		return false;
	}

	FORCEINLINE bool operator== (const FBaseRecord& other) const { return score == other.score && name.Equals(other.name); }
};

//TODO figure out how to use template and still be able to save (somehow it looks like a variable in USaveGame requires UPROPERTY() to save, might be possible with custom serialize)
//template<typename T>
USTRUCT(BlueprintType)
struct FRecords
{
	GENERATED_BODY()

public:
	FRecords() {}
	FRecords(FBaseRecord record) { records.Init(record, 1); }

	int AddRecord(FBaseRecord newRecord) {
		records.Add(newRecord);
		records.Sort();
		int newRank = records.IndexOfByKey(newRecord);
		records.SetNum(FMath::Min(records.Num(), maxRecords));
		records.Shrink();
		return newRank;
	}

	void AddRecords(TArray<FBaseRecord> newRecords)
	{
		records.Append(newRecords);
		records.Sort();
		records.SetNum(FMath::Min(records.Num(), maxRecords));
		records.Shrink();
	}

	UPROPERTY(EditDefaultsOnly)
		TArray<FBaseRecord> records;

private:
	int maxRecords = 10;
};

UCLASS()
class SAVEGAMESYSTEM_API UPersistentSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Infos")
		TArray<FLevelInfo> levelInfos;

	UFUNCTION(BlueprintPure)
		bool GetInfoForLevelPath(FString path, FLevelInfo& info);

	UFUNCTION(BlueprintPure)
		bool GetInfoForLevel(TSoftObjectPtr<UWorld> level, FLevelInfo& info);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject"))
		FLevelInfo GetInfoForCurrentLevel(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure)
		TArray<FLevelInfo> GetLevelInfos(bool bOnlyUnlocked);

	UFUNCTION(BlueprintCallable)
		bool UnlockLevel(TSoftObjectPtr<UWorld> level, bool bUnlock);

	UPROPERTY(EditDefaultsOnly, Category = "Infos")
		TArray<FLevelInfo> minigameInfos;

	UFUNCTION(BlueprintPure)
		TArray<FLevelInfo> GetMinigameInfos(bool bOnlyUnlocked);

	UFUNCTION(BlueprintCallable)
		bool UnlockMinigame(TSoftObjectPtr<UWorld> level, bool bUnlock);

	/* increase this if records have changed, so it can be serialized */
	UPROPERTY(EditDefaultsOnly, Category = "Records")
		int recordsVersion = 0;

	//TODO create a K2Node with function library to have only one getter function (CustomStructureParam doesnt change pin to correct struct) -> see K2Node_GetDataTableRow for example
	/*UFUNCTION(BlueprintPure, CustomThunk, meta = (CustomStructureParam = "records"))
		void GetRecords(ERecordType type, FName key, FRecords& records);*/

private:
	DECLARE_FUNCTION(execGetRecords);

protected:
	UPROPERTY()
		int32 SavedDataVersion = -1;

public:
	/** Overridden to allow version fixups */
	virtual void Serialize(FArchive& Ar) override;
	virtual void HandlePreSave();
	virtual void HandlePostLoad();

};