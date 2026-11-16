// Animation gallery: every state and transition on a neutral background, from the front, the side and the game camera.
// Interactive: Left/Right switch the entry, 1/2/3 switch the camera, the entry loops.
// Capture: -FallGallery=M1/gallery records every entry from all three cameras frame by frame (use with -benchmark -fps=30).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FallAnimGallery.generated.h"

class AFallBody;
class ACameraActor;
class UStaticMeshComponent;

UCLASS()
class FALL_API AFallAnimGallery : public AActor
{
	GENERATED_BODY()

public:
	AFallAnimGallery();
	virtual void Tick(float DeltaSeconds) override;

	struct FEntry { const TCHAR* Name; float Duration; };
	static TArray<FEntry> Entries();

protected:
	virtual void BeginPlay() override;
	void StartEntry(int32 Index, int32 Angle);
	void ApplyEntry(float Dt);
	void PlaceCamera();

	UPROPERTY() TObjectPtr<AFallBody> Body;
	UPROPERTY() TObjectPtr<ACameraActor> Cam;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Backdrop;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Prop;

	int32 Entry = 0;
	int32 Angle = 2;
	float T = 0.f;
	float PrevT = 0.f;
	float Warmup = 0.f;
	int32 CapFrame = 0;
	FString CaptureDir;
	bool bCapture = false;
	bool bPrevKeys[5] = {false, false, false, false, false};
	FVector GrabPoint = FVector::ZeroVector;
	int32 OnlyEntry = -1;
	bool bCloseup = false;
	FVector HandFocus = FVector::ZeroVector;
	struct FIntersect { int32 Frames = 0, Bad = 0; float Worst = 0.f; float WorstT = 0.f; FString Pair; };
	TMap<FString, FIntersect> Intersections;
	void WriteIntersections();
};
