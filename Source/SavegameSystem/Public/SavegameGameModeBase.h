#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SavegameGameModeBase.generated.h"

UCLASS()
class SAVEGAMESYSTEM_API ASavegameGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
};