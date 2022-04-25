#include "ImageImporter.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageSaver.h"

#include "TgaImageSupport.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/MessageDialog.h"


DEFINE_LOG_CATEGORY(ImageImporter)

template<typename PixelDataType, typename ColorDataType, int32 RIdx, int32 GIdx, int32 BIdx, int32 AIdx> class PNGDataFill
{
public:

	PNGDataFill(int32 SizeX, int32 SizeY, uint8* SourceTextureData)
		: SourceData(reinterpret_cast<PixelDataType*>(SourceTextureData))
		, TextureWidth(SizeX)
		, TextureHeight(SizeY)
	{
	}

	void ProcessData()
	{
		int32 NumZeroedTopRowsToProcess = 0;
		int32 FillColorRow = -1;
		for (int32 Y = 0; Y < TextureHeight; ++Y)
		{
			if (!ProcessHorizontalRow(Y))
			{
				if (FillColorRow != -1)
				{
					FillRowColorPixels(FillColorRow, Y);
				}
				else
				{
					NumZeroedTopRowsToProcess = Y;
				}
			}
			else
			{
				FillColorRow = Y;
			}
		}

		// Can only fill upwards if image not fully zeroed
		if (NumZeroedTopRowsToProcess > 0 && NumZeroedTopRowsToProcess + 1 < TextureHeight)
		{
			for (int32 Y = 0; Y <= NumZeroedTopRowsToProcess; ++Y)
			{
				FillRowColorPixels(NumZeroedTopRowsToProcess + 1, Y);
			}
		}
	}

	/* returns False if requires further processing because entire row is filled with zeroed alpha values */
	bool ProcessHorizontalRow(int32 Y)
	{
		// only wipe out colors that are affected by png turning valid colors white if alpha = 0
		const uint32 WhiteWithZeroAlpha = FColor(255, 255, 255, 0).DWColor();

		// Left -> Right
		int32 NumLeftmostZerosToProcess = 0;
		const PixelDataType* FillColor = nullptr;
		for (int32 X = 0; X < TextureWidth; ++X)
		{
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			ColorDataType* ColorData = reinterpret_cast<ColorDataType*>(PixelData);

			if (*ColorData == WhiteWithZeroAlpha)
			{
				if (FillColor)
				{
					PixelData[RIdx] = FillColor[RIdx];
					PixelData[GIdx] = FillColor[GIdx];
					PixelData[BIdx] = FillColor[BIdx];
				}
				else
				{
					// Mark pixel as needing fill
					*ColorData = 0;

					// Keep track of how many pixels to fill starting at beginning of row
					NumLeftmostZerosToProcess = X;
				}
			}
			else
			{
				FillColor = PixelData;
			}
		}

		if (NumLeftmostZerosToProcess == 0)
		{
			// No pixels left that are zero
			return true;
		}

		if (NumLeftmostZerosToProcess + 1 >= TextureWidth)
		{
			// All pixels in this row are zero and must be filled using rows above or below
			return false;
		}

		// Fill using non zero pixel immediately to the right of the beginning series of zeros
		FillColor = SourceData + (Y * TextureWidth + NumLeftmostZerosToProcess + 1) * 4;

		// Fill zero pixels found at beginning of row that could not be filled during the Left to Right pass
		for (int32 X = 0; X <= NumLeftmostZerosToProcess; ++X)
		{
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			PixelData[RIdx] = FillColor[RIdx];
			PixelData[GIdx] = FillColor[GIdx];
			PixelData[BIdx] = FillColor[BIdx];
		}

		return true;
	}

	void FillRowColorPixels(int32 FillColorRow, int32 Y)
	{
		for (int32 X = 0; X < TextureWidth; ++X)
		{
			const PixelDataType* FillColor = SourceData + (FillColorRow * TextureWidth + X) * 4;
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			PixelData[RIdx] = FillColor[RIdx];
			PixelData[GIdx] = FillColor[GIdx];
			PixelData[BIdx] = FillColor[BIdx];
		}
	}

	PixelDataType* SourceData;
	int32 TextureWidth;
	int32 TextureHeight;
};

#pragma pack(push,1)
class FPCXFileHeader
{
public:
	uint8	Manufacturer;		// Always 10.
	uint8	Version;			// PCX file version.
	uint8	Encoding;			// 1=run-length, 0=none.
	uint8	BitsPerPixel;		// 1,2,4, or 8.
	uint16	XMin;				// Dimensions of the image.
	uint16	YMin;				// Dimensions of the image.
	uint16	XMax;				// Dimensions of the image.
	uint16	YMax;				// Dimensions of the image.
	uint16	XDotsPerInch;		// Horizontal printer resolution.
	uint16	YDotsPerInch;		// Vertical printer resolution.
	uint8	OldColorMap[48];	// Old colormap info data.
	uint8	Reserved1;			// Must be 0.
	uint8	NumPlanes;			// Number of color planes (1, 3, 4, etc).
	uint16	BytesPerLine;		// Number of bytes per scanline.
	uint16	PaletteType;		// How to interpret palette: 1=color, 2=gray.
	uint16	HScreenSize;		// Horizontal monitor size.
	uint16	VScreenSize;		// Vertical monitor size.
	uint8	Reserved2[54];		// Must be 0.
	friend FArchive& operator<<(FArchive& Ar, FPCXFileHeader& H)
	{
		Ar << H.Manufacturer << H.Version << H.Encoding << H.BitsPerPixel;
		Ar << H.XMin << H.YMin << H.XMax << H.YMax << H.XDotsPerInch << H.YDotsPerInch;
		for (int32 i = 0; i < UE_ARRAY_COUNT(H.OldColorMap); i++)
			Ar << H.OldColorMap[i];
		Ar << H.Reserved1 << H.NumPlanes;
		Ar << H.BytesPerLine << H.PaletteType << H.HScreenSize << H.VScreenSize;
		for (int32 i = 0; i < UE_ARRAY_COUNT(H.Reserved2); i++)
			Ar << H.Reserved2[i];
		return Ar;
	}
};

struct FTGAFileFooter
{
	uint32 ExtensionAreaOffset;
	uint32 DeveloperDirectoryOffset;
	uint8 Signature[16];
	uint8 TrailingPeriod;
	uint8 NullTerminator;
};

struct FPSDFileHeader
{
	int32     Signature;      // 8BPS
	int16   Version;        // Version
	int16   nChannels;      // Number of Channels (3=RGB) (4=RGBA)
	int32     Height;         // Number of Image Rows
	int32     Width;          // Number of Image Columns
	int16   Depth;          // Number of Bits per Channel
	int16   Mode;           // Image Mode (0=Bitmap)(1=Grayscale)(2=Indexed)(3=RGB)(4=CYMK)(7=Multichannel)
	uint8    Pad[6];         // Padding

	/**
	 * @return Whether file has a valid signature
	 */
	bool IsValid(void)
	{
		// Fail on bad signature
		if (Signature != 0x38425053)
			return false;

		return true;
	}

	/**
	 * @return Whether file has a supported version
	 */
	bool IsSupported(void)
	{
		// Fail on bad version
		if (Version != 1)
			return false;
		// Fail on anything other than 1, 3 or 4 channels
		if ((nChannels != 1) && (nChannels != 3) && (nChannels != 4))
			return false;
		// Fail on anything other than 8 Bits/channel or 16 Bits/channel  
		if ((Depth != 8) && (Depth != 16))
			return false;
		// Fail on anything other than Grayscale and RGB
		// We can add support for indexed later if needed.
		if (Mode != 1 && Mode != 3)
			return false;

		return true;
	}
};

#pragma pack(pop)

void FillZeroAlphaPNGData(int32 SizeX, int32 SizeY, ETextureSourceFormat SourceFormat, uint8* SourceData)
{
	switch (SourceFormat)
	{
	case TSF_BGRA8:
	{
		PNGDataFill<uint8, uint32, 2, 1, 0, 3> PNGFill(SizeX, SizeY, SourceData);
		PNGFill.ProcessData();
		break;
	}

	case TSF_RGBA16:
	{
		PNGDataFill<uint16, uint64, 0, 1, 2, 3> PNGFill(SizeX, SizeY, SourceData);
		PNGFill.ProcessData();
		break;
	}
	}
}

void FImportedImageStruct::Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool InSRGB)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = 1;
	Format = InFormat;
	SRGB = InSRGB;
}

void FImportedImageStruct::Init2DWithOneMip(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = 1;
	Format = InFormat;
	RawData.AddUninitialized((int64)SizeX * SizeY * FTextureSource::GetBytesPerPixel(Format));
	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
}

void FImportedImageStruct::Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = InNumMips;
	Format = InFormat;

	int64 TotalSize = 0;
	for (int32 MipIndex = 0; MipIndex < InNumMips; ++MipIndex)
	{
		TotalSize += GetMipSize(MipIndex);
	}
	RawData.AddUninitialized(TotalSize);

	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
}

int64 FImportedImageStruct::GetMipSize(int32 InMipIndex) const
{
	check(InMipIndex >= 0);
	check(InMipIndex < NumMips);
	const int32 MipSizeX = FMath::Max(SizeX >> InMipIndex, 1);
	const int32 MipSizeY = FMath::Max(SizeY >> InMipIndex, 1);
	return (int64)MipSizeX * MipSizeY * FTextureSource::GetBytesPerPixel(Format);
}

void* FImportedImageStruct::GetMipData(int32 InMipIndex)
{
	int64 Offset = 0;
	for (int32 MipIndex = 0; MipIndex < InMipIndex; ++MipIndex)
	{
		Offset += GetMipSize(MipIndex);
	}
	return &RawData[Offset];
}

#pragma optimize("", off)
void UImageImporter::ImportFile(const FString Filename)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Filename))
	{
		UE_LOG(ImageImporter, Error, TEXT("Failed to load file '%s' to array"), *Filename);
	}
	UPackage* Pkg = CreatePackage(L"Game/ImagePkg");

	Data.Add(0);
	const uint8* Ptr = &Data[0];
	FString Name = FPaths::GetBaseFilename(Filename);
	FString FileExtension = FPaths::GetExtension(Filename);

	UObject* Ret = CreateBinary(UTexture::StaticClass(), Pkg, *Name, RF_Public | RF_Standalone, nullptr, *FileExtension, Ptr, Ptr + Data.Num() - 1);
	Texture2D = Cast<UTexture2D>(Ret);
	if(Texture2D)
	{
		USaveGame* GameSaver = UGameplayStatics::CreateSaveGameObject(UImageSaver::StaticClass());
		UImageSaver* ImageSaver = Cast<UImageSaver>(GameSaver);
		if(ImageSaver)
		{
			ImageSaver->SavedTexture2D = Texture2D;
			if (UGameplayStatics::SaveGameToSlot(ImageSaver, "TestSaveImage", 0))
				UE_LOG(ImageImporter, Verbose, TEXT("Image Save Successfully"))
			else
				UE_LOG(ImageImporter, Warning, TEXT("Image Save Failed"))
		}
	}
}

UObject* UImageImporter::CreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd)
{
	const int32 Length = BufferEnd - Buffer;
	FImportedImageStruct Image;
	if (ImportImage(Buffer, Length, Image))
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Image.SizeX, Image.SizeY, PF_B8G8R8A8);
		if (Texture)
		{
			// if (Image.RawDataCompressionFormat == ETextureSourceCompressionFormat::TSCF_None)
			// {
				// Texture->Source.Init(
				// 	Image.SizeX,
				// 	Image.SizeY,
				// 	/*NumSlices=*/ 1,
				// 	Image.NumMips,
				// 	Image.Format,
				// 	Image.RawData.GetData()
				// );
				void* DataPtr = Texture->GetPlatformData()->Mips[0].BulkData.Lock(EBulkDataLockFlags::LOCK_READ_WRITE);
				FMemory::Memcpy(DataPtr, Image.RawData.GetData(), Image.RawData.Num());
				Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
				Texture->UpdateResource();
			// }
			// else
			// {
			// 	Texture->Source.InitWithCompressedSourceData(
			// 		Image.SizeX,
			// 		Image.SizeY,
			// 		Image.NumMips,
			// 		Image.Format,
			// 		Image.RawData,
			// 		Image.RawDataCompressionFormat
			// 	);
			// }

			Texture->CompressionSettings = Image.CompressionSettings;

			
			Texture->SRGB = Image.SRGB;
			
			
		}
		return Texture;
	}

	return nullptr;
}

bool UImageImporter::ImportImage(const uint8* Buffer, uint32 Length, FImportedImageStruct& OutImage)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	// Use the magic bytes when possible to avoid calling inefficient code to check if the image is of the right format
	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Buffer, int64(Length));

	const bool bAllowNonPowerOfTwo = true;

	//
	// PNG
	//
	if (ImageFormat == EImageFormat::PNG)
	{
		TSharedPtr<IImageWrapper> PngImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (PngImageWrapper.IsValid() && PngImageWrapper->SetCompressed(Buffer, Length))
		{
			if (!IsImportResolutionValid(PngImageWrapper->GetWidth(), PngImageWrapper->GetHeight(), bAllowNonPowerOfTwo))
			{
				return false;
			}

			// Select the texture's source format
			ETextureSourceFormat TextureFormat = TSF_Invalid;
			int32 BitDepth = PngImageWrapper->GetBitDepth();
			ERGBFormat Format = PngImageWrapper->GetFormat();

			if (Format == ERGBFormat::Gray)
			{
				if (BitDepth <= 8)
				{
					TextureFormat = TSF_G8;
					Format = ERGBFormat::Gray;
					BitDepth = 8;
				}
				else if (BitDepth == 16)
				{
					TextureFormat = TSF_G16;
					Format = ERGBFormat::Gray;
					BitDepth = 16;
				}
			}
			else if (Format == ERGBFormat::RGBA || Format == ERGBFormat::BGRA)
			{
				if (BitDepth <= 8)
				{
					TextureFormat = TSF_BGRA8;
					Format = ERGBFormat::BGRA;
					BitDepth = 8;
				}
				else if (BitDepth == 16)
				{
					TextureFormat = TSF_RGBA16;
					Format = ERGBFormat::RGBA;
					BitDepth = 16;
				}
			}

			if (TextureFormat == TSF_Invalid)
			{
				// Warn->Logf(ELogVerbosity::Error, TEXT("PNG file contains data in an unsupported format."));
				return false;
			}

			OutImage.Init2DWithParams(
				PngImageWrapper->GetWidth(),
				PngImageWrapper->GetHeight(),
				TextureFormat,
				BitDepth < 16
			);

			if (PngImageWrapper->GetRaw(Format, BitDepth, OutImage.RawData))
			{
				bool bFillPNGZeroAlpha = true;
				GConfig->GetBool(TEXT("TextureImporter"), TEXT("FillPNGZeroAlpha"), bFillPNGZeroAlpha, GEditorIni);

				if (bFillPNGZeroAlpha)
				{
					// Replace the pixels with 0.0 alpha with a color value from the nearest neighboring color which has a non-zero alpha
					FillZeroAlphaPNGData(OutImage.SizeX, OutImage.SizeY, OutImage.Format, OutImage.RawData.GetData());
				}
			}
			else
			{
				// Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode PNG."));
				return false;
			}

			return true;
		}
	}

	//
	// JPEG
	//
	if (ImageFormat == EImageFormat::JPEG)
	{
		TSharedPtr<IImageWrapper> JpegImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		if (JpegImageWrapper.IsValid() && JpegImageWrapper->SetCompressed(Buffer, Length))
		{
			if (!IsImportResolutionValid(JpegImageWrapper->GetWidth(), JpegImageWrapper->GetHeight(), bAllowNonPowerOfTwo))
			{
				return false;
			}

			// Select the texture's source format
			ETextureSourceFormat TextureFormat = TSF_Invalid;
			int32 BitDepth = JpegImageWrapper->GetBitDepth();
			ERGBFormat Format = JpegImageWrapper->GetFormat();

			if (Format == ERGBFormat::Gray)
			{
				if (BitDepth <= 8)
				{
					TextureFormat = TSF_G8;
					Format = ERGBFormat::Gray;
					BitDepth = 8;
				}
			}
			else if (Format == ERGBFormat::RGBA)
			{
				if (BitDepth <= 8)
				{
					TextureFormat = TSF_BGRA8;
					Format = ERGBFormat::BGRA;
					BitDepth = 8;
				}
			}

			if (TextureFormat == TSF_Invalid)
			{
				// Warn->Logf(ELogVerbosity::Error, TEXT("JPEG file contains data in an unsupported format."));
				return false;
			}

			OutImage.Init2DWithParams(
				JpegImageWrapper->GetWidth(),
				JpegImageWrapper->GetHeight(),
				TextureFormat,
				BitDepth < 16
			);

			// bool bRetainJpegFormat = false;
			// // if (EnumHasAllFlags(Flags, EImageImportFlags::AllowReturnOfCompressedData))
			// // {
			// // 	// For now this option is opt in via the config files once there is no technical risk this will become the default path.
			// // 	GConfig->GetBool(TEXT("TextureImporter"), TEXT("RetainJpegFormat"), bRetainJpegFormat, GEditorIni);
			// // }
			//
			// if (bRetainJpegFormat)
			// {
			// 	OutImage.RawData.Append(Buffer, Length);
			// 	OutImage.RawDataCompressionFormat = ETextureSourceCompressionFormat::TSCF_JPEG;
			// }
			// else if (!JpegImageWrapper->GetRaw(Format, BitDepth, OutImage.RawData))
			// {
			// 	Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode JPEG."));
			// 	return false;
			// }

			return true;
		}
	}

	// //
	// // EXR
	// //
	// if (ImageFormat == EImageFormat::EXR)
	// {
	// 	TSharedPtr<IImageWrapper> ExrImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
	// 	if (ExrImageWrapper.IsValid() && ExrImageWrapper->SetCompressed(Buffer, Length))
	// 	{
	// 		int32 Width = ExrImageWrapper->GetWidth();
	// 		int32 Height = ExrImageWrapper->GetHeight();
	//
	// 		if (!IsImportResolutionValid(Width, Height, bAllowNonPowerOfTwo, Warn))
	// 		{
	// 			return false;
	// 		}
	//
	// 		// Currently the only texture source format compatible with EXR image formats is TSF_RGBA16F.
	// 		// EXR decoder automatically converts imported image channels into the requested float format.
	// 		// In case if the imported image is a gray scale image, its content will be stored in the green channel of the created texture.
	// 		ETextureSourceFormat TextureFormat = TSF_RGBA16F;
	// 		ERGBFormat Format = ERGBFormat::RGBAF;
	// 		int32 BitDepth = 16;
	//
	// 		OutImage.Init2DWithParams(
	// 			Width,
	// 			Height,
	// 			TextureFormat,
	// 			false
	// 		);
	// 		OutImage.CompressionSettings = TC_HDR;
	//
	// 		if (!ExrImageWrapper->GetRaw(Format, BitDepth, OutImage.RawData))
	// 		{
	// 			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode EXR."));
	// 			return false;
	// 		}
	//
	// 		return true;
	// 	}
	// }
	//
	// //
	// // BMP
	// //
	// if (ImageFormat == EImageFormat::BMP)
	// {
	// 	TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
	// 	if (BmpImageWrapper.IsValid() && BmpImageWrapper->SetCompressed(Buffer, Length))
	// 	{
	// 		// Check the resolution of the imported texture to ensure validity
	// 		if (!IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
	// 		{
	// 			return false;
	// 		}
	//
	// 		OutImage.Init2DWithParams(
	// 			BmpImageWrapper->GetWidth(),
	// 			BmpImageWrapper->GetHeight(),
	// 			TSF_BGRA8,
	// 			false
	// 		);
	//
	// 		return BmpImageWrapper->GetRaw(BmpImageWrapper->GetFormat(), BmpImageWrapper->GetBitDepth(), OutImage.RawData);
	// 	}
	// }
	//
	// //
	// // TIFF
	// //
	// if (ImageFormat == EImageFormat::TIFF)
	// {
	// 	TSharedPtr<IImageWrapper> TiffImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::TIFF);
	// 	if (TiffImageWrapper.IsValid())
	// 	{
	// 		if (TiffImageWrapper->SetCompressed(Buffer, Length))
	// 		{
	// 			// Check the resolution of the imported texture to ensure validity
	// 			if (!IsImportResolutionValid(TiffImageWrapper->GetWidth(), TiffImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
	// 			{
	// 				return false;
	// 			}
	//
	// 			ETextureSourceFormat SourceFormat = TSF_Invalid;
	// 			ERGBFormat TiffFormat = TiffImageWrapper->GetFormat();
	// 			const int32 BitDepth = TiffImageWrapper->GetBitDepth();
	// 			bool bIsSRGB = false;
	//
	// 			if (TiffFormat == ERGBFormat::BGRA)
	// 			{
	// 				SourceFormat = TSF_BGRA8;
	// 				bIsSRGB = true;
	// 			}
	// 			else if (TiffFormat == ERGBFormat::RGBA)
	// 			{
	// 				SourceFormat = TSF_RGBA16;
	// 			}
	// 			else if (TiffFormat == ERGBFormat::RGBAF)
	// 			{
	// 				SourceFormat = TSF_RGBA16F;
	// 			}
	// 			else if (TiffFormat == ERGBFormat::Gray)
	// 			{
	// 				SourceFormat = TSF_G8;
	// 				if (BitDepth == 16)
	// 				{
	// 					SourceFormat = TSF_G16;
	// 				}
	// 			}
	//
	// 			OutImage.Init2DWithParams(
	// 				TiffImageWrapper->GetWidth(),
	// 				TiffImageWrapper->GetHeight(),
	// 				SourceFormat,
	// 				bIsSRGB
	// 			);
	//
	// 			return TiffImageWrapper->GetRaw(TiffFormat, BitDepth, OutImage.RawData);
	// 		}
	//
	// 		return false;
	// 	}
	// }
	//
	// //
	// // PCX
	// //
	// const FPCXFileHeader* PCX = (FPCXFileHeader*)Buffer;
	// if (Length >= sizeof(FPCXFileHeader) && PCX->Manufacturer == 10)
	// {
	// 	int32 NewU = PCX->XMax + 1 - PCX->XMin;
	// 	int32 NewV = PCX->YMax + 1 - PCX->YMin;
	//
	// 	// Check the resolution of the imported texture to ensure validity
	// 	if (!IsImportResolutionValid(NewU, NewV, bAllowNonPowerOfTwo, Warn))
	// 	{
	// 		return false;
	// 	}
	// 	else if (PCX->NumPlanes == 1 && PCX->BitsPerPixel == 8)
	// 	{
	//
	// 		// Set texture properties.
	// 		OutImage.Init2DWithOneMip(
	// 			NewU,
	// 			NewV,
	// 			TSF_BGRA8
	// 		);
	// 		FColor* DestPtr = (FColor*)OutImage.RawData.GetData();
	//
	// 		// Import the palette.
	// 		uint8* PCXPalette = (uint8*)(Buffer + Length - 256 * 3);
	// 		TArray<FColor>	Palette;
	// 		for (uint32 i = 0; i < 256; i++)
	// 		{
	// 			Palette.Add(FColor(PCXPalette[i * 3 + 0], PCXPalette[i * 3 + 1], PCXPalette[i * 3 + 2], i == 0 ? 0 : 255));
	// 		}
	//
	// 		// Import it.
	// 		FColor* DestEnd = DestPtr + NewU * NewV;
	// 		Buffer += 128;
	// 		while (DestPtr < DestEnd)
	// 		{
	// 			uint8 Color = *Buffer++;
	// 			if ((Color & 0xc0) == 0xc0)
	// 			{
	// 				uint32 RunLength = Color & 0x3f;
	// 				Color = *Buffer++;
	//
	// 				for (uint32 Index = 0; Index < RunLength; Index++)
	// 				{
	// 					*DestPtr++ = Palette[Color];
	// 				}
	// 			}
	// 			else *DestPtr++ = Palette[Color];
	// 		}
	// 	}
	// 	else if (PCX->NumPlanes == 3 && PCX->BitsPerPixel == 8)
	// 	{
	// 		// Set texture properties.
	// 		OutImage.Init2DWithOneMip(
	// 			NewU,
	// 			NewV,
	// 			TSF_BGRA8
	// 		);
	//
	// 		uint8* Dest = OutImage.RawData.GetData();
	//
	// 		// Doing a memset to make sure the alpha channel is set to 0xff since we only have 3 color planes.
	// 		FMemory::Memset(Dest, 0xff, NewU * NewV * FTextureSource::GetBytesPerPixel(OutImage.Format));
	//
	// 		// Copy upside-down scanlines.
	// 		Buffer += 128;
	// 		int32 CountU = FMath::Min<int32>(PCX->BytesPerLine, NewU);
	// 		for (int32 i = 0; i < NewV; i++)
	// 		{
	// 			// We need to decode image one line per time building RGB image color plane by color plane.
	// 			int32 RunLength, Overflow = 0;
	// 			uint8 Color = 0;
	// 			for (int32 ColorPlane = 2; ColorPlane >= 0; ColorPlane--)
	// 			{
	// 				for (int32 j = 0; j < CountU; j++)
	// 				{
	// 					if (!Overflow)
	// 					{
	// 						Color = *Buffer++;
	// 						if ((Color & 0xc0) == 0xc0)
	// 						{
	// 							RunLength = FMath::Min((Color & 0x3f), CountU - j);
	// 							Overflow = (Color & 0x3f) - RunLength;
	// 							Color = *Buffer++;
	// 						}
	// 						else
	// 							RunLength = 1;
	// 					}
	// 					else
	// 					{
	// 						RunLength = FMath::Min(Overflow, CountU - j);
	// 						Overflow = Overflow - RunLength;
	// 					}
	//
	// 					//checkf(((i*NewU + RunLength) * 4 + ColorPlane) < (Texture->Source.CalcMipSize(0)),
	// 					//	TEXT("RLE going off the end of buffer"));
	// 					for (int32 k = j; k < j + RunLength; k++)
	// 					{
	// 						Dest[(i * NewU + k) * 4 + ColorPlane] = Color;
	// 					}
	// 					j += RunLength - 1;
	// 				}
	// 			}
	// 		}
	// 	}
	// 	else
	// 	{
	// 		Warn->Logf(ELogVerbosity::Error, TEXT("PCX uses an unsupported format (%i/%i)"), PCX->NumPlanes, PCX->BitsPerPixel);
	// 		return false;
	// 	}
	//
	// 	return true;
	// }
	//
	// //
	// // TGA
	// //
	// TSharedPtr<IImageWrapper> TgaImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::TGA);
	// if (TgaImageWrapper.IsValid() && TgaImageWrapper->SetCompressed(Buffer, Length))
	// {
	// 	// Check the resolution of the imported texture to ensure validity
	// 	if (!IsImportResolutionValid(TgaImageWrapper->GetWidth(), TgaImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
	// 	{
	// 		return false;
	// 	}
	//
	// 	const FTGAFileHeader* TGA = (FTGAFileHeader*)Buffer;
	//
	// 	if (TGA->ColorMapType == 1 && TGA->ImageTypeCode == 1 && TGA->BitsPerPixel == 8)
	// 	{
	// 		// Notes: The Scaleform GFx exporter (dll) strips all font glyphs into a single 8-bit texture.
	// 		// The targa format uses this for a palette index; GFx uses a palette of (i,i,i,i) so the index
	// 		// is also the alpha value.
	// 		//
	// 		// We store the image as PF_G8, where it will be used as alpha in the Glyph shader.
	// 		OutImage.Init2DWithOneMip(
	// 			TGA->Width,
	// 			TGA->Height,
	// 			TSF_G8);
	// 		OutImage.CompressionSettings = TC_Grayscale;
	// 	}
	// 	else if (TGA->ColorMapType == 0 && TGA->ImageTypeCode == 3 && TGA->BitsPerPixel == 8)
	// 	{
	// 		// standard grayscale images
	// 		OutImage.Init2DWithOneMip(
	// 			TGA->Width,
	// 			TGA->Height,
	// 			TSF_G8);
	// 		OutImage.CompressionSettings = TC_Grayscale;
	// 	}
	// 	else
	// 	{
	// 		if (TGA->ImageTypeCode == 10) // 10 = RLE compressed 
	// 		{
	// 			if (TGA->BitsPerPixel != 32 &&
	// 				TGA->BitsPerPixel != 24 &&
	// 				TGA->BitsPerPixel != 16)
	// 			{
	// 				Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported rle-compressed bit-depth: %u"), TGA->BitsPerPixel);
	// 				return false;
	// 			}
	// 		}
	// 		else
	// 		{
	// 			if (TGA->BitsPerPixel != 32 &&
	// 				TGA->BitsPerPixel != 16 &&
	// 				TGA->BitsPerPixel != 24)
	// 			{
	// 				Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported bit-depth: %u"), TGA->BitsPerPixel);
	// 				return false;
	// 			}
	// 		}
	//
	// 		OutImage.Init2DWithOneMip(
	// 			TGA->Width,
	// 			TGA->Height,
	// 			TSF_BGRA8);
	// 	}
	//
	// 	const bool bResult = TgaImageWrapper->GetRaw(TgaImageWrapper->GetFormat(), TgaImageWrapper->GetBitDepth(), OutImage.RawData);
	//
	// 	if (bResult && OutImage.CompressionSettings == TC_Grayscale && TGA->ImageTypeCode == 3)
	// 	{
	// 		// default grayscales to linear as they wont get compression otherwise and are commonly used as masks
	// 		OutImage.SRGB = false;
	// 	}
	//
	// 	return bResult;
	// }
	//
	// //
	// // PSD File
	// //
	// FPSDFileHeader			 psdhdr;
	// if (Length > sizeof(FPSDFileHeader))
	// {
	// 	psd_GetPSDHeader(Buffer, psdhdr);
	// }
	// if (psdhdr.IsValid())
	// {
	// 	// Check the resolution of the imported texture to ensure validity
	// 	if (!IsImportResolutionValid(psdhdr.Width, psdhdr.Height, bAllowNonPowerOfTwo, Warn))
	// 	{
	// 		return false;
	// 	}
	// 	if (!psdhdr.IsSupported())
	// 	{
	// 		Warn->Logf(TEXT("Format of this PSD is not supported. Only Grayscale and RGBColor PSD images are currently supported, in 8-bit or 16-bit."));
	// 		return false;
	// 	}
	//
	// 	// Select the texture's source format
	// 	ETextureSourceFormat TextureFormat = TSF_Invalid;
	// 	if (psdhdr.Depth == 8)
	// 	{
	// 		TextureFormat = TSF_BGRA8;
	// 	}
	// 	else if (psdhdr.Depth == 16)
	// 	{
	// 		TextureFormat = TSF_RGBA16;
	// 	}
	//
	// 	if (TextureFormat == TSF_Invalid)
	// 	{
	// 		Warn->Logf(ELogVerbosity::Error, TEXT("PSD file contains data in an unsupported format."));
	// 		return false;
	// 	}
	//
	// 	// The psd is supported. Load it up.        
	// 	OutImage.Init2DWithOneMip(
	// 		psdhdr.Width,
	// 		psdhdr.Height,
	// 		TextureFormat
	// 	);
	// 	uint8* Dst = (uint8*)OutImage.RawData.GetData();
	//
	// 	if (!psd_ReadData(Dst, Buffer, psdhdr))
	// 	{
	// 		Warn->Logf(TEXT("Failed to read this PSD"));
	// 		return false;
	// 	}
	//
	// 	return true;
	// }
	//
	// //
	// // DDS Texture
	// //
	// FDDSLoadHelper  DDSLoadHelper(Buffer, Length);
	// if (DDSLoadHelper.IsValid2DTexture())
	// {
	// 	// DDS 2d texture
	// 	if (!IsImportResolutionValid(DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo, Warn))
	// 	{
	// 		Warn->Logf(ELogVerbosity::Error, TEXT("DDS has invalid dimensions."));
	// 		return false;
	// 	}
	//
	// 	ETextureSourceFormat SourceFormat = DDSLoadHelper.ComputeSourceFormat();
	//
	// 	// Invalid DDS format
	// 	if (SourceFormat == TSF_Invalid)
	// 	{
	// 		Warn->Logf(ELogVerbosity::Error, TEXT("DDS uses an unsupported format."));
	// 		return false;
	// 	}
	//
	// 	uint32 MipMapCount = DDSLoadHelper.ComputeMipMapCount();
	// 	if (SourceFormat != TSF_Invalid && MipMapCount > 0)
	// 	{
	// 		OutImage.Init2DWithMips(
	// 			DDSLoadHelper.DDSHeader->dwWidth,
	// 			DDSLoadHelper.DDSHeader->dwHeight,
	// 			MipMapCount,
	// 			SourceFormat,
	// 			DDSLoadHelper.GetDDSDataPointer()
	// 		);
	//
	// 		if (MipMapCount > 1)
	// 		{
	// 			// if the source has mips we keep the mips by default, unless the user changes that
	// 			MipGenSettings = TMGS_LeaveExistingMips;
	// 		}
	//
	// 		if (FTextureSource::IsHDR(SourceFormat))
	// 		{
	// 			// the loader can suggest a compression setting
	// 			OutImage.CompressionSettings = TC_HDR;
	// 			OutImage.SRGB = false;
	// 		}
	//
	// 		return true;
	// 	}
	// }
	//
	//
	// //
	// // Legacy TIFF import (for the platforms that doesn't have libtiff)
	// //
	// FTiffLoadHelper TiffLoaderHelper;
	// if (TiffLoaderHelper.IsValid())
	// {
	// 	if (TiffLoaderHelper.Load(Buffer, Length))
	// 	{
	// 		OutImage.Init2DWithOneMip(
	// 			TiffLoaderHelper.Width,
	// 			TiffLoaderHelper.Height,
	// 			TiffLoaderHelper.TextureSourceFormat,
	// 			TiffLoaderHelper.RawData.GetData()
	// 		);
	//
	// 		OutImage.SRGB = TiffLoaderHelper.bSRGB;
	// 		OutImage.CompressionSettings = TiffLoaderHelper.CompressionSettings;
	// 		return true;
	// 	}
	// }

	return false;
}

#pragma optimize("", on)

UTexture2D* UImageImporter::CreateTexture2D(UObject* InParent, FName Name, EObjectFlags Flags)
{
	UTexture2D* NewTextureObject = NewObject<UTexture2D>(InParent, UTexture2D::StaticClass(), Name, Flags);
	UTexture2D* NewTexture = nullptr;
	if (NewTextureObject)
	{
		NewTexture = CastChecked<UTexture2D>(NewTextureObject);
	}

	return NewTexture;
}

bool UImageImporter::IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo)
{
	static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);

	// In theory this value could be much higher, but various UE image code currently uses 32bit size/offset values
	const int32 MaximumSupportedVirtualTextureResolution = 16 * 1024;

	// Calculate the maximum supported resolution utilizing the global max texture mip count
	// (Note, have to subtract 1 because 1x1 is a valid mip-size; this means a GMaxTextureMipCount of 4 means a max resolution of 8x8, not 2^4 = 16x16)
	const int32 MaximumSupportedResolution = CVarVirtualTexturesEnabled->GetValueOnAnyThread() ? MaximumSupportedVirtualTextureResolution : (1 << (15 - 1));

	bool bValid = true;

	// Check if the texture is above the supported resolution and prompt the user if they wish to continue if it is
	if (Width > MaximumSupportedResolution || Height > MaximumSupportedResolution)
	{
		if (Width * Height < 0)
		{
			// Warn->Log(ELogVerbosity::Error, *FText::Format(
				// NSLOCTEXT("UnrealEd", "Warning_TextureSizeTooLargeOrInvalid", "Texture is too large to import or it has an invalid resolution. The current maximun is {0} pixels"),
				// FText::AsNumber(FMath::Square(MaximumSupportedVirtualTextureResolution))
			// ).ToString());

			bValid = false;
		}

		if (bValid && (Width * Height) > FMath::Square(MaximumSupportedVirtualTextureResolution))
		{
			// Warn->Log(ELogVerbosity::Error, *FText::Format(
				// NSLOCTEXT("UnrealEd", "Warning_TextureSizeTooLarge", "Texture is too large to import. The current maximun is {0} pixels"),
				// FText::AsNumber(FMath::Square(MaximumSupportedVirtualTextureResolution))
			// ).ToString());

			bValid = false;
		}

		if (bValid && EAppReturnType::Yes != FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("UnrealEd", "Warning_LargeTextureImport", "Attempting to import {0} x {1} texture, proceed?\nLargest supported texture size: {2} x {3}"),
			FText::AsNumber(Width), FText::AsNumber(Height), FText::AsNumber(MaximumSupportedResolution), FText::AsNumber(MaximumSupportedResolution))))
		{
			bValid = false;
		}
	}

	const bool bIsPowerOfTwo = FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height);
	// Check if the texture dimensions are powers of two
	if (!bAllowNonPowerOfTwo && !bIsPowerOfTwo)
	{
		// Warn->Log(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions").ToString());
		bValid = false;
	}

	return bValid;
}