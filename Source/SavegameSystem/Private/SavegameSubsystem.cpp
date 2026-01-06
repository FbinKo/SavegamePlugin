// Fill out your copyright notice in the Description page of Project Settings.


#include "SavegameSubsystem.h"
#include "SaveablePlayerState.h"
#include "SavableObjectInterface.h"
#include "SaveGameSettings.h"
#include "CustomSaveGame.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/ThreadHeartBeat.h"
#include "SaveGameSystem.h"
#include "ShaderPipelineCache.h"
#include "PlatformFeatures.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "LevelSequence.h"

DEFINE_LOG_CATEGORY(LogSavegame);

//TODO for improvement, look at https://github.com/MilkyEngineer/SaveGameBasic_UnrealFestGC23/blob/main/Plugins/SaveGamePlugin/Source/SaveGamePlugin/Private/SaveGameSubsystem.cpp
//https://dev.epicgames.com/community/learning/talks-and-demos/4ORW/unreal-engine-serialization-best-practices-and-techniques

//TODO atm versioning for savegames is done manually, not in serialize
// if we want that type of versioning, we need to read and write the unreal headers or make all existing savegames invalid

void USavegameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	// Access defaults from DefaultGame.ini
	SaveSlot = SGSettings->SaveSlotName;
	SaveUserIndex = 0;
	bIsLoadedGame = false;
	bNeedsToApplySavegameData = false;

	bEnableAutoSave = false;
	autoSaveInterval = SGSettings->autoSaveInterval;

	persistentSaveSlot = SGSettings->PersistentSaveSlotName;

	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::BeginLoadingScreen);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::EndLoadingScreen);
	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USavegameSubsystem::OnLevelAdded);
	FWorldDelegates::PreLevelRemovedFromWorld.AddUObject(this, &USavegameSubsystem::OnRemoveLevel);
	FWorldDelegates::OnWorldTickStart.AddUObject(this, &USavegameSubsystem::OnTryAutoSaveIgnoreTimeDilation);
	FWorldDelegates::OnWorldTickEnd.AddUObject(this, &USavegameSubsystem::OnTryAutoSave);
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &USavegameSubsystem::OnWorldInitialized);
	FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &USavegameSubsystem::OnWorldInitializedActors);

	if (UGameplayStatics::DoesSaveGameExist(persistentSaveSlot, SaveUserIndex)) {
		UGameplayStatics::AsyncLoadGameFromSlot(persistentSaveSlot, SaveUserIndex, FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &USavegameSubsystem::HandleAsyncLoad_PersistentSlot));
	}
	else {
		USaveGame* newSaveGame = UGameplayStatics::CreateSaveGameObject(SGSettings->persistentSavegameClass);
		HandleAsyncLoad_PersistentSlot(persistentSaveSlot, SaveUserIndex, newSaveGame);
	}
}

void USavegameSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::PreLevelRemovedFromWorld.RemoveAll(this);
	FWorldDelegates::OnWorldTickStart.RemoveAll(this);
	FWorldDelegates::OnWorldTickEnd.RemoveAll(this);
	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
}

ETickableTickType USavegameSubsystem::GetTickableTickType() const
{
	return ETickableTickType::Conditional;
}

bool USavegameSubsystem::IsAllowedToTick() const
{
	return !HasAnyFlags(RF_ClassDefaultObject);
}

void USavegameSubsystem::Tick(float DeltaTime)
{
	if (bMapsLoaded)
	{
		if (FPlatformTime::Seconds() - loadStartTime >= minLoadDisplayTime)
		{
			if ((bWaitForStopInput && bUserStopped) || !bWaitForStopInput)
			{
				// now all levels are loaded and we can apply the savegame data to the player
				APlayerController* player = UGameplayStatics::GetPlayerController(GetWorld(), 0);
				if (ASaveablePlayerState* PS = player->GetPlayerState<ASaveablePlayerState>())
				{
					PS->ApplySaveGameData(CurrentSaveGame);
				}

				UGameInstance* gameInstance = GetGameInstance();
				if (LoadingScreenWidget.IsValid())
				{
					if (UGameViewportClient* gameViewportClient = gameInstance->GetGameViewportClient())
					{
						gameViewportClient->RemoveViewportWidgetContent(LoadingScreenWidget.ToSharedRef());

						//performance settings
						FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);
						gameViewportClient->bDisableWorldRendering = false;
						if (UWorld* world = gameViewportClient->GetWorld())
						{
							if (AWorldSettings* WorldSettings = world->GetWorldSettings(false, false))
								WorldSettings->bHighPriorityLoadingLocal = false;
						}

						// Resume reporting hitches now that the loading screen is down
						FGameThreadHitchHeartBeat::Get().ResumeHeartBeat();
					}
					LoadingScreenWidget.Reset();
				}

				bEnableAutoSave = true;
				OnSaveGameApplied.Broadcast(CurrentSaveGame);
			}
		}
	}
}

TStatId USavegameSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USavegameSubsystem, STATGROUP_Tickables);
}

TArray<FString> USavegameSubsystem::GetSavegameSlotNames()
{
	TArray<FString> saveNames;
	TArray<FString> filteredSaveNames;
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (SaveSystem) {
		SaveSystem->GetSaveGameNames(saveNames, SaveUserIndex);
		// remove settings
		const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
		filteredSaveNames = saveNames.FilterByPredicate([SGSettings](const FString& name) {
			return !name.IsEmpty() && !SGSettings->SaveGameSlotNamesToFilter.Contains(name) && name.Compare(SGSettings->PersistentSaveSlotName) != 0;
			});
		if (SGSettings->bSortSavegamesByTimestamp)
		{
			// always return autosave as first element and then latest to oldest save
			filteredSaveNames.Sort([SGSettings](const FString& lhPath, const FString& rhPath) {
				return (SGSettings->bAutoSaveIsAlwaysFirst && lhPath.Compare("AutoSave") == 0) || IFileManager::Get().GetTimeStamp(*GetSavegamePath(*lhPath)) > IFileManager::Get().GetTimeStamp(*GetSavegamePath(*rhPath));
				});
		}
	}
	return filteredSaveNames;
}


//TODO check ESaveFlags and ELoadFlags how to use them
void USavegameSubsystem::CreateNewGame(const TSoftObjectPtr<UWorld> FirstLevel, const TArray<TSoftObjectPtr<UWorld>> initialVisibleSublevels, bool bAbsolute, FString Options)
{
	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	CurrentSaveGame = Cast<UCustomSaveGame>(UGameplayStatics::CreateSaveGameObject(SGSettings->savegameClass));

	ChangeLevel(FirstLevel, initialVisibleSublevels, false, bAbsolute, Options);
}

void USavegameSubsystem::ChangeLevel(const TSoftObjectPtr<UWorld> NewLevel, const TArray<TSoftObjectPtr<UWorld>> initialVisibleSublevels, bool bApplySavegameData, bool bAbsolute, FString Options)
{
	const FString levelName = NewLevel ? FPackageName::ObjectPathToPackageName(NewLevel.ToString()) : GetCurrentWorldName();
	if (CurrentSaveGame) {
		bNeedsToApplySavegameData = bApplySavegameData;

		CurrentSaveGame->saveTime = FDateTime::Now();
		CurrentSaveGame->screenshotPath = "";

		CurrentSaveGame->currentMap = levelName;

		//setup new data for level if we dont have savegame data for it
		if (!CurrentSaveGame->MapSavedActors.Contains(levelName)) {
			CurrentSaveGame->MapSavedActors.Add(levelName, FMapActorSaveData());
			CurrentSaveGame->bRestartWithSavedPlayerData = false;

			CurrentSaveGame->sublevelInfos.Empty();
			for (TSoftObjectPtr<UWorld> sub : initialVisibleSublevels) {
				CurrentSaveGame->sublevelInfos.Add(FSubLevelInfo(FPackageName::ObjectPathToPackageName(sub.ToString())));
			}
		}

		if (persistentSaveGame) {
			persistentSaveGame->UnlockLevel(NewLevel, true);
			SavePersistentGame();
		}

		const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
		if (SGSettings->bSaveOnLevelChange)
		{
			if (SGSettings->bManuallySpawnPlayer)
				CurrentSaveGame->SavedPlayers.Empty();

			FLevelInfo info;
			if (!SGSettings->bUseSaveSlotAsNameForSaveOnLevelChange)
				persistentSaveGame->GetInfoForLevel(NewLevel, info);
			FString baseSlotName = SGSettings->bUseSaveSlotAsNameForSaveOnLevelChange ? SaveSlot : info.name.ToString();
			FString slotName = baseSlotName;

			if (SGSettings->bOverrideExistingSaveWhenSaveOnLevelChange)
			{
				DeleteSavegame(SaveSlot);
			}
			else
			{
				int count = 0;
				while (UGameplayStatics::DoesSaveGameExist(slotName, SaveUserIndex))
				{
					count++;
					slotName = baseSlotName + "_" + FString::FromInt(count);
					//in case we want to have folders per level, would have to rethink the naming(this would only be the count as name)
					//slotName = baseSlotName / FString::FromInt(count);
				}
			}
			SetSlotName(slotName);
			UGameplayStatics::SaveGameToSlot(CurrentSaveGame, SaveSlot, SaveUserIndex);
		}
	}

	UGameplayStatics::OpenLevel(GetWorld(), FName(levelName), bAbsolute, Options);
}

void USavegameSubsystem::EndGame()
{
	CurrentSaveGame = nullptr;
	bIsLoadedGame = false;
	bNeedsToApplySavegameData = false;
	bEnableAutoSave = false;
	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	SaveSlot = SGSettings->SaveSlotName;

	UGameplayStatics::OpenLevelBySoftObjectPtr(GetWorld(), SGSettings->mainMenuLevel);
}

void USavegameSubsystem::SetSlotName(FString NewSlotName)
{
	if (NewSlotName.IsEmpty()) return;

	SaveSlot = NewSlotName;
}

UCustomSaveGame* USavegameSubsystem::GetCurrentSaveGame()
{
	return CurrentSaveGame;
}

UPersistentSaveGame* USavegameSubsystem::GetPersistentSaveGame()
{
	return persistentSaveGame;
}

bool USavegameSubsystem::SaveGame(FString SlotName)
{
	if (!CurrentSaveGame) {
		UE_LOG(LogSavegame, Error, TEXT("USavegameSubsystem::SaveGame() failed because there is no CurrentSaveGame"));
		return false;
	}

	if (bCurrentlySaving) {
		bPendingSaveRequested = true;
		return true;
	}

	bCurrentlySaving = true;
	FString slotNameToUse = SlotName.IsEmpty() ? SaveSlot : SlotName;

	// clear all maps
	CurrentSaveGame->sublevelInfos.Empty();
	// Clear all actors from any previously loaded save to avoid duplicates
	CurrentSaveGame->SavedPlayers.Empty();
	FString currentWorld = GetCurrentWorldName();

	AGameStateBase* GS = GetWorld()->GetGameState();
	if (GS == nullptr)
	{
		// Warn about failure to save?
		return false;
	}

	if (GS->PlayerArray[0]->GetPawn())
	{
		if (USceneCaptureComponent2D* captureComponent = GS->PlayerArray[0]->GetPawn()->GetComponentByClass<USceneCaptureComponent2D>())
		{
			FString screenshotName = GetDefault<UEngine>()->GameScreenshotSaveDirectory.Path / slotNameToUse / ".png";

			captureComponent->CaptureScene();
			TArray<FColor> RawPixels;
			if (captureComponent->TextureTarget)
			{

				captureComponent->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(RawPixels);

				for (FColor& pixel : RawPixels)
				{
					pixel.A = 255;
				}

				FIntRect SourceRect(0, 0, /*GScreenshotResolutionX*/256, /*GScreenshotResolutionY*/256);
				FIntVector Size = FIntVector(captureComponent->TextureTarget->SizeX, captureComponent->TextureTarget->SizeY, 0);

				// Clip the bitmap to just the capture region if valid
				if (!SourceRect.IsEmpty())
				{
					const int32 OldWidth = captureComponent->TextureTarget->SizeX;
					const int32 OldHeight = captureComponent->TextureTarget->SizeY;

					//clamp in bounds:
					int CaptureMinX = FMath::Clamp(SourceRect.Min.X, 0, OldWidth);
					int CaptureMinY = FMath::Clamp(SourceRect.Min.Y, 0, OldHeight);

					int CaptureMaxX = FMath::Clamp(SourceRect.Max.X, 0, OldWidth);
					int CaptureMaxY = FMath::Clamp(SourceRect.Max.Y, 0, OldHeight);

					int32 NewWidth = CaptureMaxX - CaptureMinX;
					int32 NewHeight = CaptureMaxY - CaptureMinY;

					if (NewWidth > 0 && NewHeight > 0 && ((NewWidth != OldWidth) || (NewHeight != OldHeight)))
					{
						FColor* const Data = RawPixels.GetData();

						for (int32 Row = 0; Row < NewHeight; Row++)
						{
							FMemory::Memmove(Data + Row * NewWidth, Data + (Row + CaptureMinY) * OldWidth + CaptureMinX, NewWidth * sizeof(*Data));
						}

						RawPixels.RemoveAt(NewWidth * NewHeight, OldWidth * OldHeight - NewWidth * NewHeight, EAllowShrinking::No);
						Size = FIntVector(NewWidth, NewHeight, 0);
					}
				}

				FImageView Image((const FColor*)RawPixels.GetData(), Size.X, Size.Y);
				if (FImageUtils::SaveImageByExtension(*screenshotName, Image))
					CurrentSaveGame->screenshotPath = screenshotName;
			}
		}
	}

	// Iterate all player states, we don't have proper ID to match yet (requires Steam or EOS)
	// could choose to utilize SaveGame properties here too and store some of that player data automatically by converting it to binary array just like we do with Actors instead of manually writing it into SaveGame,
	// but you’d still need to manually handle the PlayerID and Pawn Transform
	for (int32 i = 0; i < GS->PlayerArray.Num(); i++)
	{
		ASaveablePlayerState* PS = Cast<ASaveablePlayerState>(GS->PlayerArray[i]);
		if (PS)
		{
			PS->SavePlayerState(CurrentSaveGame);
			break; // single player only at this point
		}
		else {
			UE_LOG(LogSavegame, Error, TEXT("player is not using ASaveablePlayerState and therefore can't be saved"));
		}
	}

	// Iterate the actors of persistent level
	FSubLevelActorSaveData persistentData;
	for (AActor* Actor : GetWorld()->PersistentLevel->Actors) {
		// Only interested in our 'gameplay actors', skip actors that are being destroyed
		// only actors with this interface get saved/loaded (the actors can mark variables in advanced for SaveGame or by UPROPERTY(SaveGame))
		if (!IsValid(Actor) || !Actor->Implements<USavableObjectInterface>())
		{
			continue;
		}
		// ignore pawns which impelement the interface AND are player controlled (eg. a parked vehicle the player entered)
		// TODO
		// atm we decided to spawn pawns which the player can possess in the game flow, but there should be a way to make it work with pawns placed in sublevels without a duplication of the object from loading
		// (they get moved to persistent level when they get possessed)
		if ((Cast<APawn>(Actor) && Cast<APawn>(Actor)->IsPlayerControlled()))
		{
			continue;
		}

		ISavableObjectInterface::Execute_OnSaveActor(Actor);
		FActorSaveData SaveData = WriteSaveData(Actor);

		persistentData.SavedActors.Add(SaveData);
	}
	CurrentSaveGame->MapSavedActors.FindChecked(GetCurrentWorldName()).ActorsInSublevels.Add(TEXT("None"), persistentData);

	// Iterate the actors of each sublevel
	for (ULevelStreaming* streamingLevel : GetWorld()->GetStreamingLevels()) {
		FSubLevelInfo info(streamingLevel);
		CurrentSaveGame->sublevelInfos.Add(info);
		if (streamingLevel->IsLevelLoaded()) {
			AssembleSaveGameDataInSubLevel(streamingLevel);
		}
	}

	// save destroyed actors
	CurrentSaveGame->MapSavedActors.FindChecked(currentWorld).DestroyedActors = destroyedActors;

	CurrentSaveGame->currentMap = currentWorld;
	CurrentSaveGame->bRestartWithSavedPlayerData = true;

	UGameplayStatics::AsyncSaveGameToSlot(CurrentSaveGame, slotNameToUse, SaveUserIndex, FAsyncSaveGameToSlotDelegate::CreateUObject(this, &USavegameSubsystem::HandleAsyncSave));

	if (persistentSaveGame)
		SavePersistentGame();

	return true;
}

bool USavegameSubsystem::SavePersistentGame()
{
	persistentSaveGame->HandlePreSave();
	UGameplayStatics::AsyncSaveGameToSlot(persistentSaveGame, persistentSaveSlot, SaveUserIndex, FAsyncSaveGameToSlotDelegate::CreateUObject(this, &USavegameSubsystem::HandleAsyncSave_PersistentSlot));
	return true;
}

bool USavegameSubsystem::HasSaveGames()
{
	/*TArray<FString> FoundFiles;
	const FString SaveGameDirectory = FPaths::ProjectSavedDir() / TEXT("SaveGames/");
	IFileManager::Get().FindFiles(FoundFiles, *SaveGameDirectory, TEXT("*.sav"));*/
	TArray<FString> slots = GetSavegameSlotNames();

	return !slots.IsEmpty();
}

bool USavegameSubsystem::ContinueGame()
{
	TArray<FString> slots = GetSavegameSlotNames();

	if (slots.IsEmpty()) {
		UE_LOG(LogSavegame, Error, TEXT("USavegameSubsystem::ContinueGame() failed because GetSaveGameNames failed to find any savegames."))
			return false;
	}

	return LoadGame(slots[0]);
}

bool USavegameSubsystem::GetSaveGame(FString InSlotName, const FOnLoadGameFromSlotDelegate& Callback)
{
	if (bCurrentlyLoading) return false;

	bCurrentlyLoading = true;

	if (UGameplayStatics::DoesSaveGameExist(InSlotName, SaveUserIndex)) {
		SavegameRetrievedDelegate = Callback;
		UGameplayStatics::AsyncLoadGameFromSlot(InSlotName, SaveUserIndex, FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &USavegameSubsystem::HandleAsyncGetSaveGame));
	}
	else {
		bCurrentlyLoading = false;
		UE_LOG(LogSavegame, Error, TEXT("USavegameSubsystem::GetSaveGame(FString InSlotName, const FAsyncLoadGameFromSlotDelegate& Callback) failed because the savegame \"%s\" doesn't exist"), *InSlotName);
	}
	return bCurrentlyLoading;
}

bool USavegameSubsystem::LoadGame(FString InSlotName, bool bShowLoadingScreen)
{
	if (bCurrentlyLoading || bCurrentlyApplyingSavegame || bCurrentlySaving) return false;

	bCurrentlyLoading = true;
	bDoShowLoadingScreen = bShowLoadingScreen;

	if (UGameplayStatics::DoesSaveGameExist(InSlotName, SaveUserIndex)) {
		SetSlotName(InSlotName);
		UGameplayStatics::AsyncLoadGameFromSlot(SaveSlot, SaveUserIndex, FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &USavegameSubsystem::HandleAsyncLoad));
	}
	else {
		bCurrentlyLoading = false;
		UE_LOG(LogSavegame, Error, TEXT("USavegameSubsystem::LoadGame(FString InSlotName) failed because the savegame \"%s\" doesn't exist"), *InSlotName);
	}
	return bCurrentlyLoading;
}

void USavegameSubsystem::SkipLoadingScreen()
{
	if (bMapsLoaded)
		bUserStopped = true;
}

void USavegameSubsystem::DeleteSavegame(FString SlotName)
{
	if (bCurrentlyApplyingSavegame || bCurrentlyLoading || bCurrentlySaving) return;

	UGameplayStatics::DeleteGameInSlot(SlotName, SaveUserIndex);
}

void USavegameSubsystem::DeletePersistentSavegame()
{
	UGameplayStatics::DeleteGameInSlot(persistentSaveSlot, SaveUserIndex);
}

// gets called from gamemode::handlestartingnewplayer
void USavegameSubsystem::HandleStartingNewPlayer(AController* NewPlayer)
{
	ASaveablePlayerState* PS = NewPlayer->GetPlayerState<ASaveablePlayerState>();
	if (ensure(PS))
	{
		PS->LoadPlayerState(CurrentSaveGame);
	}
}

bool USavegameSubsystem::RestartPlayerAtTransform(AController* NewPlayer, FTransform& StartTransform, TSubclassOf<APawn>& PawnClass)
{
	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	if (SGSettings->bManuallySpawnPlayer)
		return false;

	ASaveablePlayerState* PS = NewPlayer->GetPlayerState<ASaveablePlayerState>();
	if (ensure(PS) && CurrentSaveGame)
	{
		PawnClass = *PS->GetPlayerClass();
		StartTransform = PS->GetRestartTransform();
		return CurrentSaveGame->bRestartWithSavedPlayerData;
	}

	return false;
}

void USavegameSubsystem::AddDestroyedActor(AActor* actor)
{
	if (actor->HasAnyFlags(RF_WasLoaded) && actor->Implements<USavableObjectInterface>()) {
		destroyedActors.Add(actor->GetFName());
	}
}

void USavegameSubsystem::ApplySaveGameData()
{
	bNeedsToApplySavegameData = false;
	if (bCurrentlyApplyingSavegame) return;

	BeginLoadingScreen(*GEngine->GetWorldContextFromWorld(GetWorld()), GetWorld()->GetMapName());

	if (CurrentSaveGame) {
		if (CurrentSaveGame->MapSavedActors.Find(GetCurrentWorldName())) {
			// set destroyed actors of current level from savegame (relevant for saving later on)
			destroyedActors = CurrentSaveGame->MapSavedActors.Find(GetCurrentWorldName())->DestroyedActors;

			// load sublevels, save data for actors in them will be applied from USavegameSubsystem::OnLevelAdded
			for (ULevelStreaming* LevelStreaming : GetWorld()->GetStreamingLevels())
			{
				for (FSubLevelInfo sublevel : CurrentSaveGame->sublevelInfos)
				{
					if (UWorld::RemovePIEPrefix(LevelStreaming->GetWorldAssetPackageName()) == UWorld::RemovePIEPrefix(sublevel.name)) {
						// only relevant for LevelStreamingDynamic, adds there the level to StreamingLevelsToConsider, this is a list of streaming levels which need changes (eg. make visible, load, unload, etc.)
						LevelStreaming->SetShouldBeLoaded(sublevel.bIsLoaded);
						// adds the streaming level to StreamingLevelsToConsider
						LevelStreaming->SetShouldBeVisible(sublevel.bIsVisible);
						LevelStreaming->bShouldBlockOnLoad = false;
						LevelStreaming->SetPriority(sublevel.StreamingPriority);

						break;
					}
				}
			}

			// apply savegame data to persistent level and handles OnSavegameApplied callback if there are no sublevels trying to get visible
			OnLevelAdded(GetWorld()->GetCurrentLevel(), GetWorld());
			// if we have sublevels which are always loaded, we need to call them here separately, because when they get added at start (OnLevelAdded)
			// their actors are not yet fully loaded and for somereason dont execute OnActorLoaded in blueprint
			for (ULevelStreaming* LevelStreaming : GetWorld()->GetStreamingLevels())
			{
				if (LevelStreaming && LevelStreaming->GetLoadedLevel()) {
					OnLevelAdded(LevelStreaming->GetLoadedLevel(), GetWorld());
				}
			}
		}
	}
	else
	{
		UE_LOG(LogSavegame, Error, TEXT("USavegameSubsystem::ApplySaveGameData() failed because there was no savegame loaded, still trigger actor loaded"));
	}
}

void USavegameSubsystem::HandleAsyncSave(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
	ensure(bCurrentlySaving);
	bCurrentlySaving = false;

	OnSaveGameSaved.Broadcast(CurrentSaveGame);

	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	autoSaveInterval = SGSettings->autoSaveInterval;

	if (bPendingSaveRequested)
	{
		// Start another save as we got a request while saving
		bPendingSaveRequested = false;
		SaveGame();
	}
}

void USavegameSubsystem::HandleAsyncSave_PersistentSlot(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
	OnPersistentSaveGameSaved.Broadcast(persistentSaveGame);
}

void USavegameSubsystem::HandleAsyncLoad(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame)
{
	ensure(bCurrentlyLoading);
	bCurrentlyLoading = false;
	CurrentSaveGame = Cast<UCustomSaveGame>(SaveGame);
	bIsLoadedGame = true;
	bNeedsToApplySavegameData = true;

	OnSaveGameLoaded.Broadcast(CurrentSaveGame);

	//GetWorld()->SeamlessTravel(*CurrentSaveGame->currentMap, true);
	UGameplayStatics::OpenLevel(GetWorld(), FName(*CurrentSaveGame->currentMap), true/*, TEXT("?bShowLoadingScreen=1")*/);
}

void USavegameSubsystem::HandleAsyncLoad_PersistentSlot(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame)
{
	persistentSaveGame = Cast<UPersistentSaveGame>(SaveGame);
	persistentSaveGame->HandlePostLoad();

	OnPersistentSaveGameLoaded.Broadcast(persistentSaveGame);
}

FString USavegameSubsystem::GetSavegamePath(const TCHAR* fileName)
{
	return FString::Printf(TEXT("%sSaveGames/%s.sav"), *FPaths::ProjectSavedDir(), fileName);
}

void USavegameSubsystem::AssembleSaveGameDataInSubLevel(ULevelStreaming* streamingLevel)
{
	if (!streamingLevel) return;
	FSubLevelActorSaveData data;
	for (AActor* Actor : streamingLevel->GetLoadedLevel()->Actors)
	{
		// Only interested in our 'gameplay actors', skip actors that are being destroyed
		// aside from player, only actors with this interface get saved/loaded (the actors can mark variables in advanced for SaveGame or by UPROPERTY(SaveGame))
		if (!IsValid(Actor) || !Actor->Implements<USavableObjectInterface>())
		{
			continue;
		}

		ISavableObjectInterface::Execute_OnSaveActor(Actor);
		FActorSaveData SaveData = WriteSaveData(Actor);

		data.SavedActors.Add(SaveData);
	}

	UE_LOG(LogSavegame, Log, TEXT("saving sublevel \"%s\""), *streamingLevel->GetWorldAssetPackageName());

	CurrentSaveGame->MapSavedActors.FindChecked(GetCurrentWorldName()).ActorsInSublevels.Add(streamingLevel->GetWorldAssetPackageName(), data);
}

void USavegameSubsystem::ApplySaveGameDataInSubLevel(ULevelStreaming* streamingLevel /*= nullptr*/)
{
	// there might be no savegame yet and a sublevel is made visible -> OnLevelAdded is called
	// potential situation:
	// - start new game which only calls SetSlotName() and has not saved yet
	// - open a map without loading
	if (CurrentSaveGame) {
		FMapActorSaveData* savedActorsData = CurrentSaveGame->MapSavedActors.Find(GetCurrentWorldName());

		if (savedActorsData) {
			FString levelName = streamingLevel ? streamingLevel->GetWorldAssetPackageName() : TEXT("None");
			ULevel* level = streamingLevel ? streamingLevel->GetLoadedLevel() : GetWorld()->GetCurrentLevel();

			if (savedActorsData->ActorsInSublevels.Find(levelName)) {
				TArray<FActorSaveData> savedActorsInLevel = savedActorsData->ActorsInSublevels.Find(levelName)->SavedActors;

				// apply save data to actors in level
				for (FActorSaveData ActorData : savedActorsInLevel) {
					TObjectPtr<AActor>* Ptr = level->Actors.FindByPredicate([&](const AActor* it) { if (!IsValid(it)) return false; return it->GetFName() == ActorData.ActorName; });
					AActor* Actor = nullptr;
					// spawn missing actors
					if (Ptr) {
						Actor = Ptr->Get();
					}
					else {
						// can happen if class was removed
						if (!ActorData.ActorClass) continue;

						FActorSpawnParameters params;
						params.Name = ActorData.ActorName;
						params.bNoFail = true;
						Actor = GetWorld()->SpawnActor<AActor>(ActorData.ActorClass, ActorData.SavedComponents[0].Transform, params);
					}

					//TODO
					// actor is attached to other, it tries to do attachment in readsavedata to self somehow
					// if actor is attached to actor in other level, search below wont find it as it only looks in streaminglevel
					ReadSaveData(ActorData, Actor);

					if (!ActorData.AttachedActorName.IsNone()) {
						if (TObjectPtr<AActor>* AttachPtr = level->Actors.FindByPredicate([&](const AActor* it) { if (!IsValid(it)) return false; return it->GetFName() == ActorData.AttachedActorName; })) {
							AActor* AttachActor = AttachPtr->Get();
							USceneComponent* attachComponent = nullptr;
							TInlineComponentArray<USceneComponent*> Components;
							AttachActor->GetComponents(Components);

							for (USceneComponent* Component : Components) {
								if (ActorData.AttachedComponentName == Component->GetFName()) {
									attachComponent = Component;
									break;
								}
							}

							//TODO weldsimulated bodies atm true, should check if we can save the state and initialize it correctly
							Actor->AttachToComponent(attachComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, true), ActorData.AttachedSocketName);
						}
						else {
							if (Actor->GetIsSpatiallyLoaded() && !Actor->IsInPersistentLevel())
							{
								//TODO create option for user to customize, if they are aware how to handle the situation
								UE_LOG(LogSavegame, Warning, TEXT("attach target %s for %s (spatially loaded and not in persistant level) is not of same level, make sure they part of the same level or it could result a unload/load mismatch"), *ActorData.AttachedActorName.ToString(), *Actor->GetFName().ToString());
							}
							else
							{
								//check if target is in other level, should be avoided but just in case it is covered
								TArray<AActor*> allActors;
								UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), allActors);
								AActor** FoundActor = allActors.FindByPredicate([&](const AActor* it) { if (!IsValid(it) || (it->GetIsSpatiallyLoaded() && !it->IsInPersistentLevel())) return false; return it->GetFName() == ActorData.AttachedActorName; });
								AActor* AttachActor = nullptr;
								if (FoundActor)
									AttachActor = *FoundActor;
								if (AttachActor) {
									USceneComponent* attachComponent = nullptr;
									TInlineComponentArray<USceneComponent*> Components;
									AttachActor->GetComponents(Components);

									for (USceneComponent* Component : Components) {
										if (ActorData.AttachedComponentName == Component->GetFName()) {
											attachComponent = Component;
											break;
										}
									}

									//TODO weldsimulated bodies atm true, should check if we can save the state and initialize it correctly
									Actor->AttachToComponent(attachComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, true), ActorData.AttachedSocketName);
								}
								else
								{
									//TODO create option for user to customize, if they are aware how to handle the situation
									UE_LOG(LogSavegame, Warning, TEXT("attach target %s (spatially loaded and not in persistant level) for %s is not of same level, make sure they part of the same level or it could result a unload/load mismatch"), *ActorData.AttachedActorName.ToString(), *Actor->GetFName().ToString());
								}
							}
						}

					}

					// callback for actor, loading done
					ISavableObjectInterface::Execute_OnActorLoaded(Actor, GetWorld()->HasBegunPlay());
				}
			}
			else {
				PropagateActorLoadedToNotSavedActors(level);
			}

			// destroy killed actors
			for (FName name : savedActorsData->DestroyedActors) {
				if (TObjectPtr<AActor>* Ptr = level->Actors.FindByPredicate([&](const AActor* it) { if (!IsValid(it)) return false; return it->GetFName() == name; })) {
					Ptr->Get()->Destroy();
					break;
				}
			}
		}
	}
	else {
		// in case we load a streaming level, but dont have a savegame, we still want OnActorLoaded called on all their actors
		ULevel* level = streamingLevel ? streamingLevel->GetLoadedLevel() : GetWorld()->PersistentLevel.Get();
		PropagateActorLoadedToNotSavedActors(level);
	}
}

FActorSaveData USavegameSubsystem::WriteSaveData(AActor* actor)
{
	FActorSaveData data;
	data.ActorName = actor->GetFName();
	data.ActorClass = actor->GetClass();

	// Pass the array to fill with data from Actor
	FMemoryWriter MemWriter(data.ByteData, true);

	FObjectAndNameAsStringProxyArchive Ar(MemWriter, true);
	// Find only variables with UPROPERTY(SaveGame)
	Ar.ArIsSaveGame = true;
	// Converts Actor's SaveGame UPROPERTIES into binary array
	actor->Serialize(Ar);

	// get data from components to save (this also contains the rootcomponent is the actor transform)
	for (UActorComponent* component : actor->GetComponents()) {
		FComponentSaveData ComponentData;
		ComponentData.ComponentName = component->GetFName();

		if (USceneComponent* sC = Cast<USceneComponent>(component)) {
			//ComponentData.Transform = sC->GetComponentTransform();
			// use relative transform, because it is world transform if there is no parent
			ComponentData.Transform = sC->GetRelativeTransform();
			//dont put parent of root component (attachment is handled in actor)
			if (actor->GetRootComponent() != sC)
			{
				if (USceneComponent* parent = sC->GetAttachParent()) {
					ComponentData.AttachParentName = parent->GetFName();
					ComponentData.AttachSocketName = sC->GetAttachSocketName();
				}
			}
			//TODO add physics to save data
			//ComponentData.Velocity = sC->GetComponentVelocity();

			if (sC->IsSimulatingPhysics()) {
				if (UPrimitiveComponent* pC = Cast<UPrimitiveComponent>(component)) {
					// dont need to check for nullptr, because then it wouldnt be able to simulate
					FBodyInstance* BI = pC->GetBodyInstance(NAME_None, false);
					// use this, because attached meshes also return true for IsSimulatingPhysics() even if they dont simulate separately
					// TODO check if maybe better to check for welded or something else instead
					ComponentData.bSimulatesPhysics = BI->bSimulatePhysics;
				}
			}
		}

		FMemoryWriter CMemWriter(ComponentData.ByteData, true);

		FObjectAndNameAsStringProxyArchive CAr(CMemWriter, true);
		CAr.ArIsSaveGame = true;
		component->Serialize(CAr);

		data.SavedComponents.Add(ComponentData);
	}

	if (AActor* attachParent = actor->GetAttachParentActor()) {
		data.AttachedActorName = attachParent->GetFName();
		data.AttachedActorClass = attachParent->GetClass();
		data.AttachedComponentName = actor->GetRootComponent()->GetAttachParent()->GetFName();
		data.AttachedSocketName = actor->GetAttachParentSocketName();
		UE_LOG(LogSavegame, Log, TEXT("actor: %s, component: %s, socket: %s"), *data.AttachedActorName.ToString(), *data.AttachedComponentName.ToString(), *data.AttachedSocketName.ToString());
	}

	return data;
}

void USavegameSubsystem::ReadSaveData(FActorSaveData data, AActor* actor)
{
	FMemoryReader MemReader(data.ByteData, true);

	FObjectAndNameAsStringProxyArchive Ar(MemReader, true);
	Ar.ArIsSaveGame = true;
	// Convert binary array back into actor's variables
	actor->Serialize(Ar);

	// load data for components
	for (UActorComponent* component : actor->GetComponents()) {
		for (FComponentSaveData componentData : data.SavedComponents) {
			if (componentData.ComponentName == component->GetFName()) {
				if (USceneComponent* sC = Cast<USceneComponent>(component)) {
					//sC->SetWorldTransform(componentData.Transform, false, nullptr, ETeleportType::ResetPhysics);
					//usually this makes no difference, but just to be save if we change attachment hierarchy
					if (!componentData.AttachParentName.IsNone()) {
						TArray<USceneComponent*> attachComponents;
						actor->GetComponents(attachComponents);
						for (USceneComponent* attachComponent : attachComponents) {
							if (componentData.AttachParentName == attachComponent->GetFName()) {
								sC->AttachToComponent(attachComponent, FAttachmentTransformRules(EAttachmentRule::KeepWorld, false), componentData.AttachSocketName);
								break;
							}
						}
					}

					//TODO for now only set physics saved states when game has started, that means it excludes spawned actors but everything else should be fine
					// the spawned actors will be activated just before savegame applied finishes
					if (GetWorld()->HasBegunPlay()) {
						// physics setup needs to be done first, because it can result in component detach, this needs to be done to ensure the component hierarchy is the same as when saving
						if (UPrimitiveComponent* pC = Cast<UPrimitiveComponent>(component)) {
							pC->SetSimulatePhysics(componentData.bSimulatesPhysics);
						}
					}

					sC->SetRelativeTransform(componentData.Transform, false, nullptr, ETeleportType::ResetPhysics);

					//TODO add physics to save data
					/*if(UPrimitiveComponent* pC = Cast< UPrimitiveComponent>(component))
						pC->SetAllPhysicsLinearVelocity()*/
				}

				FMemoryReader CMemReader(componentData.ByteData, true);

				FObjectAndNameAsStringProxyArchive CAr(CMemReader, true);
				CAr.ArIsSaveGame = true;
				component->Serialize(CAr);

				break;
			}
		}
	}
}

void USavegameSubsystem::BeginLoadingScreen(const FWorldContext& WorldContext, const FString& MapName)
{
	if (!bDoShowLoadingScreen)
		return;

	if (WorldContext.OwningGameInstance == GetGameInstance())
	{
		bMapsLoaded = false;

		if (GEngine->IsInitialized())
		{
			bUserStopped = false;
			bCurrentlyApplyingSavegame = true;
			UGameInstance* gameInstance = WorldContext.OwningGameInstance;
			const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();

			TSubclassOf<UUserWidget> loadingScreenClass = SGSettings->LoadingScreenWidget.TryLoadClass<UUserWidget>();
			if (UUserWidget* loadingScreen = UUserWidget::CreateWidgetInstance(*gameInstance, loadingScreenClass, NAME_None))
			{
				loadStartTime = FPlatformTime::Seconds();
				minLoadDisplayTime = SGSettings->minLoadingScreenDuration;
				bWaitForStopInput = SGSettings->bWaitForUserInputToEndLoadingScreen;
				UGameViewportClient* gameViewportClient = WorldContext.GameViewport;
				LoadingScreenWidget = loadingScreen->TakeWidget();
				gameViewportClient->AddViewportWidgetContent(LoadingScreenWidget.ToSharedRef(), SGSettings->LoadingScreenZOrder);

				//performance settings
				FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);
				gameViewportClient->bDisableWorldRendering = true;
				if (WorldContext.World())
				{
					if (AWorldSettings* WorldSettings = WorldContext.World()->GetWorldSettings(false, false))
						WorldSettings->bHighPriorityLoadingLocal = true;
				}
				// Do not report hitches while the loading screen is up
				FGameThreadHitchHeartBeat::Get().SuspendHeartBeat();

				if (bWaitForStopInput)
				{
					//savety in case a user didnt set value
					loadingScreen->SetIsFocusable(true);
					FSlateApplication::Get().SetAllUserFocus(LoadingScreenWidget);
				}
				FSlateApplication::Get().Tick();
			}
		}
	}
}

void USavegameSubsystem::EndLoadingScreen(UWorld* World)
{
	if (bCurrentlyApplyingSavegame)
	{
		bCurrentlyApplyingSavegame = false;
		bMapsLoaded = true;
	}
}

void USavegameSubsystem::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	// only apply savegame if actors are initialized (else it would be triggered multiple times if there are some sub levels always loaded,
	// because of OnWorldInitializedActors) so we get actor loaded called before BeginPlay on startup of game
	// TODO at startup first loaded then beginplay but when loading sublevel beginplay gets called before loaded
	// TODO this seems to be called async, because when I accidentally filled the destroyedActors array with too many entries, not all were called/destroyed in there
	// so we need to figure out how to solve this
	if (InWorld->AreActorsInitialized())
	{
		ULevelStreaming* streamingLevel = ULevelStreaming::FindStreamingLevel(InLevel);
		FString levelName = streamingLevel ? streamingLevel->GetWorldAssetPackageName() : InWorld->GetCurrentLevel()->GetFName().ToString();

		UE_LOG(LogSavegame, Log, TEXT("Level '%s' added"), *levelName);
		ApplySaveGameDataInSubLevel(streamingLevel);
	}
}

void USavegameSubsystem::OnRemoveLevel(ULevel* InLevel, UWorld* InWorld)
{
	if (CurrentSaveGame && !InWorld->bIsTearingDown)
		AssembleSaveGameDataInSubLevel(ULevelStreaming::FindStreamingLevel(InLevel));
}

void USavegameSubsystem::OnTryAutoSaveIgnoreTimeDilation(UWorld* world, ELevelTick tickType, float realDeltaSeconds)
{
	// tick is called on not only the current active world, this is making sure our tick is only updating when it is supposed to
	if (GetWorld() != world)
		return;

	// check if manual savegame data application completed, because when in USavegameSubsystem::OnLevelAdded HasStreamingLevelsToConsider() still contains the added level and loadmap delegates
	// dont get called without actually loading the map
	if (bCurrentlyApplyingSavegame) {
		EndLoadingScreen(GetWorld());
	}

	if (!bEnableAutoSave || bCurrentlySaving) return;

	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	if (SGSettings->bAutoSave && !SGSettings->bAutoSaveAppliesTimeDilation) {
		autoSaveInterval -= realDeltaSeconds;

		if (autoSaveInterval <= 0.f)
			SaveGame("AutoSave");
	}
}

void USavegameSubsystem::OnTryAutoSave(UWorld* world, ELevelTick tickType, float deltaSeconds)
{
	// tick is called on not only the current active world, this is making sure our tick is only updating when it is supposed to
	if (GetWorld() != world)
		return;

	if (!bEnableAutoSave || bCurrentlySaving) return;

	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
	if (SGSettings->bAutoSave && SGSettings->bAutoSaveAppliesTimeDilation) {
		autoSaveInterval -= deltaSeconds;

		if (autoSaveInterval <= 0.f)
			SaveGame("AutoSave");
	}
}

void USavegameSubsystem::OnWorldInitialized(UWorld* World, const UWorld::InitializationValues)
{
	if (!IsValid(World) || GetWorld() != World)
	{
		return;
	}

	//World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorPreSpawn));
	World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &USavegameSubsystem::AddDestroyedActor));
}

void USavegameSubsystem::OnWorldInitializedActors(const FActorsInitializedParams&)
{
	const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();

	OnLevelAdded(GetWorld()->GetCurrentLevel(), GetWorld());
}

void USavegameSubsystem::PropagateActorLoadedToNotSavedActors(ULevel* level)
{
	for (int i = 0; i < level->Actors.Num(); i++) {
		AActor* Actor = level->Actors[i];
		// Only interested in our 'gameplay actors'
		if (!IsValid(Actor) || !Actor->Implements<USavableObjectInterface>())
		{
			continue;
		}

		// call on OnActorLoaded for savable actors to replace begin play
		ISavableObjectInterface::Execute_OnActorLoaded(Actor, GetWorld()->HasBegunPlay());
	}
}
