// BuilderPawn.cpp
#include "BuilderPawn.h"
#include "BlockDefinition.h"
#include "BuildBlock.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "UObject/ConstructorHelpers.h"

// ============================================================ Construction

ABuilderPawn::ABuilderPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	// Spring arm keeps its own world rotation: we drive yaw/pitch manually.
	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(Pivot);
	Arm->bDoCollisionTest = false;
	Arm->bUsePawnControlRotation = false;
	Arm->bInheritPitch = false;
	Arm->bInheritYaw = false;
	Arm->bInheritRoll = false;
	Arm->TargetArmLength = StartArm;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);

	Handle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("Handle"));

	BlockClass = ABuildBlock::StaticClass();

	// Ground-shadow marker: a flat disc under the held block. Makes depth readable.
	// The only engine asset the pawn still needs; block meshes now come from data assets.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Pivot);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCastShadow(false);
	Marker->SetUsingAbsoluteLocation(true);
	Marker->SetUsingAbsoluteRotation(true);
	Marker->SetUsingAbsoluteScale(true);
	if (CylinderFinder.Succeeded())  Marker->SetStaticMesh(CylinderFinder.Object);
	if (MarkerMatFinder.Succeeded()) Marker->SetMaterial(0, MarkerMatFinder.Object);
}

void ABuilderPawn::BeginPlay()
{
	Super::BeginPlay();
	ArmTarget = StartArm;
	Arm->TargetArmLength = StartArm;
	ApplyHandleTuning();
	Marker->SetVisibility(false);
}

void ABuilderPawn::ApplyHandleTuning()
{
	Handle->SetLinearStiffness(LinearStiffness);
	Handle->SetLinearDamping(LinearDamping);
	Handle->SetAngularStiffness(AngularStiffness);
	Handle->SetAngularDamping(AngularDamping);
	Handle->SetInterpolationSpeed(InterpolationSpeed);
}

void ABuilderPawn::SetupPlayerInputComponent(UInputComponent* IC)
{
	Super::SetupPlayerInputComponent(IC);
	// Raw key bindings: no Input Mapping assets needed.
	IC->BindKey(EKeys::LeftMouseButton, IE_Pressed,  this, &ABuilderPawn::GrabPressed);
	IC->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ABuilderPawn::GrabReleased);
	IC->BindKey(EKeys::MouseScrollUp,   IE_Pressed,  this, &ABuilderPawn::ScrollUp);
	IC->BindKey(EKeys::MouseScrollDown, IE_Pressed,  this, &ABuilderPawn::ScrollDown);
}

// ==================================================================== Tick

void ABuilderPawn::Tick(float DT)
{
	Super::Tick(DT);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	if (!bSetupDone) FirstFrameSetup(PC);

	UpdateCamera(PC, DT);
	if (IsHolding()) UpdateHeld(PC, DT); else UpdateHover(PC);
	UpdateCursor(PC);
	PollKeys(PC);
}

void ABuilderPawn::FirstFrameSetup(APlayerController* PC)
{
	bSetupDone = true;
	PC->bShowMouseCursor = true;

	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);

	if (GEngine)
	{
		const FString Help = FString::Printf(TEXT(
			"LMB grab | Wheel depth (zoom if empty-handed) | Q/E turn | R/F tip | RMB-drag orbit | WASD pan | "
			"1-%d spawn | G sticky | V unglue | X delete"), FMath::Clamp(Palette.Num(), 1, 9));
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, Help);
	}
}

// ==================================================================== Camera

void ABuilderPawn::UpdateCamera(APlayerController* PC, float DT)
{
	// Orbit with right mouse. Cursor hides while orbiting and we read raw deltas.
	if (PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		if (!bOrbiting) { bOrbiting = true; PC->bShowMouseCursor = false; }
		float DX = 0.f, DY = 0.f;
		PC->GetInputMouseDelta(DX, DY);
		CameraYaw += DX * OrbitSpeed * 10.f;
		CameraPitch = FMath::Clamp(CameraPitch + DY * OrbitSpeed * 10.f, -85.f, -5.f); // flip the sign for inverted Y
	}
	else if (bOrbiting)
	{
		bOrbiting = false;
		PC->bShowMouseCursor = true;
	}

	// Pan the pivot on the ground plane relative to camera yaw.
	const FRotator YawRot(0.f, CameraYaw, 0.f);
	const FVector Fwd = YawRot.RotateVector(FVector::ForwardVector);
	const FVector Right = YawRot.RotateVector(FVector::RightVector);
	FVector Move = FVector::ZeroVector;
	if (PC->IsInputKeyDown(EKeys::W)) Move += Fwd;
	if (PC->IsInputKeyDown(EKeys::S)) Move -= Fwd;
	if (PC->IsInputKeyDown(EKeys::D)) Move += Right;
	if (PC->IsInputKeyDown(EKeys::A)) Move -= Right;
	if (!Move.IsNearlyZero())
	{
		const float Boost = PC->IsInputKeyDown(EKeys::LeftShift) ? 2.5f : 1.f;
		const float ZoomScale = Arm->TargetArmLength / StartArm; // pan faster when zoomed out
		AddActorWorldOffset(Move.GetSafeNormal() * PanSpeed * Boost * ZoomScale * DT);
	}

	Arm->SetWorldRotation(FRotator(CameraPitch, CameraYaw, 0.f));
	Arm->TargetArmLength = FMath::FInterpTo(Arm->TargetArmLength, ArmTarget, DT, 8.f);
}

void ABuilderPawn::Scroll(float Dir)
{
	if (IsHolding())
	{
		HoldDistance = FMath::Clamp(HoldDistance + Dir * HoldStep, MinHold, MaxHold);
	}
	else
	{
		ArmTarget = FMath::Clamp(ArmTarget - Dir * ZoomStep, MinArm, MaxArm);
	}
}

// ==================================================================== Tracing

ABuildBlock* ABuilderPawn::TraceBlock(APlayerController* PC, FHitResult& OutHit) const
{
	FVector Origin, Dir;
	if (!PC->DeprojectMousePositionToWorld(Origin, Dir)) return nullptr;

	const FCollisionObjectQueryParams ObjParams(ECC_PhysicsBody);
	if (GetWorld()->LineTraceSingleByObjectType(OutHit, Origin, Origin + Dir * TraceDistance, ObjParams))
	{
		return Cast<ABuildBlock>(OutHit.GetActor());
	}
	return nullptr;
}

void ABuilderPawn::UpdateHover(APlayerController* PC)
{
	FHitResult Hit;
	ABuildBlock* Now = TraceBlock(PC, Hit);
	ABuildBlock* Prev = HoveredBlock.Get();
	if (Now == Prev) return;

	if (Prev) Prev->SetHovered(false);
	if (Now)  Now->SetHovered(true);
	HoveredBlock = Now;
}

void ABuilderPawn::UpdateCursor(APlayerController* PC)
{
	PC->CurrentMouseCursor = IsHolding() ? EMouseCursor::GrabHandClosed
		: (HoveredBlock.IsValid() ? EMouseCursor::GrabHand : EMouseCursor::Default);
}

// ======================================================================= Grab

void ABuilderPawn::GrabPressed()
{
	if (IsHolding()) return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	FHitResult Hit;
	if (ABuildBlock* Block = TraceBlock(PC, Hit))
	{
		BeginHold(Block, Hit.ImpactPoint, Hit.Distance);
	}
}

void ABuilderPawn::GrabReleased()
{
	if (IsHolding()) Release();
}

void ABuilderPawn::BeginHold(ABuildBlock* Block, const FVector& GrabPoint, float Distance)
{
	UPrimitiveComponent* Comp = Block->GetPhysicsMesh();
	if (!Comp) return;

	if (ABuildBlock* H = HoveredBlock.Get()) H->SetHovered(false);
	HoveredBlock = nullptr;

	Comp->WakeAllRigidBodies(); // sleeping bodies ignore the handle otherwise

	HoldDistance = FMath::Clamp(Distance, MinHold, MaxHold);
	HeldQuat = Comp->GetComponentQuat();
	GrabLocalOffset = Comp->GetComponentTransform().InverseTransformPosition(GrabPoint);
	PrevDesired = GrabPoint;
	SmoothedVel = FVector::ZeroVector;

	Handle->GrabComponentAtLocationWithRotation(Comp, NAME_None, GrabPoint, HeldQuat.Rotator());

	if (!Handle->GetGrabbedComponent() || !Comp->IsSimulatingPhysics())
	{
		const FString Msg = FString::Printf(TEXT("Grab failed on %s (simulating physics: %s). Check PhysMesh: Simulate Physics, Movable, simple collision."),
			*Block->GetName(), Comp->IsSimulatingPhysics() ? TEXT("yes") : TEXT("NO"));
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
		if (GEngine) GEngine->AddOnScreenDebugMessage(2, 6.f, FColor::Red, Msg);
		Handle->ReleaseComponent();
		return;
	}

	HeldBlock = Block;
	Block->SetHeld(true);
	Block->Boing(-3.f); // little grab squish (scaled by the block's SquashAmount)
}

void ABuilderPawn::UpdateHeld(APlayerController* PC, float DT)
{
	ABuildBlock* Block = HeldBlock.Get();
	UPrimitiveComponent* Comp = Handle->GetGrabbedComponent();
	if (!Block || !Comp)
	{
		Release(false); // block got destroyed or handle lost it
		return;
	}

	FVector Origin, Dir;
	if (!PC->DeprojectMousePositionToWorld(Origin, Dir)) return;
	FVector Desired = Origin + Dir * HoldDistance;

	// ---- rotation: Q/E spin around world up, R/F tip toward/away from camera
	const float SpinInput = (PC->IsInputKeyDown(EKeys::E) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Q) ? 1.f : 0.f);
	const float TipInput  = (PC->IsInputKeyDown(EKeys::R) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::F) ? 1.f : 0.f);
	const float Step = FMath::DegreesToRadians(RotateSpeedDegPerSec) * DT;
	if (SpinInput != 0.f) HeldQuat = FQuat(FVector::UpVector, SpinInput * Step) * HeldQuat;
	if (TipInput != 0.f)  HeldQuat = FQuat(Camera->GetRightVector(), TipInput * Step) * HeldQuat;
	HeldQuat.Normalize();

	// ---- goofy dangle: lean away from movement direction
	FQuat Tilt = FQuat::Identity;
	if (DT > KINDA_SMALL_NUMBER)
	{
		const FVector Vel = (Desired - PrevDesired) / DT;
		SmoothedVel = FMath::VInterpTo(SmoothedVel, Vel, DT, 12.f);
	}
	PrevDesired = Desired;
	const float Speed = SmoothedVel.Size();
	if (Speed > 1.f)
	{
		const FVector Axis = FVector::CrossProduct(SmoothedVel, FVector::UpVector).GetSafeNormal();
		if (!Axis.IsNearlyZero()) Tilt = FQuat(Axis, FMath::Min(Speed * TiltPerSpeed, MaxTilt));
	}

	// ---- don't let the spring target run away from the block (prevents tunnelling/explosions)
	const FVector Current = Comp->GetComponentTransform().TransformPosition(GrabLocalOffset);
	const FVector Delta = Desired - Current;
	if (Delta.SizeSquared() > FMath::Square(MaxLead))
	{
		Desired = Current + Delta.GetSafeNormal() * MaxLead;
	}

	Handle->SetTargetLocationAndRotation(Desired, (Tilt * HeldQuat).Rotator());
	UpdateMarker(Block, Comp);
}

void ABuilderPawn::UpdateMarker(ABuildBlock* Block, UPrimitiveComponent* Comp)
{
	FCollisionQueryParams Q;
	Q.AddIgnoredActor(Block);
	Q.AddIgnoredActor(this);

	const FVector C = Comp->GetComponentLocation();
	FHitResult G;
	if (GetWorld()->LineTraceSingleByChannel(G, C, C - FVector(0.f, 0.f, 5000.f), ECC_Visibility, Q))
	{
		const float R = Block->GetProbeHalfExtent().GetMax();
		Marker->SetVisibility(true);
		Marker->SetWorldLocation(G.ImpactPoint + FVector(0.f, 0.f, 1.f));
		Marker->SetWorldRotation(FRotator::ZeroRotator);
		Marker->SetWorldScale3D(FVector(R * 2.f / 100.f, R * 2.f / 100.f, 0.01f));
	}
	else
	{
		Marker->SetVisibility(false);
	}
}

void ABuilderPawn::Release(bool bAllowWeld)
{
	ABuildBlock* Block = HeldBlock.Get();
	Handle->ReleaseComponent(); // keeps velocity, so blocks can be tossed
	Marker->SetVisibility(false);
	HeldBlock = nullptr;

	if (Block)
	{
		Block->SetHeld(false);
		Block->Boing(3.f);
		if (bAllowWeld && bSticky) TryWeld(Block);
	}
}

// ==================================================================== Sticky

void ABuilderPawn::TryWeld(ABuildBlock* Block)
{
	const FCollisionShape Shape = FCollisionShape::MakeBox(Block->GetProbeHalfExtent() + FVector(WeldMargin));
	FCollisionQueryParams Q;
	Q.AddIgnoredActor(Block);
	const FCollisionObjectQueryParams ObjParams(ECC_PhysicsBody);

	TArray<FOverlapResult> Overlaps;
	if (!GetWorld()->OverlapMultiByObjectType(Overlaps, Block->GetActorLocation(), Block->GetActorQuat(), ObjParams, Shape, Q)) return;

	// WeldTo ignores duplicates and blocks whose base asset has bCanWeld off.
	for (const FOverlapResult& R : Overlaps)
	{
		if (ABuildBlock* Other = Cast<ABuildBlock>(R.GetActor()))
		{
			Block->WeldTo(Other);
		}
	}
}

// ===================================================================== Spawn

void ABuilderPawn::SpawnBlock(int32 Index)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Palette.IsValidIndex(Index)) return;

	UBlockDefinition* Def = Palette[Index];
	if (!Def || !Def->Mesh) return;

	TSubclassOf<ABuildBlock> SpawnClass = Def->BlockClassOverride ? Def->BlockClassOverride : BlockClass;
	if (!SpawnClass) return;

	FVector Origin, Dir;
	if (!PC->DeprojectMousePositionToWorld(Origin, Dir)) return;

	if (IsHolding()) Release(false);

	const FTransform T(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Origin + Dir * SpawnDistance, Def->Scale);

	ABuildBlock* Block = GetWorld()->SpawnActorDeferred<ABuildBlock>(
		SpawnClass, T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Block) return;

	Block->InitFromDefinition(Def);
	Block->FinishSpawning(T);

	// Straight into the hand, like pulling a block out of the toy box.
	BeginHold(Block, Block->GetActorLocation(), SpawnDistance);

	OnBlockSpawned.Broadcast(Index, Def);
}

// ===================================================================== Keys

void ABuilderPawn::PollKeys(APlayerController* PC)
{
	static const FKey NumKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < UE_ARRAY_COUNT(NumKeys); ++i)
	{
		if (PC->WasInputKeyJustPressed(NumKeys[i])) SpawnBlock(i);
	}

	if (PC->WasInputKeyJustPressed(EKeys::G))
	{
		bSticky = !bSticky;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 2.f, bSticky ? FColor::Green : FColor::White,
				bSticky ? TEXT("Sticky: ON (released blocks glue to neighbours)") : TEXT("Sticky: OFF"));
		}
	}

	// Block under the cursor, or the one in hand.
	ABuildBlock* Target = IsHolding() ? HeldBlock.Get() : HoveredBlock.Get();
	if (!Target) return;

	if (PC->WasInputKeyJustPressed(EKeys::V))
	{
		Target->UnweldAll();
	}

	if (PC->WasInputKeyJustPressed(EKeys::X))
	{
		if (Target == HeldBlock.Get()) Release(false);
		HoveredBlock = nullptr;
		Target->Destroy(); // EndPlay breaks its welds
	}
}
