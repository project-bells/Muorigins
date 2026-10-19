#include "stdafx.h"
#include "HelperManager.h"
#include "Attack.h"
#include "EffectManager.h"
#include "ItemManager.h"
#include "Map.h"
#include "ObjectManager.h"
#include "SkillManager.h"
#include "Util.h"

CHelperManager gHelperManager;

namespace
{
	const DWORD HELPER_PACKET_INTERVAL = 250;
	const DWORD HELPER_ACTION_INTERVAL = 600;
	const DWORD HELPER_BUFF_INTERVAL = 3000;
	const DWORD HELPER_LOOT_INTERVAL = 500;

	bool IsBuffSkill(int skill)
	{
		return skill == SKILL_MANA_SHIELD || skill == SKILL_GREATER_DEFENSE || skill == SKILL_GREATER_DAMAGE || skill == SKILL_GREATER_LIFE;
	}

	int GetBuffEffect(int skill)
	{
		switch (skill)
		{
			case SKILL_MANA_SHIELD: return EFFECT_MANA_SHIELD;
			case SKILL_GREATER_DEFENSE: return EFFECT_GREATER_DEFENSE;
			case SKILL_GREATER_DAMAGE: return EFFECT_GREATER_DAMAGE;
			case SKILL_GREATER_LIFE: return EFFECT_GREATER_LIFE;
		}
		return -1;
	}

	void SendHelperSkillAnimation(LPOBJ lpObj, int skill, int targetIndex)
	{
		PMSG_SKILL_ATTACK_SEND message = {};
		message.header.setE(0x19, sizeof(message));
		message.skill = skill;
		// Bit 15 marks a Helper-originated cast. The client masks entity keys to
		// 15 bits and uses this marker only to replay the local cast animation.
		message.index[0] = SET_NUMBERHB(lpObj->Index) | 0x80;
		message.index[1] = SET_NUMBERLB(lpObj->Index);
		message.target[0] = SET_NUMBERHB(targetIndex) | 0x80;
		message.target[1] = SET_NUMBERLB(targetIndex);
		DataSend(lpObj->Index, (BYTE*)&message, message.header.size);
		MsgSendV2(lpObj, (BYTE*)&message, message.header.size);
	}
}

CHelperManager::CHelperManager()
{
	for (int n = 0; n < MAX_OBJECT; n++) this->Reset(n);
}

void CHelperManager::Reset(int aIndex)
{
	if (OBJECT_RANGE(aIndex) == 0) return;
	memset(&this->m_Runtime[aIndex], 0, sizeof(this->m_Runtime[aIndex]));
	this->m_Runtime[aIndex].settings.version = HELPER_PROTOCOL_VERSION;
	this->m_Runtime[aIndex].settings.flags = HELPER_FLAG_AUTO_ATTACK | HELPER_FLAG_AUTO_HP | HELPER_FLAG_AUTO_MP;
	this->m_Runtime[aIndex].settings.huntingRange = 5;
	this->m_Runtime[aIndex].settings.hpThreshold = 50;
	this->m_Runtime[aIndex].settings.mpThreshold = 30;
	this->m_Runtime[aIndex].settings.secondaryEvery = 5;
	this->m_Runtime[aIndex].settings.lootFlags = HELPER_LOOT_ZEN | HELPER_LOOT_JEWELS | HELPER_LOOT_EXCELLENT;
	this->m_Runtime[aIndex].settings.lootRange = 3;
	this->m_Runtime[aIndex].targetIndex = -1;
}

bool CHelperManager::ValidateSettings(const HELPER_SETTINGS& settings) const
{
	if (settings.version != HELPER_PROTOCOL_VERSION || settings.huntingRange < 1 || settings.huntingRange > 10) return false;
	if (settings.hpThreshold > 100 || settings.mpThreshold > 100 || settings.primarySkill >= MAX_SKILL || settings.secondarySkill >= MAX_SKILL) return false;
	if (settings.secondaryEvery < 1 || settings.secondaryEvery > 20) return false;
	if (settings.lootRange < 1 || settings.lootRange > 5 || (settings.lootFlags & ~(HELPER_LOOT_ZEN | HELPER_LOOT_JEWELS | HELPER_LOOT_EXCELLENT)) != 0) return false;
	const BYTE allowed = HELPER_FLAG_AUTO_ATTACK | HELPER_FLAG_AUTO_HP | HELPER_FLAG_AUTO_MP | HELPER_FLAG_AUTO_BUFF;
	return (settings.flags & ~allowed) == 0;
}

bool CHelperManager::CheckPacketRate(HELPER_RUNTIME& runtime)
{
	DWORD tick = GetTickCount();
	if (runtime.lastPacketTick != 0 && (tick - runtime.lastPacketTick) < HELPER_PACKET_INTERVAL) return false;
	runtime.lastPacketTick = tick;
	return true;
}

void CHelperManager::ProtocolCore(BYTE subCode, BYTE* lpMsg, int size, int aIndex)
{
	if (gObjIsConnectedGP(aIndex) == 0 || lpMsg == 0) return;
	HELPER_RUNTIME& runtime = this->m_Runtime[aIndex];
	if (!this->CheckPacketRate(runtime)) { this->SendStatus(aIndex, HELPER_RESULT_RATE_LIMITED); return; }

	if (subCode == HELPER_SUB_CONFIG)
	{
		if (size != sizeof(PMSG_HELPER_CONFIG_RECV)) { this->SendStatus(aIndex, HELPER_RESULT_BAD_SETTINGS); return; }
		PMSG_HELPER_CONFIG_RECV* message = (PMSG_HELPER_CONFIG_RECV*)lpMsg;
		if (!this->ValidateSettings(message->settings))
		{
			this->SendStatus(aIndex, (message->settings.version == HELPER_PROTOCOL_VERSION) ? HELPER_RESULT_BAD_SETTINGS : HELPER_RESULT_BAD_VERSION);
			return;
		}
		runtime.settings = message->settings;
		runtime.configured = true;
		this->SendStatus(aIndex, HELPER_RESULT_OK);
		return;
	}

	if (size != sizeof(PMSG_HELPER_COMMAND_RECV)) { this->SendStatus(aIndex, HELPER_RESULT_BAD_SETTINGS); return; }
	PMSG_HELPER_COMMAND_RECV* message = (PMSG_HELPER_COMMAND_RECV*)lpMsg;
	if (message->version != HELPER_PROTOCOL_VERSION) { this->SendStatus(aIndex, HELPER_RESULT_BAD_VERSION); return; }

	switch (subCode)
	{
		case HELPER_SUB_START:
		{
			LPOBJ lpObj = &gObj[aIndex];
			if (!runtime.configured || lpObj->Live == 0 || lpObj->Teleport != 0 || lpObj->Interface.use != 0) { this->SendStatus(aIndex, HELPER_RESULT_NOT_READY); return; }
			if ((runtime.settings.primarySkill != SKILL_NONE && gSkillManager.GetSkill(lpObj, runtime.settings.primarySkill) == 0) ||
				(runtime.settings.secondarySkill != SKILL_NONE && gSkillManager.GetSkill(lpObj, runtime.settings.secondarySkill) == 0))
			{
				this->SendStatus(aIndex, HELPER_RESULT_SKILL_NOT_LEARNED);
				return;
			}
			runtime.active = true;
			runtime.originX = lpObj->X;
			runtime.originY = lpObj->Y;
			runtime.originMap = lpObj->Map;
			runtime.targetIndex = -1;
			runtime.lastActionTick = 0;
			this->SendStatus(aIndex, HELPER_RESULT_OK);
			break;
		}
		case HELPER_SUB_STOP:
			runtime.active = false; runtime.targetIndex = -1; this->SendStatus(aIndex, HELPER_RESULT_OK); break;
		case HELPER_SUB_SYNC:
			this->SendStatus(aIndex, HELPER_RESULT_OK); break;
		case HELPER_SUB_OFFLINE:
			this->SendStatus(aIndex, HELPER_RESULT_NOT_SUPPORTED); break;
		default:
			this->SendStatus(aIndex, HELPER_RESULT_BAD_SETTINGS); break;
	}
}

void CHelperManager::Run()
{
	DWORD tick = GetTickCount();
	for (int n = OBJECT_START_USER; n < MAX_OBJECT; n++)
	{
		if (gObjIsConnectedGP(n) == 0)
		{
			if (this->m_Runtime[n].configured || this->m_Runtime[n].active) this->Reset(n);
			continue;
		}
		if (this->m_Runtime[n].active) this->ProcessUser(n, tick);
	}
}

int CHelperManager::FindTarget(LPOBJ lpObj, const HELPER_RUNTIME& runtime) const
{
	int result = -1;
	int best = 0x7FFFFFFF;
	int maximum = runtime.settings.huntingRange * runtime.settings.huntingRange;
	for (int n = 0; n < MAX_VIEWPORT; n++)
	{
		VIEWPORT_STRUCT& view = lpObj->VpPlayer2[n];
		if (view.state == 0 || view.type != OBJECT_MONSTER || OBJECT_MONSTER_RANGE(view.index) == 0) continue;
		LPOBJ target = &gObj[view.index];
		if (gObjIsConnectedGS(target->Index) == 0 || target->Live == 0 || target->State != OBJECT_PLAYING || target->Map != lpObj->Map) continue;
		int dx = target->X - lpObj->X;
		int dy = target->Y - lpObj->Y;
		int distance = (dx * dx) + (dy * dy);
		if (distance <= maximum && distance < best) { best = distance; result = target->Index; }
	}
	return result;
}

void CHelperManager::ProcessUser(int aIndex, DWORD tick)
{
	LPOBJ lpObj = &gObj[aIndex];
	HELPER_RUNTIME& runtime = this->m_Runtime[aIndex];
	if (lpObj->Live == 0 || lpObj->Teleport != 0 || lpObj->Interface.use != 0 || lpObj->Map != runtime.originMap) return;
	this->ProcessPotion(lpObj, runtime);
	this->ProcessBuff(lpObj, runtime, tick);
	this->ProcessAttack(lpObj, runtime, tick);
	this->ProcessLoot(lpObj, runtime, tick);
}

bool CHelperManager::MatchLootFilter(const CItem& item, BYTE flags) const
{
	const int kind = item.GetKind();
	if ((flags & HELPER_LOOT_ZEN) != 0 && kind == GET_ITEM(14, 15)) return true;
	if ((flags & HELPER_LOOT_JEWELS) != 0 &&
		(kind == GET_ITEM(12, 15) || kind == GET_ITEM(14, 13) || kind == GET_ITEM(14, 14) || kind == GET_ITEM(14, 16) || kind == GET_ITEM(14, 22))) return true;
	if ((flags & HELPER_LOOT_EXCELLENT) != 0 && item.m_ExceOption != 0) return true;
	return false;
}

void CHelperManager::ProcessLoot(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick)
{
	if (runtime.settings.lootFlags == 0 || (runtime.lastLootTick != 0 && (tick - runtime.lastLootTick) < HELPER_LOOT_INTERVAL)) return;
	runtime.lastLootTick = tick;
	int bestIndex = -1;
	int bestDistance = 0x7FFFFFFF;
	const int maximum = runtime.settings.lootRange * runtime.settings.lootRange;

	for (int n = 0; n < MAX_VIEWPORT; n++)
	{
		VIEWPORT_STRUCT& view = lpObj->VpPlayerItem[n];
		if (view.state == 0 || view.type != OBJECT_ITEM || MAP_ITEM_RANGE(view.index) == 0) continue;
		CMapItem& item = gMap[lpObj->Map].m_Item[view.index];
		if (item.IsItem() == 0 || item.m_Live == 0 || item.m_Give != 0 || !this->MatchLootFilter(item, runtime.settings.lootFlags)) continue;
		if (OBJECT_RANGE(item.m_UserIndex) != 0 && item.m_LootTime > tick && item.m_UserIndex != lpObj->Index)
		{
			if (item.m_QuestItem != 0 || OBJECT_RANGE(lpObj->PartyNumber) == 0 || lpObj->PartyNumber != gObj[item.m_UserIndex].PartyNumber) continue;
		}
		int dx = item.m_X - lpObj->X, dy = item.m_Y - lpObj->Y;
		int distance = (dx * dx) + (dy * dy);
		if (distance <= maximum && distance < bestDistance) { bestDistance = distance; bestIndex = view.index; }
	}

	if (bestIndex >= 0)
	{
		PMSG_ITEM_GET_RECV message = {};
		message.header.set(0x22, sizeof(message));
		message.index[0] = SET_NUMBERHB(bestIndex);
		message.index[1] = SET_NUMBERLB(bestIndex);
		gItemManager.CGItemGetRecv(&message, lpObj->Index);
	}
}

void CHelperManager::ProcessPotion(LPOBJ lpObj, HELPER_RUNTIME& runtime)
{
	int first = -1, last = -1;
	float current = 0, maximum = 0;
	BYTE threshold = 0;
	if ((runtime.settings.flags & HELPER_FLAG_AUTO_HP) != 0 && runtime.settings.hpThreshold > 0)
	{
		current = lpObj->Life; maximum = lpObj->MaxLife + lpObj->AddLife; threshold = runtime.settings.hpThreshold; first = GET_ITEM(14, 1); last = GET_ITEM(14, 3);
	}
	if (first == -1 || maximum <= 0 || ((current * 100.0f) / maximum) > threshold)
	{
		if ((runtime.settings.flags & HELPER_FLAG_AUTO_MP) == 0 || runtime.settings.mpThreshold == 0) return;
		current = lpObj->Mana; maximum = lpObj->MaxMana + lpObj->AddMana; threshold = runtime.settings.mpThreshold; first = GET_ITEM(14, 4); last = GET_ITEM(14, 6);
	}
	if (maximum <= 0 || ((current * 100.0f) / maximum) > threshold) return;
	for (int kind = last; kind >= first; kind--)
	{
		for (int slot = INVENTORY_WEAR_SIZE; slot < INVENTORY_SIZE; slot++)
		{
			CItem* item = &lpObj->Inventory[slot];
			if (item->IsItem() != 0 && gItemManager.GetBehavior(item->m_Index) == kind && gObjectManager.CharacterUsePotion(lpObj, item) != 0)
			{
				gItemManager.DecreaseItemDur(lpObj, slot, 1);
				return;
			}
		}
	}
}

void CHelperManager::ProcessBuff(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick)
{
	if ((runtime.settings.flags & HELPER_FLAG_AUTO_BUFF) == 0 || !IsBuffSkill(runtime.settings.secondarySkill) || (runtime.lastBuffTick != 0 && (tick - runtime.lastBuffTick) < HELPER_BUFF_INTERVAL)) return;
	CSkill* skill = gSkillManager.GetSkill(lpObj, runtime.settings.secondarySkill);
	int effect = GetBuffEffect(runtime.settings.secondarySkill);
	if (skill != 0 && effect >= 0 && !gEffectManager.CheckEffect(lpObj, effect) && gSkillManager.CheckSkillDelay(lpObj, skill->m_index))
	{
		gSkillManager.UseAttackSkill(lpObj->Index, lpObj->Index, skill);
		runtime.lastBuffTick = tick;
	}
}

void CHelperManager::ProcessAttack(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick)
{
	if ((runtime.settings.flags & HELPER_FLAG_AUTO_ATTACK) == 0 || (runtime.lastActionTick != 0 && (tick - runtime.lastActionTick) < HELPER_ACTION_INTERVAL)) return;
	runtime.targetIndex = this->FindTarget(lpObj, runtime);
	if (OBJECT_MONSTER_RANGE(runtime.targetIndex) == 0) return;
	int skillIndex = runtime.settings.primarySkill;
	runtime.attackCounter++;
	if (!IsBuffSkill(runtime.settings.secondarySkill) && runtime.settings.secondarySkill != SKILL_NONE && (runtime.attackCounter % runtime.settings.secondaryEvery) == 0) skillIndex = runtime.settings.secondarySkill;
	if (skillIndex != SKILL_NONE)
	{
		CSkill* skill = gSkillManager.GetSkill(lpObj, skillIndex);
		if (skill == 0 || !gSkillManager.CheckSkillDelay(lpObj, skill->m_index)) return;
		// A normal right-click starts the cast animation locally before the
		// server resolves damage. Helper has no physical mouse click, so send
		// the same 0x19 skill notification explicitly. Without it damage is
		// applied correctly, but the local player appears to stand still while
		// the monster dies.
		SendHelperSkillAnimation(lpObj, skill->m_index, runtime.targetIndex);
		gSkillManager.UseAttackSkill(lpObj->Index, runtime.targetIndex, skill);
	}
	else
	{
		LPOBJ target = &gObj[runtime.targetIndex];
		int dx = target->X - lpObj->X, dy = target->Y - lpObj->Y;
		int range = (lpObj->Class == CLASS_FE) ? 6 : 3;
		if ((dx * dx) + (dy * dy) > (range * range)) return;
		GCActionSend(lpObj, ACTION_ATTACK1, lpObj->Index, target->Index);
		gAttack.Attack(lpObj, target, 0, false, 0, 0);
	}
	lpObj->LastAttackTime = tick;
	runtime.lastActionTick = tick;
}

void CHelperManager::SendStatus(int aIndex, BYTE result)
{
	if (gObjIsConnectedGP(aIndex) == 0) return;
	HELPER_RUNTIME& runtime = this->m_Runtime[aIndex];
	PMSG_HELPER_STATUS_SEND message = {};
	message.header.set(HELPER_PROTOCOL_HEAD, HELPER_SUB_STATUS, sizeof(message));
	message.version = HELPER_PROTOCOL_VERSION;
	message.active = runtime.active ? 1 : 0;
	message.result = result;
	message.targetIndex = (runtime.targetIndex >= 0) ? (WORD)runtime.targetIndex : 0xFFFF;
	message.settings = runtime.settings;
	DataSend(aIndex, (BYTE*)&message, sizeof(message));
}
