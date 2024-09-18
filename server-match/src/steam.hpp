#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define VALVE_CALLBACK_PACK_SMALL
#else
#define VALVE_CALLBACK_PACK_LARGE
#endif

typedef uint64_t SteamAPICall_t;
typedef int32_t  HSteamPipe;
typedef int32_t  HSteamUser;
typedef void     ISteamClient;

const SteamAPICall_t   k_uAPICallInvalid            = 0x0;
constexpr unsigned int k_unSteamAccountIDMask       = 0xFFFFFFFF;
constexpr unsigned int k_unSteamAccountInstanceMask = 0x000FFFFF;
constexpr unsigned int k_unSteamUserDefaultInstance = 1;

enum EGCResults
{
	k_EGCResultOK             = 0,
	k_EGCResultNoMessage      = 1,
	k_EGCResultBufferTooSmall = 2,
	k_EGCResultNotLoggedOn    = 3,
	k_EGCResultInvalidMessage = 4,
};

enum EUniverse
{
	k_EUniverseInvalid  = 0,
	k_EUniversePublic   = 1,
	k_EUniverseBeta     = 2,
	k_EUniverseInternal = 3,
	k_EUniverseDev      = 4,
	k_EUniverseMax
};

enum EChatSteamIDInstanceFlags
{
	k_EChatAccountInstanceMask  = 0x00000FFF,
	k_EChatInstanceFlagClan     = (k_unSteamAccountInstanceMask + 1) >> 1,
	k_EChatInstanceFlagLobby    = (k_unSteamAccountInstanceMask + 1) >> 2,
	k_EChatInstanceFlagMMSLobby = (k_unSteamAccountInstanceMask + 1) >> 3,
};
enum EAccountType
{
	k_EAccountTypeInvalid        = 0,
	k_EAccountTypeIndividual     = 1,
	k_EAccountTypeMultiseat      = 2,
	k_EAccountTypeGameServer     = 3,
	k_EAccountTypeAnonGameServer = 4,
	k_EAccountTypePending        = 5,
	k_EAccountTypeContentServer  = 6,
	k_EAccountTypeClan           = 7,
	k_EAccountTypeChat           = 8,
	k_EAccountTypeConsoleUser    = 9,
	k_EAccountTypeAnonUser       = 10,
	k_EAccountTypeMax
};

enum
{
	k_iSteamGameCoordinatorCallbacks = 1700
};

typedef void          (*SteamGameServer_RunCallbacksFn)();
typedef void          (*SteamAPI_RegisterCallbackFn)(class CCallbackBase *pCallback, int iCallback);
typedef void          (*SteamAPI_UnregisterCallbackFn)(class CCallbackBase *pCallback);
typedef ISteamClient *(*SteamClientFn)();
typedef HSteamPipe    (*SteamGameServer_GetHSteamPipeFn)();
typedef HSteamUser    (*SteamGameServer_GetHSteamUserFn)();
typedef void         *(*SteamAPI_ISteamClient_GetISteamGenericInterfaceFn)(ISteamClient *self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char *pchVersion);
typedef uint64_t      (*SteamGameServer_GetSteamIDFn)();

#if defined(VALVE_CALLBACK_PACK_SMALL)
#pragma pack(push, 4)
#elif defined(VALVE_CALLBACK_PACK_LARGE)
#pragma pack(push, 8)
#else
#error "Unknown VALVE_CALLBACK_PACK
#endif

struct GCMessageAvailable_t
{
	enum
	{
		k_iCallback = k_iSteamGameCoordinatorCallbacks + 1
	};
	uint32_t m_nMessageSize;
};

#pragma pack(pop)

class CCallbackBase
{
public:
	CCallbackBase()
	{
		m_nCallbackFlags = 0;
		m_iCallback      = 0;
	}
	// don't add a virtual destructor because we export this binary interface across dll's
	virtual void Run(void *pvParam)                                                = 0;
	virtual void Run(void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) = 0;
	int          GetICallback()
	{
		return m_iCallback;
	}
	virtual int GetCallbackSizeBytes() = 0;

protected:
	enum
	{
		k_ECallbackFlagsRegistered = 0x01,
		k_ECallbackFlagsGameServer = 0x02
	};
	uint8_t m_nCallbackFlags;
	int     m_iCallback;
	friend class CCallbackMgr;

private:
	CCallbackBase(const CCallbackBase &);
	CCallbackBase &operator=(const CCallbackBase &);
};

class ISteamGameCoordinator
{
public:
	virtual EGCResults SendMessage(uint32_t unMsgType, const void *pubData, uint32_t cubData)                        = 0;
	virtual bool       IsMessageAvailable(uint32_t *pcubMsgSize)                                                     = 0;
	virtual EGCResults RetrieveMessage(uint32_t *punMsgType, void *pubDest, uint32_t cubDest, uint32_t *pcubMsgSize) = 0;
};
#define STEAMGAMECOORDINATOR_INTERFACE_VERSION "SteamGameCoordinator001"

class CSteamID
{
public:
	CSteamID()
	{
		m_steamid.m_comp.m_unAccountID       = 0;
		m_steamid.m_comp.m_EAccountType      = k_EAccountTypeInvalid;
		m_steamid.m_comp.m_EUniverse         = k_EUniverseInvalid;
		m_steamid.m_comp.m_unAccountInstance = 0;
	}

	CSteamID(uint32_t unAccountID, EUniverse eUniverse, EAccountType eAccountType)
	{
		Set(unAccountID, eUniverse, eAccountType);
	}

	CSteamID(uint32_t unAccountID, uint32_t unAccountInstance, EUniverse eUniverse, EAccountType eAccountType)
	{
		InstancedSet(unAccountID, unAccountInstance, eUniverse, eAccountType);
	}

	CSteamID(uint64_t ulSteamID)
	{
		SetFromUint64(ulSteamID);
	}

	void Set(uint32_t unAccountID, EUniverse eUniverse, EAccountType eAccountType)
	{
		m_steamid.m_comp.m_unAccountID  = unAccountID;
		m_steamid.m_comp.m_EUniverse    = eUniverse;
		m_steamid.m_comp.m_EAccountType = eAccountType;

		if (eAccountType == k_EAccountTypeClan || eAccountType == k_EAccountTypeGameServer)
		{
			m_steamid.m_comp.m_unAccountInstance = 0;
		}
		else
		{
			m_steamid.m_comp.m_unAccountInstance = k_unSteamUserDefaultInstance;
		}
	}

	void InstancedSet(uint32_t unAccountID, uint32_t unInstance, EUniverse eUniverse, EAccountType eAccountType)
	{
		m_steamid.m_comp.m_unAccountID       = unAccountID;
		m_steamid.m_comp.m_EUniverse         = eUniverse;
		m_steamid.m_comp.m_EAccountType      = eAccountType;
		m_steamid.m_comp.m_unAccountInstance = unInstance;
	}

	void FullSet(uint64_t ulIdentifier, EUniverse eUniverse, EAccountType eAccountType)
	{
		m_steamid.m_comp.m_unAccountID       = (ulIdentifier & k_unSteamAccountIDMask);
		m_steamid.m_comp.m_unAccountInstance = ((ulIdentifier >> 32) & k_unSteamAccountInstanceMask);
		m_steamid.m_comp.m_EUniverse         = eUniverse;
		m_steamid.m_comp.m_EAccountType      = eAccountType;
	}

	void SetFromUint64(uint64_t ulSteamID)
	{
		m_steamid.m_unAll64Bits = ulSteamID;
	}

	void Clear()
	{
		m_steamid.m_comp.m_unAccountID       = 0;
		m_steamid.m_comp.m_EAccountType      = k_EAccountTypeInvalid;
		m_steamid.m_comp.m_EUniverse         = k_EUniverseInvalid;
		m_steamid.m_comp.m_unAccountInstance = 0;
	}

	uint64_t ConvertToUint64() const
	{
		return m_steamid.m_unAll64Bits;
	}

	uint64_t GetStaticAccountKey() const
	{
		return (uint64_t)((((uint64_t)m_steamid.m_comp.m_EUniverse) << 56) + ((uint64_t)m_steamid.m_comp.m_EAccountType << 52) + m_steamid.m_comp.m_unAccountID);
	}

	void CreateBlankAnonLogon(EUniverse eUniverse)
	{
		m_steamid.m_comp.m_unAccountID       = 0;
		m_steamid.m_comp.m_EAccountType      = k_EAccountTypeAnonGameServer;
		m_steamid.m_comp.m_EUniverse         = eUniverse;
		m_steamid.m_comp.m_unAccountInstance = 0;
	}

	void CreateBlankAnonUserLogon(EUniverse eUniverse)
	{
		m_steamid.m_comp.m_unAccountID       = 0;
		m_steamid.m_comp.m_EAccountType      = k_EAccountTypeAnonUser;
		m_steamid.m_comp.m_EUniverse         = eUniverse;
		m_steamid.m_comp.m_unAccountInstance = 0;
	}

	bool BBlankAnonAccount() const
	{
		return m_steamid.m_comp.m_unAccountID == 0 && BAnonAccount() && m_steamid.m_comp.m_unAccountInstance == 0;
	}

	bool BGameServerAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeGameServer || m_steamid.m_comp.m_EAccountType == k_EAccountTypeAnonGameServer;
	}

	bool BPersistentGameServerAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeGameServer;
	}

	bool BAnonGameServerAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeAnonGameServer;
	}

	bool BContentServerAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeContentServer;
	}

	bool BClanAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeClan;
	}

	bool BChatAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeChat;
	}

	bool IsLobby() const
	{
		return (m_steamid.m_comp.m_EAccountType == k_EAccountTypeChat) && (m_steamid.m_comp.m_unAccountInstance & k_EChatInstanceFlagLobby);
	}

	bool BIndividualAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeIndividual || m_steamid.m_comp.m_EAccountType == k_EAccountTypeConsoleUser;
	}

	bool BAnonAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeAnonUser || m_steamid.m_comp.m_EAccountType == k_EAccountTypeAnonGameServer;
	}

	bool BAnonUserAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeAnonUser;
	}

	bool BConsoleUserAccount() const
	{
		return m_steamid.m_comp.m_EAccountType == k_EAccountTypeConsoleUser;
	}

	void SetAccountID(uint32_t unAccountID)
	{
		m_steamid.m_comp.m_unAccountID = unAccountID;
	}

	void SetAccountInstance(uint32_t unInstance)
	{
		m_steamid.m_comp.m_unAccountInstance = unInstance;
	}

	uint32_t GetAccountID() const
	{
		return m_steamid.m_comp.m_unAccountID;
	}

	uint32_t GetUnAccountInstance() const
	{
		return m_steamid.m_comp.m_unAccountInstance;
	}

	EAccountType GetEAccountType() const
	{
		return (EAccountType)m_steamid.m_comp.m_EAccountType;
	}

	EUniverse GetEUniverse() const
	{
		return m_steamid.m_comp.m_EUniverse;
	}

	void SetEUniverse(EUniverse eUniverse)
	{
		m_steamid.m_comp.m_EUniverse = eUniverse;
	}

	bool IsValid() const
	{
		if (m_steamid.m_comp.m_EAccountType <= k_EAccountTypeInvalid || m_steamid.m_comp.m_EAccountType >= k_EAccountTypeMax)
			return false;

		if (m_steamid.m_comp.m_EUniverse <= k_EUniverseInvalid || m_steamid.m_comp.m_EUniverse >= k_EUniverseMax)
			return false;

		if (m_steamid.m_comp.m_EAccountType == k_EAccountTypeIndividual)
		{
			if (m_steamid.m_comp.m_unAccountID == 0 || m_steamid.m_comp.m_unAccountInstance != k_unSteamUserDefaultInstance)
				return false;
		}

		if (m_steamid.m_comp.m_EAccountType == k_EAccountTypeClan)
		{
			if (m_steamid.m_comp.m_unAccountID == 0 || m_steamid.m_comp.m_unAccountInstance != 0)
				return false;
		}

		if (m_steamid.m_comp.m_EAccountType == k_EAccountTypeGameServer)
		{
			if (m_steamid.m_comp.m_unAccountID == 0)
				return false;
		}

		return true;
	}

	explicit CSteamID(const char *pchSteamID, EUniverse eDefaultUniverse = k_EUniverseInvalid)
	{
		SetFromString(pchSteamID, eDefaultUniverse);
	}

	// I just stole this from the CSGO source code leak
	void SetFromString(const char *pchSteamID, EUniverse eDefaultUniverse)
	{
		uint32_t  nAccountID = 0;
		uint32_t  nInstance  = 1;
		EUniverse eUniverse  = eDefaultUniverse;

		static_assert(sizeof(eUniverse) == sizeof(int));
		EAccountType eAccountType = k_EAccountTypeIndividual;
		if (*pchSteamID == '[')
			pchSteamID++;

		if (*pchSteamID == 'A')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeAnonGameServer;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;

			if (std::strchr(pchSteamID, '('))
				std::sscanf(std::strchr(pchSteamID, '('), "(%u)", &nInstance);
			const char *pchColon = std::strchr(pchSteamID, ':');
			if (pchColon && *pchColon != 0 && std::strchr(pchColon + 1, ':'))
				std::sscanf(pchSteamID, "%u:%u:%u", (uint32_t *)&eUniverse, &nAccountID, &nInstance);
			else if (pchColon)
				std::sscanf(pchSteamID, "%u:%u", (uint32_t *)&eUniverse, &nAccountID);
			else
				std::sscanf(pchSteamID, "%u", &nAccountID);

			if (nAccountID == 0)
				CreateBlankAnonLogon(eUniverse);
			else
				InstancedSet(nAccountID, nInstance, eUniverse, eAccountType);
			return;
		}
		else if (*pchSteamID == 'G')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeGameServer;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'C')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeContentServer;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'g')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeClan;
			nInstance    = 0;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'c')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeChat;
			nInstance    = k_EChatInstanceFlagClan;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'L')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeChat;
			nInstance    = k_EChatInstanceFlagLobby;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'T')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeChat;
			nInstance    = 0;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'U')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeIndividual;
			nInstance    = 1;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else if (*pchSteamID == 'i')
		{
			pchSteamID++;
			eAccountType = k_EAccountTypeInvalid;
			nInstance    = 1;
			if (*pchSteamID == '-' || *pchSteamID == ':')
				pchSteamID++;
		}
		else
		{
			uint64_t unSteamID64 = std::strtoull(pchSteamID, nullptr, 10);
			if (unSteamID64 > 0xffffffffU)
			{
				SetFromUint64(unSteamID64);
				return;
			}
		}

		if (strchr(pchSteamID, ':'))
		{
			if (*pchSteamID == '[')
				pchSteamID++;
			sscanf(pchSteamID, "%u:%u", (uint32_t *)&eUniverse, &nAccountID);
			if (eUniverse == k_EUniverseInvalid)
				eUniverse = eDefaultUniverse;
		}
		else
		{
			sscanf(pchSteamID, "%u", &nAccountID);
		}

		InstancedSet(nAccountID, nInstance, eUniverse, eAccountType);
	}

	inline bool operator==(const CSteamID &val) const
	{
		return m_steamid.m_unAll64Bits == val.m_steamid.m_unAll64Bits;
	}

	inline bool operator!=(const CSteamID &val) const
	{
		return !operator==(val);
	}

	inline bool operator<(const CSteamID &val) const
	{
		return m_steamid.m_unAll64Bits < val.m_steamid.m_unAll64Bits;
	}

	inline bool operator>(const CSteamID &val) const
	{
		return m_steamid.m_unAll64Bits > val.m_steamid.m_unAll64Bits;
	}

private:
	union SteamID_t
	{
		struct SteamIDComponent_t
		{
#ifdef ENDIAN_BIG
			EUniverse m_EUniverse : 8;
			uint32_t  m_EAccountType : 4;
			uint32_t  m_unAccountInstance : 20;
			uint32_t  m_unAccountID : 32;
#else
			uint32_t  m_unAccountID : 32;
			uint32_t  m_unAccountInstance : 20;
			uint32_t  m_EAccountType : 4;
			EUniverse m_EUniverse : 8;
#endif
		} m_comp;
		uint64_t m_unAll64Bits;
	} m_steamid;
};
