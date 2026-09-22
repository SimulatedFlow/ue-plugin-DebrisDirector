// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "DebrisDirectorSettings.h"

UDebrisDirectorSettings::UDebrisDirectorSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("DebrisDirector");
}

FName UDebrisDirectorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UDebrisDirectorSettings::GetSectionName() const
{
	return TEXT("DebrisDirector");
}

const UDebrisDirectorSettings& UDebrisDirectorSettings::Get()
{
	// GetDefault never returns null for a UDeveloperSettings; the check is here so a future refactor that
	// makes this a per-world object fails loudly rather than dereferencing nothing.
	const UDebrisDirectorSettings* Settings = GetDefault<UDebrisDirectorSettings>();
	check(Settings);
	return *Settings;
}
