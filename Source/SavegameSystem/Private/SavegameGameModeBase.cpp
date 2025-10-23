#include "SavegameGameModeBase.h"
#include "SavegameSubsystem.h"
#include "GameFramework/DefaultPawn.h"

void ASavegameGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Calling Before Super:: so we set variables before 'beginplayingstate' is called in PlayerController (which is where we instantiate UI)
	USavegameSubsystem* SG = GetGameInstance()->GetSubsystem<USavegameSubsystem>();
	SG->HandleStartingNewPlayer(NewPlayer);

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ASavegameGameModeBase::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	// spawn at saved transform if possible, else use default spawn behavior with playerstarts
	USavegameSubsystem* SG = GetGameInstance()->GetSubsystem<USavegameSubsystem>();
	FTransform SpawnTransform;
	TSubclassOf<APawn> SpawnClass;
	if (SG->RestartPlayerAtTransform(NewPlayer, SpawnTransform, SpawnClass)) {
		if (SpawnClass != ADefaultPawn::StaticClass())
			DefaultPawnClass = SpawnClass;

		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
		return;
	}

	Super::RestartPlayer(NewPlayer);
}