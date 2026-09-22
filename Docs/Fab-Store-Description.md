# DebrisDirector — Impact Debris On A Budget

**One pooled service for impact debris, shells and decals: a hard per-frame spawn budget, distance-
and-importance eviction, and physics that goes to sleep on schedule instead of never.**

---

## The problem

Your game already knows how to spawn a piece of rubble. What it does not know is when to stop.

A grenade in a corridor reports around **40 impacts in one frame**. A hand-written spawner creates 40
actors, all simulating, on the frame the player is least able to afford it. Ten seconds later there
are four hundred of them on the floor — and a good number are still *almost* asleep, jittering
against a corner or resting on something that is itself still moving, costing physics time forever.
When the cheap fix finally goes in, it is a ring buffer that takes the oldest piece away: the one the
player is standing over, blinking out of existence in front of them.

DebrisDirector is the layer that decides those three things properly.

## What it does

**A hard per-frame spawn budget — default 24 — that is not a cut-off.** Requests above the budget are
**merged**, not dropped: five hits on the same surface become one larger piece instead of five small
ones. Forty impacts against a budget of 24 produce 24 pieces that still stand for all 40 — the wall
is fully marked and nothing is missing from one side of the blast. That is why an explosion costs no
more frame time than a single shot.

**Eviction by importance and distance.** At the population ceiling — default 400 — the piece that
dies is not the oldest one, it is the **least important far-away** one. A piece inside the player's
view outlives one behind their back. The sort is stable, so a scene sitting exactly on the ceiling
does not flicker.

**Physics with an expiry date.** Every piece simulates for at most `MaxSimSeconds` — default 3 — and
is then frozen where it lies. A hundred sleeping bodies are cheap; a hundred *almost* sleeping ones
are not, and engine sleep thresholds never settle the ones that matter.

**Fade instead of vanish.** An evicted piece leaves over a short material parameter, so nothing blinks
out in front of the player.

## Everything it claims is on screen

A budget plugin without visible numbers is unprovable on a screenshot. The counter box is drawn on
`UCanvas` — it survives a cooked **Shipping** build, unlike a debug widget:

```
Spawned        24 this frame   (budget 24)   [budgeted]
Requests       40 in   16 merged   0 dropped
Population     400 / 400   (+6 fading out)
Bodies         31 simulating   369 sleeping   expiry ON
Evicted        24 this frame   118.0 / s
```

Console: `Debris.Show`, `Debris.Budget`, `Debris.Cap`, `Debris.Clear`, `Debris.Stress`,
`Debris.Stats`. `Debris.Stress 40` at budget 4, then at budget 128, is the whole product in two
commands.

## What it is not

**DebrisDirector breaks nothing apart.** It produces no fragments from geometry — that is Chaos — and
it does not replace particles — that is Niagara, which has no collision and leaves no remains. It is
not another object pool either: it contains one because it must, but pooling is the part that is
already solved. It manages the **count, the lifetime and the cost** of the pieces you bring.

## Included

- One runtime module. No UMG, no Niagara, no Chaos, no editor module.
- `UDebrisDirectorSubsystem` — the service: queue, merge, ranked eviction, sleep timers, pools.
- `UDebrisProfile` — a data asset per surface: meshes with weights, scale range, materials, decal,
  lifetime, `MaxSimSeconds`, importance, collision.
- `UDebrisDirectorComponent` — drop it on an actor and its hits become debris. Reads the physical
  surface off a hit result, so a project that already paints its geometry needs no new names.
- `ADebrisDirectorHUD` — the counter box.
- `UDebrisDirectorStatics` — Blueprint entries, plus `PlanBurst` and `RankForEviction` as **pure
  static functions** that need no world.
- `UDebrisDirectorSettings` — Project Settings: budget, ceiling, cell size, sleep, fade, pooling.
- **Six automation tests** under `DebrisDirector.*` covering the budget, the no-loss invariant, the
  eviction order, the ceiling under 1000 requests a frame, and the physics expiry.
- A demo map with three surfaces and a button bar: budget 4 / 24 / 128, cap 100 / 400, merge on/off,
  physics sleep on/off, grenade, clear.
- Full documentation.

## Technical

- **Engine:** Unreal Engine 5.8
- **Platform:** Win64 (built and verified)
- **Modules:** 1 runtime (`DebrisDirector`)
- **Dependencies:** Core, CoreUObject, Engine, PhysicsCore, DeveloperSettings, RenderCore
- **Network replicated:** no — debris is cosmetic; each client makes its own from the impacts it
  already receives
- **Supported build targets:** Development, Shipping (the counter box included)

## Support

Documentation: <https://wiki.teufel-engineering.com/en/DebrisDirector/documentation>
Support: <mailto:teufelsilvan@gmail.com>
