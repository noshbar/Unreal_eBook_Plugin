// Copyright 2023 Dirk de la Hunt

/*
 NOTE!
 1. Your build .CS file should have the last two appended to it:
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "RHI", "RenderCore" });
 2. You should create a texture the size of what you're going to use, 1024x1024 by default
 3. Import it
 4. Change
       Mip Gen Settings -> NoMipmaps
       sRGB -> false
       Compression Settings -> TC Vector Displacementmap (aka B8G8R8A8)
 5. Create a material, open it
 6. Create a TextureSampleParameter2D node, name it "DynamicTextureParam"
 7. Change the texture to be the one you imported earlier
 8. Change "Sampler Type" to "Linear Color"
 9. Add 2 scalar parameters named "ScaleX" and "ScaleY"
 10. Add component to an actor with a StaticMeshComponent on it, assign this material to it
*/


#include "EbookToTextureComponent.h"
#include "Kismet/GameplayStatics.h"

#define RED 2
#define GREEN 1
#define BLUE 0
#define ALPHA 3

void UpdateTextureRegions(UTexture2D* Texture, int32 MipIndex, uint32 NumRegions, FUpdateTextureRegion2D* Regions, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData, bool bFreeData)
{
    if (Texture && Texture->GetResource())
    {
        typedef struct FUpdateTextureRegionsData
        {
            FTexture2DResource* Texture2DResource;
            int32 MipIndex;
            uint32 NumRegions;
            FUpdateTextureRegion2D* Regions;
            uint32 SrcPitch;
            uint32 SrcBpp;
            uint8* SrcData;
        }FUpdateTextureRegionsData;

        FUpdateTextureRegionsData* RegionData = new FUpdateTextureRegionsData;

        RegionData->Texture2DResource = (FTexture2DResource*)Texture->GetResource();
        RegionData->MipIndex = MipIndex;
        RegionData->NumRegions = NumRegions;
        RegionData->Regions = Regions;
        RegionData->SrcPitch = SrcPitch;
        RegionData->SrcBpp = SrcBpp;
        RegionData->SrcData = SrcData;

        ENQUEUE_RENDER_COMMAND(UpdateTextureRegionsData)(
            [RegionData, bFreeData](FRHICommandListImmediate& RHICmdList)
            {
                for (uint32 RegionIndex = 0; RegionIndex < RegionData->NumRegions; ++RegionIndex)
                {
                    int32 CurrentFirstMip = RegionData->Texture2DResource->GetCurrentFirstMip();
                    if (RegionData->MipIndex >= CurrentFirstMip)
                    {
                        RHIUpdateTexture2D(
                            RegionData->Texture2DResource->GetTexture2DRHI(),
                            RegionData->MipIndex - CurrentFirstMip,
                            RegionData->Regions[RegionIndex],
                            RegionData->SrcPitch,
                            RegionData->SrcData
                            + RegionData->Regions[RegionIndex].SrcY * RegionData->SrcPitch
                            + RegionData->Regions[RegionIndex].SrcX * RegionData->SrcBpp
                        );
                    }
                }
                if (bFreeData)
                {
                    FMemory::Free(RegionData->Regions);
                    FMemory::Free(RegionData->SrcData);
                }
                delete RegionData;
            });
    }
}


// Sets default values for this component's properties
UEbookToTextureComponent::UEbookToTextureComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

    mDynamicColors = nullptr;
    mUpdateTextureRegion = nullptr;
}


// Called when the game starts
void UEbookToTextureComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
    // !!! don't forget to put this DLL (and libmudpdf.dll) into one of the folders that Unreal looks in, if in doubt, try run it as is
    // and check the output log, it'll have listed all the locations it tried to find it in
    dllHandle = FPlatformProcess::GetDllHandle(_T("H:\\programming\\mupdf2rgb\\x64\\Release\\mupdf2rgb.dll"));
    if (dllHandle == nullptr)
    {
        GEngine->AddOnScreenDebugMessage(0, 10, FColor::Red, "Could not load ebook DLL");
        return;
    }
    else
    {
        pdfCreate = (Pdf_create)FPlatformProcess::GetDllExport(dllHandle, TEXT("Pdf_create"));
        pdfDestroy = (Pdf_destroy)FPlatformProcess::GetDllExport(dllHandle, TEXT("Pdf_destroy"));
        pdfGetPageFittedBGRA = (Pdf_getPageFittedBGRA)FPlatformProcess::GetDllExport(dllHandle, TEXT("Pdf_getPageFittedBGRA"));
        pdfGet2PagesFittedBGRA = (Pdf_get2PagesFittedBGRA)FPlatformProcess::GetDllExport(dllHandle, TEXT("Pdf_get2PagesFittedBGRA"));
    }

    mStaticMeshComponent = Cast<UStaticMeshComponent>(GetOwner()->GetComponentByClass(UStaticMeshComponent::StaticClass()));

    SetupTexture();
    UpdateTexture();
}

void UEbookToTextureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (dllHandle)
    {
        if (pdfDestroy)
            pdfDestroy(currentBook);
        FPlatformProcess::FreeDllHandle(dllHandle);
        currentBook = nullptr;
        dllHandle = nullptr;
    }

    delete[] mDynamicColors; mDynamicColors = nullptr;
    delete mUpdateTextureRegion; mUpdateTextureRegion = nullptr;

    Super::EndPlay(EndPlayReason);
}

// Called every frame
void UEbookToTextureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}


void UEbookToTextureComponent::SetupTexture()
{
    if (mDynamicColors) delete[] mDynamicColors;
    if (mUpdateTextureRegion) delete mUpdateTextureRegion;

    if (!mStaticMeshComponent)
    {
        GEngine->AddOnScreenDebugMessage(1, 1, FColor::Red, "Could not get static mesh component");
        return;
    }
    int32 w, h;
    w = mTextureWidth;
    h = mTextureHeight;

    mDynamicMaterials.Empty();
    mDynamicMaterials.Add(mStaticMeshComponent->CreateAndSetMaterialInstanceDynamic(0));
    mDynamicTexture = UTexture2D::CreateTransient(w, h);
    mDynamicTexture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
    mDynamicTexture->SRGB = 0;
    mDynamicTexture->Filter = TextureFilter::TF_Nearest;
    mDynamicTexture->AddToRoot();
    mDynamicTexture->UpdateResource();

    mUpdateTextureRegion = new FUpdateTextureRegion2D(0, 0, 0, 0, w, h);

    mDynamicMaterials[0]->SetTextureParameterValue("DynamicTextureParam", mDynamicTexture);

    mDataSize = w * h * 4;
    mDataSqrtSize = w * 4;
    mArraySize = w * h;
    mArrayRowSize = w;

    mDynamicColors = new uint8[mDataSize];

    memset(mDynamicColors, 0, mDataSize);
}

void UEbookToTextureComponent::UpdateTexture()
{
    if (!mDynamicTexture)
        return;

    UpdateTextureRegions(mDynamicTexture, 0, 1, mUpdateTextureRegion, mDataSqrtSize, (uint32)4, mDynamicColors, false);
    mDynamicMaterials[0]->SetTextureParameterValue("DynamicTextureParam", mDynamicTexture);
}

bool UEbookToTextureComponent::Open(FString FilePath)
{
    if (!pdfCreate)
        return false;

    pdfDestroy(currentBook);
    currentBook = nullptr;

    return !!pdfCreate(&currentBook, TCHAR_TO_ANSI(*FilePath));
}

bool UEbookToTextureComponent::updatePage(int pageNumber, int pageCount)
{
    if (!currentBook)
        return false;

    int resultingWidth;
    int resultingHeight;

    if (pageCount == 1)
    {
        if (!pdfGetPageFittedBGRA(currentBook, pageNumber, mTextureWidth, mTextureHeight, &resultingWidth, &resultingHeight, mDynamicColors))
            return false;
    }
    else 
    {
        if (!pdfGet2PagesFittedBGRA(currentBook, pageNumber, mTextureWidth, mTextureHeight, &resultingWidth, &resultingHeight, mDynamicColors))
            return false;
    }

    // adjust uv to fit resultingWidth and Height
    float u = resultingWidth / (float)mTextureWidth;
    float v = resultingHeight / (float)mTextureHeight;
    mDynamicMaterials[0]->SetScalarParameterValue("ScaleX", u);
    mDynamicMaterials[0]->SetScalarParameterValue("ScaleY", v);

    UpdateTexture();
    return true;
}

bool UEbookToTextureComponent::ShowPage(int Page)
{
    return updatePage(Page, 1);
}

bool UEbookToTextureComponent::Show2Pages(int StartPage)
{
    return updatePage(StartPage, 2);
}
