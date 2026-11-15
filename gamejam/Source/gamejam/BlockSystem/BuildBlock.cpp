// BuildBlock.cpp
#include "BuildBlock.h"
#include "BlockBaseDefinition.h"
#include "BlockDefinition.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Sound/SoundBase.h"

namespace
{
	/** Clamp the frame time fed to the squash spring so a hitch can't make it explode. */
	constexpr float MaxSquashStep = 1.f / 30.f;
}

// ============================================================ Construction

ABuildBlock::ABuildBlock()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // read the body's final transform each frame

	// Physics body is the root. It is hidden; the Visual child is what the player sees.
	PhysMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysMesh"));
	SetRootComponent(PhysMesh);
	PhysMesh->SetMobility(EComponentMobility::Movable);
	PhysMesh->SetCollisionProfileName(TEXT("PhysicsActor")); // object type = PhysicsBody
	PhysMesh->SetSimulatePhysics(true);
	PhysMesh->SetNotifyRigidBodyCollision(true);              // "Simulation Generates Hit Events"
	PhysMesh->SetVisibility(false);
	PhysMesh->BodyInstance.bUseCCD = true;                    // thrown blocks don't tunnel

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(PhysMesh);
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCustomDepthStencilValue(1);
}

const UBlockBaseDefinition& ABuildBlock::GetBase() const
{
	if (Definition && Definition->Base)
	{
		return *Definition->Base;
	}
	return *GetDefault<UBlockBaseDefinition>(); // class defaults = wood
}

void ABuildBlock::InitFromDefinition(UBlockDefinition* InDefinition)
{
	Definition = InDefinition;
	bTintPicked = false;
	// The look itself is applied by OnConstruction during FinishSpawning.
}

void ABuildBlock::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void ABuildBlock::PickTint()
{
	bTintPicked = true;
	Tint = (Definition && Definition->Colors.Num() > 0)
		? Definition->Colors[FMath::RandRange(0, Definition->Colors.Num() - 1)]
		: FLinearColor::White;
}

void ABuildBlock::ApplyLook()
{
	// Mesh: the definition wins. Otherwise keep whichever component already has one, so
	// Blueprint children and hand-placed blocks still work. Both components must match.
	UStaticMesh* Mesh = (Definition && Definition->Mesh) ? Definition->Mesh.Get() : nullptr;
	if (!Mesh) Mesh = PhysMesh->GetStaticMesh();
	if (!Mesh) Mesh = Visual->GetStaticMesh();
	if (!Mesh) return;

	if (PhysMesh->GetStaticMesh() != Mesh) PhysMesh->SetStaticMesh(Mesh);
	if (Visual->GetStaticMesh() != Mesh)   Visual->SetStaticMesh(Mesh);

	// Material: only when the definition supplies one. Needs params Color, HoverColor, Hover.
	VisualMID = nullptr;
	LastHoverValue = -1.f;
	if (Definition && Definition->Material)
	{
		if (!bTintPicked) PickTint();

		VisualMID = Visual->CreateDynamicMaterialInstance(0, Definition->Material);
		if (VisualMID)
		{
			VisualMID->SetVectorParameterValue(TEXT("Color"), Tint);
			VisualMID->SetVectorParameterValue(TEXT("HoverColor"), GetBase().HoverColor);
			VisualMID->SetScalarParameterValue(TEXT("Hover"), 0.f);
		}
	}
}

void ABuildBlock::ApplyPhysicsSettings()
{
	const UBlockBaseDefinition& B = GetBase();

	PhysMesh->SetLinearDamping(B.LinearDamping);
	PhysMesh->SetAngularDamping(B.AngularDamping);

	// Mass by volume so big planks aren't the same weight as small cubes,
	// but nothing is so heavy that the grab spring can't lift it.
	const FVector S = GetActorScale3D();
	const float Volume = FMath::Abs(S.X * S.Y * S.Z);
	PhysMesh->SetMassOverrideInKg(NAME_None, FMath::Max(1.f, B.MassPerUnitVolume * Volume), true);

	UPhysicalMaterial* PhysMat = NewObject<UPhysicalMaterial>(this);
	PhysMat->Restitution = B.Restitution;
	PhysMat->Friction = B.Friction;
	PhysMesh->SetPhysMaterialOverride(PhysMat);
}

// ============================================================== Lifecycle

void ABuildBlock::BeginPlay()
{
	Super::BeginPlay();

	HomeLocation = GetActorLocation();

	// Drive the visual's world transform ourselves in Tick instead of trusting attachment.
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetUsingAbsoluteLocation(true);
	Visual->SetUsingAbsoluteRotation(true);
	Visual->SetUsingAbsoluteScale(true);

	// A Blueprint override or level placement can switch these off, and then the
	// grab handle has nothing to move. Force them back on.
	PhysMesh->SetMobility(EComponentMobility::Movable);
	PhysMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PhysMesh->SetSimulatePhysics(true);
	PhysMesh->SetEnableGravity(true);

	// Simulated bodies need SIMPLE collision. Imported meshes often lack it.
	if (const UStaticMesh* M = PhysMesh->GetStaticMesh())
	{
		const UBodySetup* Setup = M->GetBodySetup();
		if (!Setup || Setup->AggGeom.GetElementCount() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("BuildBlock '%s': mesh '%s' has no simple collision, so it cannot simulate physics. Add a box/convex collision in the Static Mesh editor."),
				*GetName(), *M->GetName());
		}
	}

	ApplyPhysicsSettings();
}

void ABuildBlock::EndPlay(const EEndPlayReason::Type Reason)
{
	UnweldAll();
	Super::EndPlay(Reason);
}

void ABuildBlock::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UBlockBaseDefinition& B = GetBase();
	const float DT = FMath::Min(DeltaTime, MaxSquashStep);

	// --- squash spring. SquashAmount scales the kicks; MaxSquash is a hard cap.
	SquashVel += (-B.SquashStiffness * Squash - B.SquashDamping * SquashVel) * DT;
	Squash = FMath::Clamp(Squash + SquashVel * DT, -B.MaxSquash, B.MaxSquash);

	// --- hover/held feedback: emissive glow + a tiny scale bump (no deformation)
	const bool bHighlight = bHovered || bHeld;
	HoverAlpha = FMath::FInterpTo(HoverAlpha, bHighlight ? 1.f : 0.f, DT, 16.f);
	if (VisualMID)
	{
		const float GlowValue = HoverAlpha * B.HoverGlow;
		if (!FMath::IsNearlyEqual(GlowValue, LastHoverValue, 0.001f))
		{
			VisualMID->SetScalarParameterValue(TEXT("Hover"), GlowValue);
			LastHoverValue = GlowValue;
		}
	}

	const float TargetPop = 1.f + (bHeld ? B.HeldPop : 0.f) + (bHovered ? B.HoverPop : 0.f);
	Pop = FMath::FInterpTo(Pop, TargetPop, DT, 14.f);

	// --- copy the physics body's world transform onto the visual, then apply squash/pop.
	// Roughly volume-preserving: squash on Z fattens X/Y.
	const FTransform BodyT = PhysMesh->GetComponentTransform();
	const FVector SquashScale(1.f - Squash * 0.5f, 1.f - Squash * 0.5f, 1.f + Squash);
	Visual->SetWorldLocationAndRotation(BodyT.GetLocation(), BodyT.GetRotation());
	Visual->SetWorldScale3D(BodyT.GetScale3D() * SquashScale * Pop);

	// --- fell off the world? pop it back above where it started
	if (GetActorLocation().Z < KillZ)
	{
		PhysMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PhysMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		SetActorLocation(HomeLocation + FVector(0.f, 0.f, 500.f), false, nullptr, ETeleportType::TeleportPhysics);
		Boing(-4.f);
	}
}

// ================================================================= Impact

void ABuildBlock::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	const UBlockBaseDefinition& B = GetBase();

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastImpactTime < B.ImpactCooldown) return;

	// impulse / mass ~= velocity change in cm/s. Tune Min/MaxImpactSpeed by ear.
	const float Mass = FMath::Max(PhysMesh->GetMass(), 1.f);
	const float Speed = NormalImpulse.Size() / Mass;
	if (Speed < B.MinImpactSpeed) return;
	LastImpactTime = Now;

	const float Strength = FMath::Clamp(Speed / FMath::Max(B.MaxImpactSpeed, 1.f), 0.f, 1.f);
	Boing(-Strength * B.SquashKick);

	if (USoundBase* Sound = B.ImpactSound)
	{
		const float Volume = FMath::Lerp(0.2f, 1.f, Strength);
		// Small blocks tok higher, big ones thunk lower.
		const float SizePitch = FMath::Clamp(FMath::Pow(Mass / 20.f, -0.15f), 0.7f, 1.4f);
		const float Pitch = SizePitch * FMath::RandRange(0.92f, 1.08f);
		UGameplayStatics::PlaySoundAtLocation(this, Sound, HitLocation, Volume, Pitch);
	}
}

void ABuildBlock::Boing(float Kick)
{
	SquashVel += Kick * GetBase().SquashAmount;
}

// ============================================================ Hover / held

void ABuildBlock::SetHeld(bool bInHeld)
{
	if (bHeld == bInHeld) return;
	bHeld = bInHeld;
	UpdateOutline();
	OnHeldChanged(bHeld);
}

void ABuildBlock::SetHovered(bool bInHovered)
{
	if (bHovered == bInHovered) return;
	bHovered = bInHovered;
	UpdateOutline();
	OnHoverChanged(bHovered);
}

void ABuildBlock::UpdateOutline()
{
	// Needs a post-process outline material reading custom stencil: 1 = hover, 2 = held.
	Visual->SetCustomDepthStencilValue(bHeld ? 2 : 1);
	Visual->SetRenderCustomDepth(GetBase().bUseOutline && (bHeld || bHovered));
}

FVector ABuildBlock::GetProbeHalfExtent() const
{
	const FVector S = GetActorScale3D().GetAbs();
	if (const UStaticMesh* M = PhysMesh->GetStaticMesh())
	{
		return M->GetBounds().BoxExtent * S;
	}
	return FVector(50.f) * S;
}

// ================================================================== Welding

void ABuildBlock::PruneWelds()
{
	Welds.RemoveAll([](const FBlockWeld& W)
		{
			return !W.Other.IsValid() || !W.Constraint.IsValid()
				|| !W.Constraint->ConstraintInstance.IsValidConstraintInstance();
		});
}

bool ABuildBlock::IsWeldedTo(const ABuildBlock* Other)
{
	PruneWelds();
	return Welds.ContainsByPredicate([Other](const FBlockWeld& W) { return W.Other.Get() == Other; });
}

bool ABuildBlock::WeldTo(ABuildBlock* Other)
{
	if (!IsValid(Other) || Other == this || IsWeldedTo(Other)) return false;

	const UBlockBaseDefinition& B = GetBase();
	if (!B.bCanWeld || !Other->GetBase().bCanWeld) return false;

	UPhysicsConstraintComponent* C = NewObject<UPhysicsConstraintComponent>(this);
	C->SetupAttachment(PhysMesh);
	C->RegisterComponent();
	C->SetWorldLocation((GetActorLocation() + Other->GetActorLocation()) * 0.5f);

	C->SetDisableCollision(true); // welded pair shouldn't fight each other
	C->SetLinearXLimit(LCM_Locked, 0.f);
	C->SetLinearYLimit(LCM_Locked, 0.f);
	C->SetLinearZLimit(LCM_Locked, 0.f);
	C->SetAngularSwing1Limit(ACM_Locked, 0.f);
	C->SetAngularSwing2Limit(ACM_Locked, 0.f);
	C->SetAngularTwistLimit(ACM_Locked, 0.f);

	if (B.WeldBreakForce > 0.f)
	{
		C->SetLinearBreakable(true, B.WeldBreakForce);
		C->SetAngularBreakable(true, B.WeldBreakForce);
	}
	C->OnConstraintBroken.AddDynamic(this, &ABuildBlock::HandleWeldBroken);

	// Initialises the constraint using the CURRENT relative pose, so the pair
	// freezes exactly as they sit.
	C->SetConstrainedComponents(PhysMesh, NAME_None, Other->PhysMesh, NAME_None);

	Welds.Add(FBlockWeld{ Other, C });
	Other->Welds.Add(FBlockWeld{ this, C });

	Boing(-2.f);
	Other->Boing(-2.f);
	return true;
}

void ABuildBlock::UnweldAll()
{
	TArray<FBlockWeld> Copy = Welds;
	Welds.Empty();

	for (FBlockWeld& W : Copy)
	{
		if (ABuildBlock* O = W.Other.Get())
		{
			O->Welds.RemoveAll([this](const FBlockWeld& X) { return X.Other.Get() == this; });
			O->Boing(4.f);
		}
		if (UPhysicsConstraintComponent* C = W.Constraint.Get())
		{
			C->OnConstraintBroken.Clear(); // deliberate unweld, not a "snap"
			C->BreakConstraint();
			C->DestroyComponent();
		}
	}
	if (Copy.Num() > 0) Boing(5.f);
}

void ABuildBlock::HandleWeldBroken(int32 /*ConstraintIndex*/)
{
	PruneWelds();
	Boing(6.f); // snap!
}
