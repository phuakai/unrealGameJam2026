// BlockDefinition.h
// One placeable block type (Cube, Plank, Pillar...). It points at a base asset
// (e.g. DA_Base_Wood) for all physics/feel/hover/audio/weld settings and only
// decides what the block looks like and how big it is.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BlockBaseDefinition.h"
#include "BlockDefinition.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UTexture2D;
class ABuildBlock;

UCLASS(BlueprintType)
class GAMEJAM_API UBlockDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shared settings (physics, feel, hover, audio, weld). Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UBlockBaseDefinition> Base;

	// ---------------------------------------------------------------- Identity / UI
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block") TObjectPtr<UTexture2D> Icon;
	/** Optional Blueprint child of ABuildBlock for special behaviour. Empty = pawn's BlockClass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block") TSubclassOf<ABuildBlock> BlockClassOverride;

	// ---------------------------------------------------------------- Look (the only things this asset changes)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") TObjectPtr<UStaticMesh> Mesh;
	/** Actor scale. Engine basic shapes are 100cm, so 0.5 = 50cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") FVector Scale = FVector(0.5);
	/** Needs vector params "Color" and "HoverColor" and scalar "Hover". Use M_Block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") TObjectPtr<UMaterialInterface> Material;
	/** One is picked at random per spawn. Leave empty for white (use this for textured wood). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look") TArray<FLinearColor> Colors;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("BlockDefinition"), GetFName());
	}
};
