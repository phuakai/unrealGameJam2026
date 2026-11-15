// BuilderPawn.h
// The player: orbit/pan/zoom camera, mouse-cursor physics grab, depth + rotation
// control, ground-shadow marker, data-asset spawn palette, sticky (weld) mode.
#pragma once


#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "BuilderPawn.generated.h"

class ABuildBlock;
class UBlockDefinition;
class UCameraComponent;
class UPhysicsHandleComponent;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBlockSpawned, int32, PaletteIndex, UBlockDefinition*, Definition);

UCLASS(Blueprintable)
class GAMEJAM_API ABuilderPawn : public APawn
{
	GENERATED_BODY()

public:
	ABuilderPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	bool IsHolding() const { return HeldBlock.IsValid(); }

	// Spawns palette entry PaletteIndex into the hand.Same as pressing its number key. * /
	UFUNCTION(BlueprintCallable, Category = "Builder|Blocks") void SpawnBlock(int32 PaletteIndex);

	// Fires after any spawn(UI click or number key).Use it to highlight the active slot. * /
	UPROPERTY(BlueprintAssignable, Category = "Builder|Blocks") FOnBlockSpawned OnBlockSpawned;

	// ------------------------------------------------------------ Blocks
	/** Class spawned for every block unless the definition names its own BlockClassOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Builder|Blocks") TSubclassOf<ABuildBlock> BlockClass;
	/** Spawnable blocks. Index 0 = key 1, index 1 = key 2, and so on (up to 9). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Builder|Blocks") TArray<TObjectPtr<UBlockDefinition>> Palette;
	/** How far in front of the camera new blocks appear (and the initial hold depth). */
	UPROPERTY(EditAnywhere, Category = "Builder|Blocks") float SpawnDistance = 700.f;

	// ------------------------------------------------------------ Grab feel
	// Lower stiffness = floppier. Lower damping = more overshoot/wobble.
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float LinearStiffness = 300.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float LinearDamping = 30.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float AngularStiffness = 600.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float AngularDamping = 120.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float InterpolationSpeed = 8.f;
	/** The spring target may lead the block by at most this many cm. Stops blocks being
	 *  yanked through stacks when you flick the mouse. Lower = lazier, safer. */
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float MaxLead = 250.f;
	/** Held block leans away from its movement direction (radians per cm/s, and cap). */
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float TiltPerSpeed = 0.0004f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float MaxTilt = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float RotateSpeedDegPerSec = 140.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float HoldStep = 60.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float MinHold = 120.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float MaxHold = 5000.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Grab") float TraceDistance = 20000.f;

	// ------------------------------------------------------------ Sticky mode
	/** When true, released blocks weld to anything they are touching. Toggle with G. */
	UPROPERTY(EditAnywhere, Category = "Builder|Sticky") bool bSticky = false;
	UPROPERTY(EditAnywhere, Category = "Builder|Sticky") float WeldMargin = 6.f;

	// ------------------------------------------------------------ Camera
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float OrbitSpeed = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float PanSpeed = 900.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float ZoomStep = 150.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float MinArm = 300.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float MaxArm = 4500.f;
	UPROPERTY(EditAnywhere, Category = "Builder|Camera") float StartArm = 1400.f;

private:
	// Input
	void GrabPressed();
	void GrabReleased();
	void ScrollUp()   { Scroll(+1.f); }
	void ScrollDown() { Scroll(-1.f); }
	void Scroll(float Dir);

	// Per-frame
	void FirstFrameSetup(APlayerController* PC);
	void UpdateCamera(APlayerController* PC, float DT);
	void UpdateHover(APlayerController* PC);
	void UpdateHeld(APlayerController* PC, float DT);
	void UpdateMarker(ABuildBlock* Block, UPrimitiveComponent* Comp);
	void UpdateCursor(APlayerController* PC);
	void PollKeys(APlayerController* PC);

	// Actions
	ABuildBlock* TraceBlock(APlayerController* PC, FHitResult& OutHit) const;
	void BeginHold(ABuildBlock* Block, const FVector& GrabPoint, float Distance);
	void Release(bool bAllowWeld = true);
	void TryWeld(ABuildBlock* Block);
	void ApplyHandleTuning();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Pivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Arm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsHandleComponent> Handle;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Marker;

	TWeakObjectPtr<ABuildBlock> HeldBlock;
	TWeakObjectPtr<ABuildBlock> HoveredBlock;

	// Grab state
	float HoldDistance = 700.f;
	FQuat HeldQuat = FQuat::Identity;
	FVector GrabLocalOffset = FVector::ZeroVector;
	FVector PrevDesired = FVector::ZeroVector;
	FVector SmoothedVel = FVector::ZeroVector;

	// Camera state
	float CameraYaw = 35.f;
	float CameraPitch = -40.f;
	float ArmTarget = 1400.f;
	bool bOrbiting = false;
	bool bSetupDone = false;
};

/** Set this as your GameMode (or make a Blueprint child) and you spawn straight into the builder. */
UCLASS(Blueprintable)
class GAMEJAM_API ABuilderGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ABuilderGameMode() { DefaultPawnClass = ABuilderPawn::StaticClass(); }
};
