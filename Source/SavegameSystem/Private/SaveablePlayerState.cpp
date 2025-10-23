#include "SaveablePlayerState.h"
#include "CustomSaveGame.h"
#include "SaveGameSettings.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/DefaultPawn.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

ASaveablePlayerState::ASaveablePlayerState(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ResetPlayerState();
}

void ASaveablePlayerState::SavePlayerState_Implementation(UCustomSaveGame* SaveObject)
{
	if (SaveObject)
	{
		// Gather all relevant data for player
		FPlayerSaveData SaveData;

		// Stored as FString for simplicity (original Steam ID is uint64)
		SaveData.PlayerID = GetUniqueId().ToString();

		// May not be alive while we save
		if (APawn* MyPawn = GetPawn())
		{
			SaveData.Location = MyPawn->GetActorLocation();
			SaveData.Rotation = MyPawn->GetActorRotation();
			//SaveData.bResumeAtTransform = true;
			SaveData.PlayerClass = MyPawn->GetClass();

			// serialize save date of pawn
			FMemoryWriter PawnMemoryWriter(SaveData.PawnByteData);

			FObjectAndNameAsStringProxyArchive PawnAr(PawnMemoryWriter, true);
			PawnAr.ArIsSaveGame = true;
			MyPawn->Serialize(PawnAr);
		}

		// add further relevat data for player here

		// Pass the array to fill with data
		FMemoryWriter MemWriter(SaveData.ByteData);

		FObjectAndNameAsStringProxyArchive Ar(MemWriter, true);
		// Find only variables with UPROPERTY(SaveGame)
		Ar.ArIsSaveGame = true;
		// Converts SaveGame UPROPERTIES into binary array
		Serialize(Ar);

		for (UActorComponent* component : GetComponents()) {
			FComponentSaveData ComponentData;
			ComponentData.ComponentName = component->GetFName();

			if (USceneComponent* sC = Cast<USceneComponent>(component)) {
				ComponentData.Transform = sC->GetComponentTransform();
				//TODO add physics to save data
				//ComponentData.Velocity = sC->GetComponentVelocity();
			}

			FMemoryWriter CMemWriter(ComponentData.ByteData);

			FObjectAndNameAsStringProxyArchive CAr(CMemWriter, true);
			CAr.ArIsSaveGame = true;
			component->Serialize(CAr);

			SaveData.SavedComponents.Add(ComponentData);
		}

		SaveObject->SavedPlayers.Add(SaveData);
	}
}

void ASaveablePlayerState::LoadPlayerState_Implementation(UCustomSaveGame* SaveObject)
{
	if (SaveObject)
	{
		FPlayerSaveData* FoundData = SaveObject->GetPlayerData(this);
		if (FoundData)
		{
			StartTransform = FTransform(FoundData->Rotation, FoundData->Location, FVector(1.f));
			//bIsStartingAtTransform = FoundData->bResumeAtTransform;
			PlayerClass = FoundData->PlayerClass;
			// TODO (https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/Private/SGameModeBase.cpp#L95
			// https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/Private/SSaveGameSubsystem.cpp#L44)
			// dont need to apply PlayerID (maybe because its an automatic online id and is only used to find the correct player?)

			loadedSaveData = FoundData;

			// apply further relevant data here

			FMemoryReader MemReader(FoundData->ByteData);

			FObjectAndNameAsStringProxyArchive Ar(MemReader, true);
			Ar.ArIsSaveGame = true;
			// Convert binary array back into variables
			Serialize(Ar);

			for (UActorComponent* component : GetComponents()) {
				for (FComponentSaveData componentData : FoundData->SavedComponents) {
					if (componentData.ComponentName == component->GetFName()) {
						if (USceneComponent* sC = Cast<USceneComponent>(component)) {
							sC->SetWorldTransform(componentData.Transform, false, nullptr, ETeleportType::ResetPhysics);
							//TODO add physics to save data
							/*if(UPrimitiveComponent* pC = Cast< UPrimitiveComponent>(component))
								pC->SetAllPhysicsLinearVelocity()*/
						}

						FMemoryReader CMemReader(componentData.ByteData);

						FObjectAndNameAsStringProxyArchive CAr(CMemReader, true);
						CAr.ArIsSaveGame = true;
						component->Serialize(CAr);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not find SaveGame data for player id '%i'."), GetPlayerId());
		}
	}
}

void ASaveablePlayerState::ApplySaveGameData_Implementation(UCustomSaveGame* SaveObject)
{
	if (SaveObject)
	{
		const USaveGameSettings* SGSettings = GetDefault<USaveGameSettings>();
		if (!SGSettings->bManuallySpawnPlayer) {
			BP_ApplySaveGameDataToPawn();
		}
	}
}

void ASaveablePlayerState::BP_ApplySaveGameDataToPawn()
{
	if (loadedSaveData) {
		if (APawn* MyPawn = GetPawn())
		{
			if(MyPawn->GetClass() == PlayerClass)
			{
				// serialize save date of pawn
				FMemoryReader PawnMemoryReader(loadedSaveData->PawnByteData);

				FObjectAndNameAsStringProxyArchive PawnAr(PawnMemoryReader, true);
				PawnAr.ArIsSaveGame = true;
				MyPawn->Serialize(PawnAr);
			}
			else {
				UE_LOG(LogTemp, Warning, TEXT("Tried to apply savegame data to pawn of class \'%s\' but savedata is for class \'%s\'"), *MyPawn->GetClass()->GetName(), *PlayerClass->GetName());
			}
		}
		else {
			UE_LOG(LogTemp, Error, TEXT("Wanted to apply savegame data to pawn, but there was no pawn."));
		}
	}
}

FTransform ASaveablePlayerState::GetRestartTransform()
{
	return StartTransform;
}

void ASaveablePlayerState::ResetPlayerState()
{
	StartTransform = FTransform::Identity;
	PlayerClass = ADefaultPawn::StaticClass();
	loadedSaveData = nullptr;
}

TSubclassOf<APawn> ASaveablePlayerState::GetPlayerClass()
{
	return PlayerClass;
}
