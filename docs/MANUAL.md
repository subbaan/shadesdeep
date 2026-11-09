# Shadesdeep — Player's Manual

*Covers Shadesdeep 0.16.7. This manual grows with the game: each new feature gets
written up here as it lands.*

You go down into a dungeon with a dim lantern, a short sword, two torches, a
flask of oil and a pack. Down there, light is the thing that matters most. It shows you the way
and holds back the things that live in the dark, but it also shows you to
everything else.

At the bottom, on depth 26, the **Heartflame** has gone cold, and the dark has
been creeping up from it ever since. Your task is to carry fire all the way
down and [kindle it again](#the-heartflame).

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
13. [Traps](#traps)
14. [Special rooms](#special-rooms)
15. [The Heartflame](#the-heartflame)
16. [The hall of fame](#the-hall-of-fame)
17. [Tips](#tips)

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
| Search for hidden doors (5 turns) | `s` | **Search** button |
| Close an open door beside you | `c` | **Close** button (shows beside an open door) |
| Pack | `i` | **Items** button |
| Throw | `t`, then aim | **Throw** button |
| Use (light, drink, pour, wield, wear) | `a` | from the item card |
| Drop | `d` | from the item card |
| Light / snuff your torch | `T` | **Torch** / **Snuff** button |
| Go down / up stairs | `>` (travels there first) / `<` | click the stairs |
| Map | `m` | **Map** button |
| Help | `?` | **?** button |
| Zoom | `+` / `-` | mouse wheel |
| Sound on/off | `F2` | |
| Save and quit | `Esc` twice | |

**Explore and travel** take the shortest safe way. They go round shallow water
when there's a dry way (even a fair bit longer), so you don't splash about and
draw attention. They only wade when there's no other way. They never swim deep
water, step into a chasm or fire, or walk onto a trap you know about.

**Aiming a throw:** move the reticle with the mouse or the direction keys.
`Tab` cycles through the monsters you can see. Click or press `Enter` to
throw, and `Esc` cancels.

**Hovering** the mouse over anything shows a card describing it. Cards fade
after a few seconds. Pressing any key hides the hover marker and path until
you move the mouse again, so things scrolling under a resting pointer won't
pop up cards.

**Text size** follows kitty's own font zoom (`ctrl+shift+=` / `-`).

**The look:** you, the rats, goblins, slingers, bats, ogres and eels are drawn
pixel-art sprites, and each one is never quite still: you breathe and your
lantern sways, rats sniff, goblins fidget, slingers whirl their slings, ogres
heave, bats flap and eels sway. Their eyes, and your lantern, glow in the dark.
Every item has its own picture too, on the floor and in your pack, and the
healing potion, warming draught and embers glow. Dust drifts in the air and
glitters where the light catches it. Zoomed in (`+`, or the mouse wheel)
creatures and items are drawn from the full-detail art rather than blown up, so
they stay sharp at any zoom. Start with `--classic-art` to play with the
game's original blocky sprites instead (it doesn't still the dust). The
buttons are worn slabs of carved slate. The one under the mouse, or the one
picked with the keys on the title screen, glows with firelight. Shades and
the Hollow are always drawn live, as moving darkness.

### The title screen

Shadesdeep opens on a title screen:

- **Continue** (`c`) picks up your saved game. It's only there when you have one.
- **New game** (`n`) starts a fresh dungeon. If you have a saved game, it asks
  first. Giving the saved game up ends it for good, and it goes into the
  [hall of fame](#the-hall-of-fame) as it stands.
- **Hall of fame** (`h`) shows your best runs.
- **Quit** (`q` or `Esc`) leaves.

Choose with the mouse, the letter keys, or the up and down arrows and `Enter`.
`--no-title` skips the title screen and starts playing straight away.

### Saving

There's one game at a time, and it's always kept. Quitting (`Esc` twice, or
just closing the window) saves it, and the next time you start Shadesdeep you carry
on exactly where you left off. The game is also saved each time you reach a
new level, in case something goes wrong. **Dying ends the game for good:** the
save is deleted and the next start is a new dungeon. So does winning.

The save lives in `~/.local/share/shadesdeep/save.gz`. Starting with `--new`
begins a fresh game (your saved one is replaced when you quit), `--save=FILE`
keeps it somewhere else, and `--no-save` plays without one. A save from a
version of Shadesdeep that this one can't read is set aside as `save.gz.old`
rather than deleted.

---

## The screen

- **Bottom bar:** HP and its bar, the current depth, your torch's fuel (when
  you have one going), your embers (once you've earned any), the turn count,
  and the action buttons. **SNARED** shows while a snare holds you. On the last level, **FLAME n/3** shows how far
  you've kindled the Heartflame, and the Descend button becomes **Hearth**.
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

Torches are the clock in Shadesdeep, the way food is in other roguelikes. You find
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
| **Rat** | Weak and quick to die. Often asleep. Can't open doors. |
| **Goblin** | A real fight early on. Sees well in the dark and better in the light. Won't swim, so deep water is a refuge from goblins. |
| **Shade** | From depth 2. See [above](#shades-and-the-chill). |
| **Bat** | From depth 2. Quick (it moves twice a turn) and erratic: it flits in, bites, and flits off again, so it's hard to pin down. Light draws it from a long way off; in the dark it only finds you up close. It flies, so chasms and deep water don't stop it, but it can't open doors. Silent on the wing. |
| **Goblin slinger** | From depth 3. If it can see you clearly **in the light**, it stays back and slings stones at you from up to 7 tiles away (a little less accurate than a blow). In the dark, or with something in the way, it has to come close and fight with a knife. Snuff your torch, or step out of the lamplight, and it loses its aim. Won't swim. |
| **Ogre** | From depth 8. Big, tough and hits very hard, but **slow**: it moves and swings only every other turn, so you can always outrun it, and you get two blows for each of its. Far too heavy for a mace to knock back. Won't swim. |
| **The Hollow** | Only on depth 26, by the cold hearth. See [the Heartflame](#the-hollow). |
| **Eel** | From depth 2, in pools of deep water. It lurks **unseen** under the surface (now and then a ripple gives it away) and lunges at anything swimming near it or standing **on the bank beside it**. Once it has bitten it stays up for a turn or two, long enough to hit back, then slips under again. It never leaves the water. |

Monsters may be asleep. Noise (splashing, a thrown torch or dart landing, mail
jingling) wakes them and draws them to look.

**Wandering bands.** Not everything waits in its room. On many levels a band
is on the move: a pack of two or three rats down to depth 3, small goblin
packs below that (often just a pair at first). From depth 7 a slinger may go
with them, from depth 12 the packs grow to four, and from depth 15 an ogre may
lead them. They go
from room to room, stopping a while in each, and they're awake. When one of them
spots you, or you strike one, it cries out, and the rest of the band joins the
hunt. They hunt the long way round corners and through doors, so a band is
harder to shake off than a lone goblin. Only one band roams a level at a time.
From depth 3, once the first band is dead and you've thinned out most of what
lives there, another may come in from the stairs, though rarely (about once in
a thousand turns). If you hear footsteps somewhere in the dark, something has
arrived.

**Sneak attacks.** A monster that hasn't noticed you can't defend itself. That
means one that's asleep, dazzled, or awake but not hunting you. Your blow
**always lands and does triple damage** with any weapon. A dagger does **five
times** the damage. The first blow alerts it, so you get one free strike; make it
count. The Hollow is never caught unawares. A monster that comes through a door
right onto you is caught off guard too (see [ambush at a door](#the-dungeon)).

**Coming up from behind.** A monster on the move watches the way it's going:
the way its picture faces when it walks left or right, or straight ahead when
it walks up or down a corridor (on a diagonal, the way it's heading). Approach
from behind it and it notices you from only **half as far** (in the light or the
dark), though it always notices you right beside it. The hover card says when
its back is to you. A monster that has stood still for a couple of turns looks
all around, so this works on things that are walking: a wandering band going
from room to room, or something going to look at a noise. Shades feel your
warmth and bats hear you, so neither has a back to sneak up on.

**Creeping up on a sleeper:** each turn you're close and in its sight, a
sleeping monster may stir. That's far more likely in the light than in the
dark: right beside it, about 3 in 10 with a light on you, but only about 1 in 8
in the dark. A shadow cloak halves the chance. So snuff your torch, step out of
the lamplight, and come in quietly. Wading and mail still make noise. Deeper monsters are a little
tougher: more health and a bit more accuracy and damage per depth. Below depth 9
that climb slows to two thirds of its pace, so the forge and the gear you find
can keep up all the way down.

---

## Items

| Item | Use |
|---|---|
| **Torch** | Light it to carry, or throw it to light a place from afar. See [Torches](#torches). |
| **Dart** | Throw it for 2–4 damage at range. It can usually be picked up again, though it sometimes snaps. Passes through shades. |
| **Healing potion** | Drink it to heal 15 HP (not past your chilled maximum). |
| **Oil flask** | Throw it and it bursts into a pool of [burning oil](#burning-oil). Or **soak your torch** with it (from the item card): +50 turns of fuel on the torch you have going. You start with one. |
| **Flash powder** | Throw it for a [blinding flash](#flash-powder). Lands in water and it fizzles, spoilt. |
| **Salt** | **Pour** it to lay a [ring of salt](#salt) around you that shades can't cross. From depth 2. |
| **Warming draught** | Drink it to clear the chill at once, and for **80 turns** no shade's touch can chill you (a shade can still hurt you). The bottom bar shows **WARM** and the turns left. From depth 2. |
| **Cold embers** | A pile of them lies in every [shade's nest](#special-rooms). Walk onto it and they join your [embers](#embers-and-the-forge); they don't take up room in your pack. |
| **Weapons and armour** | See [Gear](#gear-weapons-and-armour). |

**The pack:** press `i` or the **Items** button. It shows what you wield and
wear at the top, then everything you carry. Pick an item to see its card and
the actions it allows.

**Aiming oil and flash powder:** while you aim, the preview marks the tiles
the oil will spread to, or draws the reach of the flash.

### Burning oil

A thrown oil flask bursts where it lands and spreads burning oil over that
tile and most of the tiles around it (not round a wall's corner, not through
a door). It burns for about 13–16 turns in the middle and 9–13 at the edges,
and leaves soot.

- **It's a fixed fire**, like a lit sconce: shades can't enter it, a shade
  caught in it burns, and its sudden blaze holds them the way a sconce lit
  beside them does. Its warmth also clears the chill.
- **Nothing walks into it willingly.** Goblins, rats, ogres and bats all go
  round, so a burning doorway or corridor holds a chase off for a while.
- **The burst engulfs everything in the splash** for 3–5 (a little more
  deeper down: +1 for every three levels' worth of monster toughness). Then
  it burns them once more, for 2–4 (with the same deep bonus), before they can
  scramble out on their own turn. A pack bunched up together takes all of it.
- **It sears whatever stands in it** for 2–4 a turn (you included). A monster
  caught in it steps out on its next turn, unless it's dazzled, in which case it
  keeps burning. Walking into it yourself asks first: move again to go through.
- **It burns on water too**, shallow or deep.
- **Into a chasm**, it lands and burns on the level below.
- The landing makes a little noise.

### Flash powder

A thrown twist of flash powder goes off where it lands in a blinding flash.

- **Everything within 5 tiles that has a clear line to the flash is dazzled**
  for 3–4 turns (2 for an ogre): it can't act, it loses track of you, and it
  wakes up if it was asleep. While a monster is dazzled your blows and darts
  **always hit**, and every blow is a sneak attack: triple damage, or five
  times with a dagger.
- **Shades are scorched** by it for 4–8. They still feel your warmth, though.
- **You see the room** it lights, as if you'd explored it.
- **The bang carries**: things up to 8 tiles away come to look.
- **Water spoils it**, and down a chasm it's wasted.

### Salt

Pour a pouch of salt to lay salt on your tile and every tile around you
(not on water or a door).

- **No shade can cross salt**, or be knocked onto it.
- **Anything else scuffs it away** by walking through it, and so do you: the
  tile you step out onto loses its salt, and the rest of the ring stays.
- Salt across a corridor shuts the shades out of it until something else
  walks through.
- A tile with a shade on it gets no salt, and the ring has a gap there.

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
| **Dagger** | 95 | 1–3 | Its [sneak attacks](#other-monsters) do **five times** the damage, not three: on anything asleep, dazzled, or not hunting you. Weak in an open fight. |
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
  you're full or something turns up. While you rest you're also looking about
  you, so you may notice a [hidden door](#hidden-doors-and-hidden-rooms) nearby.
- **Burning oil** sears you for 2–4 a turn while you stand in it.
- **Healing potions** restore 15 HP.
- **Chill** lowers your maximum until you warm up by a fire (see
  [the chill](#shades-and-the-chill)).

---

## The dungeon

- **Stairs down** lead deeper. `>` travels to them and descends once you've
  seen them.
- **Chasms** are where the floor has fallen in: ragged holes with broken
  flagstones and rubble round the rim, sometimes a smaller hole beside the main
  one. They drop to the level below, and through them you can see what's down
  there. Stepping in asks first. The fall hurts (more the deeper you are),
  water below breaks it, and it can dent tempered gear. It's a quick way down,
  not a free one.
- **Lakes** from depth 2: wide water that spreads across rooms, corridors and
  the walls between them, deep in the middle with shallows round it. There's
  always a dry way round to everything (or at worst a wade), never a swim you
  can't avoid.
- **Water:** wading through the shallows splashes, and splashing carries to
  monsters nearby. Deep water must be swum. It puts out a lit torch, swallows
  anything you drop in it, and drags you under if you wear mail. Goblins,
  slingers and ogres won't follow you in, but bats fly over it and eels may be
  waiting in it.
- **Doors** block sight and light until opened. An open door is drawn swung
  back against its post, with an oak sill across the doorway, so you can tell
  an open doorway from plain corridor. Walk into one to open it (that
  takes your move). Standing beside an open door (straight across, not on a
  diagonal), press `c` or the **Close** button to shut it again. You can't
  close it on a monster, an item, a burning torch or burning oil. If there are
  two open doors beside you, both close.
- **Who opens doors:** goblins, slingers, ogres, shades and the Hollow open a
  door and come through it in a single move. Rats and bats can't open them at
  all, so a shut door keeps them out. A rat chasing you gives up once a door
  is shut between you.
- **Ambush at a door.** A monster can't see through a closed door. If it comes
  through one and you're standing right by the doorway on the other side, it
  walks into you unready: your next blow is a [sneak attack](#other-monsters),
  even if it was hunting you. That only lasts until its next turn. Shades feel
  your warmth through the wood and the Hollow is never caught off guard, so
  neither can be ambushed. A doorway's corners are wall, so stand **straight
  in front of** the door; from the side you can't reach it.
- **Crystals and glowmoss** in the caverns give off a cold light. It lets you
  see, but it doesn't count as fire, so shades don't mind it.
- **The map** (`m`) shows the whole of the level you've seen, drawn with the
  real graphics. Click a place on it to travel there.
- **Depths** go down to 26, the last level. It has no stairs down and no
  chasms; it's where the [Heartflame](#the-heartflame) is.

---

## Traps

From depth 2, a few traps lie hidden in the floors, more the deeper you go,
and storerooms are often trapped at the way in.

**Spotting them.** Each turn you may notice a hidden trap within two tiles of
you. A lit **torch** makes that far more likely, as does a well-lit room or
standing right beside it. Walking in the dark with only your lantern, you'll
step on about half the traps you meet; with a torch, about one in four.
**Flash powder** shows every trap in its glare.

**Known traps** are marked on the floor and on the map. Travel and explore
step around them, and walking onto one yourself asks first ("Move again to
step on it anyway"). Hover one to see what it is.

| Trap | What it does |
|---|---|
| **Alarm plate** | A bell clangs in the walls: anything within 16 tiles may wake, and all of it comes to look. |
| **Draught vent** | An icy gust blows out your torch (it keeps its fuel; light it again) and chills you by 1, unless a warming draught is in you. |
| **Trapdoor** | Drops you to the level below, like jumping into a chasm: it hurts, and it can dent tempered gear. There are none on the last level. |
| **Snare** | A wire noose holds your ankle. Each step you try is a turn spent tearing at it (3–4 tries). You can still fight, throw and use things while it holds you. |

**Monsters** know where the traps are and step lightly over them. But knock
one onto a trap with a mace and it sets it off: a snared monster is held
(dazed) for 3 turns (shades and bats slip free), a trapdoor drops it to the level below, and an alarm
brings everything else running.

---

## Special rooms

From depth 2, most levels have a room with a purpose, and some have two. The message log tells you what it is the first time you see into it.

- **Storeroom** (a small room, straw on the floor): 3–5 supplies, such as oil,
  darts, potions, flash powder, salt, a warming draught, now and then a torch.
  Its keeper often left a trap just inside the way in.
- **Armoury** (rusted blades on the floor): two pieces of gear, often
  tempered, guarded by goblins (or slingers) or, from depth 8, an ogre, likely asleep.
- **Shade's nest** (from depth 3, bones on the floor): a dark room, its
  sconces cold, with 3 or more shades in it and a pile of **cold embers**.
  Light its sconces with the shades inside and the blaze burns them, each one
  leaving an ember of its own.
- **Hidden room** (dust and old coins on the floor): see below.

### Hidden doors and hidden rooms

About a quarter of the first levels, and two levels in five below that, have a
**hidden room** behind a secret door. The door looks like any other piece of
wall. It's in the wall of a room or corridor, or at the end of a short passage
that seems to go nowhere. Nothing has been in that room for an age, and what was
left there is still there:

- a piece of **tempered gear**, better than you'd expect for the depth
  (+1 at the top, more further down);
- a pile of **embers**;
- two **supplies**, such as potions, flash powder, oil, darts or a warming draught.

**Finding the door.** Walking past right beside it, you might notice it, more
likely with a lit torch. Resting nearby gives you a better chance, out to two
tiles. **Searching** (`s`, or the **Search** button) spends five turns looking
hard at the walls within three tiles, and the last of the five turns is the most
thorough. Beside the door with a torch lit, one search almost always finds it.
Searching stops at once if something comes into view. A dead-end passage is
the best place to look.

---

## The Heartflame

Depth 26 is the bottom of the world. Somewhere on it is a great pillared hall,
dark, with its sconces cold, and at its heart a round stone hearth where the
Heartflame used to burn. Lighting it again wins the game.

- **Bring fire.** You kindle the hearth by walking into it (or clicking it)
  with a **lit torch**. Your lantern won't do. Make sure you still have a
  torch when you get there; the shades in the hall will be gnawing at it.
- **It takes three turns.** The first turn wakes a glow in the stone, the
  second a small flame, and the third brings the Heartflame roaring back.
  You don't have to do the three turns in a row: progress stays unless
  something undoes it. `>` (the **Hearth** button) travels to the hearth and
  kindles it.
- **The dark notices.** The moment you start, every shade on the level, and the
  Hollow, comes for you.
- **When it catches,** every shade on the level, the Hollow included, comes
  apart, and every cold sconce lights at once. The closing screen tells the
  story of your run: the turns it took, foes slain, embers gathered and
  sconces lit.

### The Hollow

The thing that put the Heartflame out. It's a shade grown huge, and it waits
by the hearth.

- It hunts by your warmth, like any shade, from up to 10 tiles away.
- Your **torch doesn't frighten it**, and **salt doesn't stop it** (it sweeps
  the salt aside). It eats your torch's fuel twice as fast as a shade does.
- A **fixed fire burns it** (3–5 a turn: a lit sconce, a torch on the floor,
  burning oil, the hearth itself once it's kindled), but doesn't drive it off.
  It comes on through the flames.
- **Burning oil** is the one thing it won't walk into.
- **Flash powder** scorches it and dazzles it, but only for **2 turns**.
- Darts pass through it, as they do through any shade. Its touch hits hard
  (5–10) and chills you by 2–3 at a time. It's far too big for a mace to
  knock back. It has 50 HP, and killing it leaves **5 embers**.
- **While you're kindling, every blow it lands smothers the flame** back a
  step. You can't kindle with the Hollow at your side: dazzle it, wall it off
  with fire, or kill it first.

---

## The hall of fame

Every run that ends goes into the hall of fame: won, killed, or given up
from the title screen. Each entry records:

- how it ended ("Killed by a goblin", "Drowned, dragged down by mail",
  "Rekindled the Heartflame")
- the deepest level you reached
- the turns it took
- the embers you gathered
- the monsters you killed
- the date

Runs are ranked like this:

1. Wins come first.
2. Then the deepest runs.
3. Then the most embers.
4. Then the most kills.
5. A quicker win beats a slower one, and a death put off longer beats a quick one.

The closing screen tells you where the run you've just finished stands, and its
**Hall of fame** button (`h`) shows the table with your run picked out. From
the closing screen, `Enter` starts a new game, `Esc` goes back to the title, and
`q` quits.

The hall is kept next to the save, in `~/.local/share/shadesdeep/halloffame.txt`.
It's plain text, one run per line. Lines are only ever added, so nothing in it
is lost. `shadesdeep --hall-of-fame` prints it in the terminal. Games played
with `--no-save` aren't recorded.

Beside it, `runlog.txt` keeps a line of numbers each time a run reaches a new
depth and one when it ends: health, gear, embers, potions, and how much damage
each kind of monster has done to you. The game never reads it; it's there so
the difficulty can be tuned from real runs. It's safe to delete.

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
- **Strike first, in the dark.** With your torch snuffed, most sleepers stay
  asleep until your blow lands, and that blow does triple damage with any weapon.
  A dagger and a shadow cloak make it five times, and they rarely wake first.
- **Tail a band.** A band walking between rooms has its back to you. Follow
  it in the dark, catch the last one up, and your first blow is a sneak attack.
  Once it stops to look around, keep your distance.
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
- **Flash, then strike.** A dazzled monster can't fight back, and every blow
  lands. With a dagger, a flash turns a fight into an ambush.
- **Flash, then oil.** Dazzled monsters don't step out of the fire, so a
  flash followed by an oil flask on the same pack burns them turn after turn.
- **Burn the doorway behind you.** Oil thrown into a doorway or corridor
  you've just come through stops anything following you, shades included,
  for a dozen turns or so.
- **Oil stretches the clock.** When torches are short, an oil flask poured on
  your torch is 50 turns of light. Thrown, it's a weapon and a wall. You
  have to choose.
- **Salt is a safe night's sleep** against shades, but not against anything
  else. Pour it in a dead end where goblins won't wander.
- **Search the dead ends.** A short passage that stops at a blank wall often
  has a door at the end of it. Light your torch and press `s`.
- **Don't camp too long.** Bands of wanderers come in from the stairs now and
  then. Rest where you can see them coming, or behind a door.
- **Split a band at a door.** Hold a doorway or corridor so they come at you one
  at a time, and catch the leader unawares if you can.
- **Shut the door and wait.** Running from a goblin? Get through a door, close
  it, and stand right in front of it. When it opens the door it's standing
  next to you, unready, and your blow is a sneak attack. A dagger makes that
  five times the damage.
- **A door stops rats cold.** Shut one between you and a pack of rats and they
  lose interest.
- **Carry a lit torch where traps are likely**, in corridors and at the
  doors of storerooms, and you'll usually see them first.
- **A mace and a known trapdoor** make a fine pair: knock the ogre in.
- **Save something for the bottom.** Two twists of flash powder, or a flask
  of oil between you and the Hollow, buy the three turns the Heartflame needs.
  Keep a spare torch, too.
- **Light the hall's sconces first** if you can reach them. The Hollow burns
  in their light, and the other shades can't follow you into it.
