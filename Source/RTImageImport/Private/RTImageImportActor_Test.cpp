// Fill out your copyright notice in the Description page of Project Settings.


#include "RTImageImportActor_Test.h"

#include "AssetToolsModule.h"
#include "ImageImporter.h"
#include "ImageSaver.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "DesktopPlatformModule.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMSPACE "Property Actor"

// Sets default values
ARTImageImportActor_Test::ARTImageImportActor_Test()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

void ARTImageImportActor_Test::ImportTest(const FString Path)
{
	FDesktopPlatformModule& DesktopModule = FModuleManager::LoadModuleChecked<FDesktopPlatformModule>("DesktopPlatform");
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	FString FileTypes, AllExtensions;
	AllExtensions = "*.png";
	FileTypes = FString::Printf(TEXT("All Files (%s)|%s|%s"), *AllExtensions, *AllExtensions, *FileTypes);

	TArray<FString> SelectedFiles;
	int32 FilterIndex = -1;
	DesktopPlatform->OpenFileDialog(
		ParentWindowWindowHandle,
		"Import",
		FPaths::ProjectDir(),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::Multiple,
		SelectedFiles,
		FilterIndex
	);

	if(SelectedFiles.Num() > 0)
	{
		const FString FilePath = SelectedFiles[0];
		UImageImporter* Importer = NewObject<UImageImporter>(this, UImageImporter::StaticClass(), NAME_None, RF_Transient);
		if (Importer)
			Importer->ImportFile(FilePath);
	}
}

UTexture2D* ARTImageImportActor_Test::TestLoadImage()
{
	USaveGame* GameSaver = UGameplayStatics::LoadGameFromSlot("TestSaveImage", 0);
	UImageSaver* ImageSaver = Cast<UImageSaver>(GameSaver);
	if(ImageSaver)
	{
		UTexture2D* Image = ImageSaver->SavedTexture2D;
		if(Image)
		{
			const FName Name = Image->GetFName();
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Name.ToString());
			
			return Image;
		}
	}

	return nullptr;
}

void ARTImageImportActor_Test::CreateSaveGameObject()
{
	USaveGame* GameSaver = UGameplayStatics::CreateSaveGameObject(UImageSaver::StaticClass());
	UImageSaver* ImageSaver = Cast<UImageSaver>(GameSaver);
}

// Called when the game starts or when spawned
void ARTImageImportActor_Test::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ARTImageImportActor_Test::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

#undef LOCTEXT_NAMSPACE