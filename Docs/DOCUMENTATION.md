# DebrisDirector — Documentation

**Impact debris on a budget.**
Unreal Engine **5.8** · one runtime module · **Win64** · no editor module, no UMG, no Niagara, no Chaos.

---

## Table of contents

1. [What this plugin is, and what it is not](#1-what-this-plugin-is-and-what-it-is-not)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Five minutes to working debris](#4-five-minutes-to-working-debris)
5. [The four rules, in detail](#5-the-four-rules-in-detail)
6. [Project Settings](#6-project-settings)
7. [The counter box](#7-the-counter-box)
8. [Console commands](#8-console-commands)
9. [API reference](#9-api-reference)
10. [Code examples](#10-code-examples)
11. [The demo map](#11-the-demo-map)
12. [Automation tests](#12-automation-tests)
13. [Notes and limits](#13-notes-and-limits)
14. [Troubleshooting](#14-troubleshooting)
15. [Support](#15-support)

---

## 1. What this plugin is, and what it is not

DebrisDirector is **one service for all the short-lived remains of an action scene**: shards, shells,
impact decals, knocked-off parts. A game reports that something hit somewhere. The director decides
how much of that becomes a piece, what disappears when there is too much, and when the physics stops
costing anything.

**It is not a fragmentation system.** It produces no fragments from geometry — that is Chaos and its
Geometry Collections, and the two are complementary rather than alternatives. Chaos decides *how a
thing breaks*; DebrisDirector decides *how many of the results are allowed to exist*.

**It is not a particle system.** Niagara is faster than this for anything that does not need to
collide or stay — but a particle has no collision and leaves no remains, which is precisely the case
this plugin serves.

**It is not another object pool.** It contains a pool, because it has to, but pooling is the part
that is already solved. The product is the layer above it: a per-frame ceiling on creation, a ranked
order of removal, and a hard expiry date on physics.

---

## 2. Supported engine and platforms

| | |
|---|---|
| **Engine version** | Unreal Engine **5.8.0** (`"EngineVersion": "5.8.0"`) |
| **Platform** | **Win64** — the only platform in the module's `PlatformAllowList` |
| **Build configurations** | Editor Development, Game Development, Game **Shipping** — all three built and verified |
| **Modules** | one, `DebrisDirector`, `Type: Runtime`, `LoadingPhase: PreDefault` |
| **Engine dependencies** | `Core`, `CoreUObject`, `Engine`, `PhysicsCore`, `DeveloperSettings` (public), `RenderCore` (private) |
| **Third-party dependencies** | none |
| **Other plugin dependencies** | none |
| **Project type** | Blueprint-only and C++ projects both |
| **Replication** | none — debris is cosmetic, see [§13](#13-notes-and-limits) |

Nothing in the source is platform-specific: there is no Windows header, no intrinsic and no
platform-conditional code path anywhere in the module. Win64 is the platform the release was built,
run and measured on, and the descriptor states only what was verified. A licensee who wants another
platform adds it to `PlatformAllowList` in `DebrisDirector.uplugin` and rebuilds.

Verified with the real packaging command, not with a self-assessment:

```
RunUAT BuildPlugin -Plugin=<...>/DebrisDirector.uplugin -Package=<tmp> -Rocket -TargetPlatforms=Win64
→ BUILD SUCCESSFUL — zero errors, zero warnings
```

---

## 3. Installation

1. Copy the plugin into `YourProject/Plugins/DebrisDirector/`, or install it from the Epic Games
   Launcher / Fab library into the engine.
2. Enable **DebrisDirector** in *Edit → Plugins → Game Mechanics* and restart the editor.
3. C++ projects: add `"DebrisDirector"` to `PublicDependencyModuleNames` in your `Build.cs` if you
   intend to call the subsystem or the types directly.

   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] { "DebrisDirector" });
   ```

   Blueprint-only projects need nothing further — every entry point is exposed to Blueprint.

The module loads at `PreDefault`, so the subsystem, the piece actor and the `Debris.*` console
commands exist before the first game world is created. A level that opens with a scripted explosion
can therefore report impacts on its very first frame.

---

## 4. Five minutes to working debris

### 4.1 Make a profile

*Content Browser → right-click → Miscellaneous → Data Asset → **Debris Profile***.

| Field | Meaning |
|---|---|
| `SurfaceType` | the name a request asks for — `Stone`, `Metal`, `Wood` |
| `PhysicalSurfaces` | physical surfaces this profile also answers for |
| `Meshes` | the meshes this surface produces, each with a pick weight |
| `ScaleRange` | uniform scale range for one unmerged piece |
| `MaterialOverrides` | materials applied over the mesh's own, by element index |
| `FadeParameterName` | scalar parameter the fade is written to, `0` solid `1` gone (default `DebrisFade`) |
| `bCollides` / `CollisionProfileName` | pieces of this profile collide and simulate |
| `MaxSimSeconds` | how long they may simulate; negative takes the project default |
| `ImpulseScale` / `LaunchConeAngle` / `MaxAngularVelocity` | how a piece leaves the surface |
| `LifetimeSeconds` | how long they lie around; negative takes the project default |
| `Importance` | 0..1, the first term of the eviction order |
| `DecalMaterial` / `DecalSize` / `DecalLifetimeSeconds` | the mark left at the impact point |

Useful importance values are a small, deliberate spread: bullet chips at `0.3`, structural debris at
`0.6`, anything scripted at `0.9`. Setting everything to `1.0` protects nothing — it just turns the
ranking back into distance and age.

### 4.2 Register it

**Project Settings → Plugins → DebrisDirector → Profiles**: add the profile to `StartupProfiles` and
pick a `DefaultProfile`. They are loaded when a world starts, so a request can name a surface that
nothing else in the level has referenced yet — without this, the first `Stone` request in a cooked
build can silently fall back to the default.

At runtime, `RegisterProfile` on the subsystem or on the Blueprint library does the same thing. It is
harmless to call twice.

### 4.3 Show the numbers

Set your game mode's **HUD Class** to `Debris Director HUD`. If your project already has a HUD class
you do not want to reparent, turn on `bAutoDrawStatsOnAnyHUD` in Project Settings instead — the same
box is then drawn through `AHUD::OnHUDPostRender`. The two paths know about each other and cannot
stack.

### 4.4 Give the pieces a fade material

Add a scalar parameter named exactly as the profile's `FadeParameterName` (default `DebrisFade`) to
the master material your rubble meshes use, and wire it into opacity — `1 − DebrisFade` into
**Opacity** on a translucent material, or into a dither/masked opacity for an opaque one.

A material without the parameter simply does not fade, and nothing warns about it every frame. The
piece then blinks out, which is the one visual defect this plugin set out to remove — so it is worth
the two nodes.

### 4.5 Report impacts

C++:

```cpp
#include "DebrisDirectorSubsystem.h"
#include "DebrisDirectorTypes.h"

void AMyWeapon::OnTraceHit(const FHitResult& Hit)
{
    FDebrisRequest Request;
    Request.Location    = Hit.ImpactPoint;
    Request.Normal      = Hit.ImpactNormal;
    Request.Impulse     = 400.0f;
    Request.SurfaceType = TEXT("Stone");   // or leave empty and let the physical material decide

    if (UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this))
    {
        Director->RequestBurst(Request);
    }
}
```

Blueprint: **Report Impact At** on the *Debris Director Statics* library, or drop a **Debris Director
Component** on the actor and call **Report Hit**, which reads the physical surface off the hit's
physical material for you.

Nothing is created at the moment you report. The request waits for the end of the frame it arrived
in, because merging five impacts into one piece is only possible once all five are known — a spawner
that acts on the first request it sees has already spent the budget by the time the fifth arrives.

---

## 5. The four rules, in detail

### 5.1 The spawn budget is not a cut-off

`FDebrisPlanRules::SpawnBudget` (default **24**) is the hard ceiling on pieces created in one frame.

Here is the arithmetic it exists for. A grenade in a corridor typically reports around **40 impacts
inside a single frame**, one per trace that found a wall. Without merging, 16 of them are not there —
and they are missing from whichever side of the blast the code happened to reach last, so the gap in
the pattern is asymmetric and immediately visible. That is worse than fewer pieces everywhere.

With merging, **none of the 40 is missing**:

1. **Cell merge.** Requests are bucketed by grid cell (`MergeCellSize`, default 120 cm) *and*
   surface. Everything in one bucket becomes one piece, placed at the impulse-weighted centre of the
   bucket — so the merged piece sits where the hardest hit landed, not at the geometric middle of a
   spray.

2. **Fold into neighbour.** If that still leaves more pieces than the budget, the survivors are the
   most important ones, and every piece that did not survive is folded into its nearest surviving
   neighbour of the same surface. Its count is added; the piece it joined grows.

The size of a merged piece is the **cube root** of how many impacts it stands for, clamped to
`MaxScaleMultiplier` (default 3). Eight merged impacts double the piece; they do not multiply it by
eight. Volume goes as the cube of length, and scaling linearly would turn a grenade into a boulder.

**The invariant:** the sum of `MergedCount` over a plan is always exactly the number of requests that
went in. That is asserted by `DebrisDirector.Plan.OverBudgetLosesNothing`, not merely claimed here.

Setting `bMergeEnabled` to false switches all of this off: requests above the budget are dropped and
counted as dropped. That path exists so the difference can be seen on one screen — it is not an
alternative worth shipping.

### 5.2 Eviction by importance and distance

`PopulationCap` (default **400**) is how many pieces may exist at once. At the ceiling a new piece is
still created and the worst-ranked old one is taken away, because debris is the one thing a player
expects the instant they pull the trigger. The honest way to pay for that is to take something else
away in an order you can defend.

`ScoreForEviction` is the sum of five terms; higher goes first:

| Term | Default weight | Effect |
|---|---|---|
| unimportance | `1000 × (1 − Importance)` | dominates — the ordering is *importance first* |
| distance | `100 × clamp(d / 2000 cm, 0, 4)` | decides between equally important pieces |
| age | `1 × seconds` | tie-breaker only |
| on screen | `− 250` | a discount, not an exemption |
| still simulating | `+ 10` | among equals, the expensive ones leave first |

Being looked at is worth less than being important on purpose: a worthless chip in the middle of the
screen should still go before an important piece behind the player, or a player standing still in a
firefight would fill the ceiling with rubbish they happen to be facing and then have nothing left for
anything that mattered.

Distance stops counting past four reference lengths — beyond a certain distance everything is equally
out of sight, and an unbounded term would let a very distant important piece lose to a nearby
worthless one.

The sort is **stable**, so pieces that score identically are evicted in creation order. Without that,
a scene sitting exactly on the ceiling would pick a different one of two identical chips every frame
and both would flicker.

`MaxEvictionsPerFrame` (default 48) bounds the work, so the frame that lowers the ceiling from 400 to
100 is not itself the spike this plugin exists to remove.

### 5.3 Physics with an expiry date

Every piece simulates for at most `MaxSimSeconds` (default **3**) and is then frozen where it lies
with `SetSimulatePhysics(false)`. Its collision drops to query-only: it still blocks a trace and still
stops a player walking through it; what it no longer does is take part in the solver.

This is the rule most hand-written debris systems do not have, and it is where they lose their frame.
Engine sleep thresholds settle a body that has genuinely come to rest — they do nothing for the piece
balanced on a slope, jittering against a corner, or resting on another piece that is itself still
moving. Those never sleep, and a hundred of them is a permanent slice of every physics tick.

**A hundred sleeping bodies are cheap. A hundred *almost* sleeping ones are not.**

The counter box's `Bodies … simulating … sleeping` line is that difference, live. In a settled scene
the left number goes to zero and stays there. `bPhysicsSleepEnabled` turns the rule off so the two can
be compared on one screen.

Velocities are zeroed before a body is frozen. A body frozen while still carrying velocity keeps it in
its state and hands it straight back the moment anything switches simulation on again.

### 5.4 Fade instead of vanish

A piece that is evicted, or whose lifetime ran out, fades over `FadeSeconds` (default **0.35 s**) by
writing its profile's `FadeParameterName` on a dynamic material instance, and is then returned to its
pool.

The dynamic material instances are built once per material slot on first activation and then reused —
a MID per piece per spawn is not free — and an unchanged fade value costs no material update at all.

A fading piece is **not** counted against the population ceiling; it has already given its slot up.
The counter box shows it separately as `(+n fading out)` rather than hiding it inside the population
number.

---

## 6. Project Settings

**Project Settings → Plugins → DebrisDirector**

| Group | Setting | Default | Meaning |
|---|---|---|---|
| General | `bEnabled` | `true` | off counts requests and creates nothing |
| Budget | `PlanRules.SpawnBudget` | `24` | pieces one frame may create |
| Budget | `PlanRules.MergeCellSize` | `120 cm` | how far apart two hits must be to count separately |
| Budget | `PlanRules.MaxScaleMultiplier` | `3.0` | how large a merged piece may get |
| Budget | `PlanRules.bMergeEnabled` | `true` | merge over budget, or drop |
| Budget | `PlanRules.MinImpulse` | `0` | requests weaker than this never become anything |
| Budget | `PopulationCap` | `400` | pieces that may exist at once |
| Budget | `MaxEvictionsPerFrame` | `48` | bounds the eviction work in one frame |
| Budget | `MaxPendingRequests` | `2048` | queue ceiling; above it requests are dropped and counted |
| Budget | `EvictionWeights` | see [§5.2](#52-eviction-by-importance-and-distance) | the order pieces are taken away in |
| Physics | `bPhysicsSleepEnabled` | `true` | the expiry date |
| Physics | `DefaultMaxSimSeconds` | `3 s` | for profiles that do not set their own |
| Lifetime | `DefaultLifetimeSeconds` | `20 s` | for profiles that do not set their own |
| Lifetime | `FadeSeconds` | `0.35 s` | how long a piece takes to leave |
| Decals | `bDecalsEnabled` | `true` | let profiles drop decals at all |
| Decals | `DecalCap` | `128` | decals are capped oldest-first |
| Decals | `DefaultDecalLifetimeSeconds` | `12 s` | for profiles that do not set their own |
| Pooling | `MaxPooledPerProfile` | `256` | parked pieces one profile may keep |
| Pooling | `bParkPooledPieces` / `ParkLocation` | `true` / `Z −100000` | move parked pieces out of the level |
| Profiles | `StartupProfiles` / `DefaultProfile` | — | loaded when a world starts |
| Presentation | `bShowStatsByDefault` | `true` | start with the counter box on |
| Presentation | `bAutoDrawStatsOnAnyHUD` | `false` | draw the box through any HUD class |

The settings are `config = Game, defaultconfig`, so they are written to `DefaultGame.ini` and travel
with the project.

---

## 7. The counter box

`ADebrisDirectorHUD` draws every number this plugin claims, on `UCanvas`:

```
DebrisDirector
Spawned        24 this frame   (budget 24)   [budgeted]
Requests       40 in   16 merged   0 dropped
Merge          ON  - over budget becomes bigger
Population     400 / 400   (+6 fading out)
Bodies         31 simulating   369 sleeping   expiry ON
Evicted        24 this frame   118.0 / s
Pool           hits 1204   new 400   parked 37
Decals         128 / 128
Totals         1604 spawned   642 merged   1204 evicted   0.184 ms
```

Canvas rather than UMG for two reasons that pull the same way. It has to survive a cooked Shipping
build — `DrawDebug` is compiled out there and a debug widget is usually stripped, a Canvas overlay is
not. And anything that has to be *clicked* belongs in UMG instead: an `AHUD` hit box is tested against
`UGameViewportClient::GetMousePosition()`, which reports nothing on a machine with no mouse attached —
a capture rig, a build agent, a headless test — so the click never lands. Numbers here, controls in a
widget.

Everything drawn is read from the director on the frame it is drawn. Nothing is cached, so the box
cannot claim one thing while the director does another. `LastStatsDrawFrame` prevents the HUD class
and the `OnHUDPostRender` path from stacking two boxes on top of each other.

Position and size: `StatsBoxOrigin` (default `28, 90`) and `StatsBoxWidth` (default `470`) on the HUD
class.

---

## 8. Console commands

| Command | Does |
|---|---|
| `Debris.Show 0\|1` | show or hide the counter box; no argument toggles |
| `Debris.Budget <n>` | set the per-frame spawn budget; no argument prints it |
| `Debris.Cap <n>` | set the population ceiling; lowering it evicts the excess |
| `Debris.Clear` | take every piece and decal away at once |
| `Debris.Stress <n> [radius]` | report *n* impacts in front of the viewer in a single frame |
| `Debris.Stats` | print the counters to the log |

`Debris.Stress 40` against `Debris.Budget 4` and then `Debris.Budget 128` is the whole product in two
commands, with the counter box in shot.

---

## 9. API reference

### 9.1 `UDebrisDirectorSubsystem` (`UTickableWorldSubsystem`)

The service. One per world, game and PIE only — it does not run in an editor world, because filling a
level designer's viewport with rubble is not help.

```cpp
static UDebrisDirectorSubsystem* Get(const UObject* WorldContextObject);
```

| Member | Does |
|---|---|
| `bool RequestBurst(const FDebrisRequest&)` | report one impact; false when the queue is full or the director is off |
| `int32 RequestBurstMany(const TArray<FDebrisRequest>&)` | report a whole burst — the shape the director wants |
| `int32 RequestStress(int32 Count, float Radius = 400)` | fabricate a burst in front of the viewer |
| `void Clear()` | remove every piece and decal at once, with no fade |
| `SetBudget(int32)` / `GetBudget()` | pieces per frame |
| `SetPopulationCap(int32)` / `GetPopulationCap()` | pieces at once; lowering it evicts the excess |
| `GetPopulation()` / `GetFadingCount()` | alive against the ceiling, and on the way out |
| `GetSimulatingCount()` / `GetSleepingCount()` | the physics line |
| `GetPlanRules()` / `SetPlanRules(const FDebrisPlanRules&)` | the planning rules wholesale |
| `SetMergeEnabled(bool)` / `IsMergeEnabled()` | the merge switch |
| `SetPhysicsSleepEnabled(bool)` / `IsPhysicsSleepEnabled()` | the expiry-date switch |
| `SetEnabled(bool)` / `IsEnabled()` | run the director at all |
| `SetShowStats(bool)` / `AreStatsShown()` | the counter box |
| `RegisterProfile(UDebrisProfile*)` | make a profile answerable; harmless to call twice |
| `ResolveProfile(const FDebrisRequest&)` | which profile a request would use |
| `GetRegisteredProfiles(TArray<UDebrisProfile*>&)` | every profile the director knows |
| `FDebrisDirectorStats GetStats()` | everything the counter box draws |
| `DrawStatsBox(UCanvas*, const FVector2D&, float)` | draw the box yourself, from your own HUD |
| `LogStats()` | what `Debris.Stats` runs |

Blueprint-assignable events:

```cpp
FDebrisPieceSpawned OnPieceSpawned;   // (ADebrisPiece* Piece, UDebrisProfile* Profile)
FDebrisPieceEvicted OnPieceEvicted;   // (ADebrisPiece* Piece)
```

### 9.2 `UDebrisDirectorStatics` (`UBlueprintFunctionLibrary`)

**The world-free half is the important half.**

| Function | Does |
|---|---|
| `PlanBurst(Requests, Rules, OutPlanned, OutReport)` | the merge, in full |
| `ScoreForEviction(Candidate, Weights) → float` | one piece's evictability; higher goes first |
| `RankForEviction(Candidates, Weights, Count, OutIndices)` | the worst-ranked *Count*, worst first, stable |
| `PlanEvictionCount(Population, Incoming, Cap) → int32` | how many have to go |
| `AdvancePiece(State, DeltaSeconds) → EDebrisLifetimeEvent` | the simulate / freeze / fade / gone state machine |
| `BeginFade(State) → bool` | start the fade on a piece that is not already fading |
| `MergedScaleMultiplier(Count, Max) → float` | the cube root, clamped |
| `MergeCellForLocation(Location, CellSize) → FIntVector` | the merge grid key |

These are static and take nothing but plain structs, which is why they have tests at all: a
`UTickableWorldSubsystem` cannot be created in an automation test, so every rule written inside one
ships unverified. The running director calls exactly these functions — there is no second
implementation that agrees with the first until somebody edits one of the two.

The world entries forward to the subsystem, so a Blueprint never has to fetch it:
`ReportImpact`, `ReportImpactAt`, `ReportImpacts`, `RequestStress`, `Clear`, `SetBudget`, `GetBudget`,
`SetPopulationCap`, `GetPopulationCap`, `SetMergeEnabled`, `IsMergeEnabled`, `SetPhysicsSleepEnabled`,
`IsPhysicsSleepEnabled`, `SetShowStats`, `AreStatsShown`, `GetStats`, `GetPopulation`,
`GetSimulatingCount`, `GetSleepingCount`, `RegisterProfile`.

### 9.3 `UDebrisProfile` (`UPrimaryDataAsset`)

What one kind of surface leaves behind, and nothing else. Fields are listed in
[§4.1](#41-make-a-profile). Two queries: `PickMesh(float Roll01)` and `GetTotalPickWeight()`.

A `UPrimaryDataAsset` rather than a DataTable row because the interesting fields are asset references,
and a table of asset references is a table that either loads everything or nothing.

### 9.4 `UDebrisDirectorComponent` (`UActorComponent`)

The convenient façade: drop it on an actor and its hits become debris.

| Member | Does |
|---|---|
| `ReportImpact(Location, Normal, Impulse) → bool` | one impact, with this component's surface / importance / scaling |
| `ReportImpactSimple(Location, Normal) → bool` | the same, using `DefaultImpulse` |
| `ReportHit(const FHitResult&, Impulse) → bool` | reads the physical surface off the hit's physical material |
| `ReportRadialBurst(Origin, Radius, Count, Impulse) → int32` | an explosion, in one call |
| `MakeRequest(Location, Normal, Impulse) → FDebrisRequest` | build the request without sending it |
| `SetReportingEnabled(bool)` / `IsReportingEnabled()` | stop or resume without removing the component |
| `GetReportedCount() → int32` | impacts passed on since BeginPlay |

Properties: `SurfaceType`, `Importance`, `ImpulseScale`, `DefaultImpulse`, `bWantsDecals`,
`bAutoReportPointDamage`, `DamageToImpulse`. Set `bAutoReportPointDamage` and the owner's
`OnTakePointDamage` becomes debris without a single new node.

> **Naming note.** Nothing in this component is called `IsRegistered()`. `UActorComponent` already has
> that method, it is not virtual, and a component that shadows it compiles cleanly and then answers
> the engine's registration question with its own unrelated business logic. It is
> `IsReportingEnabled()` here for exactly that reason.

### 9.5 `ADebrisPiece` (`AActor`)

One pooled piece: a movable static mesh component that can simulate, be frozen, fade over a material
parameter and be handed back. `Blueprintable`, so a project can subclass it for an impact sound or a
trail, but never required to — the director spawns this class directly.

`ActivateFor(...)`, `FreezePhysics()`, `SetFadeAmount(float)`, `ParkForPool(bool, const FVector&)`,
`GetActiveProfile()`.

An actor rather than an instanced-static-mesh slot, deliberately. Instancing wins on draw calls and
loses everything else here: an ISM instance cannot simulate physics, cannot carry its own dynamic
material for a fade, and cannot be collided with. Debris that neither bounces nor can be shot off a
ledge is a decal with extra steps. The cost instancing would have saved is instead removed by the two
rules above — the piece stops simulating on a timer, and the population never grows past a ceiling —
and both are visible on the counter box.

### 9.6 `ADebrisDirectorHUD` (`AHUD`)

The counter box. `bShowStats`, `StatsBoxOrigin`, `StatsBoxWidth`, `ToggleStats()`. See [§7](#7-the-counter-box).

### 9.7 `UDebrisDirectorSettings` (`UDeveloperSettings`)

Project-wide budgets and ceilings. See [§6](#6-project-settings).

### 9.8 Types

| Type | Is |
|---|---|
| `FDebrisRequest` | one impact: location, normal, impulse, surface name, physical surface, importance, decal wanted |
| `FDebrisPlannedSpawn` | one piece the plan decided on, with `MergedCount` and `ScaleMultiplier` |
| `FDebrisPlanRules` | budget, cell size, max scale, merge on/off, min impulse |
| `FDebrisPlanReport` | requests in, spawns out, merged away, dropped, budget saturated |
| `FDebrisEvictionCandidate` | one live piece as the ranking sees it — pointer-free and world-free |
| `FDebrisEvictionWeights` | the five weights behind the eviction order |
| `FDebrisPieceState` | age, sim seconds, lifetime, `MaxSimSeconds`, fade, importance, flags |
| `EDebrisLifetimeEvent` | `None`, `FrozePhysics`, `StartedFade`, `Expired` |
| `FDebrisDirectorStats` | every number the counter box draws |

---

## 10. Code examples

### 10.1 A whole explosion in one call

Report the burst together rather than one impact at a time. It is the shape the director wants —
`RequestBurstMany` queues them all for the same frame, so they can merge with each other.

```cpp
#include "DebrisDirectorSubsystem.h"

void AMyGrenade::Detonate(const FVector& Origin, float Radius)
{
    UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this);
    if (!Director)
    {
        return;
    }

    TArray<FDebrisRequest> Requests;
    Requests.Reserve(40);

    for (int32 Index = 0; Index < 40; ++Index)
    {
        const FVector Direction = FMath::VRand();

        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(
                Hit, Origin, Origin + Direction * Radius, ECC_Visibility))
        {
            continue;
        }

        FDebrisRequest& Request = Requests.AddDefaulted_GetRef();
        Request.Location   = Hit.ImpactPoint;
        Request.Normal     = Hit.ImpactNormal;
        Request.Impulse    = 900.0f;
        Request.Importance = 0.7f;                       // a scripted blast outlives corridor chips
        Request.PhysicalSurface = UGameplayStatics::GetSurfaceType(Hit);
    }

    // 40 requests, budget 24: 24 pieces are created and every one of the 40 is represented.
    Director->RequestBurstMany(Requests);
}
```

### 10.2 Debris straight out of a hit result, with no new names

`ReportHit` takes the physical surface off the hit's physical material, so a project that already
paints its geometry gets matching debris without naming a surface anywhere.

```cpp
// In the actor's constructor
DebrisReporter = CreateDefaultSubobject<UDebrisDirectorComponent>(TEXT("DebrisReporter"));
DebrisReporter->Importance   = 0.3f;   // corridor chips
DebrisReporter->ImpulseScale = 1.0f;

// Wherever the actor is hit
void AMyWall::HandleShot(const FHitResult& Hit, float Impulse)
{
    DebrisReporter->ReportHit(Hit, Impulse);
}
```

### 10.3 Point damage becomes debris with no new nodes at all

```cpp
DebrisReporter->bAutoReportPointDamage = true;
DebrisReporter->DamageToImpulse        = 20.0f;   // 25 damage → 500 impulse
```

The component binds to the owner's `OnTakePointDamage` for the lifetime of play. A location, a normal
and an amount are exactly an impact request.

### 10.4 Turn the budget down for a low-end platform

```cpp
void AMyGameMode::ApplyDebrisQuality(int32 QualityLevel)
{
    UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this);
    if (!Director)
    {
        return;
    }

    switch (QualityLevel)
    {
    case 0:  Director->SetBudget(4);   Director->SetPopulationCap(60);  break;
    case 1:  Director->SetBudget(24);  Director->SetPopulationCap(400); break;
    default: Director->SetBudget(128); Director->SetPopulationCap(2000); break;
    }
}
```

Lowering the ceiling below the current population does not clear the level — it evicts the excess in
ranked order, at most `MaxEvictionsPerFrame` per frame.

### 10.5 Replace the plan rules wholesale

```cpp
FDebrisPlanRules Rules = Director->GetPlanRules();
Rules.SpawnBudget   = 48;
Rules.MergeCellSize = 200.0f;    // a top-down game counts hits as one from further apart
Rules.MinImpulse    = 50.0f;     // ignore the very lightest taps entirely
Director->SetPlanRules(Rules);
```

### 10.6 Use the planner without a world

Both decisions are pure functions over plain structs, so a tool, a test or a design experiment can
drive them with no engine world at all.

```cpp
#include "DebrisDirectorStatics.h"

TArray<FDebrisRequest> Requests;   // 40 fabricated impacts
FDebrisPlanRules Rules;            // defaults: budget 24, cell 120, merge on

TArray<FDebrisPlannedSpawn> Planned;
FDebrisPlanReport Report;
UDebrisDirectorStatics::PlanBurst(Requests, Rules, Planned, Report);

check(Planned.Num() <= Rules.SpawnBudget);      // the budget holds

int32 Represented = 0;
for (const FDebrisPlannedSpawn& Spawn : Planned)
{
    Represented += Spawn.MergedCount;
}
check(Represented == Requests.Num());           // and nothing was lost
```

### 10.7 Drive the lifetime state machine

```cpp
FDebrisPieceState State;
State.MaxSimSeconds   = 3.0f;
State.LifetimeSeconds = 20.0f;

// Ten simulated seconds in a fraction of a millisecond
for (int32 Step = 0; Step < 600; ++Step)
{
    switch (UDebrisDirectorStatics::AdvancePiece(State, 1.0f / 60.0f))
    {
    case EDebrisLifetimeEvent::FrozePhysics:  Piece->FreezePhysics();  break;
    case EDebrisLifetimeEvent::StartedFade:   /* fade has begun */     break;
    case EDebrisLifetimeEvent::Expired:       Recycle(Piece);          break;
    default: break;
    }

    Piece->SetFadeAmount(State.GetFadeAlpha());
}
```

`AdvancePiece` returns **at most one event per call**, so a caller never has to guess which of two
things to react to first.

### 10.8 Draw the counter box from your own HUD class

If you would rather not set `bAutoDrawStatsOnAnyHUD`, call the subsystem directly:

```cpp
void AMyHUD::DrawHUD()
{
    Super::DrawHUD();

    if (const UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this))
    {
        Director->DrawStatsBox(Canvas, FVector2D(28.0f, 90.0f), 470.0f);
    }
}
```

### 10.9 React to a piece being created or evicted

```cpp
void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this))
    {
        Director->OnPieceSpawned.AddDynamic(this, &AMyGameMode::HandlePieceSpawned);
        Director->OnPieceEvicted.AddDynamic(this, &AMyGameMode::HandlePieceEvicted);
    }
}

void AMyGameMode::HandlePieceSpawned(ADebrisPiece* Piece, UDebrisProfile* Profile)
{
    // e.g. play a settle sound for heavy profiles only
}
```

### 10.10 Register a profile at runtime

```cpp
if (UDebrisDirectorSubsystem* Director = UDebrisDirectorSubsystem::Get(this))
{
    Director->RegisterProfile(MyStoneProfile);   // answerable under its SurfaceType and physical surfaces
}
```

---

## 11. The demo map

`Content/DebrisDirector/Maps/L_DebrisDirectorDemo` — a lit hall seen from slightly above, so the
pieces stay in frame and can be counted. Everything ships in the single pack folder
`Content/DebrisDirector/`, with no references to anything outside the plugin.

| Asset | Does |
|---|---|
| `Blueprints/BP_DebrisDemoGameMode` | registers the three profiles and sets budget 24 / cap 400 on BeginPlay |
| `Blueprints/BP_DebrisDemoHUD` | child of `ADebrisDirectorHUD`; creates the control panel and sets UI-only input |
| `Blueprints/BP_DebrisImpactTarget` | a crate with a `UDebrisDirectorComponent`; a timer calls `ReportImpactSimple`, so the counters stay alive with no clicking. Three are placed |
| `UI/WBP_DebrisDemoPanel` | twelve buttons, each **one** call into `UDebrisDirectorStatics`: single / rifle / grenade / stress, merge on / off, budget 4 / 24 / 128, freeze on / off, clear |
| `Profiles/DA_Debris_Stone`, `_Metal`, `_Wood` | the three surface profiles |
| `Materials/M_DebrisPiece` | translucent, `DebrisFade → 1−x → Opacity` — the fade wiring, in two nodes |
| `Materials/M_DebrisImpactDecal` | the deferred impact decal |
| `Materials/M_DebrisDemoSurface` + `MI_*` | the hall's stone / metal / wood surfaces |

The two shots worth taking are **the same grenade with merging on and with merging off**, and **the
population sitting at the ceiling with eviction running**. Both need the counter box in frame — that
is the whole point of it.

> **Capture note.** `SpawnedThisFrame` and `RequestsThisFrame` reset every tick, so a screen grab at
> normal frame rate almost never lands on the `[budgeted]` frame. Set a fixed frame rate of 2–3 fps
> while capturing the budget shots. Take the beauty shots with the lock off — the director's
> `TickMilliseconds` reads high under the lock, because a 0.3–0.5 s delta makes dozens of bodies cross
> `MaxSimSeconds` in one step.

---

## 12. Automation tests

`DebrisDirector.*`, run from *Tools → Session Frontend → Automation*:

| Test | Asserts |
|---|---|
| `DebrisDirector.Plan.BudgetAndMerge` | the budget holds; impacts in one cell become one larger piece; cube-root scaling |
| `DebrisDirector.Plan.OverBudgetLosesNothing` | 40 requests against a budget of 24 give 24 pieces standing for all 40; with merging off, 16 are dropped |
| `DebrisDirector.Plan.Edges` | `MinImpulse`, empty input, different surfaces in one cell, floor division either side of the origin |
| `DebrisDirector.Evict.PrefersFarAndUnimportant` | far and unimportant first; importance beats distance; on-screen survives; asking for more than exist is safe |
| `DebrisDirector.Evict.CapIsNeverExceeded` | 1000 requests a frame for 100 frames, never past the ceiling |
| `DebrisDirector.Lifetime.PhysicsExpiry` | freeze fires once, on time, and stays; expiry off never freezes; fade starts once and ends once |

---

## 13. Notes and limits

- **Win64.** The plugin descriptor lists Win64 only; nothing in the code is platform-specific, but
  nothing else was built or measured for this release.
- **Not replicated.** Debris is cosmetic. Replicating four hundred rigid bodies would cost more
  bandwidth than the game they are decorating; every client makes its own from the impacts it already
  receives. This also means two clients see different rubble — which is correct for debris and wrong
  for anything a player can be blocked by in a competitive sense.
- **Game and PIE only.** The subsystem does not run in an editor world. A level being built is not a
  level being played.
- **One piece per impact.** A profile has no "pieces per impact" multiplier on purpose: a hard budget
  you can multiply by a per-profile count is not a hard budget. Variety comes from the mesh list and
  the scale range.
- **Decals are capped oldest-first**, not by the ranked eviction the pieces get. A decal costs a
  fraction of a rigid body and never simulates, so ranking them would spend more on the decision than
  on the thing being decided.
- **One frame of latency.** A request reported by an actor that ticks after the director is serviced
  on the next frame. This is what makes merging possible at all.
- **No editor module.** Nothing can go missing between what a designer places in the editor and what
  the packaged game runs.

---

## 14. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Nothing appears at all | No profile resolved. Add a `DefaultProfile` in Project Settings, or check that the profile's `SurfaceType` matches what the request names. `ResolveProfile` tells you which one a request would use. |
| Nothing appears in the editor viewport | Expected — the subsystem is game and PIE only. |
| Pieces blink out instead of fading | The profile's `FadeParameterName` does not exist on the material. Add the scalar parameter and wire it into opacity ([§4.4](#44-give-the-pieces-a-fade-material)). |
| The counter box is not drawn | Game mode's HUD class is not `ADebrisDirectorHUD`. Either set it, or turn on `bAutoDrawStatsOnAnyHUD`, or call `DrawStatsBox` yourself ([§10.8](#108-draw-the-counter-box-from-your-own-hud-class)). |
| `Dropped` is not zero | Either `MinImpulse` is rejecting requests, or merging is off and the frame went over budget, or more than `MaxPendingRequests` arrived in one frame. |
| `Simulating` never falls | `bPhysicsSleepEnabled` is off, or the profile's `MaxSimSeconds` is very large. |
| `New allocations` keeps climbing | `MaxPooledPerProfile` is smaller than the share of the ceiling that profile holds. Raise it. |
| Pieces vanish while the player watches | The ceiling is too low for the scene, so eviction is reaching pieces on screen. Raise `PopulationCap`, or raise the importance of what matters. |
| A merged piece looks like a boulder | Lower `MaxScaleMultiplier`, or lower `MergeCellSize` so fewer impacts land in one cell. |

---

## 15. Support

Documentation: <https://wiki.teufel-engineering.com/en/DebrisDirector/documentation>
Support: <mailto:teufelsilvan@gmail.com>

© 2026 Silvan Teufel. All Rights Reserved.
