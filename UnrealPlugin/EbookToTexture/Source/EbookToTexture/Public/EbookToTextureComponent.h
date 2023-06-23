// Copyright 2023 Dirk de la Hunt

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"
#include "EbookToTextureComponent.generated.h"

typedef struct Pdf Pdf;

typedef int(__cdecl* Pdf_create)(Pdf **newPdf, const char *filePath);
typedef int(__cdecl* Pdf_destroy)(Pdf *pdf);
typedef int(__cdecl* Pdf_getPageFittedBGRA)(Pdf *pdf, int pageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, unsigned char *outBuffer);
typedef int(__cdecl* Pdf_get2PagesFittedBGRA)(Pdf *pdf, int startPageNumber, int availableWidth, int availableHeight, int *resultingWidth, int *resultingHeight, unsigned char *outBuffer);


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class EBOOKTOTEXTURE_API UEbookToTextureComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UEbookToTextureComponent();

// dll stuff
protected:
    void* dllHandle = nullptr;
    Pdf* currentBook = nullptr;

    Pdf_create pdfCreate = nullptr;
    Pdf_destroy pdfDestroy = nullptr;
    Pdf_getPageFittedBGRA pdfGetPageFittedBGRA = nullptr;
    Pdf_get2PagesFittedBGRA pdfGet2PagesFittedBGRA = nullptr;

// texture stuff
protected:
    void SetupTexture();
    void UpdateTexture();

    TArray<class UMaterialInstanceDynamic*> mDynamicMaterials;
    UTexture2D* mDynamicTexture = nullptr;
    FUpdateTextureRegion2D* mUpdateTextureRegion;

    uint8* mDynamicColors = nullptr;
    int mTextureWidth = 1024;
    int mTextureHeight = 1024;

    uint32 mDataSize;
    uint32 mDataSqrtSize;
    uint32 mArraySize;
    uint32 mArrayRowSize;

    UStaticMeshComponent* mStaticMeshComponent;

// book stuff
protected:
    bool updatePage(int pageNumber, int pageCount);

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
    UFUNCTION(BlueprintCallable, Category = "EBook")
        bool Open(FString FilePath);
    UFUNCTION(BlueprintCallable, Category = "EBook")
        bool ShowPage(int Page);
    UFUNCTION(BlueprintCallable, Category = "EBook")
        bool Show2Pages(int StartPage);
};
