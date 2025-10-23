using UnrealBuildTool;

public class SavegameSystemEditor : ModuleRules
{
    public SavegameSystemEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "BlueprintGraph"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "TraceLog",
                "SavegameSystem",
                "SlateCore"
            }
        );
    }
}