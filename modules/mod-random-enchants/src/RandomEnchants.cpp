/*
* Converted from the original LUA script to a module for Azerothcore(Sunwell) :D
*/
#include "ScriptMgr.h"
#include "Player.h"
#include "Configuration/Config.h"
#include "Chat.h"
#include "../Server/game/Spells/Spell.h"

// Enum for item qualities
enum ItemQuality {
    GREY = 0,
    WHITE,
    GREEN,
    BLUE,
    PURPLE,
    ORANGE
};

class RandomEnchantsPlayer : public PlayerScript {
public:
    RandomEnchantsPlayer() : PlayerScript("RandomEnchantsPlayer") { }

    void OnLogin(Player* player) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.AnnounceOnLogin", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
            ChatHandler(player->GetSession()).SendSysMessage(sConfigMgr->GetOption<std::string>("RandomEnchants.OnLoginMessage", "This server is running a RandomEnchants Module.").c_str());
    }

    void OnStoreNewItem(Player* player, Item* item, uint32 /*count*/) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.OnCreate", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    void OnLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.OnLoot", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    //void OnItemCreate(Item* item, ItemTemplate const* itemProto, Player* player) {
    void OnCreateItem(Player* player, Item* item, uint32 /*count*/) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.OnCreate", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    void OnQuestRewardItem(Player* player, Item* item, uint32 /*count*/) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.OnQuestReward", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    void OnGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/, RollVote /*voteType*/, Roll* /*roll*/) override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.OnGroupRoll", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    void OnAfterStoreOrEquipNewItem(Player* player, uint32 /*vendorslot*/, Item* item, uint8 /*count*/, uint8 /*bag*/, uint8 /*slot*/, ItemTemplate const* /*pProto*/, Creature* /*pVendor*/, VendorItem const* /*crItem*/, bool /*bStore*/)  override {
        if (sConfigMgr->GetOption<bool>("RandomEnchants.BuyfromNpc", true) && sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true))
        {
            if (item->IsEnchanted())
                return;
            else
                RollPossibleEnchant(player, item);
        }
    }

    void RollPossibleEnchant(Player* player, Item* item) {
        // Check global enable option
        if (!sConfigMgr->GetOption<bool>("RandomEnchants.Enable", true)) {
            return;
        }
        uint32 Quality = item->GetTemplate()->Quality;
        uint32 Class = item->GetTemplate()->Class;

        if (
            (Quality > 5 || Quality < 1 || item->GetTemplate()->ItemLevel < 110) /* eliminates enchanting anything that isn't a recognized quality */ ||
            (Class != 2 && Class != 4) /* eliminates enchanting anything but weapons/armor */) {
            return;
        }

        int slotRand[3] = { -1, -1, -1 };
        uint32 slotEnch[3] = { 0, 1, 5 };

        // Fetching the configuration values as float
        float enchantChance1 = sConfigMgr->GetOption<float>("RandomEnchants.EnchantChance1", 70.0f);
        float enchantChance2 = sConfigMgr->GetOption<float>("RandomEnchants.EnchantChance2", 65.0f);
        float enchantChance3 = sConfigMgr->GetOption<float>("RandomEnchants.EnchantChance3", 60.0f);

        if (rand_chance() < enchantChance1)
            slotRand[0] = getRandEnchantment(item);
        if (slotRand[0] != -1 && rand_chance() < enchantChance2)
            slotRand[1] = getRandEnchantment(item);
        if (slotRand[1] != -1 && rand_chance() < enchantChance3)
            slotRand[2] = getRandEnchantment(item);

        for (int i = 0; i < 3; i++) {
            if (slotRand[i] != -1) {
                if (sSpellItemEnchantmentStore.LookupEntry(slotRand[i])) { //Make sure enchantment id exists
                    player->ApplyEnchantment(item, EnchantmentSlot(slotEnch[i]), false);
                    item->SetEnchantment(EnchantmentSlot(slotEnch[i]), slotRand[i], 0, 0);
                    player->ApplyEnchantment(item, EnchantmentSlot(slotEnch[i]), true);
                }
            }
        }
        ChatHandler chathandle = ChatHandler(player->GetSession());
        std::vector<std::string> localname = sObjectMgr->GetItemLocale(item->GetEntry())->Name;
        std::string chineseName = localname.size() > 4 ? localname[4] : "";
        if (slotRand[2] != -1)
            //chathandle.PSendSysMessage("拾取 |cffFF0000 %s |r时获得|cffFF0000 3项 |r随机附魔!", item->GetTemplate()->Name1.c_str());
            chathandle.PSendSysMessage("拾取|cffFF0000 {} |r时获得|cffFF0000 3项 |r随机附魔!", chineseName.c_str());
        else if (slotRand[1] != -1)
            chathandle.PSendSysMessage("拾取|cffFF0000 {} |r时获得|cffFF0000 2项 |r随机附魔!", chineseName.c_str());
        else if (slotRand[0] != -1)
            chathandle.PSendSysMessage("拾取|cffFF0000 {} |r时获得|cffFF0000 1项 |r随机附魔!", chineseName.c_str());
    }

    int getRandEnchantment(Item* item) {
        uint32 Class = item->GetTemplate()->Class;
        std::string ClassQueryString = "";
        switch (Class) {
        case 2:
            ClassQueryString = "WEAPON";
            break;
        case 4:
            ClassQueryString = "ARMOR";
            break;
        }
        if (ClassQueryString == "")
            return -1;
        uint32 Quality = item->GetTemplate()->Quality;
        int rarityRoll = -1;
        switch (Quality) {
        case GREY:
            rarityRoll = rand_norm() * 25;
            break;
        case WHITE:
            rarityRoll = rand_norm() * 50;
            break;
        case GREEN:
            rarityRoll = 45 + (rand_norm() * 20);
            break;
        case BLUE:
            rarityRoll = 65 + (rand_norm() * 15);
            break;
        case PURPLE:
            rarityRoll = 80 + (rand_norm() * 14);
            break;
        case ORANGE:
            rarityRoll = 93;
            break;
        }
        if (rarityRoll < 0)
            return -1;
        int tier = 0;
        if (rarityRoll <= 44)
            tier = 1;
        else if (rarityRoll <= 64)
            tier = 2;
        else if (rarityRoll <= 79)
            tier = 3;
        else if (rarityRoll <= 92)
            tier = 4;
        else
            tier = 5;

        QueryResult qr = WorldDatabase.Query("SELECT enchantID FROM item_enchantment_random_tiers WHERE tier='{}' AND exclusiveSubClass=NULL AND class='{}' OR exclusiveSubClass='{}' OR class='ANY' ORDER BY RAND() LIMIT 1", tier, ClassQueryString, item->GetTemplate()->SubClass);
        if (!qr)
            return -1;
        return qr->Fetch()[0].Get<uint32>();
    }
};

class XilianRandomEnchantItem : public ItemScript {
public:
    XilianRandomEnchantItem() : ItemScript("XilianRandomEnchantItem") {}

    bool OnUse(Player* player, Item* self, SpellCastTargets const& targets) override {
        Item* item = targets.GetItemTarget();

        // 如果目标物品为空，直接返回 true，避免后续操作
        if (!item) {
            ChatHandler(player->GetSession()).PSendSysMessage("目标物品无效，无法洗练。");
            return true;
        }

        // 获取物品的背包槽位和插槽，确保它们有效
        uint8 bagSlot = item->GetBagSlot();
        uint8 slot = item->GetSlot();
        uint8 selfbagSlot = self->GetBagSlot();
        uint8 selfslot = self->GetSlot();

        // 检查物品槽位有效性
        if (bagSlot == INVENTORY_SLOT_BAG_0 || slot == NULL_SLOT) {
            ChatHandler(player->GetSession()).PSendSysMessage("无法识别物品的位置，操作失败。");
            return true;
        }

        // 直接调用 RollPossibleEnchant 方法为物品应用随机附魔
        //RandomEnchantsPlayer().RollPossibleEnchant(player, item);

        // 销毁目标物品
        uint32 selfEntry = self->GetEntry();
        uint32 itemEntry = item->GetEntry();  // 保存物品的Entry
        player->DestroyItem(bagSlot, slot, true);

        // 销毁洗练物品自身
        player->DestroyItem(selfbagSlot, selfslot, true);

        // 发送成功消息
        //ChatHandler(player->GetSession()).PSendSysMessage("随机附魔属性洗练成功");

        // 重新添加被洗练的物品
        if (player->AddItem(itemEntry, 1)) {
            // 成功添加新物品，发送确认消息
            ChatHandler(player->GetSession()).PSendSysMessage("随机附魔属性洗练成功");
        }
        else {
            // 如果添加新物品失败，发送错误消息
            ChatHandler(player->GetSession()).PSendSysMessage("物品洗练失败，可能是背包已满。");
        }

        return true;
    }
};

void AddRandomEnchantsScripts() {
    new RandomEnchantsPlayer();
    new XilianRandomEnchantItem();
}
