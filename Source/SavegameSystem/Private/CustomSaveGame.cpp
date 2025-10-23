// Fill out your copyright notice in the Description page of Project Settings.


#include "CustomSaveGame.h"
//#include "Blueprint/BlueprintExceptionInfo.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/DefaultPawn.h"
#include "ImageUtils.h"
#include "SaveGameSettings.h"
#include "Tasks/Pipe.h"
#include "Engine/AssetManager.h"

UE::Tasks::FPipe UCustomSaveGame::AsyncTaskPipe{ TEXT("LoadScreenshotPipe") };

FPlayerSaveData* UCustomSaveGame::GetPlayerData(APlayerState* PlayerState)
{
	if (PlayerState == nullptr)
	{
		return nullptr;
	}

	// Will not give unique ID while PIE so we skip that step while testing in editor.
	// UObjects don't have access to UWorld, so we grab it via PlayerState instead
	if (PlayerState->GetWorld()->IsPlayInEditor())
	{
		UE_LOG(LogTemp, Log, TEXT("During PIE we cannot use PlayerID to retrieve Saved Player data. Using first entry in array if available."));

		if (SavedPlayers.IsValidIndex(0))
		{
			return &SavedPlayers[0];
		}

		// No saved player data available
		return nullptr;
	}

	// Easiest way to deal with the different IDs is as FString (original Steam id is uint64)
	// Keep in mind that GetUniqueId() returns the online id, where GetUniqueID() is a function from UObject (very confusing...)
	FString PlayerID = PlayerState->GetUniqueId().ToString();
	// Iterate the array and match by PlayerID (eg. unique ID provided by Steam)
	return SavedPlayers.FindByPredicate([&](const FPlayerSaveData& Data) { return Data.PlayerID == PlayerID; });
}

void UCustomSaveGame::GetScreenshot(const UObject* WorldContextObject, const FOnScreenshotLoaded& Callback)
{
	if (FPaths::FileExists(screenshotPath))
	{
		AsyncTaskPipe.Launch(UE_SOURCE_LOCATION,
			[this, Callback]()
			{
				FTaskTagScope Scope(ETaskTag::EParallelRenderingThread);

				UTexture2D* texture = FImageUtils::ImportFileAsTexture2D(screenshotPath);

				OnAsyncComplete([texture, Callback]()
					{
						Callback.ExecuteIfBound(texture);
					});
			});
	}

	else if (const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>()) {
		if (SGSettings->defaultSaveGameScreenshot.IsValid()) {
			struct FLoadScreenshotAction : public FPendingLatentAction
			{
			public:
				FSoftObjectPath SoftObjectPath;
				FStreamableManager StreamableManager;
				TSharedPtr<FStreamableHandle> Handle;
				FName ExecutionFunction;
				int32 OutputLink;
				FWeakObjectPtr CallbackTarget;
				FOnScreenshotLoaded OnLoadedCallback;

				FLoadScreenshotAction(const FSoftObjectPath& InSoftObjectPath, const FLatentActionInfo& InLatentInfo, FOnScreenshotLoaded Callback)
					: SoftObjectPath(InSoftObjectPath)
					, ExecutionFunction(InLatentInfo.ExecutionFunction)
					, OutputLink(InLatentInfo.Linkage)
					, CallbackTarget(InLatentInfo.CallbackTarget)
					, OnLoadedCallback(Callback)
				{
					Handle = StreamableManager.RequestAsyncLoad(SoftObjectPath);
				}

				~FLoadScreenshotAction()
				{
					if (Handle.IsValid())
					{
						Handle->ReleaseHandle();
					}
				}


				virtual void UpdateOperation(FLatentResponse& Response) override
				{
					const bool bLoaded = !Handle.IsValid() || Handle->HasLoadCompleted() || Handle->WasCanceled();
					if (bLoaded)
					{
						OnLoaded();
					}
					Response.FinishAndTriggerIf(bLoaded, ExecutionFunction, OutputLink, CallbackTarget);
				}

				void OnLoaded()
				{
					UTexture2D* screenshot = Cast<UTexture2D>(SoftObjectPath.ResolveObject());
					OnLoadedCallback.ExecuteIfBound(screenshot);
				}
			};
			if (TSoftObjectPtr<UWorld> World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
			{
				FLatentActionManager& LatentManager = World->GetLatentActionManager();

				FLoadScreenshotAction* NewAction = new FLoadScreenshotAction(SGSettings->defaultSaveGameScreenshot, FLatentActionInfo(), Callback);
				LatentManager.AddNewAction(this, GetUniqueID(), NewAction);
			}
			/*if (UAssetManager* manager = UAssetManager::GetIfValid())
			{
				manager->GetStreamableManager().RequestAsyncLoad(SGSettings->defaultSaveGameScreenshot, FStreamableDelegate::CreateLambda(MoveTemp(OnAsyncComplete[SGSettings->defaultSaveGameScreenshot])));
			}
			UTexture2D* texture = Cast<UTexture2D>(SGSettings->defaultSaveGameScreenshot.TryLoad());
			Callback.ExecuteIfBound(texture);*/
		}
	}
}

void UCustomSaveGame::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading() && SavedDataVersion != FSaveGameObjectVersion::LatestVersion) {
		// in case our save game changes formats, use this place to get the data into the correct format
		// to get it working, add new version into the enum -> LatestVersion is increased by 1

		SavedDataVersion = FSaveGameObjectVersion::LatestVersion;
	}
}

void UCustomSaveGame::OnAsyncComplete(TFunction<void()> Callback)
{
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Callback = MoveTemp(Callback)](float) -> bool
		{
			Callback();
			return false;
		}
	));
}

FPlayerSaveData::FPlayerSaveData()
{
	Location = FVector::ZeroVector;
	Rotation = FRotator::ZeroRotator;
	bResumeAtTransform = false;
	PlayerClass = ADefaultPawn::StaticClass();
}

//void UPersistentSaveGame::GetRecords(ERecordType type, FName key, FRecords& records)
//{
//	switch (type)
//	{
//	case BurpNation:
//		records = *;
//		break;
//	case UltimateFrisbee:
//		records = *;
//		break;
//	case Racing:
//		records = ;
//		break;
//	default:
//		break;
//	}
//}

TArray<FLevelInfo> UPersistentSaveGame::GetLevelInfos(bool bOnlyUnlocked)
{
	if (bOnlyUnlocked)
		return levelInfos.FilterByPredicate([](const FLevelInfo& Data) { return Data.bIsUnlocked; });
	return levelInfos;
}

FLevelInfo UPersistentSaveGame::GetInfoForCurrentLevel(const UObject* WorldContextObject)
{
	if (TSoftObjectPtr<UWorld> World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (FLevelInfo* info = levelInfos.FindByPredicate([&](const FLevelInfo& info) { return FPackageName::ObjectPathToPackageName(info.level.ToString()) == UWorld::StripPIEPrefixFromPackageName(FPackageName::ObjectPathToPackageName(World.ToString()), World->StreamingLevelsPrefix); }))
			return *info;
	}

	return FLevelInfo();
}

bool UPersistentSaveGame::GetInfoForLevelPath(FString path, FLevelInfo& info)
{
	if (FLevelInfo* i = levelInfos.FindByPredicate([&](const FLevelInfo& info) {return FPackageName::ObjectPathToPackageName(info.level.ToString()) == path; }))
	{
		info = *i;
		return true;
	}

	info = FLevelInfo();
	return false;
}

bool UPersistentSaveGame::GetInfoForLevel(TSoftObjectPtr<UWorld> level, FLevelInfo& info)
{
	if (FLevelInfo* i = levelInfos.FindByPredicate([&](const FLevelInfo& info) { return info.level == level; }))
	{
		info = *i;
		return true;
	}

	info = FLevelInfo();
	return false;
}

bool UPersistentSaveGame::UnlockLevel(TSoftObjectPtr<UWorld> level, bool bUnlock)
{
	int id = levelInfos.IndexOfByPredicate([&](const FLevelInfo& Data) {
		return Data.level == level;
		});

	if (id != INDEX_NONE) {
		levelInfos[id].bIsUnlocked = bUnlock;
		return true;
	}
	return false;
}

TArray<FLevelInfo> UPersistentSaveGame::GetMinigameInfos(bool bOnlyUnlocked)
{
	if (bOnlyUnlocked)
		return minigameInfos.FilterByPredicate([](const FLevelInfo& Data) { return Data.bIsUnlocked; });
	return minigameInfos;
}

bool UPersistentSaveGame::UnlockMinigame(TSoftObjectPtr<UWorld> level, bool bUnlock)
{
	int id = minigameInfos.IndexOfByPredicate([&](const FLevelInfo& Data) {
		return Data.level == level;
		});

	if (id != INDEX_NONE) {
		minigameInfos[id].bIsUnlocked = bUnlock;
		return true;
	}
	return false;
}

//DEFINE_FUNCTION(UPersistentSaveGame::execGetRecords)
//{
//	P_GET_ENUM(ERecordType, type);
//	P_GET_STRUCT(FName, name);
//	Stack.StepCompiledIn<FStructProperty>(nullptr);
//
//	P_FINISH;
//	bool bSuccess = false;
//
//	if (Stack.MostRecentProperty)
//	{
//		UScriptStruct* BlueprintDataType = CastField<FStructProperty>(Stack.MostRecentProperty)->Struct;
//		bSuccess = BlueprintDataType->IsChildOf(FBaseRecord::StaticStruct());
//	}
//	else
//	{
//		const FBlueprintExceptionInfo ExceptionInfo(EBlueprintExceptionType::AccessViolation, NSLOCTEXT("SaveGame", "IncompatibleProperty", "Failed to resolve the records parameter for GetRecords."));
//		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
//	}
//	*(bool*)RESULT_PARAM = bSuccess;
//}

void UPersistentSaveGame::Serialize(FArchive& Ar)
{
	//if (Ar.IsSaving() && Ar.IsSaveGame()) HanldePreSave();

	Super::Serialize(Ar);

	//UE_LOG(LogTemp, Warning, TEXT("SavedDataVersion %d pre load, minigames count %d"), SavedDataVersion, minigameInfos.Num());
	//if (Ar.IsLoading() && Ar.IsSaveGame() && SavedDataVersion != FSaveGameObjectVersion::LatestVersion) {
	//	// in case our save game changes formats, use this place to get the data into the correct format
	//	// to get it working, add new version into the enum -> LatestVersion is increased by 1
	//		HandlePostLoad();
	//	}

	//	//SavedDataVersion = FSaveGameObjectVersion::LatestVersion;
	//}
}

void UPersistentSaveGame::HandlePreSave()
{
	SavedDataVersion = FSaveGameObjectVersion::LatestVersion;
}

void UPersistentSaveGame::HandlePostLoad()
{
	const UPersistentSaveGame* DefaultObj = GetDefault<UPersistentSaveGame>();
}
