#pragma once

#include "ProtocolDefines.h"
#include "User.h"

#define HELPER_PROTOCOL_HEAD 0xFA
#define HELPER_PROTOCOL_VERSION 2

enum eHelperSubCode { HELPER_SUB_CONFIG = 0x00, HELPER_SUB_START = 0x01, HELPER_SUB_STOP = 0x02, HELPER_SUB_STATUS = 0x03, HELPER_SUB_OFFLINE = 0x04, HELPER_SUB_SYNC = 0x05 };
enum eHelperFlags { HELPER_FLAG_AUTO_ATTACK = 0x01, HELPER_FLAG_AUTO_HP = 0x02, HELPER_FLAG_AUTO_MP = 0x04, HELPER_FLAG_AUTO_BUFF = 0x08 };
enum eHelperLootFlags { HELPER_LOOT_ZEN = 0x01, HELPER_LOOT_JEWELS = 0x02, HELPER_LOOT_EXCELLENT = 0x04 };
enum eHelperResult { HELPER_RESULT_OK = 0, HELPER_RESULT_BAD_VERSION = 1, HELPER_RESULT_BAD_SETTINGS = 2, HELPER_RESULT_RATE_LIMITED = 3, HELPER_RESULT_NOT_READY = 4, HELPER_RESULT_NOT_SUPPORTED = 5 };

#pragma pack(push, 1)
struct HELPER_SETTINGS
{
	BYTE version;
	BYTE flags;
	BYTE huntingRange;
	BYTE hpThreshold;
	BYTE mpThreshold;
	BYTE primarySkill;
	BYTE secondarySkill;
	BYTE secondaryEvery;
	BYTE lootFlags;
	BYTE lootRange;
};

struct PMSG_HELPER_CONFIG_RECV { PSBMSG_HEAD header; HELPER_SETTINGS settings; };
struct PMSG_HELPER_COMMAND_RECV { PSBMSG_HEAD header; BYTE version; };
struct PMSG_HELPER_STATUS_SEND
{
	PSBMSG_HEAD header;
	BYTE version;
	BYTE active;
	BYTE result;
	BYTE reserved;
	WORD targetIndex;
	HELPER_SETTINGS settings;
};
#pragma pack(pop)

class CHelperManager
{
public:
	CHelperManager();
	void Run();
	void Reset(int aIndex);
	void ProtocolCore(BYTE subCode, BYTE* lpMsg, int size, int aIndex);

private:
	struct HELPER_RUNTIME
	{
		HELPER_SETTINGS settings;
		bool configured;
		bool active;
		short originX;
		short originY;
		BYTE originMap;
		int targetIndex;
		DWORD lastPacketTick;
		DWORD lastActionTick;
		DWORD lastBuffTick;
		DWORD lastLootTick;
		BYTE attackCounter;
	};

	bool ValidateSettings(const HELPER_SETTINGS& settings) const;
	bool CheckPacketRate(HELPER_RUNTIME& runtime);
	int FindTarget(LPOBJ lpObj, const HELPER_RUNTIME& runtime) const;
	void ProcessUser(int aIndex, DWORD tick);
	void ProcessPotion(LPOBJ lpObj, HELPER_RUNTIME& runtime);
	void ProcessBuff(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick);
	void ProcessAttack(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick);
	void ProcessLoot(LPOBJ lpObj, HELPER_RUNTIME& runtime, DWORD tick);
	bool MatchLootFilter(const CItem& item, BYTE flags) const;
	void SendStatus(int aIndex, BYTE result);

	HELPER_RUNTIME m_Runtime[MAX_OBJECT];
};

extern CHelperManager gHelperManager;
