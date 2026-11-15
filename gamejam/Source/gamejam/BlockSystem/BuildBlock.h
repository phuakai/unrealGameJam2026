// BuildBlock.h
// A single toy block: an invisible physics body plus a separate "visual" mesh that
// gets hover/held feedback and a small squash. All tuning comes from data assets:
//
//   UBlockDefinition      -> mesh, scale, material, colours   (per block type)
//     \-> UBlockBaseDefinition -> physics, feel, hover, audio, weld (per material type, e.g. wood)
//
// A block with no Definition falls back to the base asset's class defaults (wood).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildBlock.generated.h"

class UBlockBaseDefinition;
class UBlockDefinition;
class UMaterialInstanceDynamic;
class UPhysicsConstraintComponent;
class UStaticMeshComponent;

/** Bookkeeping for one weld. Both blocks keep a record pointing at the same constraint. */
struct FBlockWeld
{
	TWeakObjectPtr<ABuildBlock> Other;
	TWeakObjectPtr<UPhysicsConstraintComponent> Constraint;
};

UCLASS(Blueprintable)
class GAMEJAM_API ABuildBlock : public AActor
{
	GENERATED_BODY()

public:
	ABuildBlock();

	/** What kind of block this is. Set it on spawn, on a Blueprint child's defaults,
	 *  or per-instance on a block placed in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ExposeOnSpawn = true))
	TObjectPtr<UBlockDefinition> Definition;

	/** Blocks that fall below this Z get popped back above where they started. */
	UPROPERTY(EditAnywhere, Category = "Block") float KillZ = -3000.f;

	/** Call between SpawnActorDeferred and FinishSpawning. The look is applied in FinishSpawning. */
	void InitFromDefinition(UBlockDefinition* InDefinition);

	UStaticMeshComponent* GetPhysicsMesh() const { return PhysMesh; }

	/** Settings from the definition's base asset, or wood defaults if there is none. Never null. */
	const UBlockBaseDefinition& GetBase() const;

	void SetHeld(bool bInHeld);
	void SetHovered(bool bInHovered);

	/** Squash impulse. Negative = squash, positive = stretch. Scaled by the base's SquashAmount. */
	UFUNCTION(BlueprintCallable, Category = "Block") void Boing(float Kick);

	/** Rigidly glue this block to another. False if invalid, already welded, or either block can't weld. */
	UFUNCTION(BlueprintCallable, Category = "Block") bool WeldTo(ABuildBlock* Other);
	UFUNCTION(BlueprintCallable, Category = "Block") void UnweldAll();
	bool IsWeldedTo(const ABuildBlock* Other);

	/** Half extent of the mesh in world units (scale applied). */
	FVector GetProbeHalfExtent() const;

	/** Blueprint hooks: spawn particles, play a sound, show UI. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Block") void OnHoverChanged(bool bNowHovered);
	UFUNCTION(BlueprintImplementableEvent, Category = "Block") void OnHeldChanged(bool bNowHeld);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	UFUNCTION() void HandleWeldBroken(int32 ConstraintIndex);

private:
	void ApplyLook();
	void PickTint();
	void ApplyPhysicsSettings();
	void UpdateOutline();
	void PruneWelds();

	/** Root. Invisible. Owns collision + simulation. Never scaled by effects. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PhysMesh;
	/** Child. What you actually see. Gets the squash and pop scaling. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> VisualMID;

	TArray<FBlockWeld> Welds;

	bool bHeld = false;
	bool bHovered = false;

	FLinearColor Tint = FLinearColor::White;
	bool bTintPicked = false;

	float Squash = 0.f;      // negative = squashed, positive = stretched
	float SquashVel = 0.f;
	float Pop = 1.f;
	float HoverAlpha = 0.f;
	float LastHoverValue = -1.f;
	float LastImpactTime = -100.f;
	FVector HomeLocation = FVector::ZeroVector;
};
