# DebrisDirector — Impact Debris On A Budget

One pooled service for everything an action scene leaves behind: shards, shells, impact decals,
knocked-off parts. The game only reports *"something hit here"*. DebrisDirector decides **how much**
of that becomes a piece, **what disappears** when there is too much, and **when the physics stops
costing anything**.

**Unreal Engine 5.8 · one runtime module · Win64 · no UMG, no Niagara, no Chaos, no editor module.**

---

## This is not another object pool

Fab already has object pools, and good ones. What none of them has is the layer above the pool:

| Question | A pool answers | DebrisDirector answers |
|---|---|---|
| How many pieces may this frame create? | all of them | at most `SpawnBudget` (default 24) |
| What happens to the rest? | they are created anyway | they are **merged**, not dropped |
| What goes when the scene is full? | the oldest | the **least important far-away** one |
| When does physics stop? | when the engine decides | after `MaxSimSeconds` (default 3), always |
| How does a piece leave? | it vanishes | it fades over a material parameter |

**DebrisDirector breaks nothing apart.** It produces no fragments from geometry — that is Chaos — and
it does not replace particles — that is Niagara, which has no collision and leaves no remains. It
manages the count, the lifetime and the cost of the pieces *you* bring.

---

## The four rules

### 1. A hard per-frame spawn budget that is not a cut-off

A grenade in a corridor typically reports about **40 impacts inside a single frame**. The budget is
**24**. Without merging, 16 of those impacts are simply not there — and worse, they are missing from
whichever side of the blast the code reached last, so the hole in the pattern is asymmetric and
obvious.

With merging, **none of the 40 is missing.** Impacts that landed in the same grid cell become one
piece that is larger by the cube root of how many they were; if that is still over budget, the
remainder are folded into their nearest neighbour. The frame creates 24 objects and the wall is fully
marked. That is why an explosion costs no more frame time than a single shot.

The invariant — *the sum of `MergedCount` over a plan always equals the number of requests that went
in* — is asserted by the automation tests, not claimed in a bullet point.

### 2. Eviction by importance and distance

At the population ceiling (default **400**) the new piece is still created and the **worst-ranked old
one** is taken away: least important first, then furthest from the camera, then oldest — and a piece
inside the player's view is the last thing to go while there is anything else to take.

That order is the difference from a ring buffer, which takes the oldest piece and therefore takes the
one the player is standing over.

### 3. Physics with an expiry date

Every piece simulates for at most `MaxSimSeconds` and is then frozen where it lies with
`SetSimulatePhysics(false)`.

Engine sleep thresholds only settle a body that has genuinely come to rest. The piece balanced on a
slope, jittering against a corner, or resting on another piece that is itself still moving **never**
settles — and a hundred of those is a permanent slice of every physics tick. An expiry date has no
such failure mode. **A hundred sleeping bodies are cheap; a hundred almost-sleeping ones are not.**

### 4. Fade instead of vanish

An evicted piece leaves over a short scalar material parameter, so nothing blinks out in front of the
player.

---

## The counter box

A budget plugin without visible numbers is unprovable on a screenshot, so `ADebrisDirectorHUD` draws
all of them on `UCanvas` — which survives a cooked Shipping build, unlike a debug widget:

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

A project that already has its own HUD class turns on `bAutoDrawStatsOnAnyHUD` in Project Settings
instead of reparenting it.

---

## Quick start

1. Enable the plugin, restart the editor.
2. Make a `Debris Profile` data asset per surface — meshes with pick weights, a scale range, an
   optional decal material, importance, lifetime.
3. List them under **Project Settings → Plugins → DebrisDirector → Startup Profiles**, and pick a
   default.
4. Set your game mode's HUD class to `Debris Director HUD` (or turn on `bAutoDrawStatsOnAnyHUD`).
5. Report impacts. From a trace:

```cpp
FDebrisRequest Request;
Request.Location = Hit.ImpactPoint;
Request.Normal   = Hit.ImpactNormal;
Request.Impulse  = 400.0f;
Request.SurfaceType = TEXT("Stone");
UDebrisDirectorSubsystem::Get(this)->RequestBurst(Request);
```

From Blueprint, `Report Impact At` on the `Debris Director Statics` library, or drop a
`Debris Director Component` on the actor and call `Report Hit` — which reads the physical surface off
the hit's physical material, so a project that already paints its geometry gets matching debris
without naming a surface anywhere.

---

## Console commands

| Command | Does |
|---|---|
| `Debris.Show 0\|1` | show or hide the counter box |
| `Debris.Budget <n>` | pieces one frame may create |
| `Debris.Cap <n>` | pieces that may exist; lowering it evicts the excess |
| `Debris.Clear` | take every piece and decal away |
| `Debris.Stress <n> [radius]` | report *n* impacts in one frame — the proof button |
| `Debris.Stats` | print the counters to the log |

---

## What is where

| Class | Is |
|---|---|
| `UDebrisDirectorSubsystem` | the service: queue, merge, eviction, sleep timers, pools |
| `UDebrisProfile` | what one surface leaves behind — meshes, scale, decal, lifetime, importance |
| `UDebrisDirectorComponent` | the facade: drop it on an actor and its hits become debris |
| `ADebrisDirectorHUD` | the counter box |
| `UDebrisDirectorStatics` | Blueprint entries, **and** `PlanBurst` / `RankForEviction` as pure statics |
| `UDebrisDirectorSettings` | Project Settings: budget, ceiling, cell size, sleep, fade |
| `ADebrisPiece` | one pooled piece — a mesh that can simulate, freeze, fade and be reused |

`PlanBurst` and `RankForEviction` are static and world-free on purpose: a `UTickableWorldSubsystem`
cannot be created in an automation test, so every rule written inside one ships unverified. Here they
are driven by six automation tests under `DebrisDirector.*`.

---

## Documentation

Full documentation: <https://wiki.teufel-engineering.com/en/DebrisDirector/documentation>
Support: <mailto:teufelsilvan@gmail.com>

© 2026 Silvan Teufel. All Rights Reserved.

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

_This plugin does not have its own Fab listing yet — the store link above is where everything we currently sell lives._

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
