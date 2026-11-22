/*
 * wow-tofuworld-innkeeper (deployed as mod-tofu-innkeeper)
 *
 * Kreemo <Innkeeper>, creature entry 900003, ScriptName npc_tofu_innkeeper.
 * Root page: the greeting, the stock innkeeper buttons (bind hearthstone, vendor when the creature has npc_vendor rows, Hallow's End trick-or-treat), then the conf menu nodes.
 * Menu nodes come from TofuInnkeeper.Menu.<id>.<Field> keys in tofu_innkeeper.conf. A dotted id is a child of the id before its last dot (news.systems is a child of news).
 * A node with a Teleport field (map x y z orientation) is a teleport button: clicking it closes the menu and teleports the player; it is hidden from players already within HideWithin yards of the destination.
 * A node with a Vendor field (an npc_vendor entry) is a vendor button: clicking it opens that vendor list (a multivendor list, which needs no creature_template row) in Kreemo's own vendor window, with his Repair buttons; it is hidden while that list has no items.
 * The conf is read at startup and on .reload config, so menus change live without a rebuild or restart.
 * Page text is pushed to the client with SMSG_NPC_TEXT_UPDATE right before each menu is sent, which overwrites the client's cached copy of that text ID, so players never need to delete their Cache folder.
 */

#include "Config.h"
#include "Creature.h"
#include "CreatureScript.h"
#include "GameEventMgr.h"
#include "GridDefines.h"
#include "Log.h"
#include "NPCHandler.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include "WorldSession.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace TofuInnkeeper
{
    // Stock innkeeper values, from the core's npc_innkeeper script and stock gossip menus.
    constexpr uint32 SPELL_TRICK = 24714;
    constexpr uint32 SPELL_TREAT = 24715;
    constexpr uint32 SPELL_TRICKED_OR_TREATED = 24755;
    constexpr uint16 EVENT_HALLOWS_END = 12;
    constexpr uint32 STOCK_INN_MENU = 9733;     // option 1: "Make this inn your home.", option 2: "Let me browse your goods."
    constexpr uint32 STOCK_INN_OPTION_BIND = 1;
    constexpr uint32 STOCK_INN_OPTION_VENDOR = 2;
    constexpr uint32 STOCK_EVENT_MENU = 342;    // option 0: Hallow's End "Trick or Treat!"

    // Gossip action values. Menu nodes use ACTION_NODE_BASE + node index.
    constexpr uint32 ACTION_BIND = 1;
    constexpr uint32 ACTION_VENDOR = 2;
    constexpr uint32 ACTION_TRICK = 3;
    constexpr uint32 ACTION_ROOT = 4;
    constexpr uint32 ACTION_NODE_BASE = 100;

    constexpr uint32 MAX_NODES = 99;            // text IDs TextIdBase + 1 to TextIdBase + 99; the root page uses TextIdBase
    constexpr uint32 MAX_ICON = 20;             // GOSSIP_ICON_CHAT_20
    constexpr char const* KEY_PREFIX = "TofuInnkeeper.Menu.";

    struct Node
    {
        std::string id;
        std::string label;
        std::string text;
        int32 order = 100;
        uint32 icon = 0;
        int32 parent = -1;                      // -1 means the root page
        bool teleport = false;                  // true when the node has a valid Teleport field
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        float hideWithin = 0.0f;                // yards; 0 means always shown
        uint32 vendorEntry = 0;                 // npc_vendor entry opened by a vendor button; 0 means not a vendor button
        std::vector<uint32> children;           // node indexes, sorted by order then id
    };

    struct Tree
    {
        uint32 generation = 0;
        bool enable = true;
        bool hallowsEnd = true;
        std::string greeting;
        std::string innLabel;                   // empty means the stock text from menu 9733
        std::string vendorLabel;                // empty means the stock text from menu 9733
        std::string backLabel;
        uint32 textIdBase = 900100;
        std::vector<Node> nodes;
        std::vector<uint32> rootChildren;
    };

    // The live tree. Readers take a copy of the shared pointer under the lock and then use the immutable tree without the lock, so a reload never changes a tree that a gossip handler is reading.
    std::mutex sTreeLock;
    std::shared_ptr<Tree const> sTree;
    uint32 sGeneration = 0;

    std::shared_ptr<Tree const> GetTree()
    {
        std::lock_guard<std::mutex> guard(sTreeLock);
        return sTree;
    }

    uint32 Depth(std::string const& id)
    {
        return uint32(std::count(id.begin(), id.end(), '.'));
    }

    bool IsValidId(std::string const& id)
    {
        if (id.empty() || id.front() == '.' || id.back() == '.' || id.find("..") != std::string::npos)
            return false;

        for (char c : id)
        {
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
            if (!ok)
                return false;
        }

        return true;
    }

    // Parses "map x y z orientation" into the node. Map validity is checked by Player::TeleportTo at click time, because the map store is not loaded yet when the conf is first read.
    bool ParseTeleport(std::string const& value, Node& node)
    {
        std::istringstream in(value);
        uint32 mapId;
        float x, y, z, o;
        std::string extra;
        if (!(in >> mapId >> x >> y >> z >> o) || (in >> extra))
            return false;

        if (!Acore::IsValidMapCoord(x, y, z, o))
            return false;

        node.teleport = true;
        node.mapId = mapId;
        node.x = x;
        node.y = y;
        node.z = z;
        node.o = o;
        return true;
    }

    bool IsHiddenFor(Player* player, Node const& node)
    {
        return node.teleport && node.hideWithin > 0.0f && player->GetMapId() == node.mapId && player->IsWithinDist3d(node.x, node.y, node.z, node.hideWithin);
    }

    // A vendor button needs the vendor flag on the creature (the buy and sell handlers check it) and at least one loaded item in its npc_vendor list.
    bool HasVendorList(Creature* creature, uint32 vendorEntry)
    {
        if (!creature->IsVendor())
            return false;

        VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(vendorEntry);
        return items && !items->Empty();
    }

    bool IsShownTo(Player* player, Creature* creature, Node const& node)
    {
        if (IsHiddenFor(player, node))
            return false;
        if (node.vendorEntry && !HasVendorList(creature, node.vendorEntry))
            return false;
        return true;
    }

    void SortChildren(Tree& tree, std::vector<uint32>& children)
    {
        std::sort(children.begin(), children.end(), [&tree](uint32 a, uint32 b)
        {
            Node const& na = tree.nodes[a];
            Node const& nb = tree.nodes[b];
            if (na.order != nb.order)
                return na.order < nb.order;
            return na.id < nb.id;
        });
    }

    void LoadTree(bool reload)
    {
        auto tree = std::make_shared<Tree>();

        tree->enable = sConfigMgr->GetOption<bool>("TofuInnkeeper.Enable", true);
        tree->hallowsEnd = sConfigMgr->GetOption<bool>("TofuInnkeeper.HallowsEnd", true);
        tree->greeting = sConfigMgr->GetOption<std::string>("TofuInnkeeper.Greeting", "Sigh... these stooopid keeds...");
        tree->innLabel = sConfigMgr->GetOption<std::string>("TofuInnkeeper.InnLabel", "", false);
        tree->vendorLabel = sConfigMgr->GetOption<std::string>("TofuInnkeeper.VendorLabel", "", false);
        tree->backLabel = sConfigMgr->GetOption<std::string>("TofuInnkeeper.BackLabel", "Back");
        tree->textIdBase = sConfigMgr->GetOption<uint32>("TofuInnkeeper.TextIdBase", 900100);

        if (tree->backLabel.empty())
            tree->backLabel = "Back";

        // Collect the node ids from the keys TofuInnkeeper.Menu.<id>.<Field>.
        std::string const prefix = KEY_PREFIX;
        std::map<std::string, bool> ids;        // id, has a Label key
        for (std::string const& key : sConfigMgr->GetKeysByString(prefix))
        {
            std::string rest = key.substr(prefix.size());
            std::size_t dot = rest.rfind('.');
            if (dot == std::string::npos)
            {
                LOG_WARN("module", "TofuInnkeeper: key '{}' has no field name (expected {}<id>.Label, .Text, .Order, .Icon, .Teleport, .HideWithin, or .Vendor). Skipped.", key, prefix);
                continue;
            }

            std::string id = rest.substr(0, dot);
            std::string field = rest.substr(dot + 1);

            if (!IsValidId(id))
            {
                LOG_WARN("module", "TofuInnkeeper: key '{}' has an invalid menu id '{}' (letters, digits, and underscores, with dots between levels). Skipped.", key, id);
                continue;
            }

            if (field == "Label")
                ids[id] = true;
            else if (field == "Text" || field == "Order" || field == "Icon" || field == "Teleport" || field == "HideWithin" || field == "Vendor")
                ids.emplace(id, false);
            else
                LOG_WARN("module", "TofuInnkeeper: key '{}' has an unknown field '{}' (expected Label, Text, Order, Icon, Teleport, HideWithin, or Vendor). Skipped.", key, field);
        }

        // Parents before children: sort by depth, then by id.
        std::vector<std::string> sorted;
        for (auto const& [id, hasLabel] : ids)
        {
            if (!hasLabel)
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' has no {}{}.Label key. Skipped with its submenus.", id, prefix, id);
                continue;
            }
            sorted.push_back(id);
        }

        std::sort(sorted.begin(), sorted.end(), [](std::string const& a, std::string const& b)
        {
            uint32 da = Depth(a);
            uint32 db = Depth(b);
            if (da != db)
                return da < db;
            return a < b;
        });

        std::map<std::string, uint32> indexOf;
        for (std::string const& id : sorted)
        {
            int32 parent = -1;
            std::size_t dot = id.rfind('.');
            if (dot != std::string::npos)
            {
                std::string parentId = id.substr(0, dot);
                auto itr = indexOf.find(parentId);
                if (itr == indexOf.end())
                {
                    LOG_WARN("module", "TofuInnkeeper: menu '{}' has no parent menu '{}' (missing, skipped, or without a Label). Skipped.", id, parentId);
                    continue;
                }
                parent = int32(itr->second);
            }

            if (tree->nodes.size() >= MAX_NODES)
            {
                LOG_ERROR("module", "TofuInnkeeper: more than {} menus; '{}' and every later menu are skipped.", MAX_NODES, id);
                break;
            }

            Node node;
            node.id = id;
            node.parent = parent;
            node.label = sConfigMgr->GetOption<std::string>(prefix + id + ".Label", "", false);
            node.text = sConfigMgr->GetOption<std::string>(prefix + id + ".Text", "", false);
            node.order = sConfigMgr->GetOption<int32>(prefix + id + ".Order", 100, false);
            node.icon = sConfigMgr->GetOption<uint32>(prefix + id + ".Icon", 0, false);

            if (node.label.empty())
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' has an empty Label. Skipped with its submenus.", id);
                continue;
            }

            if (node.icon > MAX_ICON)
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' has Icon {} (allowed 0 to {}). Using 0.", id, node.icon, MAX_ICON);
                node.icon = 0;
            }

            std::string teleport = sConfigMgr->GetOption<std::string>(prefix + id + ".Teleport", "", false);
            if (!teleport.empty())
            {
                if (!ParseTeleport(teleport, node))
                {
                    LOG_WARN("module", "TofuInnkeeper: menu '{}' has Teleport '{}' (expected map x y z orientation). Skipped with its submenus.", id, teleport);
                    continue;
                }

                node.hideWithin = sConfigMgr->GetOption<float>(prefix + id + ".HideWithin", 0.0f, false);
                if (!(node.hideWithin >= 0.0f))
                {
                    LOG_WARN("module", "TofuInnkeeper: menu '{}' has HideWithin {} (must be 0 or more). Using 0.", id, node.hideWithin);
                    node.hideWithin = 0.0f;
                }
            }

            node.vendorEntry = sConfigMgr->GetOption<uint32>(prefix + id + ".Vendor", 0, false);
            if (node.vendorEntry && node.teleport)
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' has both Teleport and Vendor (a button can do only one). Skipped with its submenus.", id);
                continue;
            }

            if (parent >= 0 && (tree->nodes[parent].teleport || tree->nodes[parent].vendorEntry))
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' is under the {} menu '{}', which opens no page. Skipped.", id, tree->nodes[parent].teleport ? "teleport" : "vendor", tree->nodes[parent].id);
                continue;
            }

            uint32 index = uint32(tree->nodes.size());
            indexOf[id] = index;
            tree->nodes.push_back(node);

            if (parent < 0)
                tree->rootChildren.push_back(index);
            else
                tree->nodes[parent].children.push_back(index);
        }

        // Sort the buttons on every page and enforce the client's 32-button limit (the root keeps 3 slots for the stock buttons, every other page 1 for Back).
        SortChildren(*tree, tree->rootChildren);
        if (tree->rootChildren.size() > GOSSIP_MAX_MENU_ITEMS - 3)
        {
            LOG_WARN("module", "TofuInnkeeper: the root page has {} menus; only the first {} are shown.", tree->rootChildren.size(), GOSSIP_MAX_MENU_ITEMS - 3);
            tree->rootChildren.resize(GOSSIP_MAX_MENU_ITEMS - 3);
        }

        for (Node& node : tree->nodes)
        {
            SortChildren(*tree, node.children);
            if (node.children.size() > GOSSIP_MAX_MENU_ITEMS - 1)
            {
                LOG_WARN("module", "TofuInnkeeper: menu '{}' has {} submenus; only the first {} are shown.", node.id, node.children.size(), GOSSIP_MAX_MENU_ITEMS - 1);
                node.children.resize(GOSSIP_MAX_MENU_ITEMS - 1);
            }
        }

        {
            std::lock_guard<std::mutex> guard(sTreeLock);
            tree->generation = ++sGeneration;
            sTree = tree;
        }

        LOG_INFO("module", "TofuInnkeeper: {} {} menu node(s) ({} on the root page), text IDs {} to {}.", reload ? "reloaded" : "loaded", tree->nodes.size(), tree->rootChildren.size(), tree->textIdBase, tree->textIdBase + uint32(tree->nodes.size()));
    }

    // Overwrites the client's cached copy of textId with the given text (same packet layout as WorldSession::HandleNpcTextQueryOpcode).
    void SendNpcText(Player* player, uint32 textId, std::string const& text)
    {
        WorldPacket data(SMSG_NPC_TEXT_UPDATE, 100 + text.size() * 2);
        data << textId;
        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            data << float(i == 0 ? 1.0f : 0.0f);   // probability: always the first variant
            data << text;                          // male text
            data << text;                          // female text
            data << uint32(0);                     // language: universal
            for (uint8 j = 0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                data << uint32(0);                 // emote delay
                data << uint32(0);                 // emote
            }
        }
        player->GetSession()->SendPacket(&data);
    }

    bool HasVendorItems(Creature* creature)
    {
        if (!creature->IsVendor())
            return false;

        VendorItemData const* items = creature->GetVendorItems();
        return items && !items->Empty();
    }

    // index -1 is the root page.
    void SendPage(Player* player, Creature* creature, Tree const& tree, int32 index)
    {
        ClearGossipMenuFor(player);

        uint32 const sender = tree.generation;
        std::vector<uint32> const* children = &tree.rootChildren;
        std::string text = tree.greeting;
        uint32 textId = tree.textIdBase;

        if (index < 0)
        {
            if (tree.hallowsEnd && IsEventActive(EVENT_HALLOWS_END) && !player->HasAura(SPELL_TRICKED_OR_TREATED))
                AddGossipItemFor(player, STOCK_EVENT_MENU, uint32(0), sender, ACTION_TRICK);

            if (creature->IsInnkeeper())
            {
                if (tree.innLabel.empty())
                    AddGossipItemFor(player, STOCK_INN_MENU, STOCK_INN_OPTION_BIND, sender, ACTION_BIND);
                else
                    AddGossipItemFor(player, GOSSIP_ICON_INTERACT_2, tree.innLabel, sender, ACTION_BIND);
            }

            if (HasVendorItems(creature))
            {
                if (tree.vendorLabel.empty())
                    AddGossipItemFor(player, STOCK_INN_MENU, STOCK_INN_OPTION_VENDOR, sender, ACTION_VENDOR);
                else
                    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, tree.vendorLabel, sender, ACTION_VENDOR);
            }
        }
        else
        {
            Node const& node = tree.nodes[index];
            children = &node.children;
            text = node.text.empty() ? node.label : node.text;
            textId = tree.textIdBase + 1 + uint32(index);
        }

        for (uint32 child : *children)
        {
            Node const& c = tree.nodes[child];
            if (!IsShownTo(player, creature, c))
                continue;
            AddGossipItemFor(player, c.icon, c.label, sender, ACTION_NODE_BASE + child);
        }

        if (index >= 0)
        {
            int32 parent = tree.nodes[index].parent;
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, tree.backLabel, sender, parent < 0 ? ACTION_ROOT : ACTION_NODE_BASE + uint32(parent));
        }

        SendNpcText(player, textId, text);
        SendGossipMenuFor(player, textId, creature->GetGUID());
    }
}

class npc_tofu_innkeeper : public CreatureScript
{
public:
    npc_tofu_innkeeper() : CreatureScript("npc_tofu_innkeeper") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        auto tree = TofuInnkeeper::GetTree();
        if (!tree || !tree->enable)
            return false;   // core default gossip

        player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
        TofuInnkeeper::SendPage(player, creature, *tree, -1);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        using namespace TofuInnkeeper;

        ClearGossipMenuFor(player);

        auto tree = GetTree();
        if (!tree || !tree->enable)
        {
            CloseGossipMenuFor(player);
            return true;
        }

        // A menu opened before a .reload config carries the old generation; its node indexes may now point elsewhere, so start over at the root.
        if (sender != tree->generation)
        {
            SendPage(player, creature, *tree, -1);
            return true;
        }

        switch (action)
        {
            case ACTION_BIND:
                CloseGossipMenuFor(player);
                if (creature->IsInnkeeper())
                    player->SetBindPoint(creature->GetGUID());
                return true;
            case ACTION_VENDOR:
                CloseGossipMenuFor(player);
                if (HasVendorItems(creature))
                    player->GetSession()->SendListInventory(creature->GetGUID());
                return true;
            case ACTION_TRICK:
                CloseGossipMenuFor(player);
                if (tree->hallowsEnd && IsEventActive(EVENT_HALLOWS_END) && !player->HasAura(SPELL_TRICKED_OR_TREATED))
                {
                    player->CastSpell(player, SPELL_TRICKED_OR_TREATED, true);
                    creature->CastSpell(player, roll_chance_i(50) ? SPELL_TRICK : SPELL_TREAT, true);
                }
                return true;
            case ACTION_ROOT:
                SendPage(player, creature, *tree, -1);
                return true;
            default:
                break;
        }

        if (action >= ACTION_NODE_BASE && action - ACTION_NODE_BASE < tree->nodes.size())
        {
            Node const& node = tree->nodes[action - ACTION_NODE_BASE];
            if (node.teleport)
            {
                CloseGossipMenuFor(player);
                if (!IsHiddenFor(player, node) && !player->TeleportTo(node.mapId, node.x, node.y, node.z, node.o))
                    LOG_WARN("module", "TofuInnkeeper: teleport '{}' to map {} ({}, {}, {}) failed for {}.", node.id, node.mapId, node.x, node.y, node.z, player->GetName());
                return true;
            }

            if (node.vendorEntry)
            {
                CloseGossipMenuFor(player);
                if (HasVendorList(creature, node.vendorEntry))
                    player->GetSession()->SendListInventory(creature->GetGUID(), node.vendorEntry);
                return true;
            }

            SendPage(player, creature, *tree, int32(action - ACTION_NODE_BASE));
        }
        else
            SendPage(player, creature, *tree, -1);

        return true;
    }
};

class TofuInnkeeper_World : public WorldScript
{
public:
    TofuInnkeeper_World() : WorldScript("TofuInnkeeper_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        TofuInnkeeper::LoadTree(reload);
    }
};

void AddSC_tofu_innkeeper()
{
    new TofuInnkeeper_World();
    new npc_tofu_innkeeper();
}
