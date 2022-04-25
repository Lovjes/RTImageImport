// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTImageImportActor_Test.generated.h"

UCLASS()
class RTIMAGEIMPORT_API ARTImageImportActor_Test : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARTImageImportActor_Test();

	UFUNCTION(BlueprintCallable)
	void ImportTest(const FString Path);

	UFUNCTION(BlueprintCallable)
	UTexture2D* TestLoadImage();

	UFUNCTION(BlueprintCallable)
	void CreateSaveGameObject();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UUserWidget> WidgetClass;

};
