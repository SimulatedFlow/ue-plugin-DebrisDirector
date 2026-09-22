// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisProfile.h"

#include "Engine/StaticMesh.h"

UDebrisProfile::UDebrisProfile()
{
}

FPrimaryAssetId UDebrisProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("DebrisProfile"), GetFName());
}

float UDebrisProfile::GetTotalPickWeight() const
{
	float Total = 0.0f;
	for (const FDebrisMeshEntry& Entry : Meshes)
	{
		if (Entry.Mesh && Entry.PickWeight > 0.0f)
		{
			Total += Entry.PickWeight;
		}
	}
	return Total;
}

UStaticMesh* UDebrisProfile::PickMesh(float Roll01) const
{
	const float Total = GetTotalPickWeight();
	if (Total <= 0.0f)
	{
		return nullptr;
	}

	// The roll comes in from the caller rather than being drawn here, so that a replay, a test or a
	// deterministic capture can produce the same debris twice.
	float Cursor = FMath::Clamp(Roll01, 0.0f, 1.0f) * Total;

	for (const FDebrisMeshEntry& Entry : Meshes)
	{
		if (!Entry.Mesh || Entry.PickWeight <= 0.0f)
		{
			continue;
		}

		Cursor -= Entry.PickWeight;
		if (Cursor <= 0.0f)
		{
			return Entry.Mesh;
		}
	}

	// Floating point can leave a sliver at the very top of the range. Fall back to the last valid entry
	// rather than to null, because null here would read as "this profile has no meshes", which is a
	// different thing entirely.
	for (int32 Index = Meshes.Num() - 1; Index >= 0; --Index)
	{
		if (Meshes[Index].Mesh && Meshes[Index].PickWeight > 0.0f)
		{
			return Meshes[Index].Mesh;
		}
	}

	return nullptr;
}
