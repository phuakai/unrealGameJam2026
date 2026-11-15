// BlockBaseDefinition.h
// Shared "material type" settings: physics, feel, hover, audio, weld.
// Make one of these per kind of stuff (DA_Base_Wood, later DA_Base_Rubber, ...).
// Individual blocks (UBlockDefinition) point at one and only change look/shape.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BlockBaseDefinition.generated.h"

class USoundBase;

UCLASS(BlueprintType)
class GAMEJAM_API UBlockBaseDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- Physics (wood defaults)
	/** Mass in kg for a 1x1x1 (100cm cube) volume. Mass scales with block volume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics") float MassPerUnitVolume = 160.f;
	/** Wood doesn't bounce much. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0", ClampMax = "1")) float Restitution = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0")) float Friction = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics") float LinearDamping = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physics") float AngularDamping = 0.5f;

	// ---------------------------------------------------------------- Feel
	/** Scales every squash kick. 0 = perfectly rigid. Wood should stay low. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel", meta = (ClampMin = "0", ClampMax = "1")) float SquashAmount = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel") float SquashStiffness = 400.f;
	/** Critically damped is about 2*sqrt(Stiffness). Higher = less jiggle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel") float SquashDamping = 36.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel") float SquashKick = 6.f;
	/** Hard cap on how far the visual can squash/stretch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel", meta = (ClampMin = "0", ClampMax = "0.6")) float MaxSquash = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feel") float HeldPop = 0.05f;

	// ---------------------------------------------------------------- Hover signifier (no deformation)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover") FLinearColor HoverColor = FLinearColor(1.f, 0.95f, 0.8f);
	/** 0..1 emissive blend while hovered or held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = "0", ClampMax = "1")) float HoverGlow = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover") float HoverPop = 0.025f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover") bool bUseOutline = true;

	// ---------------------------------------------------------------- Audio
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio") TObjectPtr<USoundBase> ImpactSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio") float MinImpactSpeed = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio") float MaxImpactSpeed = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio") float ImpactCooldown = 0.07f;

	// ---------------------------------------------------------------- Weld
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weld") bool bCanWeld = true;
	/** Force needed to snap a weld. 0 = unbreakable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weld") float WeldBreakForce = 400000.f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("BlockBaseDefinition"), GetFName());
	}
};
