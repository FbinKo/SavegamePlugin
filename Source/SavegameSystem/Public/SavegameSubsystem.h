// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SavegameSubsystem.generated.h"

class USaveGame;
class UCustomSaveGame;
class UPersistentSaveGame;
class ULevelSequence;

struct FActorSaveData;

DECLARE_LOG_CATEGORY_EXTERN(LogSavegame, Warning, All);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveGameSignature, UCustomSaveGame*, SaveGame);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPersistentSaveGameSignature, UPersistentSaveGame*, SaveGame);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnLoadGameFromSlotDelegate, const FString&, SlotName, const int32, UserIndex, USaveGame*, SaveGame);

UCLASS()
class SAVEGAMESYSTEM_API USavegameSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = Save)
	FString SaveSlot;

	UPROPERTY(BlueprintReadWrite, Category = Save)
	int32 SaveUserIndex;

	UPROPERTY(BlueprintAssignable)
	FOnSaveGameSignature OnSaveGameLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnSaveGameSignature OnSaveGameSaved;

	UPROPERTY(BlueprintAssignable)
	FOnSaveGameSignature OnSaveGameApplied;

	UPROPERTY(BlueprintAssignable)
	FOnPersistentSaveGameSignature OnPersistentSaveGameLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnPersistentSaveGameSignature OnPersistentSaveGameSaved;

protected:
	UPROPERTY()
	UPersistentSaveGame* persistentSaveGame;
	UPROPERTY()
	UCustomSaveGame* CurrentSaveGame;

	UPROPERTY()
	bool bCurrentlySaving;

	UPROPERTY()
	bool bPendingSaveRequested;

	UPROPERTY()
	bool bCurrentlyLoading;

	TSharedPtr<SWidget> LoadingScreenWidget;

	FOnLoadGameFromSlotDelegate SavegameRetrievedDelegate;

	UPROPERTY()
	bool bCurrentlyApplyingSavegame;

	bool bEnableAutoSave;
	float autoSaveInterval;

	bool bIsLoadedGame;
	bool bDoShowLoadingScreen;
	bool bNeedsToApplySavegameData;

private:
	// keeps track of which actors were destroyed in the current level
	TArray<FName> destroyedActors;

	double loadStartTime;
	double minLoadDisplayTime;
	bool bWaitForStopInput;
	bool bMapsLoaded = false;
	bool bUserStopped;

	FString persistentSaveSlot;


public:
	void Initialize(FSubsystemCollectionBase& Collection) override;
	void Deinitialize() override;

	// FTickableGameObject implementation Begin
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsAllowedToTick() const override final;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	// FTickableGameObject implementation End

	UFUNCTION(BlueprintCallable)
	TArray<FString> GetSavegameSlotNames();

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "initialVisibleSublevels"), Category = Save)
	void CreateNewGame(const TSoftObjectPtr<UWorld> FirstLevel, const TArray<TSoftObjectPtr<UWorld>> initialVisibleSublevels, ULevelSequence* loadingAnimation, bool bAbsolute = true, FString Options = FString(TEXT("")));

	/*
	* opens the new level
	* @param NewLevel					Level to load
	* @param initialVisibleSublevels	levels to stream in on loading, before loading done is called
	* @param loadingAnimation			if empty, checks in persistentSaveGame if there is a loading animation referenced by the NewLevel
	*/
	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "initialVisibleSublevels"))
	void ChangeLevel(const TSoftObjectPtr<UWorld> NewLevel, const TArray<TSoftObjectPtr<UWorld>> initialVisibleSublevels, ULevelSequence* loadingAnimation = nullptr, bool bApplySavegameData = false, bool bAbsolute = true, FString Options = FString(TEXT("")));

	UFUNCTION(BlueprintCallable)
	void EndGame();

	UFUNCTION(BlueprintCallable)
	void SetSlotName(FString NewSlotName);

	UFUNCTION(BlueprintCallable, Category = Load)
	UCustomSaveGame* GetCurrentSaveGame();

	UFUNCTION(BlueprintCallable, Category = Load)
	UPersistentSaveGame* GetPersistentSaveGame();

	/* if slot is not specified, game is saved to the default or manually set safeSlot (SetSlotName) */
	UFUNCTION(BlueprintCallable, Category = Save)
	bool SaveGame(FString SlotName = "");

	UFUNCTION(BlueprintCallable, Category = Save)
	bool SavePersistentGame();

	UFUNCTION(BlueprintPure, Category = Load)
	bool HasSaveGames();

	UFUNCTION(BlueprintCallable, Category = Load)
	bool ContinueGame();

	UFUNCTION(BlueprintCallable, Category = Load)
	bool GetSaveGame(FString SlotName, const FOnLoadGameFromSlotDelegate& Callback);

	/*
	* @param bShowLoadingScreen		if false, user will have to call ApplySavegameData() manually
	*/
	UFUNCTION(BlueprintCallable, Category = Load)
	bool LoadGame(FString InSlotName = "", bool bShowLoadingScreen = true);

	UFUNCTION(BlueprintPure, Category = Load)
	inline bool IsLoadedGame() { return bIsLoadedGame; }

	UFUNCTION(BlueprintCallable)
	void SkipLoadingScreen();

	UFUNCTION(BlueprintCallable)
	void DeleteSavegame(FString SlotName);
	
	UFUNCTION(BlueprintCallable)
	void DeletePersistentSavegame();

	void HandleStartingNewPlayer(AController* NewPlayer);
	bool RestartPlayerAtTransform(AController* NewPlayer, FTransform& StartTransform, TSubclassOf<APawn>& PawnClass);

	/* needs to be UFUNCTION to be able to bind on dynamic delegate*/
	UFUNCTION()
	void AddDestroyedActor(AActor* actor);

	/*
	* use this function to reset the level to the current savegame state without loading the entire map
	*/
	UFUNCTION(BlueprintCallable, Category = Load)
	void ApplySaveGameData();

	UFUNCTION(BlueprintPure, Category = Load)
	inline bool NeedsToApplySavegameData() { return bNeedsToApplySavegameData; }

protected:
	virtual void HandleAsyncSave(const FString& SlotName, const int32 UserIndex, bool bSuccess);
	virtual void HandleAsyncSave_PersistentSlot(const FString& SlotName, const int32 UserIndex, bool bSuccess);

	virtual void HandleAsyncLoad(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame);
	virtual void HandleAsyncLoad_PersistentSlot(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame);

	virtual void HandleAsyncGetSaveGame(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame) { SavegameRetrievedDelegate.ExecuteIfBound(SlotName, UserIndex, SaveGame); SavegameRetrievedDelegate.Unbind(); bCurrentlyLoading = false; }

	static FString GetSavegamePath(const TCHAR* fileName);

private:
	inline FString GetCurrentWorldName() { return FPackageName::ObjectPathToPackageName(UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName())); }
	void AssembleSaveGameDataInSubLevel(ULevelStreaming* streamingLevel);
	void ApplySaveGameDataInSubLevel(ULevelStreaming* streamingLevel = nullptr);
	void AttachSavedActor(FActorSaveData* data, AActor* actor);
	FActorSaveData WriteSaveData(AActor* actor);
	void ReadSaveData(FActorSaveData data, AActor* actor);
	void BeginLoadingScreen(const FWorldContext& WorldContext, const FString& MapName);
	void EndLoadingScreen(UWorld* World);
	void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);
	void OnRemoveLevel(ULevel* InLevel, UWorld* InWorld);
	void OnTryAutoSaveIgnoreTimeDilation(UWorld* world, ELevelTick tickType, float realDeltaSeconds);
	void OnTryAutoSave(UWorld* world, ELevelTick tickType, float deltaSeconds);
	void OnWorldInitialized(UWorld* World, const UWorld::InitializationValues);
	void OnWorldInitializedActors(const FActorsInitializedParams&);
	void PropagateActorLoadedToNotSavedActors(ULevel* level);
};
