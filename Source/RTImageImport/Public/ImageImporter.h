#pragma once

#include "CoreMinimal.h"


#include "ImageImporter.generated.h"


DECLARE_LOG_CATEGORY_EXTERN(ImageImporter, Log, All)

struct FImportedImageStruct
{
	TArray64<uint8> RawData;
	ETextureSourceFormat Format = TSF_Invalid;
	TextureCompressionSettings CompressionSettings = TC_Default;
	int32 NumMips;
	int32 SizeX = 0;
	int32 SizeY = 0;
	bool SRGB = true;
	/** Which compression format (if any) that is applied to RawData */
	ETextureSourceCompressionFormat RawDataCompressionFormat = TSCF_None;

	void Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool InSRGB);
	void Init2DWithOneMip(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData = nullptr);
	void Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData = nullptr);

	int64 GetMipSize(int32 InMipIndex) const;
	void* GetMipData(int32 InMipIndex);
};

UCLASS()
class UImageImporter : public UObject
{
public:
	GENERATED_BODY()

	void ImportFile(const FString Filename);
	UObject* CreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd);
	bool ImportImage(const uint8* Buffer, uint32 Length, FImportedImageStruct& OutImage);
	UTexture2D* CreateTexture2D(UObject* InParent, FName Name, EObjectFlags Flags);
	bool IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo);

protected:
	UPROPERTY()
	UTexture2D* Texture2D;
};