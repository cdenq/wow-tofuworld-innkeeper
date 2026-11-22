# wow-tofuworld-innkeeper

AzerothCore module for the Tofu Worldview server (mod-playerbots fork of azerothcore-wotlk, WotLK 3.3.5a). It adds Kreemo <Innkeeper>, an innkeeper whose menus are built from the module conf and can be changed live with `.reload config`.

## What it does

- SQL (`data/sql/db-world/updates/tofu_innkeeper_2026_11_20_00_kreemo.sql`, applied by the worldserver's updater as a module file): creature_template 900003 Kreemo, subname Innkeeper, level 80, faction 35, npcflag 69761 (gossip, vendor, repair, innkeeper), ScriptName `npc_tofu_innkeeper`, and creature_template_model 900003 with display 7346 (the Gadgetzan innkeeper's goblin model). No spawn; the server spawns him with a Tofu custom SQL file.
- C++ (`src/tofu_innkeeper.cpp`):
  - First page: the greeting, the stock innkeeper buttons (Make this inn your home., Let me browse your goods. while Kreemo has `npc_vendor` items, Trick or Treat! during Hallow's End), then the conf menus.
  - Every conf menu opens a page with its paragraph, its submenus, and Back, unless it is a teleport button (`.Teleport`) or a vendor button (`.Vendor`).
  - A vendor button opens another `npc_vendor` list (a multivendor list; the entry needs no creature_template row) in Kreemo's vendor window, which keeps his Repair and sell functions. The shipped conf has Ammunition, list 900043 (custom SQL `tofu_2026_11_22_06_kreemo_ammo_vendor` in wow-tofuworld-internal). The button is hidden while that list has no items.
  - The conf is read at startup and on `.reload config`. Each page's text is sent to the client right before the page (`SMSG_NPC_TEXT_UPDATE`), overwriting the client's cached copy, so players never delete their Cache folder after a text change.
  - A page opened before a reload falls back to the first page when clicked, since its buttons may point at menus that moved.

## Install

1. Copy this repo folder, with `.git`, into the deploy set `WoW Projects\modules` under the folder name `mod-tofu-innkeeper` (the core calls the loader `Addmod_tofu_innkeeperScripts` from that folder name), and run `Update Source.bat` (answer Y to Rebuild anyway).
2. Copy `tofu_innkeeper.conf.dist` to `tofu_innkeeper.conf` in `C:\Build\bin\RelWithDebInfo\configs\modules`.
3. Start the servers. Test with `.npc add temp 900003` as a GM (a temporary spawn that is not saved to the database; plain `.npc add` would write a permanent `creature` row outside the custom SQL files).

## Configuration

| Key | Default | Meaning |
|---|---|---|
| `TofuInnkeeper.Enable` | 1 | Custom menus on |
| `TofuInnkeeper.HallowsEnd` | 1 | Trick or Treat! button during Hallow's End |
| `TofuInnkeeper.Greeting` | Kreemo's greeting | First page text |
| `TofuInnkeeper.InnLabel` | empty (stock text) | Hearthstone button label |
| `TofuInnkeeper.VendorLabel` | empty (stock text) | Vendor button label |
| `TofuInnkeeper.BackLabel` | Back | Back button label |
| `TofuInnkeeper.TextIdBase` | 900100 | First of 100 text IDs used by the pages; no `npc_text` row may use the range |
| `TofuInnkeeper.Menu.<id>.Label` | | Button text (required) |
| `TofuInnkeeper.Menu.<id>.Text` | the label | Paragraph of the page the button opens |
| `TofuInnkeeper.Menu.<id>.Order` | 100 | Sort among siblings, lowest first; ties by id |
| `TofuInnkeeper.Menu.<id>.Icon` | 0 | Button icon 0 to 20 |
| `TofuInnkeeper.Menu.<id>.Teleport` | empty | `map x y z orientation`; the button teleports and closes the menu instead of opening a page |
| `TofuInnkeeper.Menu.<id>.HideWithin` | 0 | Yards; hides a teleport button from players within this distance of its destination (0 = always shown) |
| `TofuInnkeeper.Menu.<id>.Vendor` | 0 | `npc_vendor` entry; the button opens that list in Kreemo's vendor window and closes the menu instead of opening a page; hidden while the list is empty; not with Teleport |

A dot in an id makes a submenu: `news.systems` is a button on the page of `news`. Ids are letters, digits, and underscores. Limits: 99 menus; 29 buttons on the first page, 31 on the others. Values cannot contain double quotes (the core's conf parser removes them); `$n` is the player's name, `$c` the class, `$B` a line break.

## Updating the menus

1. Edit `configs\modules\tofu_innkeeper.conf` (keep a dated `.bak-<yyyymmdd>` copy next to it).
2. In game as a GM, or at the worldserver console: `.reload config` (console: `reload config`).
3. Check Server.log for `TofuInnkeeper: reloaded N menu node(s)` and for any `TofuInnkeeper:` warning.

## Check

- Server.log at startup: `TofuInnkeeper: loaded 25 menu node(s) (3 on the root page), text IDs 900100 to 900125.` with the shipped conf.
- SQL (acore_world): `SELECT entry, name, subname, npcflag, ScriptName FROM creature_template WHERE entry = 900003;` = 900003, Kreemo, Innkeeper, 69761, npc_tofu_innkeeper.
- In game: Kreemo shows the greeting with Make this inn your home., Let me browse your goods., Ammunition, Guild Hub (away from the hub), and What's new in Azeroth?; Ammunition opens the 14 arrows and bullets with the Repair buttons; What's new opens Systems, Content, Features, and Back, each listing its entries; Make this inn your home. asks to bind the hearthstone there.
