#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"


#include "ImageSaver.generated.h"



UCLASS()
class UImageSaver : public USaveGame
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TObjectPtr<UTexture2D> SavedTexture2D;
};