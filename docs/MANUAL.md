# Lume — Player's Manual

*Covers Lume 0.8.0. This manual grows with the game: each new feature gets
written up here as it lands.*

You go down into a dungeon with a dim lantern, a short sword, two torches and
a pack. Down there, light is the thing that matters most. It shows you the way
and holds back the things that live in the dark, but it also shows you to
everything else.

---

## Contents

1. [Controls](#controls)
2. [The screen](#the-screen)
3. [Light and darkness](#light-and-darkness)
4. [Torches](#torches)
5. [Sconces](#sconces)
6. [Shades and the chill](#shades-and-the-chill)
7. [Other monsters](#other-monsters)
8. [Items](#items)
9. [Gear: weapons and armour](#gear-weapons-and-armour)
10. [Embers and the forge](#embers-and-the-forge)
11. [Health and resting](#health-and-resting)
12. [The dungeon](#the-dungeon)
13. [Tips](#tips)

---

## Controls

Everything can be done with the mouse alone, the keyboard alone, or a mix.
Every action also has a button on screen.

| Action | Keys | Mouse |
|---|---|---|
| Move / attack | arrows, numpad, `h j k l y u b n` | click a place to travel there; click a monster to attack it |
| Explore | `x` | **Explore** button |
| Rest a turn | `z`, `.`, numpad 5 | click yourself |
| Rest until healed | `Z` | **Rest** button |
| Pack | `i` | **Items** button |
| Throw | `t`, then aim | **Throw** button |
| Use (light, drink, wield, wear) | `a` | from the item card |
| Drop | `d` | from the item card |
| Light / snuff your torch | `T` | **Torch** / **Snuff** button |
| Go down / up stairs | `>` (travels there first) / `<` | click the stairs |
| Map | `m` | **Map** button |
| Help | `?` | **?** button |
| Zoom | `+` / `-` | mouse wheel |
| Sound on/off | `F2` | |
| Quit | `Esc` twice | |

**Aiming a throw:** move the reticle with the mouse or the direction keys.
`Tab` cycles through the monsters you can see. Click or press `Enter` to
throw, and `Esc` cancels.

**Hovering** the mouse over anything shows a card describing it. Cards fade
after a few seconds. Pressing any key hides the hover marker and path until
you move the mouse again, so things scrolling under a resting pointer won't
pop up cards.

**Text size** follows kitty's own font zoom (`ctrl+shift+=` / `-`).

---

## The screen

- **Bottom bar:** HP and its bar, the current depth, your torch's fuel (when
  you have one going), your embers (once you've earned any), the turn count,
  and the action buttons.
- **Top left:** the monsters you can see, with their health and whether they
  are asleep, hunting you, or only *watching* (eyes in the dark).
- **Bottom left:** the message log.
- **HP in pale blue**, with a frosted end to the bar, means you are chilled
  (see [the chill](#shades-and-the-chill)).

---

## Light and darkness

Some rooms start lit and some start dark. Lit rooms have sconces burning on
their walls. Dark rooms have cold ones.

- **In the dark,** monsters only notice you when you are close (about two
  tiles). Your lantern gives you a small circle of dim light that lets you see
  nearby without being seen.
- **In the light,** whether from a sconce, a burning torch on the floor or a
  torch in your hand, monsters can see you from much further away.
- Goblins see well in the dark, but even better in the light.
- Light is the only thing that holds off shades (see below).

So light keeps the shades away while it shows you to everything else. Most of
the game is choosing when to be seen.

---

## Torches

Torches are the clock in Lume, the way food is in other roguelikes. You find
about three for every four levels, plus the two you start with, and the
supply is paced so you're never flooded or starved by luck.

- **A lit torch** lights a wide circle around you for 200 turns, but makes you
  visible from afar.
- **Lighting or snuffing** takes a turn. Snuffing wastes about 5 turns of
  fuel, since the charred head has to catch again.
- **Lighting a fresh torch** while one is burning replaces the old one.
- **Thrown,** a torch lands burning and lights the area for about 120 turns.
  The noise of it landing draws monsters to look. Walk onto a burning torch
  with your hand free and you take it up again. One that burns out leaves a
  charred stub.
- **Deep water** puts out a carried torch and ruins it. You're warned first.
- **Into a chasm:** a torch thrown into a chasm lands on the level below,
  still burning, and you can see it down there through the hole.

---

## Sconces

Sconces are iron brackets on room walls that hold a torch. Cold ones still
catch a faint glint, so you can find them in a dark room.

- **Lighting one:** walk into it, or click it to travel there and light it. You
  have to stand right in front of it.
- **It lights the room** for good, so you can see, but so can everything there.
- **You take an ember** from every sconce you light (see
  [the forge](#embers-and-the-forge)).
- **It is a fixed fire:** it burns shades caught in it, and its warmth drives
  out the chill and helps you heal.

Each sconce can only be lit once, so a cold sconce in a dark room is worth
saving for the right moment.

---

## Shades and the chill

From depth 2, **shades** gather in the unlit rooms. A shade is living
darkness. It hunts you by your warmth whether you're lit or not, and in the
dark all you see of it is a pair of pale eyes.

**What light does to them:**
- **They cannot enter firelight.** The cold glow of crystals and moss doesn't
  count.
- **Your carried torch only drives them off.** It can't hurt them. They circle
  at the edge of its light and **gnaw at the flame**: each shade nearby burns
  an extra turn of your torch's fuel every turn, up to three extra.
- **Only a fixed fire burns them,** meaning a lit sconce, the forge, or a torch
  lying on the floor. A shade in a fixed fire burns and flees.
- **A sudden blaze holds them.** When a fire flares up around a shade (you
  light a sconce next to it, or a thrown torch lands by it), the shade is held
  for two turns and can't flee. It almost always burns away.
- **A thrown torch** that hits a shade sears it as well.
- **Darts** pass straight through them, and plain weapons can still hurt them.
- **A shade that burns away** leaves an ember behind.

**The chill.** A shade's touch does a little damage and also chills you. The
chill takes 1–2 from your *maximum* HP with each touch, up to half of it, until
you warm up:
- Standing by a fixed fire (a lit sconce, the forge, a torch on the floor)
  warms you quickly.
- A torch in your hand warms you slowly.
- In the dark, the chill doesn't fade at all.

---

## Other monsters

| Monster | Notes |
|---|---|
| **Rat** | Weak and quick to die. Often asleep. |
| **Goblin** | A real fight early on. Sees well in the dark and better in the light. Won't swim, so deep water is a refuge from goblins. |
| **Shade** | From depth 2. See [above](#shades-and-the-chill). |
| **Bat** | From depth 2. Quick (it moves twice a turn) and erratic: it flits in, bites, and flits off again, so it's hard to pin down. Light draws it from a long way off; in the dark it only finds you up close. It flies, so chasms and deep water don't stop it, but it can't open doors. Silent on the wing. |
| **Goblin slinger** | From depth 3. If it can see you clearly **in the light**, it stays back and slings stones at you from up to 7 tiles away (a little less accurate than a blow). In the dark, or with something in the way, it has to come close and fight with a knife. Snuff your torch, or step out of the lamplight, and it loses its aim. Won't swim. |
| **Ogre** | From depth 4. Big, tough and hits very hard, but **slow**: it moves and swings only every other turn, so you can always outrun it, and you get two blows for each of its. Far too heavy for a mace to knock back. Won't swim. |
| **Eel** | From depth 2, in pools of deep water. It lurks **unseen** under the surface (now and then a ripple gives it away) and lunges at anything swimming near it or standing **on the bank beside it**. Once it has bitten it stays up for a turn or two, long enough to hit back, then slips under again. It never leaves the water. |

Monsters may be asleep. Noise (splashing, a thrown torch or dart landing, mail
jingling) wakes them and draws them to look. Deeper monsters are a little
tougher: more health and a bit more accuracy and damage per depth.

---

## Items

| Item | Use |
|---|---|
| **Torch** | Light it to carry, or throw it to light a place from afar. See [Torches](#torches). |
| **Dart** | Throw it for 2–4 damage at range. It can usually be picked up again, though it sometimes snaps. Passes through shades. |
| **Healing potion** | Drink it to heal 15 HP (not past your chilled maximum). |
| **Weapons and armour** | See [Gear](#gear-weapons-and-armour). |

**The pack:** press `i` or the **Items** button. It shows what you wield and
wear at the top, then everything you carry. Pick an item to see its card and
the actions it allows.

---

## Gear: weapons and armour

**Anything you find, you can use.** There are no strength or skill
requirements. Each weapon and armour plays differently rather than simply
being better, so a find is a choice of play style, not a lottery ticket.

To change gear, open the pack, pick the piece and press **Wield** or
**Wear** (or `a`). What you had on goes into the pack in its place. The item
card shows the piece's numbers next to yours.

### Weapons

| Weapon | Hit | Damage | Special |
|---|---|---|---|
| **Short sword** *(you start with it)* | 85 | 2–5 | Steady and accurate. No tricks. |
| **Dagger** | 95 | 1–3 | **Triple damage** on anything that hasn't noticed you: asleep, or not hunting you. Weak in an open fight. |
| **Spear** | 80 | 2–5 | Each thrust **also hits whatever stands behind** your target. |
| **Axe** | 72 | 2–5 | **Hits every foe around you** at once. Harder to aim. |
| **Mace** | 70 | 3–6 | **Knocks the foe back** a step: into a wall for extra damage, into deep water, or over the edge of a chasm onto the level below. A knocked-back foe spends its next turn stepping back in. |

### Armour

| Armour | Armour value | Special |
|---|---|---|
| **Leather jerkin** *(you start in it)* | 10 | Some protection, no drawbacks. |
| **Shadow cloak** | 4 | In the dark, things must be **right next to you** to notice you, and shades sense your warmth from **half as far**. |
| **Mail coat** | 22 | Strong, but it **jingles as you walk** (waking sleepers nearby), and in **deep water it drags you under** (2–3 damage a turn while swimming; you're warned first). |

Armour value is taken straight off a monster's chance to hit you.

**Tempered gear** shows as "+1", "+2"… Each step of tempering on a weapon adds
+3 hit and +1 maximum damage (and +1 minimum damage every second step). On
armour it adds +3 armour. Gear found deeper down is sometimes already
tempered.

---

## Embers and the forge

**Embers** are what the forge wants. You get them by:
- lighting a cold sconce (+1),
- a shade burning away, by any means (+1).

Your ember count shows on the bottom bar once you have some.

**The forge brazier.** Each level has one: a bowl of coals on iron legs
standing in a room, lighting it. It is also a fixed fire, so shades won't come
near it and it warms off the chill. **Walk into it** (or click it) to open the
forge:

| Offer | Cost | Effect |
|---|---|---|
| **Temper your weapon** | 3, then 5, 7, 9… | +1: more accurate and harder hitting |
| **Temper your armour** | 3, then 5, 7, 9… | +1: harder to hit |
| **Kindle your heart** | 4, then 6, 8… | +3 max HP, for good |
| **Trim your lantern** | 5, then 8 (twice at most) | Your lantern's own dim glow reaches a tile further: you see more in the dark without being seen |

Tempering stays with the piece, not with you. If you swap your +3 spear for a
mace, the spear keeps its +3 in your pack.

**Falls are hard on gear.** Jumping down a chasm may blunt your weapon or
buckle your armour, taking away one step of tempering. Water below makes that
less likely.

---

## Health and resting

- You start with **24 HP**.
- You heal **1 HP every 9 turns**, or every 5 turns beside a fixed fire.
- **Rest** (`z`) passes a turn. **Rest until healed** (`Z`) keeps going until
  you're full or something turns up.
- **Healing potions** restore 15 HP.
- **Chill** lowers your maximum until you warm up by a fire (see
  [the chill](#shades-and-the-chill)).

---

## The dungeon

- **Stairs down** lead deeper. `>` travels to them and descends once you've
  seen them.
- **Chasms** drop to the level below, and through them you can see what's down
  there. Stepping in asks first. The fall hurts (more the deeper you are),
  water below breaks it, and it can dent tempered gear. It's a quick way down,
  not a free one.
- **Water:** wading through the shallows splashes, and splashing carries to
  monsters nearby. Deep water must be swum. It puts out a lit torch, swallows
  anything you drop in it, and drags you under if you wear mail. Goblins,
  slingers and ogres won't follow you in, but bats fly over it and eels may be
  waiting in it.
- **Doors** block sight and light until opened.
- **Crystals and glowmoss** in the caverns give off a cold light. It lets you
  see, but it doesn't count as fire, so shades don't mind it.
- **The map** (`m`) shows the whole of the level you've seen, drawn with the
  real graphics. Click a place on it to travel there.
- **Depths** go down to 26.

---

## Tips

- **Save cold sconces for traps.** Shades come straight for your warmth in the
  dark. Wait by a cold sconce with your torch snuffed, and light it when they
  close in. The sudden blaze holds them and burns them away, and each one
  leaves an ember.
- **Light a sconce to cut off a chase.** A lit sconce fills its corridor or
  doorway with fire that shades can't cross.
- **Your torch is a shield, not a sword.** Against shades it buys time and
  costs fuel. Use that time to reach a sconce, the forge, or the stairs.
- **Shades eat fuel.** With several following you, a torch burns down fast.
  Deal with them or leave the level.
- **The forge is a safe room.** Shades won't approach it, and its warmth heals
  you faster and clears the chill.
- **A thrown torch isn't lost.** It lights a room from a distance, sears a
  shade, or lures monsters with its noise, and you can pick it back up.
- **Daggers love the dark.** In a shadow cloak, sleeping and wandering monsters
  rarely notice you before the dagger lands.
- **A mace by a chasm** sends monsters to the level below.
- **Fight slingers in the dark.** Snuff your torch and close in through the
  shadows; they can't aim at what they can't see. Or duck round a corner and
  make them come to you.
- **Never stand and trade blows with an ogre** in the open if you're hurt.
  It's slow: step back, throw darts, and step back again.
- **Give still, dark pools a wide berth.** If you must walk the bank, keep a
  tile between you and the deep water, and watch for rings on the surface.
- **Bats come to the light.** A lit torch draws every bat on the level.
  Fight them in a corridor, where they have less room to flit about.
- **Mail and deep water don't mix.** Swap to leather before a long swim, or go
  around.
