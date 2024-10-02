/*
 * Credits: silviu20092
 */

#include <numeric>
#include <iomanip>
#include "Item.h"
#include "Config.h"
#include "Tokenize.h"
#include "StringConvert.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "Chat.h"
#include "SpellMgr.h"
#include "item_upgrade.h"

ItemUpgrade::ItemUpgrade()
{
    reloading = false;
}

ItemUpgrade::~ItemUpgrade()
{
    for (auto& pageData : playerPagedData)
        pageData.second.Reset();
}

ItemUpgrade* ItemUpgrade::instance()
{
    static ItemUpgrade instance;
    return &instance;
}

bool ItemUpgrade::IsAllowedStatType(uint32 statType) const
{
    return FindInContainer(allowedStats, statType) != nullptr;
}

void ItemUpgrade::LoadAllowedStats(const std::string& stats)
{
    allowedStats.clear();
    std::vector<std::string_view> tokenized = Acore::Tokenize(stats, ',', false);
    std::transform(tokenized.begin(), tokenized.end(), std::back_inserter(allowedStats),
        [](const std::string_view& str) { return *Acore::StringTo<uint32>(str); });
}

bool ItemUpgrade::GetBoolConfig(ItemUpgradeBoolConfigs index) const
{
    return cfg.GetBoolConfig(index);
}

std::string ItemUpgrade::GetStringConfig(ItemUpgradeStringConfigs index) const
{
    return cfg.GetStringConfig(index);
}

float ItemUpgrade::GetFloatConfig(ItemUpgradeFloatConfigs index) const
{
    return cfg.GetFloatConfig(index);
}

int32 ItemUpgrade::GetIntConfig(ItemUpgradeIntConfigs index) const
{
    return cfg.GetIntConfig(index);
}

void ItemUpgrade::LoadConfig()
{
    cfg.Initialize();
    LoadAllowedStats(cfg.GetStringConfig(CONFIG_ITEM_UPGRADE_ALLOWED_STATS));
}

void ItemUpgrade::LoadFromDB(bool reload)
{
    LOG_INFO("server.loading", " ");
    LOG_INFO("server.loading", "Loading item upgrade mod custom tables...");

    CleanupDB(reload);

    LoadAllowedItems();
    LoadBlacklistedItems();
    LoadAllowedStatsItems();
    LoadBlacklistedStatsItems();
    LoadStatRequirements();
    LoadStatRequirementsOverrides();

    LoadUpgradeStats();
    if (!CheckDataValidity())
    {
        LOG_ERROR("server.loading", "Found data validity errors while loading item upgrade mod tables. Check the FATAL error messages and fix the issues before attempting to restart the server");
        World::StopNow(ERROR_EXIT_CODE);
        return;
    }

    LoadCharacterUpgradeData();

    CreateUpgradesPctMap();
}

void ItemUpgrade::LoadAllowedItems()
{
    allowedItems.clear();

    QueryResult result = CharacterDatabase.Query("SELECT entry FROM mod_item_upgrade_allowed_items");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].Get<uint32>();
        const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(entry);
        if (!itemTemplate)
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_allowed_items` has invalid item entry {}, skip", entry);
            continue;
        }

        allowedItems.insert(entry);
    } while (result->NextRow());
}

void ItemUpgrade::LoadAllowedStatsItems()
{
    allowedStatItems.clear();

    QueryResult result = CharacterDatabase.Query("SELECT stat_id, entry FROM mod_item_upgrade_allowed_stats_items");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 stat_id = fields[0].Get<uint32>();
        uint32 entry = fields[1].Get<uint32>();
        const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(entry);
        if (!itemTemplate)
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_allowed_stats_items` has invalid item entry {}, skip", entry);
            continue;
        }

        allowedStatItems[stat_id].insert(entry);
    } while (result->NextRow());
}

void ItemUpgrade::LoadBlacklistedItems()
{
    blacklistedItems.clear();

    QueryResult result = CharacterDatabase.Query("SELECT entry FROM mod_item_upgrade_blacklisted_items");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].Get<uint32>();
        const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(entry);
        if (!itemTemplate)
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_blacklisted_items` has invalid item entry {}, skip", entry);
            continue;
        }

        blacklistedItems.insert(entry);
    } while (result->NextRow());
}

void ItemUpgrade::LoadBlacklistedStatsItems()
{
    blacklistedStatItems.clear();

    QueryResult result = CharacterDatabase.Query("SELECT stat_id, entry FROM mod_item_upgrade_blacklisted_stats_items");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 stat_id = fields[0].Get<uint32>();
        uint32 entry = fields[1].Get<uint32>();
        const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(entry);
        if (!itemTemplate)
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_blacklisted_stats_items` has invalid item entry {}, skip", entry);
            continue;
        }

        blacklistedStatItems[stat_id].insert(entry);
    } while (result->NextRow());
}

void ItemUpgrade::CleanupDB(bool reload)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append("DELETE FROM mod_item_upgrade_stats_req WHERE stat_id NOT IN (SELECT id FROM mod_item_upgrade_stats)");
    trans->Append("DELETE FROM mod_item_upgrade_stats_req_override WHERE stat_id NOT IN (SELECT id FROM mod_item_upgrade_stats)");
    trans->Append("DELETE FROM character_item_upgrade WHERE stat_id NOT IN (SELECT id FROM mod_item_upgrade_stats)");
    if (!reload)
        trans->Append("DELETE FROM character_item_upgrade WHERE NOT EXISTS (SELECT 1 FROM item_instance WHERE item_instance.guid = character_item_upgrade.item_guid)");
    trans->Append("DELETE FROM mod_item_upgrade_allowed_stats_items WHERE stat_id NOT IN (SELECT id FROM mod_item_upgrade_stats)");
    trans->Append("DELETE FROM mod_item_upgrade_blacklisted_stats_items WHERE stat_id NOT IN (SELECT id FROM mod_item_upgrade_stats)");
    CharacterDatabase.DirectCommitTransaction(trans);
}

void ItemUpgrade::MergeStatRequirements(std::unordered_map<uint32, StatRequirementContainer>& statRequirementMap)
{
    for (auto& statPair : statRequirementMap)
    {
        StatRequirementContainer newStatReq;

        float copperTotal = std::accumulate(statPair.second.begin(), statPair.second.end(), 0.0f,
            [](float a, const UpgradeStatReq& req) { return a + (req.reqType == REQ_TYPE_COPPER ? req.reqVal1 : 0.0f); });
        if (copperTotal > 0.0f)
        {
            int32 val = static_cast<int32>(copperTotal);
            if (val < 1 || val > MAX_MONEY_AMOUNT)
                LOG_ERROR("sql.sql", "Stat requirement has invalid total copper amount for stat id {}, skip", statPair.first);
            else
                newStatReq.push_back(UpgradeStatReq(statPair.first, REQ_TYPE_COPPER, copperTotal));
        }

        float honorTotal = std::accumulate(statPair.second.begin(), statPair.second.end(), 0.0f,
            [](float a, const UpgradeStatReq& req) { return a + (req.reqType == REQ_TYPE_HONOR ? req.reqVal1 : 0.0f); });
        if (honorTotal > 0.0f)
        {
            int32 val = static_cast<int32>(honorTotal);
            if (val < 1 || val > sWorld->getIntConfig(CONFIG_MAX_HONOR_POINTS))
                LOG_ERROR("sql.sql", "Stat requirement has invalid total honor points for stat id {}, skip", statPair.first);
            else
                newStatReq.push_back(UpgradeStatReq(statPair.first, REQ_TYPE_HONOR, honorTotal));
        }

        float arenaTotal = std::accumulate(statPair.second.begin(), statPair.second.end(), 0.0f,
            [](float a, const UpgradeStatReq& req) { return a + (req.reqType == REQ_TYPE_ARENA ? req.reqVal1 : 0.0f); });
        if (arenaTotal > 0.0f)
        {
            int32 val = static_cast<int32>(arenaTotal);
            if (val < 1 || val > sWorld->getIntConfig(CONFIG_MAX_ARENA_POINTS))
                LOG_ERROR("sql.sql", "Stat requirement has invalid total arena points for stat id {}, skip", statPair.first);
            else
                newStatReq.push_back(UpgradeStatReq(statPair.first, REQ_TYPE_ARENA, arenaTotal));
        }

        std::unordered_map<uint32, uint32> itemCountMap;
        for (const UpgradeStatReq& req : statPair.second)
        {
            if (req.reqType != REQ_TYPE_ITEM)
                continue;

            itemCountMap[(uint32)req.reqVal1] += (uint32)req.reqVal2;
        }
        if (!itemCountMap.empty())
        {
            for (const auto& itemPair : itemCountMap)
                newStatReq.push_back(UpgradeStatReq(statPair.first, REQ_TYPE_ITEM, itemPair.first, itemPair.second));
        }

        StatRequirementContainer::const_iterator citer = std::find_if(statPair.second.begin(), statPair.second.end(),
            [&](const UpgradeStatReq& req) { return req.reqType == REQ_TYPE_NONE; });
        if (citer != statPair.second.end()) {
            newStatReq.push_back(UpgradeStatReq(statPair.first, REQ_TYPE_NONE));
        }

        statPair.second = newStatReq;
    }
}

void ItemUpgrade::LoadStatRequirements()
{
    baseStatRequirements.clear();

    QueryResult result = CharacterDatabase.Query("SELECT id, stat_id, req_type, req_val1, req_val2 FROM mod_item_upgrade_stats_req");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 statId = fields[1].Get<uint32>();
        uint8 reqType = fields[2].Get<uint8>();
        if (!IsValidReqType(reqType))
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_stats_req` has invalid `req_type` {}, skip", reqType);
            continue;
        }
        float reqVal1 = fields[3].Get<float>();
        float reqVal2 = fields[4].Get<float>();
        if (!ValidateReq(fields[0].Get<uint32>(), (UpgradeStatReqType)reqType, reqVal1, reqVal2, "mod_item_upgrade_stats_req"))
            continue;

        UpgradeStatReq statReq;
        statReq.statId = statId;
        statReq.reqType = (UpgradeStatReqType)reqType;
        statReq.reqVal1 = reqVal1;
        statReq.reqVal2 = reqVal2;
        baseStatRequirements[statId].push_back(statReq);
    } while (result->NextRow());

    MergeStatRequirements(baseStatRequirements);
}

void ItemUpgrade::LoadStatRequirementsOverrides()
{
    overrideStatRequirements.clear();

    QueryResult result = CharacterDatabase.Query("SELECT id, stat_id, item_entry, req_type, req_val1, req_val2 FROM mod_item_upgrade_stats_req_override");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 statId = fields[1].Get<uint32>();
        uint8 reqType = fields[3].Get<uint8>();
        if (!IsValidReqType(reqType))
        {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_stats_req_override` has invalid `req_type` {}, skip", reqType);
            continue;
        }
        uint32 entry = fields[2].Get<uint32>();
        const ItemTemplate* proto = sObjectMgr->GetItemTemplate(entry);
        if (proto == nullptr) {
            LOG_ERROR("sql.sql", "Table `mod_item_upgrade_stats_req_override` has invalid `item_entry` {}, skip", entry);
            continue;
        }
        float reqVal1 = fields[4].Get<float>();
        float reqVal2 = fields[5].Get<float>();
        if (!ValidateReq(fields[0].Get<uint32>(), (UpgradeStatReqType)reqType, reqVal1, reqVal2, "mod_item_upgrade_stats_req_override"))
            continue;

        UpgradeStatReq statReq;
        statReq.statId = statId;
        statReq.reqType = (UpgradeStatReqType)reqType;
        statReq.reqVal1 = reqVal1;
        statReq.reqVal2 = reqVal2;

        overrideStatRequirements[entry][statId].push_back(statReq);
    } while (result->NextRow());

    for (auto& pair : overrideStatRequirements)
        MergeStatRequirements(pair.second);
}

void ItemUpgrade::LoadUpgradeStats()
{
    upgradeStatList.clear();

    QueryResult result = CharacterDatabase.Query("SELECT id, stat_type, stat_mod_pct, stat_rank FROM mod_item_upgrade_stats");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].Get<uint32>();
        uint32 statType = fields[1].Get<uint32>();
        float statModPct = fields[2].Get<float>();
        uint16 statRank = fields[3].Get<uint16>();

        UpgradeStat upgradeStat;
        upgradeStat.statId = id;
        upgradeStat.statType = statType;
        upgradeStat.statModPct = statModPct;
        upgradeStat.statRank = statRank;
        upgradeStatList.push_back(upgradeStat);
    } while (result->NextRow());
}

void ItemUpgrade::LoadCharacterUpgradeData()
{
    characterUpgradeData.clear();

    uint32 oldMSTime = getMSTime();

    QueryResult result = CharacterDatabase.Query("SELECT guid, item_guid, stat_id FROM character_item_upgrade");
    if (!result)
    {
        LOG_INFO("server.loading", ">> Loaded 0 character item upgrades.");
        LOG_INFO("server.loading", " ");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 guidLow = fields[0].Get<uint32>();
        ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(fields[1].Get<uint32>());
        uint32 statId = fields[2].Get<uint32>();

        CharacterUpgrade characterUpgrade;
        characterUpgrade.guid = guidLow;
        characterUpgrade.itemGuid = itemGuid;
        characterUpgrade.upgradeStat = FindUpgradeStat(statId);
        if (characterUpgrade.upgradeStat == nullptr)
        {
            LOG_ERROR("sql.sql", "Table `character_item_upgrade` has invalid `stat_id` {}, this should never happen, skip", statId);
            continue;
        }
        characterUpgradeData[guidLow].push_back(characterUpgrade);
        count++;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded {} character item upgrades in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
}

bool ItemUpgrade::IsValidReqType(uint8 reqType) const
{
    return reqType >= REQ_TYPE_COPPER && reqType < MAX_REQ_TYPE;
}

bool ItemUpgrade::ValidateReq(uint32 id, UpgradeStatReqType reqType, float val1, float val2, const std::string& table) const
{
    int32 val1Int = static_cast<int32>(val1);
    switch (reqType)
    {
        case ItemUpgrade::REQ_TYPE_COPPER:
            if (val1Int >= 1 && val1Int <= MAX_MONEY_AMOUNT)
                return true;
            LOG_ERROR("sql.sql", "Table `{}` has invalid `req_val1` {} (copper amount) for `id` {}, skip", table, val1, id);
            return false;
        case ItemUpgrade::REQ_TYPE_HONOR:
            if (val1Int >= 1 && val1Int <= sWorld->getIntConfig(CONFIG_MAX_HONOR_POINTS))
                return true;
            LOG_ERROR("sql.sql", "Table `{}` has invalid `req_val1` {} (honor points) for `id` {}, skip", table, val1, id);
            return false;
        case ItemUpgrade::REQ_TYPE_ARENA:
            if (val1Int >= 1 && val1Int <= sWorld->getIntConfig(CONFIG_MAX_ARENA_POINTS))
                return true;
            LOG_ERROR("sql.sql", "Table `{}` has invalid `req_val1` {} (arena points) for `id` {}, skip", table, val1, id);
            return false;
        case ItemUpgrade::REQ_TYPE_ITEM:
        {
            const ItemTemplate* itemTemplate = sObjectMgr->GetItemTemplate(val1Int);
            if (!itemTemplate)
            {
                LOG_ERROR("sql.sql", "Table `{}` has invalid `req_val1` {} (item entry not found) for `id` {}, skip", table, val1, id);
                return false;
            }
            int32 val2Int = static_cast<int32>(val2);
            if (val2Int >= 1)
                return true;
            LOG_ERROR("sql.sql", "Table `{}` has invalid `req_val2` {} (item count invalid) for `id` {}, skip", table, val2, id);
            return false;
        }
        case ItemUpgrade::REQ_TYPE_NONE:
            return true;
    }
    return false;
}

/*static*/ std::string ItemUpgrade::ItemIcon(const ItemTemplate* proto, uint32 width, uint32 height, int x, int y)
{
    std::ostringstream ss;
    ss << "|TInterface";
    const ItemDisplayInfoEntry* dispInfo = nullptr;
    if (proto)
    {
        dispInfo = sItemDisplayInfoStore.LookupEntry(proto->DisplayInfoID);
        if (dispInfo)
            ss << "/ICONS/" << dispInfo->inventoryIcon;
    }
    if (!dispInfo)
        ss << "/InventoryItems/WoWUnknownItem01";
    ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";
    return ss.str();
}

/*static*/ std::string ItemUpgrade::ItemIcon(const ItemTemplate* proto)
{
    return ItemIcon(proto, 30, 30, 0, 0);
}

/*static*/ std::string ItemUpgrade::ItemNameWithLocale(const Player* player, const ItemTemplate* itemTemplate, int32 randomPropertyId)
{
    LocaleConstant loc_idx = player->GetSession()->GetSessionDbLocaleIndex();
    std::string name = itemTemplate->Name1;
    if (ItemLocale const* il = sObjectMgr->GetItemLocale(itemTemplate->ItemId))
        ObjectMgr::GetLocaleString(il->Name, loc_idx, name);

    std::array<char const*, 16> const* suffix = nullptr;
    if (randomPropertyId < 0)
    {
        if (const ItemRandomSuffixEntry* itemRandEntry = sItemRandomSuffixStore.LookupEntry(-randomPropertyId))
            suffix = &itemRandEntry->Name;
    }
    else
    {
        if (const ItemRandomPropertiesEntry* itemRandEntry = sItemRandomPropertiesStore.LookupEntry(randomPropertyId))
            suffix = &itemRandEntry->Name;
    }
    if (suffix)
    {
        std::string_view test((*suffix)[(name != itemTemplate->Name1) ? loc_idx : DEFAULT_LOCALE]);
        if (!test.empty())
        {
            name += ' ';
            name += test;
        }
    }

    return name;
}

/*static*/ std::string ItemUpgrade::ItemLink(const Player* player, const ItemTemplate* itemTemplate, int32 randomPropertyId)
{
    std::stringstream oss;
    oss << "|c";
    oss << std::hex << ItemQualityColors[itemTemplate->Quality] << std::dec;
    oss << "|Hitem:";
    oss << itemTemplate->ItemId;
    oss << ":0:0:0:0:0:0:0:0:0|h[";
    oss << ItemNameWithLocale(player, itemTemplate, randomPropertyId);
    oss << "]|h|r";

    return oss.str();
}

/*static*/ std::string ItemUpgrade::ItemLink(const Player* player, const Item* item)
{
    const ItemTemplate* itemTemplate = item->GetTemplate();
    std::stringstream oss;
    oss << "|c";
    oss << std::hex << ItemQualityColors[itemTemplate->Quality] << std::dec;
    oss << "|Hitem:";
    oss << itemTemplate->ItemId;
    oss << ":" << item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
    oss << ":" << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT);
    oss << ":" << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_2);
    oss << ":" << item->GetEnchantmentId(SOCK_ENCHANTMENT_SLOT_3);
    oss << ":" << item->GetEnchantmentId(BONUS_ENCHANTMENT_SLOT);
    oss << ":" << item->GetItemRandomPropertyId();
    oss << ":" << item->GetItemSuffixFactor();
    oss << ":" << (uint32)item->GetOwner()->GetLevel();
    oss << "|h[" << ItemNameWithLocale(player, itemTemplate, item->GetItemRandomPropertyId());
    oss << "]|h|r";

    return oss.str();
}

/*static*/ void ItemUpgrade::SendMessage(const Player* player, const std::string& message)
{
    ChatHandler(player->GetSession()).SendSysMessage(message);
}

void ItemUpgrade::PagedData::Reset()
{
    totalPages = 0;
    for (Identifier* identifier : data)
        delete identifier;
    data.clear();
}

void ItemUpgrade::PagedData::CalculateTotals()
{
    totalPages = data.size() / PAGE_SIZE;
    if (data.size() % PAGE_SIZE != 0)
        totalPages++;
}

void ItemUpgrade::PagedData::SortAndCalculateTotals()
{
    if (data.size() > 0)
    {
        std::sort(data.begin(), data.end(), CompareIdentifier);
        CalculateTotals();
    }
}

bool ItemUpgrade::PagedData::IsEmpty() const
{
    return data.empty();
}

const ItemUpgrade::Identifier* ItemUpgrade::PagedData::FindIdentifierById(uint32 id) const
{
    std::vector<Identifier*>::const_iterator citer = std::find_if(data.begin(), data.end(), [&](const Identifier* idnt) { return idnt->id == id; });
    if (citer != data.end())
        return *citer;
    return nullptr;
}

void ItemUpgrade::BuildUpgradableItemCatalogue(const Player* player, PagedDataType type)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.item.guid = ObjectGuid::Empty;
    pagedData.upgradeStat = nullptr;
    pagedData.type = type;

    std::vector<Item*> playerItems = GetPlayerItems(player, false);
    std::vector<Item*>::iterator iter = playerItems.begin();
    for (iter; iter != playerItems.end(); ++iter)
        AddItemToPagedData(*iter, player, pagedData);

    pagedData.SortAndCalculateTotals();
}

bool ItemUpgrade::IsValidItemForUpgrade(const Item* item, const Player* player) const
{
    if (!item)
        return false;

    if (item->GetOwnerGUID() != player->GetGUID())
        return false;

    if (LoadItemStatInfo(item).empty())
        return false;

    const ItemTemplate* proto = item->GetTemplate();
    if (proto->Quality == ITEM_QUALITY_HEIRLOOM)
        return false;

    if (item->IsBroken())
        return false;

    return true;
}

void ItemUpgrade::AddItemToPagedData(const Item* item, const Player* player, PagedData& pagedData)
{
    if (IsValidItemForUpgrade(item, player))
    {
        const ItemTemplate* proto = item->GetTemplate();

        ItemIdentifier* itemIdentifier = new ItemIdentifier();
        itemIdentifier->id = pagedData.data.size();
        itemIdentifier->guid = item->GetGUID();
        itemIdentifier->name = ItemNameWithLocale(player, proto, item->GetItemRandomPropertyId());
        itemIdentifier->uiName = ItemLinkForUI(item, player);

        pagedData.data.push_back(itemIdentifier);
    }
}

ItemUpgrade::PagedData& ItemUpgrade::GetPagedData(const Player* player)
{
    return playerPagedData[player->GetGUID().GetCounter()];
}

ItemUpgrade::PagedDataMap& ItemUpgrade::GetPagedDataMap()
{
    return playerPagedData;
}

bool ItemUpgrade::_AddPagedData(Player* player, const PagedData& pagedData, uint32 page) const
{
    const std::vector<Identifier*>& data = pagedData.data;
    if (data.size() == 0 || (page + 1) > pagedData.totalPages)
        return false;

    uint32 lowIndex = page * PagedData::PAGE_SIZE;
    if (data.size() <= lowIndex)
        return false;

    uint32 highIndex = lowIndex + PagedData::PAGE_SIZE - 1;
    if (highIndex >= data.size())
        highIndex = data.size() - 1;

    std::unordered_map<uint32, const UpgradeStat*> upgrades;
    Item* item = nullptr;
    if (pagedData.type == PAGED_DATA_TYPE_STATS || pagedData.type == PAGED_DATA_TYPE_REQS || pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS_STATS
        || pagedData.type == PAGED_DATA_TYPE_STATS_BULK || pagedData.type == PAGED_DATA_TYPE_STAT_UPGRADE_BULK || pagedData.type == PAGED_DATA_TYPE_REQS_BULK)
    {
        item = player->GetItemByGuid(pagedData.item.guid);
        if (!IsValidItemForUpgrade(item, player))
            return false;

        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, ItemLinkForUI(item, player), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);

        if (pagedData.type == PAGED_DATA_TYPE_STATS)
        {
            std::vector<_ItemStat> statTypes = LoadItemStatInfo(item);
            std::ostringstream ossStatTypes;
            ossStatTypes << "可升级: ";
            for (uint32 i = 0; i < statTypes.size(); i++)
            {
                if (IsAllowedStatType(statTypes[i].ItemStatType))
                    ossStatTypes << StatTypeToString(statTypes[i].ItemStatType);
                else
                    ossStatTypes << "|cffb50505" << StatTypeToString(statTypes[i].ItemStatType) << "|r";
                if (i < statTypes.size() - 1)
                    ossStatTypes << ", ";
            }
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ossStatTypes.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        }
        else if (pagedData.type == PAGED_DATA_TYPE_REQS)
        {
            const UpgradeStat* upgradeStat = pagedData.upgradeStat;
            std::vector<_ItemStat> statInfoList = LoadItemStatInfo(item);
            const _ItemStat* statInfo = GetStatByType(statInfoList, upgradeStat->statType);
            if (!statInfo)
                return false;

            std::ostringstream oss;
            oss << "升级 " << StatTypeToString(upgradeStat->statType) << " [物品等级 " << upgradeStat->statRank << "]";
            oss << " " << "[" << upgradeStat->statModPct << "% 增加 - ";
            oss << "|cffb50505" << statInfo->ItemStatValue << "|r --> ";
            oss << "|cff056e3a" << CalculateModPct(statInfo->ItemStatValue, upgradeStat) << "|r]";

            const UpgradeStat* currentUpgrade = FindUpgradeForItem(player, item, upgradeStat->statType);
            if (currentUpgrade != nullptr)
                oss << " [当前: " << CalculateModPct(statInfo->ItemStatValue, currentUpgrade) << "|r]";

            std::pair<uint32, uint32> itemLevel = CalculateItemLevel(player, item, upgradeStat);
            std::ostringstream ilvloss;
            ilvloss << "[物品等级 ";
            ilvloss << "|cffb50505" << itemLevel.first << "|r --> ";
            ilvloss << "|cff056e3a" << itemLevel.second << "|r]";
            itemLevel = CalculateItemLevel(player, item);
            ilvloss << " [当前: " << itemLevel.second << "]";
            
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, oss.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ilvloss.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        }
        else if (pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS_STATS)
        {
            std::pair<uint32, uint32> itemLevel = CalculateItemLevel(player, item);
            uint32 diff = itemLevel.second - itemLevel.first;

            std::ostringstream oss;
            oss << "物品等级增加 " << diff;
            oss << " [|cffb50505" << itemLevel.first << "|r --> ";
            oss << " |cff056e3a" << itemLevel.second << "|r]";

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, oss.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);

            if (!item->IsEquipped())
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "[装备物品]", GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + 1);
        }
        else if (pagedData.type == PAGED_DATA_TYPE_STAT_UPGRADE_BULK)
        {
            upgrades = FindAllUpgradeableRanks(player, item, pagedData.pct);
            std::ostringstream oss;
            if (upgrades.empty())
                oss << "[物品等级 |cffb50505将不会|r 增加, 没有要应用的升级]";
            else
            {
                std::pair<uint32, uint32> ilvl = CalculateItemLevel(player, item, upgrades);
                std::pair<uint32, uint32> currentIlvl = CalculateItemLevel(player, item);
                oss << "[物品等级 |cffb50505" << ilvl.first << "|r --> " << "|cff056e3a" << ilvl.second << "|r]";
                oss << " [当前: " << currentIlvl.second << "]";
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, oss.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "升级总数: " + Acore::ToString(pagedData.data.size()), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);

        uint32 totalUpgrades = 0;
        for (const Identifier* idnt : pagedData.data)
        {
            ItemIdentifier* itemIdnt = (ItemIdentifier*)idnt;
            Item* item = player->GetItemByGuid(itemIdnt->guid);
            if (item)
                totalUpgrades += FindUpgradesForItem(player, item).size();
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "升级总数: " + Acore::ToString(totalUpgrades), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
    }
    else if (pagedData.type == PAGED_DATA_TYPE_ITEMS_FOR_PURGE)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cffb50505清除升级|r", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        const ItemTemplate* proto = sObjectMgr->GetItemTemplate((uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_PURGE_TOKEN));
        if (proto != nullptr)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "清除后将收到:", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);

            std::ostringstream oss;
            oss << ItemIcon(proto);
            oss << ItemLink(player, proto, 0);
            oss << " " << (uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_PURGE_TOKEN_COUNT) << "x";
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, oss.str(), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        }
        if (GetBoolConfig(CONFIG_ITEM_UPGRADE_REFUND_ALL_ON_PURGE))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cff056e3a将在清除时退还所有花费|r", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "选择要清除的升级项目:", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page);
    }

    for (uint32 i = lowIndex; i <= highIndex; i++)
    {
        const Identifier* identifier = data[i];
        if (pagedData.type != PAGED_DATA_TYPE_ITEMS_FOR_PURGE)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, identifier->uiName, GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + identifier->id);
        else
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, identifier->uiName, GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + identifier->id, "您确定要删除所有升级吗？此操作无法撤消!", 0, false);
    }

    if (pagedData.type == PAGED_DATA_TYPE_REQS)
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, (MeetsRequirement(player, pagedData.upgradeStat, item) ? "|cff056e3a[点击升级]|r" : "|cffb50505[点击升级]|r"), GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + 1, "您确定要升级吗?", 0, false);

    if (!upgrades.empty())
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "[升级条件]", GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + 1);
        StatRequirementContainer reqs = BuildBulkRequirements(upgrades, item);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, (MeetsRequirement(player, &reqs) ? "|cff056e3a[全部升级]|r" : "|cffb50505[全部升级]|r"), GOSSIP_SENDER_MAIN + 1, GOSSIP_ACTION_INFO_DEF + 2, "您确定要升级吗?", 0, false);
    }

    if (page + 1 < pagedData.totalPages)
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "[下一页] ->", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + page + 1);

    uint32 pageZeroSender = GOSSIP_SENDER_MAIN;
    if (pagedData.type == PAGED_DATA_TYPE_STATS)
        pageZeroSender += 9;
    else if (pagedData.type == PAGED_DATA_TYPE_REQS)
        pageZeroSender += 10;
    else if (pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS_STATS)
        pageZeroSender += 11;
    else if (pagedData.type == PAGED_DATA_TYPE_STATS_BULK)
        pageZeroSender += 12;
    else if (pagedData.type == PAGED_DATA_TYPE_STAT_UPGRADE_BULK)
        pageZeroSender += 13;
    else if (pagedData.type == PAGED_DATA_TYPE_REQS_BULK)
        pageZeroSender += 14;

    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<- [返回]", page == 0 ? pageZeroSender : GOSSIP_SENDER_MAIN + 2, page == 0 ? GOSSIP_ACTION_INFO_DEF : GOSSIP_ACTION_INFO_DEF + page - 1);

    return true;
}

bool ItemUpgrade::AddPagedData(Player* player, Creature* creature, uint32 page)
{
    ClearGossipMenuFor(player);
    PagedData& pagedData = GetPagedData(player);
    while (!_AddPagedData(player, pagedData, page))
    {
        if (page == 0)
        {
            NoPagedData(player, pagedData);
            break;
        }
        else
            page--;
    }
    
    pagedData.currentPage = page;

    SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    return true;
}

void ItemUpgrade::NoPagedData(Player* player, const PagedData& pagedData) const
{
    if (pagedData.type == PAGED_DATA_TYPE_ITEMS || pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS || pagedData.type == PAGED_DATA_TYPE_ITEMS_BULK)
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cffb50505这页什么都没有|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
    else if (pagedData.type == PAGED_DATA_TYPE_STATS || pagedData.type == PAGED_DATA_TYPE_STATS_BULK || pagedData.type == PAGED_DATA_TYPE_STAT_UPGRADE_BULK)
    {
        Item* item = player->GetItemByGuid(pagedData.item.guid);
        if (IsValidItemForUpgrade(item, player))
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, ItemLinkForUI(item, player), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cffb50505物品无法升级|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
    }
    else if (pagedData.type == PAGED_DATA_TYPE_ITEMS_FOR_PURGE)
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cffb50505无需清除|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<- [第一页]", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
}

bool ItemUpgrade::TakePagedDataAction(Player* player, Creature* creature, uint32 action)
{
    PagedData& pagedData = GetPagedData(player);
    if (pagedData.type == PAGED_DATA_TYPE_ITEMS)
    {
        Item* item = FindItemIdentifierFromPage(pagedData, action, player);
        if (item == nullptr)
            SendMessage(player, "物品不可再升级.");
        else
        {
            BuildStatsUpgradeCatalogue(player, item);
            return AddPagedData(player, creature, 0);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_STATS)
    {
        const Identifier* identifier = pagedData.FindIdentifierById(action);
        if (identifier != nullptr)
        {
            Item* item = player->GetItemByGuid(pagedData.item.guid);
            if (!IsValidItemForUpgrade(item, player))
                SendMessage(player, "物品不可再升级.");
            else
            {
                const UpgradeStat* upgradeStat = FindUpgradeStat(identifier->id);
                if (upgradeStat == nullptr)
                    SendMessage(player, "不能再升级.");
                else
                {
                    const UpgradeStat* playerUpgrade = FindUpgradeForItem(player, item, upgradeStat->statType);
                    if (playerUpgrade != nullptr)
                    {
                        if (!FindUpgradeStat(upgradeStat->statType, playerUpgrade->statRank + 1))
                        {
                            SendMessage(player, "物品属性已到最高等级.");
                            BuildStatsUpgradeCatalogue(player, item);
                            return AddPagedData(player, creature, pagedData.currentPage);
                        }
                    }

                    if (!CanApplyUpgradeForItem(item, upgradeStat))
                    {
                        SendMessage(player, "此物品等级不适用于 " + ItemLink(player, item));
                        BuildStatsUpgradeCatalogue(player, item);
                        return AddPagedData(player, creature, pagedData.currentPage);
                    }

                    BuildStatsRequirementsCatalogue(player, upgradeStat, item);
                    return AddPagedData(player, creature, 0);
                }
            }
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_REQS)
    {
        if (action == 0)
        {
            Item* item = player->GetItemByGuid(pagedData.item.guid);
            if (!IsValidItemForUpgrade(item, player))
                SendMessage(player, "物品不可再升级.");
            else
            {
                BuildStatsRequirementsCatalogue(player, pagedData.upgradeStat, item);
                return AddPagedData(player, creature, pagedData.currentPage);
            }
        }
        else
        {
            if (!PurchaseUpgrade(player))
                SendMessage(player, "无法处理升级.除非该物品不再可用，否则不应发生这种情况.");
            else
            {
                CloseGossipMenuFor(player);
                return true;
            }
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS)
    {
        Item* item = FindItemIdentifierFromPage(pagedData, action, player);
        if (item == nullptr)
            SendMessage(player, "物品不再可用.");
        else
        {
            BuildItemUpgradeStatsCatalogue(player, item);
            return AddPagedData(player, creature, 0);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_UPGRADED_ITEMS_STATS)
    {
        Item* item = player->GetItemByGuid(pagedData.item.guid);
        if (!IsValidItemForUpgrade(item, player))
            SendMessage(player, "物品因属性不可再升级.");
        else
        {
            if (action == 1)
                EquipItem(player, item);

            BuildItemUpgradeStatsCatalogue(player, item);
            return AddPagedData(player, creature, pagedData.currentPage);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_ITEMS_FOR_PURGE)
    {
        Item* item = FindItemIdentifierFromPage(pagedData, action, player);
        if (item == nullptr)
            SendMessage(player, "物品因损坏不再可用.");
        else
        {
            if (PurgeUpgrade(player, item))
                VisualFeedback(player);

            BuildAlreadyUpgradedItemsCatalogue(player, ItemUpgrade::PAGED_DATA_TYPE_ITEMS_FOR_PURGE);
            return AddPagedData(player, creature, pagedData.currentPage);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_ITEMS_BULK)
    {
        Item* item = FindItemIdentifierFromPage(pagedData, action, player);
        if (item == nullptr)
            SendMessage(player, "物品因拆散不再可用.");
        else
        {
            BuildStatsUpgradeCatalogueBulk(player, item);
            return AddPagedData(player, creature, 0);
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_STATS_BULK)
    {
        const Identifier* identifier = pagedData.FindIdentifierById(action);
        if (identifier != nullptr && identifier->GetType() == UPGRADE_BULK_IDENTIFIER)
        {
            Item* item = player->GetItemByGuid(pagedData.item.guid);
            if (!IsValidItemForUpgrade(item, player))
                SendMessage(player, "物品因属性拆散不再可用.");
            else
            {
                const UpgradeBulkIdentifier* bulkIdentifier = (UpgradeBulkIdentifier*)identifier;
                BuildStatsUpgradeByPctCatalogueBulk(player, item, bulkIdentifier->modPct);
                return AddPagedData(player, creature, 0);
            }
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_STAT_UPGRADE_BULK)
    {
        Item* item = player->GetItemByGuid(pagedData.item.guid);
        if (!IsValidItemForUpgrade(item, player))
            SendMessage(player, "物品因属性拆散不再可升级.");
        else
        {
            if (action == 0)
            {
                BuildStatsUpgradeByPctCatalogueBulk(player, item, pagedData.pct);
                return AddPagedData(player, creature, 0);
            }
            else if (action == 1)
            {
                BuildStatsRequirementsCatalogueBulk(player, item, pagedData.pct);
                return AddPagedData(player, creature, 0);
            }
            else if (action == 2)
            {
                if (!PurchaseUpgradeBulk(player))
                    SendMessage(player, "无法处理升级.除非该物品不再可用，否则不应发生这种情况.");
                else
                {
                    CloseGossipMenuFor(player);
                    return true;
                }
            }
        }
    }
    else if (pagedData.type == PAGED_DATA_TYPE_REQS_BULK)
    {
        Item* item = player->GetItemByGuid(pagedData.item.guid);
        if (!IsValidItemForUpgrade(item, player))
            SendMessage(player, "物品不可升级.");
        else
        {
            BuildStatsRequirementsCatalogueBulk(player, item, pagedData.pct);
            return AddPagedData(player, creature, 0);
        }
    }

    CloseGossipMenuFor(player);
    return false;
}

Item* ItemUpgrade::FindItemIdentifierFromPage(const PagedData& pagedData, uint32 id, Player* player) const
{
    const Identifier* identifier = pagedData.FindIdentifierById(id);
    if (identifier != nullptr && identifier->GetType() == ITEM_IDENTIFIER)
    {
        const ItemIdentifier* itemIdentifier = (ItemIdentifier*)identifier;
        Item* item = player->GetItemByGuid(itemIdentifier->guid);
        if (IsValidItemForUpgrade(item, player))
            return item;
    }

    return nullptr;
}

bool ItemUpgrade::HandlePurchaseRank(Player* player, Item* item, const UpgradeStat* upgrade)
{
    const UpgradeStat* foundUpgrade = FindUpgradeForItem(player, item, upgrade->statType);
    std::vector<CharacterUpgrade>& upgrades = characterUpgradeData[player->GetGUID().GetCounter()];
    if (foundUpgrade != nullptr)
    {
        std::vector<CharacterUpgrade>::const_iterator citer = std::remove_if(upgrades.begin(), upgrades.end(),
            [&](const CharacterUpgrade& upgrade) { return upgrade.itemGuid == item->GetGUID() && upgrade.upgradeStat->statId == foundUpgrade->statId; });
        if (citer == upgrades.end())
            return false;
        upgrades.erase(citer, upgrades.end());

        CharacterDatabase.Execute("UPDATE character_item_upgrade SET stat_id = {} WHERE guid = {} AND item_guid = {} AND stat_id = {}",
            upgrade->statId, player->GetGUID().GetCounter(), item->GetGUID().GetCounter(), foundUpgrade->statId);
    }
    else
        AddItemUpgradeToDB(player, item, upgrade);

    CharacterUpgrade newUpgrade;
    newUpgrade.guid = player->GetGUID().GetCounter();
    newUpgrade.itemGuid = item->GetGUID();
    newUpgrade.upgradeStat = upgrade;
    upgrades.push_back(newUpgrade);

    return true;
}

bool ItemUpgrade::PurchaseUpgrade(Player* player)
{
    PagedData& pagedData = GetPagedData(player);
    if (!pagedData.upgradeStat)
        return false;

    Item* item = player->GetItemByGuid(pagedData.item.guid);
    if (!item)
        return false;

    if (!MeetsRequirement(player, pagedData.upgradeStat, item))
    {
        SendMessage(player, "您不符合购买此升级的要求.");
        return true;
    }

    if (item->IsEquipped())
        player->_ApplyItemMods(item, item->GetSlot(), false);

    HandlePurchaseRank(player, item, pagedData.upgradeStat);

    if (item->IsEquipped())
        player->_ApplyItemMods(item, item->GetSlot(), true);

    TakeRequirements(player, pagedData.upgradeStat, item);

    VisualFeedback(player);
    SendMessage(player, "物品成功升级!");

    SendItemPacket(player, item);

    return true;
}

bool ItemUpgrade::PurchaseUpgradeBulk(Player* player)
{
    PagedData& pagedData = GetPagedData(player);
    Item* item = player->GetItemByGuid(pagedData.item.guid);
    if (!item)
        return false;

    std::unordered_map<uint32, const UpgradeStat*> upgrades = FindAllUpgradeableRanks(player, item, pagedData.pct);
    if (upgrades.empty())
        return false;

    StatRequirementContainer reqs = BuildBulkRequirements(upgrades, item);
    if (!MeetsRequirement(player, &reqs))
    {
        SendMessage(player, "您不符合购买这些升级的要求.");
        return true;
    }

    if (item->IsEquipped())
        player->_ApplyItemMods(item, item->GetSlot(), false);

    for (const auto& upair : upgrades)
        HandlePurchaseRank(player, item, upair.second);

    if (item->IsEquipped())
        player->_ApplyItemMods(item, item->GetSlot(), true);

    TakeRequirements(player, &reqs);

    VisualFeedback(player);
    SendMessage(player, "物品成功升级!");

    SendItemPacket(player, item);

    return true;
}

int32 ItemUpgrade::HandleStatModifier(const Player* player, uint8 slot, uint32 statType, int32 amount) const
{
    if (amount == 0)
        return 0;

    Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!item)
        return amount;

    return HandleStatModifier(player, item, statType, amount, MAX_ENCHANTMENT_SLOT);
}

int32 ItemUpgrade::HandleStatModifier(const Player* player, Item* item, uint32 statType, int32 amount, EnchantmentSlot slot) const
{
    if (!GetBoolConfig(CONFIG_ITEM_UPGRADE_ENABLED) || !IsAllowedItem(item) || IsBlacklistedItem(item) || !IsAllowedStatType(statType))
        return amount;

    if (slot < MAX_INSPECTED_ENCHANTMENT_SLOT)
        return amount;

    const UpgradeStat* foundUpgrade = FindUpgradeForItem(player, item, statType);
    if (foundUpgrade != nullptr && CanApplyUpgradeForItem(item, foundUpgrade))
        return CalculateModPct(amount, foundUpgrade);

    return amount;
}

void ItemUpgrade::HandleItemRemove(Player* player, Item* item)
{
    if (!FindUpgradesForItem(player, item).empty())
    {
        player->_ApplyItemMods(item, item->GetSlot(), false);
        RemoveItemUpgrade(player, item);
        player->_ApplyItemMods(item, item->GetSlot(), true);
    }
}

void ItemUpgrade::RemoveItemUpgrade(Player* player, Item* item)
{
    std::vector<CharacterUpgrade>& upgrades = characterUpgradeData[player->GetGUID().GetCounter()];
    std::vector<CharacterUpgrade>::const_iterator citer = std::remove_if(upgrades.begin(), upgrades.end(),
        [&](const CharacterUpgrade& upgrade) { return upgrade.itemGuid == item->GetGUID(); });
    upgrades.erase(citer, upgrades.end());

    CharacterDatabase.Execute("DELETE FROM character_item_upgrade WHERE guid = {} AND item_guid = {}", player->GetGUID().GetCounter(), item->GetGUID().GetCounter());
}

void ItemUpgrade::HandleCharacterRemove(uint32 guid)
{
    characterUpgradeData[guid].clear();
}

void ItemUpgrade::BuildRequirementsPage(const Player* player, PagedData& pagedData, const StatRequirementContainer* reqs) const
{
    if (EmptyRequirements(reqs))
    {
        Identifier* identifier = new Identifier();
        identifier->id = 0;
        identifier->name = "0";
        identifier->uiName = "没有要求，可以自由购买";
        pagedData.data.push_back(identifier);
    }
    else
    {
        for (const auto& req : *reqs)
        {
            if (req.reqType == REQ_TYPE_NONE)
                continue;

            std::ostringstream oss;
            switch (req.reqType)
            {
                case REQ_TYPE_COPPER:
                    oss << "金钱: " << CopperToMoneyStr((uint32)req.reqVal1, true);
                    break;
                case REQ_TYPE_HONOR:
                    oss << "荣誉点: " << (uint32)req.reqVal1 << " points";
                    break;
                case REQ_TYPE_ARENA:
                    oss << "竞技点: " << (uint32)req.reqVal1 << " points";
                    break;
                case REQ_TYPE_ITEM:
                {
                    const ItemTemplate* proto = sObjectMgr->GetItemTemplate((uint32)req.reqVal1);
                    oss << ItemIcon(proto);
                    oss << ItemLink(player, proto, 0);
                    if (req.reqVal2 > 1.0f)
                        oss << " - " << (uint32)req.reqVal2 << "x";
                    break;
                }
            }

            std::string missing;
            if (!MeetsRequirement(player, req))
            {
                switch (req.reqType)
                {
                    case REQ_TYPE_COPPER:
                        missing = "缺少 " + CopperToMoneyStr((uint32)req.reqVal1 - player->GetMoney(), true);
                        break;
                    case REQ_TYPE_HONOR:
                        missing = "缺少 " + Acore::ToString<uint32>((uint32)req.reqVal1 - player->GetHonorPoints()) + " 点数";
                        break;
                    case REQ_TYPE_ARENA:
                        missing = "缺少 " + Acore::ToString<uint32>((uint32)req.reqVal1 - player->GetArenaPoints()) + " 点数";
                        break;
                    case REQ_TYPE_ITEM:
                        missing = "缺少 " + Acore::ToString<uint32>((uint32)req.reqVal2 - player->GetItemCount((uint32)req.reqVal1, true)) + " 物品";
                        break;
                }
            }

            oss << " - ";
            if (missing.empty())
                oss << "|cff056e3a已达条件|r";
            else
                oss << "|cffb50505尚未凑齐|r" << " - " << missing;

            Identifier* identifier = new Identifier();
            identifier->id = 0;
            identifier->name = Acore::ToString<uint32>((uint32)req.reqType);
            identifier->uiName = oss.str();
            pagedData.data.push_back(identifier);
        }
    }
}

void ItemUpgrade::BuildStatsRequirementsCatalogue(const Player* player, const UpgradeStat* upgradeStat, const Item* item)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.upgradeStat = upgradeStat;
    pagedData.type = PAGED_DATA_TYPE_REQS;

    BuildRequirementsPage(player, pagedData, GetStatRequirements(upgradeStat, item));

    pagedData.SortAndCalculateTotals();
}

void ItemUpgrade::BuildAlreadyUpgradedItemsCatalogue(const Player* player, PagedDataType type)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.upgradeStat = nullptr;
    pagedData.item.guid = ObjectGuid::Empty;
    pagedData.type = type;

    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            AddUpgradedItemToPagedData(item, player, pagedData, "身上");

    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
        if (Bag* bag = player->GetBagByPos(i))
            for (uint32 j = 0; j < bag->GetBagSize(); j++)
                if (Item* item = player->GetItemByPos(i, j))
                    AddUpgradedItemToPagedData(item, player, pagedData, "背包");

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            AddUpgradedItemToPagedData(item, player, pagedData, "已装备");

    for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            AddUpgradedItemToPagedData(item, player, pagedData, "银行");

    for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
        if (Bag* bag = player->GetBagByPos(i))
            for (uint32 j = 0; j < bag->GetBagSize(); j++)
                if (Item* item = player->GetItemByPos(i, j))
                    AddUpgradedItemToPagedData(item, player, pagedData, "银行仓库");

    pagedData.SortAndCalculateTotals();
}

void ItemUpgrade::AddUpgradedItemToPagedData(const Item* item, const Player* player, PagedData& pagedData, const std::string& from)
{
    const std::vector<const UpgradeStat*> itemUpgrades = FindUpgradesForItem(player, item);
    if (!itemUpgrades.empty())
    {
        const ItemTemplate* proto = item->GetTemplate();

        ItemIdentifier* itemIdentifier = new ItemIdentifier();
        itemIdentifier->id = pagedData.data.size();
        itemIdentifier->guid = item->GetGUID();
        itemIdentifier->name = ItemNameWithLocale(player, proto, item->GetItemRandomPropertyId());
        itemIdentifier->uiName = ItemLinkForUI(item, player) + " [" + from + "]";

        if (!IsAllowedItem(item) || IsBlacklistedItem(item))
            itemIdentifier->uiName += " [|cffb50505未激活|r]";

        pagedData.data.push_back(itemIdentifier);
    }
}

void ItemUpgrade::BuildItemUpgradeStatsCatalogue(const Player* player, const Item* item)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.upgradeStat = nullptr;
    pagedData.item.guid = item->GetGUID();
    pagedData.type = PAGED_DATA_TYPE_UPGRADED_ITEMS_STATS;

    std::vector<const UpgradeStat*> itemUpgrades = FindUpgradesForItem(player, item);
    if (!itemUpgrades.empty())
    {
        std::vector<_ItemStat> statInfo = LoadItemStatInfo(item);
        for (const UpgradeStat* upgradeStat : itemUpgrades)
        {
            const _ItemStat* foundStat = GetStatByType(statInfo, upgradeStat->statType);
            if (!foundStat)
                continue;

            std::string statTypeStr = StatTypeToString(upgradeStat->statType);

            std::ostringstream oss;
            oss << "升级 " << statTypeStr << " [装等 " << upgradeStat->statRank << "]";
            oss << " " << "[" << upgradeStat->statModPct << "% 增加 - ";
            oss << "|cffb50505" << foundStat->ItemStatValue << "|r --> ";
            oss << "|cff056e3a" << CalculateModPct(foundStat->ItemStatValue, upgradeStat) << "|r]";

            if (!IsAllowedItem(item)
                || IsBlacklistedItem(item)
                || !IsAllowedStatType(upgradeStat->statType)
                || !CanApplyUpgradeForItem(item, upgradeStat))
                oss << " [|cffb50505INACTIVE|r]";

            Identifier* identifier = new Identifier();
            identifier->id = 0;
            identifier->name = statTypeStr;
            identifier->uiName = oss.str();
            pagedData.data.push_back(identifier);
        }
    }

    pagedData.SortAndCalculateTotals();
}

bool ItemUpgrade::MeetsRequirement(const Player* player, const UpgradeStatReq& req) const
{
    switch (req.reqType)
    {
        case REQ_TYPE_COPPER:
            return player->HasEnoughMoney((int32)req.reqVal1);
        case REQ_TYPE_HONOR:
            return player->GetHonorPoints() >= (uint32)req.reqVal1;
        case REQ_TYPE_ARENA:
            return player->GetArenaPoints() >= (uint32)req.reqVal1;
        case REQ_TYPE_ITEM:
            return player->HasItemCount((uint32)req.reqVal1, (uint32)req.reqVal2, true);
        case REQ_TYPE_NONE:
            return true;
    }

    return false;
}

bool ItemUpgrade::MeetsRequirement(const Player* player, const UpgradeStat* upgradeStat, const Item* item) const
{
    return MeetsRequirement(player, GetStatRequirements(upgradeStat, item));
}

bool ItemUpgrade::MeetsRequirement(const Player* player, const StatRequirementContainer* reqs) const
{
    if (EmptyRequirements(reqs))
        return true;

    for (const auto& req : *reqs)
        if (!MeetsRequirement(player, req))
            return false;

    return true;
}

void ItemUpgrade::TakeRequirements(Player* player, const UpgradeStat* upgradeStat, const Item* item)
{
    TakeRequirements(player, GetStatRequirements(upgradeStat, item));
}

void ItemUpgrade::TakeRequirements(Player* player, const StatRequirementContainer* reqs)
{
    if (EmptyRequirements(reqs))
        return;

    for (const auto& req : *reqs)
    {
        switch (req.reqType)
        {
            case REQ_TYPE_COPPER:
                player->ModifyMoney(-(int32)req.reqVal1);
                break;
            case REQ_TYPE_HONOR:
                player->ModifyHonorPoints(-(int32)req.reqVal1);
                break;
            case REQ_TYPE_ARENA:
                player->ModifyArenaPoints(-(int32)req.reqVal1);
                break;
            case REQ_TYPE_ITEM:
                //player->DestroyItemCount((uint32)req.reqVal1, (uint32)req.reqVal2 - 1, true);
                player->DestroyItemCount((uint32)req.reqVal1, (uint32)req.reqVal2 - 1, true, true);
                break;
        }
    }
}

void ItemUpgrade::BuildStatsUpgradeCatalogue(const Player* player, const Item* item)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.item.guid = item->GetGUID();
    pagedData.upgradeStat = nullptr;
    pagedData.type = PAGED_DATA_TYPE_STATS;

    if (IsAllowedItem(item) && !IsBlacklistedItem(item))
    {
        std::vector<_ItemStat> statInfoList = LoadItemStatInfo(item);
        std::unordered_map<uint32, bool> processed;
        for (const UpgradeStat& stat : upgradeStatList)
        {
            if (processed.find(stat.statType) != processed.end())
                continue;

            if (!IsAllowedStatType(stat.statType))
                continue;

            const _ItemStat* statInfo = GetStatByType(statInfoList, stat.statType);
            if (!statInfo)
                continue;

            processed[stat.statType] = true;

            const UpgradeStat* foundUpgrade = FindUpgradeForItem(player, item, stat.statType);
            const UpgradeStat* currentUpgrade = nullptr;
            bool atMaxRank = false;
            Identifier* identifier = new Identifier();
            std::ostringstream oss;
            oss << "升级 " << StatTypeToString(statInfo->ItemStatType) << " ";
            if (foundUpgrade != nullptr)
            {
                currentUpgrade = foundUpgrade;

                const UpgradeStat* nextUpgrade = FindUpgradeStat(stat.statType, foundUpgrade->statRank + 1);
                if (nextUpgrade == nullptr)
                {
                    oss << "[等级 " << foundUpgrade->statRank << " |cffb50505最大|r]";
                    identifier->id = foundUpgrade->statId;
                    atMaxRank = true;
                }
                else
                {
                    oss << "[等级 " << foundUpgrade->statRank << " -> " << "|cff056e3a" << foundUpgrade->statRank + 1 << "|r" << "]";
                    identifier->id = nextUpgrade->statId;
                    foundUpgrade = nextUpgrade;
                }
            }
            else
            {
                foundUpgrade = FindUpgradeStat(stat.statType, 1);
                if (foundUpgrade == nullptr)
                    continue;

                oss << "[获得等级 1]";
                identifier->id = foundUpgrade->statId;
            }

            oss << " " << "[" << foundUpgrade->statModPct << "% 增加 - ";
            oss << "|cffb50505" << statInfo->ItemStatValue << "|r --> ";
            oss << "|cff056e3a" << CalculateModPct(statInfo->ItemStatValue, foundUpgrade) << "|r]";
            if (currentUpgrade != nullptr)
            {
                oss << " [当前: " << CalculateModPct(statInfo->ItemStatValue, currentUpgrade);
                if (!CanApplyUpgradeForItem(item, currentUpgrade))
                    oss << ", |cffb50505INACTIVE|r" << "]";
                else
                    oss << "]";
            }

            if (!atMaxRank && !CanApplyUpgradeForItem(item, foundUpgrade))
                oss << " [|cffb50505禁止升级|r]";

            identifier->uiName = oss.str();
            identifier->name = StatTypeToString(stat.statType);
            pagedData.data.push_back(identifier);
        }
    }

    pagedData.SortAndCalculateTotals();
}

void ItemUpgrade::CreateUpgradesPctMap()
{
    upgradesPctMap.clear();
    for (const UpgradeStat& ustat : upgradeStatList)
        upgradesPctMap[ustat.statModPct].push_back(&ustat);
}

void ItemUpgrade::BuildStatsUpgradeCatalogueBulk(const Player* player, const Item* item)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.item.guid = item->GetGUID();
    pagedData.upgradeStat = nullptr;
    pagedData.type = PAGED_DATA_TYPE_STATS_BULK;

    if (IsAllowedItem(item) && !IsBlacklistedItem(item))
    {
        for (const auto& upair : upgradesPctMap)
        {
            UpgradeBulkIdentifier* identifier = new UpgradeBulkIdentifier();
            identifier->id = pagedData.data.size();
            identifier->name = "";
            identifier->modPct = upair.first;
            identifier->uiName = "通过以下方式升级所有属性 " + FormatFloat(upair.first) + "%";
            pagedData.data.push_back(identifier);
        }
    }

    pagedData.SortAndCalculateTotals();
}

void ItemUpgrade::BuildStatsUpgradeByPctCatalogueBulk(const Player* player, const Item* item, float pct)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.item.guid = item->GetGUID();
    pagedData.upgradeStat = nullptr;
    pagedData.type = PAGED_DATA_TYPE_STAT_UPGRADE_BULK;
    pagedData.pct = pct;

    if (upgradesPctMap.find(pct) != upgradesPctMap.end())
    {
        const std::vector<const UpgradeStat*>& upgrades = upgradesPctMap.at(pct);
        std::vector<_ItemStat> statInfoList = LoadItemStatInfo(item);
        for (const UpgradeStat* stat : upgrades)
        {
            const _ItemStat* foundStat = GetStatByType(statInfoList, stat->statType);
            if (foundStat == nullptr)
                continue;

            std::ostringstream oss;
            std::string statTypeStr = StatTypeToString(stat->statType);
            if (!IsAllowedStatType(stat->statType))
                oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 物品属性不允许升级";
            else if (!CanApplyUpgradeForItem(item, stat))
                oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 物品装等不允许升级";
            else
            {
                const UpgradeStat* currentUpgrade = FindUpgradeForItem(player, item, stat->statType);
                bool willUpgrade = false;
                if (currentUpgrade != nullptr)
                {
                    const UpgradeStat* nextUpgrade = FindUpgradeStat(stat->statType, currentUpgrade->statRank + 1);
                    if (nextUpgrade == nullptr)
                        oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 已到最大物品等级";
                    else
                    {
                        if (currentUpgrade->statRank == stat->statRank - 1)
                            willUpgrade = true;
                        else if (currentUpgrade->statRank >= stat->statRank)
                            oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 已经获得该阶段升级后的物品等级";
                        else
                            oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 需要获得前一阶段升级的物品等级";
                    }
                }
                else
                {
                    if (stat->statRank == 1)
                        willUpgrade = true;
                    else
                        oss << "|cffb50505将不会|r 升级 " << statTypeStr << ": 需要获得前一阶段升级的物品等级";
                }

                if (willUpgrade)
                {
                    oss << "|cff056e3a将|r 升级 " << statTypeStr << " 到物品等级 " << stat->statRank;
                    oss << " [" << FormatFloat(stat->statModPct) << "% 增加, ";
                    oss << "|cffb50505" << foundStat->ItemStatValue << "|r --> ";
                    oss << "|cff056e3a" << CalculateModPct(foundStat->ItemStatValue, stat) << "|r]";

                    if (currentUpgrade != nullptr)
                        oss << " [当前: " << CalculateModPct(foundStat->ItemStatValue, currentUpgrade) << "]";
                }
            }

            Identifier* identifier = new Identifier();
            identifier->id = 0;
            identifier->name = statTypeStr;
            identifier->uiName = oss.str();
            pagedData.data.push_back(identifier);
        }
    }

    pagedData.SortAndCalculateTotals();
}

void ItemUpgrade::BuildStatsRequirementsCatalogueBulk(const Player* player, const Item* item, float pct)
{
    PagedData& pagedData = GetPagedData(player);
    pagedData.Reset();
    pagedData.item.guid = item->GetGUID();
    pagedData.upgradeStat = nullptr;
    pagedData.type = PAGED_DATA_TYPE_REQS_BULK;

    StatRequirementContainer reqs = BuildBulkRequirements(FindAllUpgradeableRanks(player, item, pct), item);
    BuildRequirementsPage(player, pagedData, &reqs);

    pagedData.SortAndCalculateTotals();
}

ItemUpgrade::StatRequirementContainer ItemUpgrade::BuildBulkRequirements(const std::unordered_map<uint32, const UpgradeStat*>& upgrades, const Item* item) const
{
    StatRequirementContainer reqs;
    if (upgrades.empty())
        return reqs;

    uint64 copper = 0;
    uint32 arena = 0;
    uint32 honor = 0;
    std::unordered_map<uint32, uint32> itemMap;
    for (const auto& upair : upgrades)
    {
        const StatRequirementContainer* ureq = GetStatRequirements(upair.second, item);
        if (EmptyRequirements(ureq))
            continue;

        for (const UpgradeStatReq& statReq : *ureq)
        {
            switch (statReq.reqType)
            {
                case REQ_TYPE_COPPER:
                    copper += (uint32)statReq.reqVal1;
                    break;
                case REQ_TYPE_HONOR:
                    honor += (uint32)statReq.reqVal1;
                    break;
                case REQ_TYPE_ARENA:
                    arena += (uint32)statReq.reqVal1;
                    break;
                case REQ_TYPE_ITEM:
                    itemMap[(uint32)statReq.reqVal1] += (uint32)statReq.reqVal2;
                    break;
            }
        }
    }
    // 新增逻辑：为升级添加一个需求，即需要该物品本身 3 件
    if (item->GetTemplate()->MaxCount == 0)
        itemMap[item->GetEntry()] += 3;
    
    if (copper != 0)
    {
        if (copper > MAX_MONEY_AMOUNT)
            copper = MAX_MONEY_AMOUNT;

        reqs.push_back(UpgradeStatReq(0, REQ_TYPE_COPPER, (float)copper));
    }

    if (honor != 0)
        reqs.push_back(UpgradeStatReq(0, REQ_TYPE_HONOR, (float)honor));

    if (arena != 0)
        reqs.push_back(UpgradeStatReq(0, REQ_TYPE_ARENA, (float)arena));

    for (const auto& ipair : itemMap)
        reqs.push_back(UpgradeStatReq(0, REQ_TYPE_ITEM, (float)ipair.first, (float)ipair.second));

    return reqs;
}

std::unordered_map<uint32, const ItemUpgrade::UpgradeStat*> ItemUpgrade::FindAllUpgradeableRanks(const Player* player, const Item* item, float pct) const
{
    std::unordered_map<uint32, const UpgradeStat*> possibleUpgrades;
    if (upgradesPctMap.find(pct) != upgradesPctMap.end())
    {
        const std::vector<const UpgradeStat*>& upgrades = upgradesPctMap.at(pct);
        std::vector<_ItemStat> statInfoList = LoadItemStatInfo(item);
        for (const UpgradeStat* stat : upgrades)
        {
            const _ItemStat* foundStat = GetStatByType(statInfoList, stat->statType);
            if (foundStat == nullptr)
                continue;

            if (!IsAllowedStatType(stat->statType))
                continue;

            if (!CanApplyUpgradeForItem(item, stat))
                continue;

            const UpgradeStat* currentUpgrade = FindUpgradeForItem(player, item, stat->statType);
            if (currentUpgrade != nullptr)
            {
                const UpgradeStat* nextUpgrade = FindUpgradeStat(stat->statType, currentUpgrade->statRank + 1);
                if (nextUpgrade != nullptr && currentUpgrade->statRank == stat->statRank - 1)
                    possibleUpgrades[stat->statType] = stat;
            }
            else
            {
                if (stat->statRank == 1)
                    possibleUpgrades[stat->statType] = stat;
            }
        }
    }
    return possibleUpgrades;
}

/*static*/ int32 ItemUpgrade::CalculateModPct(int32 value, const UpgradeStat* upgradeStat)
{
    int32 newAmount = (int32)(value * (1 + upgradeStat->statModPct / 100.0f));
    return std::max(newAmount, value + upgradeStat->statRank);
}

/*static*/ bool ItemUpgrade::CompareIdentifier(const Identifier* a, const Identifier* b)
{
    if (a->GetType() == UPGRADE_BULK_IDENTIFIER && b->GetType() == UPGRADE_BULK_IDENTIFIER)
        return ((UpgradeBulkIdentifier*)a)->modPct < ((UpgradeBulkIdentifier*)b)->modPct;

    return a->name < b->name;
}

const _ItemStat* ItemUpgrade::GetStatByType(const std::vector<_ItemStat>& statInfo, uint32 statType) const
{
    std::vector<_ItemStat>::const_iterator citer = std::find_if(statInfo.begin(), statInfo.end(), [&](const _ItemStat& stat) { return stat.ItemStatType == statType; });
    if (citer != statInfo.end())
        return &*citer;
    return nullptr;
}

std::vector<_ItemStat> ItemUpgrade::LoadItemStatInfo(const Item* item) const
{
    std::vector<_ItemStat> statInfo;
    ItemTemplate const* proto = item->GetTemplate();

    for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        if (i >= proto->StatsCount)
            continue;

        uint32 statType = proto->ItemStat[i].ItemStatType;
        if (proto->ItemStat[i].ItemStatValue > 0)
        {
            _ItemStat stat;
            stat.ItemStatType = statType;
            stat.ItemStatValue = proto->ItemStat[i].ItemStatValue;
            statInfo.push_back(stat);
        }
    }

    for (uint32 slot = PROP_ENCHANTMENT_SLOT_0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
    {
        uint32 enchant_id = item->GetEnchantmentId(EnchantmentSlot(slot));
        if (!enchant_id)
            continue;

        SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if (!pEnchant)
            continue;

        for (int s = 0; s < MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS; ++s)
        {
            uint32 enchant_display_type = pEnchant->type[s];
            uint32 enchant_amount = pEnchant->amount[s];
            uint32 enchant_spell_id = pEnchant->spellid[s];

            if (enchant_display_type == ITEM_ENCHANTMENT_TYPE_STAT)
            {
                if (!enchant_amount)
                {
                    ItemRandomSuffixEntry const* item_rand_suffix = sItemRandomSuffixStore.LookupEntry(std::abs(item->GetItemRandomPropertyId()));
                    if (item_rand_suffix)
                    {
                        for (int k = 0; k < MAX_ITEM_ENCHANTMENT_EFFECTS; ++k)
                        {
                            if (item_rand_suffix->Enchantment[k] == enchant_id)
                            {
                                enchant_amount = uint32((item_rand_suffix->AllocationPct[k] * item->GetItemSuffixFactor()) / 10000);
                                break;
                            }
                        }
                    }
                }
                _ItemStat stat;
                stat.ItemStatType = enchant_spell_id;
                stat.ItemStatValue = enchant_amount;
                statInfo.push_back(stat);
            }
        }
    }

    return statInfo;
}

std::string ItemUpgrade::StatTypeToString(uint32 statType) const
{
    static std::unordered_map<uint32, std::string> statTypeToStrMap = {
        {ITEM_MOD_MANA, "法力"}, {ITEM_MOD_HEALTH, "生命"}, {ITEM_MOD_AGILITY, "敏捷"},
        {ITEM_MOD_STRENGTH, "力量"}, {ITEM_MOD_INTELLECT, "智力"}, {ITEM_MOD_SPIRIT, "精神"},
        {ITEM_MOD_STAMINA, "耐力"}, {ITEM_MOD_DEFENSE_SKILL_RATING, "防御等级"}, {ITEM_MOD_DODGE_RATING, "躲闪等级"},
        {ITEM_MOD_PARRY_RATING, "招架等级"}, {ITEM_MOD_BLOCK_RATING, "格挡等级"}, {ITEM_MOD_HIT_MELEE_RATING, "近战命中率"},
        {ITEM_MOD_HIT_RANGED_RATING, "远程命中率"}, {ITEM_MOD_HIT_SPELL_RATING, "法术命中率"}, {ITEM_MOD_CRIT_MELEE_RATING, "近战暴击"},
        {ITEM_MOD_CRIT_RANGED_RATING, "远程暴击"}, {ITEM_MOD_CRIT_SPELL_RATING, "法术暴击"}, {ITEM_MOD_HIT_TAKEN_MELEE_RATING, "近战命中承受率"},
        {ITEM_MOD_HIT_TAKEN_RANGED_RATING, "远程命中承受率"}, {ITEM_MOD_HIT_TAKEN_SPELL_RATING, "法术命中承受率"}, {ITEM_MOD_CRIT_TAKEN_MELEE_RATING, "近战暴击承受率"},
        {ITEM_MOD_CRIT_TAKEN_RANGED_RATING, "远程暴击承受率"}, {ITEM_MOD_CRIT_TAKEN_SPELL_RATING, "法术暴击承受率"}, {ITEM_MOD_HASTE_MELEE_RATING, "近战急速等级"},
        {ITEM_MOD_HASTE_RANGED_RATING, "远程急速等级"}, {ITEM_MOD_HASTE_SPELL_RATING, "法术急速等级"}, {ITEM_MOD_HIT_RATING, "命中等级"},
        {ITEM_MOD_CRIT_RATING, "暴击等级"}, {ITEM_MOD_HIT_TAKEN_RATING, "命中承受率"}, {ITEM_MOD_CRIT_TAKEN_RATING, "暴击承受率"},
        {ITEM_MOD_RESILIENCE_RATING, "韧性等级"}, {ITEM_MOD_HASTE_RATING, "急速等级"}, {ITEM_MOD_EXPERTISE_RATING, "专精"},
        {ITEM_MOD_ATTACK_POWER, "攻击力"}, {ITEM_MOD_RANGED_ATTACK_POWER, "远程攻击力"}, {ITEM_MOD_MANA_REGENERATION, "魔法回复"},
        {ITEM_MOD_ARMOR_PENETRATION_RATING, "护甲穿透"}, {ITEM_MOD_SPELL_POWER, "法术强度"}, {ITEM_MOD_HEALTH_REGEN, "生命回复"},
        {ITEM_MOD_SPELL_PENETRATION, "法术穿透"}, {ITEM_MOD_BLOCK_VALUE, "格挡值"}
    };

    if (statTypeToStrMap.find(statType) != statTypeToStrMap.end())
        return statTypeToStrMap.at(statType);

    return "未知";
}

bool ItemUpgrade::IsValidStatType(uint32 statType) const
{
    return StatTypeToString(statType) != "未知";
}

std::string ItemUpgrade::ItemLinkForUI(const Item* item, const Player* player) const
{
    const ItemTemplate* proto = item->GetTemplate();
    std::ostringstream oss;
    oss << ItemIcon(proto);
    oss << ItemLink(player, proto, item->GetItemRandomPropertyId());
    return oss.str();
}

const ItemUpgrade::UpgradeStat* ItemUpgrade::FindUpgradeStat(uint32 statId) const
{
    UpgradeStatContainer::const_iterator citer = std::find_if(upgradeStatList.begin(), upgradeStatList.end(), [&](const UpgradeStat& stat) { return stat.statId == statId; });
    if (citer != upgradeStatList.end())
        return &*citer;
    return nullptr;
}

const ItemUpgrade::UpgradeStat* ItemUpgrade::FindUpgradeStat(uint32 statType, uint16 rank) const
{
    UpgradeStatContainer::const_iterator citer = std::find_if(upgradeStatList.begin(), upgradeStatList.end(), [&](const UpgradeStat& stat) { return stat.statType == statType && stat.statRank == rank; });
    if (citer != upgradeStatList.end())
        return &*citer;
    return nullptr;
}

std::vector<const ItemUpgrade::UpgradeStat*> ItemUpgrade::FindUpgradesForItem(const Player* player, const Item* item) const
{
    std::vector<const UpgradeStat*> statsForItem;
    if (characterUpgradeData.find(player->GetGUID().GetCounter()) == characterUpgradeData.end())
        return statsForItem;

    const std::vector<CharacterUpgrade>& upgrades = characterUpgradeData.at(player->GetGUID().GetCounter());
    for (auto const& upgrade : upgrades)
        if (upgrade.itemGuid == item->GetGUID())
            statsForItem.push_back(upgrade.upgradeStat);

    return statsForItem;
}

const ItemUpgrade::UpgradeStat* ItemUpgrade::FindUpgradeForItem(const Player* player, const Item* item, uint32 statType) const
{
    std::vector<const UpgradeStat*> statsForItem = FindUpgradesForItem(player, item);
    if (statsForItem.empty())
        return nullptr;

    std::vector<const UpgradeStat*>::const_iterator citer = std::find_if(statsForItem.begin(), statsForItem.end(), [&](const UpgradeStat* upgradeStat) { return upgradeStat->statType == statType; });
    if (citer != statsForItem.end())
        return *citer;

    return nullptr;
}

/*static*/ std::string ItemUpgrade::CopperToMoneyStr(uint32 money, bool colored)
{
    uint32 gold = money / GOLD;
    uint32 silver = (money % GOLD) / SILVER;
    uint32 copper = (money % GOLD) % SILVER;

    std::ostringstream oss;
    if (gold > 0)
    {
        if (colored)
            oss << gold << "|cffb3aa34g|r";
        else
            oss << gold << "g";
    }
    if (silver > 0)
    {
        if (colored)
            oss << silver << "|cff7E7C7Fs|r";
        else
            oss << silver << "s";
    }
    if (copper > 0)
    {
        if (colored)
            oss << copper << "|cff974B29c|r";
        else
            oss << copper << "c";
    }

    return oss.str();
}

/*static*/ std::string ItemUpgrade::FormatFloat(float val, uint32 decimals)
{
    std::ostringstream oss;
    oss << std::setprecision(decimals) << val;
    return oss.str();
}

void ItemUpgrade::SetReloading(bool value)
{
    reloading = value;
}

bool ItemUpgrade::GetReloading() const
{
    return reloading;
}

void ItemUpgrade::HandleDataReload(bool apply)
{
    const SessionMap& sessions = sWorld->GetAllSessions();
    SessionMap::const_iterator itr;
    for (itr = sessions.begin(); itr != sessions.end(); ++itr)
        if (itr->second && itr->second->GetPlayer() && itr->second->GetPlayer()->IsInWorld())
            HandleDataReload(itr->second->GetPlayer(), apply);
}

void ItemUpgrade::HandleDataReload(Player* player, bool apply)
{
    std::vector<Item*> playerItems = GetPlayerItems(player, true);
    std::vector<Item*>::iterator iter = playerItems.begin();
    for (iter; iter != playerItems.end(); ++iter)
    {
        Item* item = *iter;
        if (apply)
            SendItemPacket(player, item);

        if (!item->IsEquipped())
            continue;

        player->_ApplyItemMods(item, item->GetSlot(), apply);
    }
}

std::vector<Item*> ItemUpgrade::GetPlayerItems(const Player* player, bool inBankAlso) const
{
    std::vector<Item*> items;
    //只包含身上已经穿上的装备
    //for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
    //    if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
    //        items.push_back(item);

    //for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    //    if (Bag* bag = player->GetBagByPos(i))
    //        for (uint32 j = 0; j < bag->GetBagSize(); j++)
    //            if (Item* item = player->GetItemByPos(i, j))
    //                items.push_back(item);

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            items.push_back(item);

    if (inBankAlso)
    {
        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                items.push_back(item);

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
            if (Bag* bag = player->GetBagByPos(i))
                for (uint32 j = 0; j < bag->GetBagSize(); j++)
                    if (Item* item = player->GetItemByPos(i, j))
                        items.push_back(item);
    }

    return items;
}

bool ItemUpgrade::IsAllowedItem(const Item* item) const
{
    if (allowedItems.empty())
        return true;

    return allowedItems.find(item->GetEntry()) != allowedItems.end();
}

bool ItemUpgrade::IsBlacklistedItem(const Item* item) const
{
    if (blacklistedItems.empty())
        return false;

    return blacklistedItems.find(item->GetEntry()) != blacklistedItems.end();
}

void ItemUpgrade::SendItemPacket(Player* player, Item* item) const
{
    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(item->GetEntry());
    std::string Name = pProto->Name1;
    std::string Description = pProto->Description;

    int loc_idx = player->GetSession()->GetSessionDbLocaleIndex();
    if (loc_idx >= 0)
    {
        if (ItemLocale const* il = sObjectMgr->GetItemLocale(pProto->ItemId))
        {
            ObjectMgr::GetLocaleString(il->Name, loc_idx, Name);
            ObjectMgr::GetLocaleString(il->Description, loc_idx, Description);
        }
    }
    // guess size
    WorldPacket queryData(SMSG_ITEM_QUERY_SINGLE_RESPONSE, 600);
    queryData << pProto->ItemId;
    queryData << pProto->Class;
    queryData << pProto->SubClass;
    queryData << pProto->SoundOverrideSubclass;
    queryData << Name;
    queryData << uint8(0x00);                                //pProto->Name2; // blizz not send name there, just uint8(0x00); <-- \0 = empty string = empty name...
    queryData << uint8(0x00);                                //pProto->Name3; // blizz not send name there, just uint8(0x00);
    queryData << uint8(0x00);                                //pProto->Name4; // blizz not send name there, just uint8(0x00);
    queryData << pProto->DisplayInfoID;
    queryData << pProto->Quality;
    queryData << pProto->Flags;
    queryData << pProto->Flags2;
    queryData << pProto->BuyPrice;
    queryData << pProto->SellPrice;
    queryData << pProto->InventoryType;
    queryData << pProto->AllowableClass;
    queryData << pProto->AllowableRace;
    if (GetBoolConfig(CONFIG_ITEM_UPGRADE_SEND_PACKETS) && pProto->StatsCount > 0)
        queryData << CalculateItemLevel(player, item).second;
    else
        queryData << pProto->ItemLevel;
    queryData << pProto->RequiredLevel;
    queryData << pProto->RequiredSkill;
    queryData << pProto->RequiredSkillRank;
    queryData << pProto->RequiredSpell;
    queryData << pProto->RequiredHonorRank;
    queryData << pProto->RequiredCityRank;
    queryData << pProto->RequiredReputationFaction;
    queryData << pProto->RequiredReputationRank;
    queryData << int32(pProto->MaxCount);
    queryData << int32(pProto->Stackable);
    queryData << pProto->ContainerSlots;
    queryData << pProto->StatsCount;                         // item stats count
    for (uint32 i = 0; i < pProto->StatsCount; ++i)
    {
        queryData << pProto->ItemStat[i].ItemStatType;
        if (GetBoolConfig(CONFIG_ITEM_UPGRADE_SEND_PACKETS))
            queryData << HandleStatModifier(player, item, pProto->ItemStat[i].ItemStatType, pProto->ItemStat[i].ItemStatValue, MAX_ENCHANTMENT_SLOT);
        else
            queryData << pProto->ItemStat[i].ItemStatValue;
    }
    queryData << pProto->ScalingStatDistribution;            // scaling stats distribution
    queryData << pProto->ScalingStatValue;                   // some kind of flags used to determine stat values column
    for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
    {
        queryData << pProto->Damage[i].DamageMin;
        queryData << pProto->Damage[i].DamageMax;
        queryData << pProto->Damage[i].DamageType;
    }

    // resistances (7)
    queryData << pProto->Armor;
    queryData << pProto->HolyRes;
    queryData << pProto->FireRes;
    queryData << pProto->NatureRes;
    queryData << pProto->FrostRes;
    queryData << pProto->ShadowRes;
    queryData << pProto->ArcaneRes;

    queryData << pProto->Delay;
    queryData << pProto->AmmoType;
    queryData << pProto->RangedModRange;

    for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
    {
        // send DBC data for cooldowns in same way as it used in Spell::SendSpellCooldown
        // use `item_template` or if not set then only use spell cooldowns
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(pProto->Spells[s].SpellId);
        if (spell)
        {
            bool db_data = pProto->Spells[s].SpellCooldown >= 0 || pProto->Spells[s].SpellCategoryCooldown >= 0;

            queryData << pProto->Spells[s].SpellId;
            queryData << pProto->Spells[s].SpellTrigger;
            queryData << int32(pProto->Spells[s].SpellCharges);

            if (db_data)
            {
                queryData << uint32(pProto->Spells[s].SpellCooldown);
                queryData << uint32(pProto->Spells[s].SpellCategory);
                queryData << uint32(pProto->Spells[s].SpellCategoryCooldown);
            }
            else
            {
                queryData << uint32(spell->RecoveryTime);
                queryData << uint32(spell->GetCategory());
                queryData << uint32(spell->CategoryRecoveryTime);
            }
        }
        else
        {
            queryData << uint32(0);
            queryData << uint32(0);
            queryData << uint32(0);
            queryData << uint32(-1);
            queryData << uint32(0);
            queryData << uint32(-1);
        }
    }
    queryData << pProto->Bonding;
    queryData << Description;
    queryData << pProto->PageText;
    queryData << pProto->LanguageID;
    queryData << pProto->PageMaterial;
    queryData << pProto->StartQuest;
    queryData << pProto->LockID;
    queryData << int32(pProto->Material);
    queryData << pProto->Sheath;
    queryData << pProto->RandomProperty;
    queryData << pProto->RandomSuffix;
    queryData << pProto->Block;
    queryData << pProto->ItemSet;
    queryData << pProto->MaxDurability;
    queryData << pProto->Area;
    queryData << pProto->Map;                                // Added in 1.12.x & 2.0.1 client branch
    queryData << pProto->BagFamily;
    queryData << pProto->TotemCategory;
    for (int s = 0; s < MAX_ITEM_PROTO_SOCKETS; ++s)
    {
        queryData << pProto->Socket[s].Color;
        queryData << pProto->Socket[s].Content;
    }
    queryData << pProto->socketBonus;
    queryData << pProto->GemProperties;
    queryData << pProto->RequiredDisenchantSkill;
    queryData << pProto->ArmorDamageModifier;
    queryData << pProto->Duration;                           // added in 2.4.2.8209, duration (seconds)
    queryData << pProto->ItemLimitCategory;                  // WotLK, ItemLimitCategory
    queryData << pProto->HolidayId;                          // Holiday.dbc?
    player->GetSession()->SendPacket(&queryData);
}

void ItemUpgrade::UpdateVisualCache(Player* player)
{
    std::vector<Item*> items = GetPlayerItems(player, true);
    std::vector<Item*>::const_iterator citer = items.begin();
    for (citer; citer != items.end(); ++citer)
        SendItemPacket(player, *citer);
}

void ItemUpgrade::VisualFeedback(Player* player)
{
    player->CastSpell(player, VISUAL_FEEDBACK_SPELL_ID, true);
}

std::pair<uint32, uint32> ItemUpgrade::CalculateItemLevel(const Player* player, Item* item, const UpgradeStat* upgrade) const
{
    std::unordered_map<uint32, const UpgradeStat*> upgrades;
    if (upgrade != nullptr)
        upgrades[upgrade->statType] = upgrade;
    return CalculateItemLevel(player, item, upgrades);
}

std::pair<uint32, uint32> ItemUpgrade::CalculateItemLevel(const Player* player, Item* item, std::unordered_map<uint32, const UpgradeStat*> upgrades) const
{
    const ItemTemplate* proto = item->GetTemplate();
    std::vector<_ItemStat> originalStats = LoadItemStatInfo(item);
    if (originalStats.empty())
        return std::make_pair(proto->ItemLevel, proto->ItemLevel);

    uint32 originalSum = std::accumulate(originalStats.begin(), originalStats.end(), 0, [&](uint32 a, const _ItemStat& stat) { return a + stat.ItemStatValue; });
    uint32 upgradedSum = 0;

    for (const _ItemStat& stat : originalStats)
    {
        if (upgrades.find(stat.ItemStatType) != upgrades.end())
            upgradedSum += (uint32)CalculateModPct(stat.ItemStatValue, upgrades.at(stat.ItemStatType));
        else
            upgradedSum += HandleStatModifier(player, item, stat.ItemStatType, stat.ItemStatValue, MAX_ENCHANTMENT_SLOT);
    }

    if (upgradedSum <= originalSum)
        return std::make_pair(proto->ItemLevel, proto->ItemLevel);

    //return std::make_pair(proto->ItemLevel, (upgradedSum * proto->ItemLevel) / originalSum);
    return std::make_pair(proto->ItemLevel, proto->ItemLevel + 5);
}

bool ItemUpgrade::TryAddItem(Player* player, uint32 entry, uint32 count, bool add)
{
    const ItemTemplate* proto = sObjectMgr->GetItemTemplate(entry);
    if (proto != nullptr)
    {
        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, count);
        if (msg != EQUIP_ERR_OK)
        {
            std::ostringstream oss;
            oss << "尝试添加 " << count << "x " << ItemLink(player, proto, 0);
            oss << " 失败.请检查背包空间并重试";
            SendMessage(player, oss.str());
            return false;
        }

        if (add)
        {
            Item* tokenItem = player->StoreNewItem(dest, entry, true);
            player->SendNewItem(tokenItem, count, true, false);
        }
    }
    return true;
}

bool ItemUpgrade::PurgeUpgrade(Player* player, Item* item)
{
    std::vector<const ItemUpgrade::UpgradeStat*> upgrades = FindUpgradesForItem(player, item);
    if (!upgrades.empty())
    {
        if (!TryAddItem(player, (uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_PURGE_TOKEN), (uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_PURGE_TOKEN_COUNT), true))
            return false;

        if (!RefundEverything(player, item, upgrades))
            return false;

        if (item->IsEquipped())
            player->_ApplyItemMods(item, item->GetSlot(), false);

        RemoveItemUpgrade(player, item);

        if (item->IsEquipped())
            player->_ApplyItemMods(item, item->GetSlot(), true);

        SendItemPacket(player, item);

        return true;
    }
    else
        return false;
}

bool ItemUpgrade::RefundEverything(Player* player, Item* item, const std::vector<const UpgradeStat*>& upgrades)
{
    if (!GetBoolConfig(CONFIG_ITEM_UPGRADE_REFUND_ALL_ON_PURGE))
        return true;

    uint32 index = 0;
    std::unordered_map<uint32, const UpgradeStat*> bulkUpgrades;
    for (const UpgradeStat* stat : upgrades)
    {
        uint16 rank = stat->statRank;
        while (rank >= 1)
        {
            bulkUpgrades[index++] = FindUpgradeStat(stat->statType, rank);
            rank--;
        }
    }

    StatRequirementContainer reqs = BuildBulkRequirements(bulkUpgrades, item);
    for (const UpgradeStatReq& r : reqs)
    {
        switch (r.reqType)
        {
            case REQ_TYPE_COPPER:
                if (player->GetMoney() + (uint32)r.reqVal1 > MAX_MONEY_AMOUNT)
                {
                    SendMessage(player, "无法退还铜币，将达到金币限额.");
                    return false;
                }
                break;
            case REQ_TYPE_ITEM:
                if (!TryAddItem(player, (uint32)r.reqVal1, (uint32)r.reqVal2, false))
                    return false;
                break;
        }
    }

    for (const UpgradeStatReq& r : reqs)
    {
        switch (r.reqType)
        {
            case REQ_TYPE_COPPER:
                player->ModifyMoney((int32)r.reqVal1);
                break;
            case REQ_TYPE_HONOR:
                player->ModifyHonorPoints((int32)r.reqVal1);
                break;
            case REQ_TYPE_ARENA:
                player->ModifyArenaPoints((int32)r.reqVal1);
                break;
            case REQ_TYPE_ITEM:
                TryAddItem(player, (uint32)r.reqVal1, (uint32)r.reqVal2, true);
                break;
        }
    }

    return true;
}

bool ItemUpgrade::ChooseRandomUpgrade(Player* player, Item* item)
{
    if (!GetBoolConfig(CONFIG_ITEM_UPGRADE_ENABLED))
        return false;

    if (!GetBoolConfig(CONFIG_ITEM_UPGRADE_RANDOM_UPGRADES))
        return false;

    if (!IsAllowedItem(item) || IsBlacklistedItem(item))
        return false;

    if (!FindUpgradesForItem(player, item).empty())
        return false;

    if (!roll_chance_f(GetFloatConfig(CONFIG_ITEM_UPGRADE_RANDOM_UPGRADES_CHANCE)))
        return false;

    uint32 statCountToUpgrade = urand(1, (uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_RANDOM_UPGRADES_MAX_STATS));
    std::vector<_ItemStat> statTypes = LoadItemStatInfo(item);
    std::vector<const UpgradeStat*> upgrades;
    for (const _ItemStat& stat : statTypes)
    {
        if (!IsAllowedStatType(stat.ItemStatType))
            continue;

        const UpgradeStat* foundUpgradeStat = FindNearestUpgradeStat(stat.ItemStatType, (uint16)urand(1, (uint32)GetIntConfig(CONFIG_ITEM_UPGRADE_RANDOM_UPGRADES_MAX_RANK)), item);
        if (foundUpgradeStat != nullptr)
            upgrades.push_back(foundUpgradeStat);
    }

    if (upgrades.empty())
        return false;

    Acore::Containers::RandomShuffle(upgrades);
    uint32 currentStatCount = 0;
    for (const UpgradeStat* stat : upgrades)
    {
        if (currentStatCount == statCountToUpgrade)
            break;

        AddUpgradeForNewItem(player, item, stat, GetStatByType(statTypes, stat->statType));

        currentStatCount++;
    }

    return true;
}

bool ItemUpgrade::AddUpgradeForNewItem(Player* player, Item* item, const UpgradeStat* upgrade, const _ItemStat* stat)
{
    if (stat == nullptr)
        return false;

    const UpgradeStat* foundUpgrade = FindUpgradeForItem(player, item, upgrade->statType);
    std::vector<CharacterUpgrade>& upgrades = characterUpgradeData[player->GetGUID().GetCounter()];
    if (foundUpgrade != nullptr)
        return false;
    else
        AddItemUpgradeToDB(player, item, upgrade);

    CharacterUpgrade newUpgrade;
    newUpgrade.guid = player->GetGUID().GetCounter();
    newUpgrade.itemGuid = item->GetGUID();
    newUpgrade.upgradeStat = upgrade;
    upgrades.push_back(newUpgrade);

    std::ostringstream oss;
    oss << "|cffeb891a[物品升级系统]:|r";
    oss << " " << ItemLink(player, item);
    oss << " 已经 " << StatTypeToString(upgrade->statType) << " 升级到 等级 " << upgrade->statRank << ".";
    oss << " 增加 " << upgrade->statModPct << "% [";
    oss << stat->ItemStatValue << " --> " << CalculateModPct(stat->ItemStatValue, upgrade) << "]";
    std::pair<uint32, uint32> itemLevel = CalculateItemLevel(player, item);
    oss << " [新等级: " << itemLevel.second << "]";
    SendMessage(player, oss.str());

    SendItemPacket(player, item);

    return true;
}

void ItemUpgrade::AddItemUpgradeToDB(const Player* player, const Item* item, const UpgradeStat* upgrade) const
{
    CharacterDatabase.Execute("INSERT INTO character_item_upgrade (guid, item_guid, stat_id) VALUES ({}, {}, {})",
        player->GetGUID().GetCounter(), item->GetGUID().GetCounter(), upgrade->statId);
}

const ItemUpgrade::UpgradeStat* ItemUpgrade::FindNearestUpgradeStat(uint32 statType, uint16 rank, const Item* item) const
{
    while (rank > 0)
    {
        const UpgradeStat* foundStat = FindUpgradeStat(statType, rank);
        if (foundStat != nullptr && CanApplyUpgradeForItem(item, foundStat))
            return foundStat;

        rank--;
    }

    return nullptr;
}

bool ItemUpgrade::IsAllowedStatForItem(const Item* item, const UpgradeStat* upgrade) const
{
    if (allowedStatItems.find(upgrade->statId) == allowedStatItems.end())
        return true;

    const std::set<uint32>& allowedStatsForItems = allowedStatItems.at(upgrade->statId);
    return allowedStatsForItems.find(item->GetEntry()) != allowedStatsForItems.end();
}

bool ItemUpgrade::IsBlacklistedStatForItem(const Item* item, const UpgradeStat* upgrade) const
{
    if (blacklistedStatItems.find(upgrade->statId) == blacklistedStatItems.end())
        return false;

    const std::set<uint32>& blacklistedStatsForItems = blacklistedStatItems.at(upgrade->statId);
    return blacklistedStatsForItems.find(item->GetEntry()) != blacklistedStatsForItems.end();
}

bool ItemUpgrade::CanApplyUpgradeForItem(const Item* item, const UpgradeStat* upgrade) const
{
    return IsAllowedStatForItem(item, upgrade) && !IsBlacklistedStatForItem(item, upgrade);
}

bool ItemUpgrade::CheckDataValidity() const
{
    if (upgradeStatList.empty())
        return true;

    bool ok = true;
    for (const UpgradeStat& upgrade : upgradeStatList)
    {
        if (!IsValidStatType(upgrade.statType))
        {
            LOG_ERROR("sql.sql", "FATAL: Table `mod_item_upgrade_stats` has invalid `stat_type` {}", upgrade.statType);
            ok = false;
        }
        if (upgrade.statModPct <= 0)
        {
            LOG_ERROR("sql.sql", "FATAL: Table `mod_item_upgrade_stats` has invalid `stat_mod_pct` {}", upgrade.statModPct);
            ok = false;
        }
    }

    if (!ok)
        return false;

    std::unordered_map<uint32, std::vector<uint16>> ranksMap;
    for (const UpgradeStat& upgrade : upgradeStatList)
        ranksMap[upgrade.statType].push_back(upgrade.statRank);

    for (auto& rpair : ranksMap)
    {
        std::vector<uint16>& ranks = rpair.second;
        std::sort(ranks.begin(), ranks.end());
        if (ranks[0] != 1)
        {
            ok = false;
            LOG_ERROR("sql.sql", "FATAL: Table `mod_item_upgrade_stats` has invalid starting rank (`stat_rank`) {} for stat type (`stat_type`) {}", ranks[0], rpair.first);
        }

        bool consecutive = true;
        for (uint32 i = 1; i < ranks.size(); i++)
        {
            if (ranks[i] != ranks[i - 1] + 1)
            {
                consecutive = false;
                break;
            }
        }
        if (!consecutive)
        {
            ok = false;
            LOG_ERROR("sql.sql", "FATAL: Table `mod_item_upgrade_stats` does not have consecutive ranks (`stat_rank`) for stat type (`stat_type`) {}", rpair.first);
        }
    }

    return ok;
}

const ItemUpgrade::StatRequirementContainer* ItemUpgrade::GetStatRequirements(const UpgradeStat* upgrade, const Item* item) const
{
    if (overrideStatRequirements.find(item->GetEntry()) != overrideStatRequirements.end())
    {
        const std::unordered_map<uint32, StatRequirementContainer>& itemReqs = overrideStatRequirements.at(item->GetEntry());
        if (itemReqs.find(upgrade->statId) != itemReqs.end())
            return &itemReqs.at(upgrade->statId);
    }

    if (baseStatRequirements.find(upgrade->statId) != baseStatRequirements.end())
        return &baseStatRequirements.at(upgrade->statId);

    return nullptr;
}

bool ItemUpgrade::EmptyRequirements(const StatRequirementContainer* reqs) const
{
    if (reqs == nullptr || reqs->size() == 0)
        return true;

    if (reqs->size() == 1 && reqs->at(0).reqType == REQ_TYPE_NONE)
        return true;

    return false;
}

void ItemUpgrade::EquipItem(Player* player, Item* item)
{
    if (!item || item->IsEquipped())
        return;

    uint16 pos;
    InventoryResult res = player->CanEquipItem(NULL_SLOT, pos, item, true);
    if (res != EQUIP_ERR_OK)
    {
        player->SendEquipError(res, item, nullptr);
        return;
    }

    player->SwapItem(item->GetPos(), pos);
}
