#include "google/protobuf/json/json.h"
#include "library.hpp"
#include "nlohmann/json_fwd.hpp"
#include "steam.hpp"
#include <Windows.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <citadel_gcmessages_common.pb.h>
#include <citadel_gcmessages_server.pb.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>
#include <google/protobuf/util/json_util.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <safetyhook.hpp>
#include <steammessages.pb.h>
#include <utility>
#include <vector>

#ifdef SendMessage
#undef SendMessage
#endif

typedef void (*MsgFn)(const char *fmt, ...);

const uint32_t k_EMsgProtoBufFlag = 0x80000000;

bool                                              wantsProtobufDebugLog                            = false;
MsgFn                                             pMsg                                             = nullptr;
SteamGameServer_GetSteamIDFn                      pSteamGameServer_GetSteamID                      = nullptr;
SteamClientFn                                     pSteamClient                                     = nullptr;
SteamGameServer_GetHSteamPipeFn                   pSteamGameServer_GetHSteamPipe                   = nullptr;
SteamGameServer_GetHSteamUserFn                   pSteamGameServer_GetHSteamUser                   = nullptr;
SteamAPI_ISteamClient_GetISteamGenericInterfaceFn pSteamAPI_ISteamClient_GetISteamGenericInterface = nullptr;
SafetyHookInline                                  hook_SteamGameServer_RunCallbacks;
SafetyHookInline                                  hook_SteamAPI_RegisterCallback;
SafetyHookInline                                  hook_SteamAPI_UnregisterCallback;
SafetyHookVmt                                     hook_ISteamGameCoordinator;
SafetyHookVm                                      hook_ISteamGameCoordinator_SendMessage;
SafetyHookVm                                      hook_ISteamGameCoordinator_IsMessageAvailable;
SafetyHookVm                                      hook_ISteamGameCoordinator_RetrieveMessage;
struct
{
	CCallbackBase *GCMessageAvailable;
} steam_callbacks;
std::vector<std::pair<uint32_t, std::string>> gc_custom_pending;
struct ObjectCache
{
	static constexpr int Lobby              = 101;
	static constexpr int ServerStaticLobby  = 102;
	static constexpr int ServerDynamicLobby = 106;

	CSOCitadelLobby              lobby; // Not sure if we need this too, its the only place with match_id, gamemode, etc. Probably?
	CSOCitadelServerStaticLobby  static_lobby;
	CSOCitadelServerDynamicLobby dynamic_lobby;

	bool sent_lobby = false;
} object_cache;

inline uint64_t GetServerSteamID()
{
	if (pSteamGameServer_GetSteamID)
		return pSteamGameServer_GetSteamID();
	return 0;
}

template<typename T>
inline T GetRandom(T min, T max)
{
	static std::random_device seed;
	static std::mt19937       gen;

	std::uniform_int_distribution<T> dist(min, max);
	return dist(gen);
}

template<typename... Args>
inline void Msg(const std::format_string<Args...> fmt, Args &&...args)
{
	const std::string s = std::vformat(fmt.get(), std::make_format_args(args...));
	if (pMsg)
		pMsg("[Server-Match] %s\n", s.c_str());
}

template<typename... Args>
inline void MsgIf(bool value, const std::format_string<Args...> fmt, Args &&...args)
{
	if (value)
	{
		const std::string s = std::vformat(fmt.get(), std::make_format_args(args...));
		if (pMsg)
			pMsg("[Server-Match] %s\n", s.c_str());
	}
}

template<typename T>
inline T ForceEndian(T value, std::endian expected)
{
	if (expected == std::endian::native)
		return value;
	return std::byteswap(value);
}

inline std::optional<ECitadelMatchMode> ConvertMatchMode(const std::string &s)
{
	// Try to parse directly
	ECitadelMatchMode value = k_ECitadelMatchMode_Invalid;
	if (ECitadelMatchMode_Parse(s, &value))
		return value;

	std::string lower(s.size(), '\0');
	std::transform(s.begin(), s.end(), lower.begin(), [](char c) -> char {
		return std::tolower(c);
	});

	if (lower == "invalid")
		return k_ECitadelMatchMode_Invalid;
	if (lower == "unranked")
		return k_ECitadelMatchMode_Unranked;
	if (lower == "privatelobby")
		return k_ECitadelMatchMode_PrivateLobby;
	if (lower == "coopbot")
		return k_ECitadelMatchMode_CoopBot;
	if (lower == "ranked")
		return k_ECitadelMatchMode_Ranked;
	if (lower == "servertest")
		return k_ECitadelMatchMode_ServerTest;
	if (lower == "tutorial")
		return k_ECitadelMatchMode_Tutorial;
	return std::nullopt;
}

inline std::optional<ECitadelGameMode> ConvertGameMode(const std::string &s)
{
	// Try to parse directly
	ECitadelGameMode value = k_ECitadelGameMode_Invalid;
	if (ECitadelGameMode_Parse(s, &value))
		return value;

	std::string lower(s.size(), '\0');
	std::transform(s.begin(), s.end(), lower.begin(), [](char c) -> char {
		return std::tolower(c);
	});

	if (lower == "invalid")
		return k_ECitadelGameMode_Invalid;
	if (lower == "normal")
		return k_ECitadelGameMode_Normal;
	if (lower == "1v1test")
		return k_ECitadelGameMode_1v1Test;
	if (lower == "sandbox")
		return k_ECitadelGameMode_Sandbox;
	return std::nullopt;
}

inline std::optional<ECitadelLobbyTeam> ConvertTeam(const std::string &s)
{
	// Try to parse directly
	ECitadelLobbyTeam value = k_ECitadelLobbyTeam_Spectator;
	if (ECitadelLobbyTeam_Parse(s, &value))
		return value;

	std::string lower(s.size(), '\0');
	std::transform(s.begin(), s.end(), lower.begin(), [](char c) -> char {
		return std::tolower(c);
	});

	if (lower == "team0")
		return k_ECitadelLobbyTeam_Team0;
	if (lower == "team1")
		return k_ECitadelLobbyTeam_Team1;
	if (lower == "spectator")
		return k_ECitadelLobbyTeam_Spectator;

	// TODO: Is "amber" team0 and "sapphire" team1?
	// Extra
	if (lower == "amber")
		return k_ECitadelLobbyTeam_Team0;
	if (lower == "sapphire")
		return k_ECitadelLobbyTeam_Team1;
	if (lower == "spec")
		return k_ECitadelLobbyTeam_Spectator;
	return std::nullopt;
}

inline std::filesystem::path GetMatchDirectory()
{
	return std::filesystem::weakly_canonical(std::filesystem::current_path() / ".." / "match");
}

inline const google::protobuf::json::PrintOptions &GetProtobufJsonPrintOptions()
{
	static google::protobuf::json::PrintOptions options {
	    .add_whitespace                       = false,
	    .always_print_fields_with_no_presence = false,
	    .always_print_enums_as_ints           = true,
	    .preserve_proto_field_names           = true,
	    .unquote_int64_if_possible            = false,
	};
	return options;
}

template<typename T>
inline std::optional<nlohmann::json> ProtobufToJSON(const CExtraMsgBlock &extra)
{
	try
	{
		// TODO: I have no idea what compression format they use
		if (extra.is_compressed())
		{
			Msg("WARNING: The following CExtraMsgBlock is compressed {}, we don't support compression yet, please report this", extra.msg_type());
			return std::nullopt;
		}

		T msg;
		if (!msg.ParseFromString(extra.contents()))
		{
			Msg("WARNING: The following CExtraMsgBlock failed to parse, will be ignored in the dump", extra.msg_type());
			return std::nullopt;
		}

		std::string str = "";
		if (!google::protobuf::util::MessageToJsonString(msg, &str, GetProtobufJsonPrintOptions()).ok())
		{
			Msg("WARNING: The following CExtraMsgBlock failed to serialize to JSON, will be ignored in the dump", extra.msg_type());
			return std::nullopt;
		}

		nlohmann::json result;
		result["msg_type"] = extra.msg_type();
		result["contents"] = nlohmann::json::parse(str);
		return result;
	}
	catch (...)
	{
		Msg("WARNING: The following CExtraMsgBlock failed failed to convert to JSON {}, will be ignored in the dump", extra.msg_type());
		return std::nullopt;
	}
}

inline std::optional<nlohmann::json> MatchSignoutToFullJson(const CMsgServerToGCMatchSignout &msg)
{
	// CMsgServerToGCMatchSignout has some data in bytes which we want to handle specifically
	nlohmann::json extra = nlohmann::json::array();
	for (int i = 0; i < msg.additional_data_size(); i++)
	{
		const CExtraMsgBlock &block = msg.additional_data(i);
		if (block.is_compressed())
		{
			// Lets hope not... I don't know how this is compressed, zlib maybe? Should probably log this better so we can figure out the compression format
			Msg("WARNING: The following CExtraMsgBlock is compressed {}, we don't support compression yet!", block.msg_type());
			continue;
		}

		switch (block.msg_type())
		{
			case k_EServerSignoutData_Disconnections:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_Disconnections>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_AccountStatChanges:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_AccountStatChanges>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_DetailedStats:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_DetailedStats>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_ServerPerfStats:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_ServerPerfStats>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_PerfData:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_PerfData>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_PlayerChat:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_PlayerChat>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_BookRewards:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_BookRewards>(block))
					extra.push_back(json.value());
				break;
			}
			case k_EServerSignoutData_PenalizedPlayers:
			{
				if (std::optional<nlohmann::json> json = ProtobufToJSON<CMsgServerSignoutData_PenalizedPlayers>(block))
					extra.push_back(json.value());
				break;
			}
			default:
			{
				Msg("WARNING: The following CExtraMsgBlock is unknown to us {}, will be ignored in the dump", block.msg_type());
				break;
			}
		}
	}

	try
	{
		// Do this whole Protobuf->String->JSON stuff one more time (Surely this will never cause any issues)
		std::string  str;
		absl::Status status = google::protobuf::util::MessageToJsonString(msg, &str, GetProtobufJsonPrintOptions());
		if (!status.ok())
		{
			Msg("Failed to serialize 'CMsgServerToGCMatchSignout' to JSON: {}", status.message());
			return std::nullopt;
		}
		nlohmann::json result = nlohmann::json::parse(str);

		// Delete the "additional_data" block since its just binary data and add the parsed version back
		result.erase("additional_data");
		result["additional_data"] = extra;
		return result;
	}
	catch (...)
	{
		Msg("Failed to process 'CMsgServerToGCMatchSignout', won't save match data");
		return std::nullopt;
	}
}

inline bool ParseMatchInformation()
{
	object_cache.lobby.Clear();
	object_cache.static_lobby.Clear();
	object_cache.dynamic_lobby.Clear();

	// Prefer 'match.json' but allow 'match.jsonc'
	std::filesystem::path jsonFilePath = std::filesystem::weakly_canonical(GetMatchDirectory() / "match.json");
	if (!std::filesystem::exists(jsonFilePath))
		jsonFilePath.replace_extension(".jsonc");

	if (!std::filesystem::exists(jsonFilePath))
	{
		Msg("Missing file 'match.json/match.jsonc' in '{}'", jsonFilePath.remove_filename().string());
		return false;
	}

	object_cache.lobby.set_lobby_id(1);
	object_cache.lobby.set_match_id(1);
	object_cache.lobby.set_server_steam_id(GetServerSteamID());
	object_cache.lobby.set_server_state(k_eLobbyServerState_Assign);
	object_cache.lobby.set_safe_to_abandon(false);
	object_cache.static_lobby.set_server_steam_id(GetServerSteamID());
	object_cache.static_lobby.set_lobby_id(1);
	object_cache.static_lobby.set_gc_provided_heroes(true);
	object_cache.static_lobby.set_bot_difficulty(k_ECitadelBotDifficulty_None); // I don't even know how to add bots, this makes no difference
	object_cache.static_lobby.set_match_start_time(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
	object_cache.static_lobby.set_region_mode(k_ECitadelRegionMode_ROW);
	object_cache.static_lobby.set_new_player_pool(false);
	object_cache.static_lobby.set_low_pri_pool(false);
	object_cache.dynamic_lobby.set_lobby_id(1);
	object_cache.dynamic_lobby.clear_left_account_ids();
	object_cache.dynamic_lobby.set_spectator_count(0);
	object_cache.dynamic_lobby.set_broadcast_active(false);

	try
	{
		std::ifstream  f(jsonFilePath);
		nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true); // Parse with comments

		if (std::optional<ECitadelMatchMode> mode = ConvertMatchMode(j.value("match_mode", "ranked")))
		{
			object_cache.lobby.set_match_mode(mode.value());
		}
		else
		{
			Msg("Failed to parse 'match_mode' to valid 'ECitadelMatchMode'");
			return false;
		}

		if (std::optional<ECitadelGameMode> mode = ConvertGameMode(j.value("game_mode", "normal")))
		{
			object_cache.lobby.set_game_mode(mode.value());
		}
		else
		{
			Msg("Failed to parse 'game_mode' to valid 'ECitadelGameMode'");
			return false;
		}

		object_cache.static_lobby.set_level_name(j.value("map", "street_test"));

		if (j.contains("broadcast") && j["broadcast"].is_string())
		{
			object_cache.static_lobby.set_broadcast_url(j["broadcast"].get<std::string>());
			object_cache.dynamic_lobby.set_broadcast_active(true);
		}

		if (j.contains("console") && j["console"].is_string())
		{
			if (CSOCitadelServerStaticLobby_DevSettings *settings = object_cache.static_lobby.mutable_dev_settings())
			{
				settings->set_console_string(j["console"].get<std::string>());
			}
			else
			{
				Msg("Failed to allocate 'CSOCitadelServerStaticLobby_DevSettings'");
				return false;
			}
		}

		if (j.contains("players") && j["players"].is_array())
		{
			for (auto it = j["players"].begin(); it != j["players"].end(); it++)
			{
				CSOCitadelServerStaticLobby_Member *member = object_cache.static_lobby.add_members();
				if (!member)
				{
					Msg("Failed to allocate 'CSOCitadelServerStaticLobby_Member'");
					return false;
				}

				if (!it->is_object())
				{
					Msg("Entry in 'players' array is not an object");
					return false;
				}

				CSteamID steamID(it->value("steam_id", "").c_str());
				if (!steamID.IsValid() || !steamID.BIndividualAccount())
				{
					Msg("Entry in 'players' array has invalid 'steam_id' field");
					return false;
				}
				member->set_account_id(steamID.GetAccountID());
				member->set_persona_name(it->value("name", std::to_string(steamID.ConvertToUint64())));

				if (std::optional<ECitadelLobbyTeam> team = ConvertTeam(it->value("team", "")))
				{
					member->set_team(team.value());
				}
				else
				{
					Msg("Entry in 'players' array has invalid 'team' field");
					return false;
				}

				if (!it->contains("hero") || !it->operator[]("hero").is_number())
				{
					Msg("Entry in 'players' array has invalid 'hero' field");
					return false;
				}
				member->set_hero_id(it->operator[]("hero").get<uint32_t>());

				// The rest
				member->set_player_slot(object_cache.static_lobby.members_size());
				member->set_party_index(object_cache.static_lobby.members_size());
				member->set_platform(k_eGCPlatform_PC);
				member->clear_award_ids();
				member->set_is_comms_restricted(false);

				// Some neat log
				Msg("Added player '{}' to team '{}' in lane '{}' on hero '{}'", member->account_id(), ECitadelLobbyTeam_Name(member->team()), "<unknown>", member->hero_id());
			}
		}
		else
		{
			Msg("Missing 'players' array");
			return false;
		}

		Msg("Created match on '{}' in mode '{}/{}', broadcasting: {}", object_cache.static_lobby.level_name(), ECitadelMatchMode_Name(object_cache.lobby.match_mode()), ECitadelGameMode_Name(object_cache.lobby.game_mode()), object_cache.static_lobby.has_broadcast_url() ? "Active" : "Disabled");
		return true;
	}
	catch (...)
	{
		Msg("Failed to parse '{}', invalid JSON?", jsonFilePath.string());
		return false;
	}
}

template<uint32_t type, class proto>
inline std::optional<proto> CheckProtoAndRemoveHeader(uint32_t unMsgType, const void *pubData, uint32_t cubData, CMsgProtoBufHeader &headerMsg)
{
	if (!(unMsgType & k_EMsgProtoBufFlag))
		return std::nullopt;

	if ((unMsgType & ~k_EMsgProtoBufFlag) != type)
		return std::nullopt;

	// First 4 bytes are just unMsgType again
	const char *data         = reinterpret_cast<const char *>(pubData);
	uint32_t    headerLength = ForceEndian(*reinterpret_cast<const uint32_t *>((data + sizeof(uint32_t))), std::endian::little);
	const char *header       = data + sizeof(uint32_t) + sizeof(uint32_t);
	if (!headerMsg.ParseFromArray(header, headerLength))
		return std::nullopt;

	const char *body = data + sizeof(uint32_t) + sizeof(uint32_t) + headerLength;
	proto       msg;
	if (!msg.ParseFromArray(body, cubData - sizeof(uint32_t) - sizeof(uint32_t) - headerLength))
		return std::nullopt;
	return msg;
}

inline std::optional<std::pair<uint32_t, std::string>> CreateGCSendProto(uint32_t type, google::protobuf::Message &msg, CMsgProtoBufHeader &header)
{
	// Type + HeaderLength + Header + Body
	std::string s(sizeof(uint32_t) + sizeof(uint32_t) + header.ByteSizeLong() + msg.ByteSizeLong(), '\0');
	*reinterpret_cast<uint32_t *>(s.data())                    = ForceEndian(type | k_EMsgProtoBufFlag, std::endian::little);
	*reinterpret_cast<uint32_t *>(s.data() + sizeof(uint32_t)) = header.ByteSizeLong();
	if (!header.SerializeToArray(s.data() + sizeof(uint32_t) + sizeof(uint32_t), header.ByteSizeLong()))
		return std::nullopt;
	if (!msg.SerializeToArray(s.data() + sizeof(uint32_t) + sizeof(uint32_t) + header.ByteSizeLong(), msg.ByteSizeLong()))
		return std::nullopt;
	return std::make_pair(type | k_EMsgProtoBufFlag, s);
}

inline std::optional<std::pair<uint32_t, std::string>> CreateGCSendProto(uint32_t type, google::protobuf::Message &msg)
{
	CMsgProtoBufHeader header;
	return CreateGCSendProto(type, msg, header);
}

EGCResults ISteamGameCoordinator__SendMessage(ISteamGameCoordinator *self, uint32_t unMsgType, const void *pubData, uint32_t cubData)
{
	CMsgProtoBufHeader header;
	if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCEnterMatchmaking, CMsgServerToGCEnterMatchmaking>(unMsgType, pubData, cubData, header))
	{
		Msg("CMsgServerToGCEnterMatchmaking (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		// TODO: Make this a proper safe guard
		if (object_cache.sent_lobby)
		{
			for (int i = 0; i < 5; i++)
				Msg("Received k_EMsgServerToGCEnterMatchmaking but lobby already sent prior");
			return k_EGCResultOK;
		}
		object_cache.sent_lobby = true;

		if (!ParseMatchInformation())
		{
			// Already logged
			return k_EGCResultOK;
		}

		// Create an object cache on the server with our data
		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::Lobby);
			msg.set_object_data(object_cache.lobby.SerializeAsString());
			msg.set_version(GetRandom(std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()));
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Create, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerStaticLobby);
			msg.set_object_data(object_cache.static_lobby.SerializeAsString());
			msg.set_version(GetRandom(std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()));
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Create, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerDynamicLobby);
			msg.set_object_data(object_cache.dynamic_lobby.SerializeAsString());
			msg.set_version(GetRandom(std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()));
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Create, msg))
				gc_custom_pending.push_back(send.value());
		}

		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCUpdateLobbyServerState, CMsgServerToGCUpdateLobbyServerState>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCUpdateLobbyServerState (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		bool didUpdate = false;
		if (msg->lobby_id() == object_cache.lobby.lobby_id())
		{
			if (msg->has_server_state())
			{
				object_cache.lobby.set_server_state(msg->server_state());
				didUpdate = true;
			}
			if (msg->has_safe_to_abandon())
			{
				object_cache.lobby.set_safe_to_abandon(msg->safe_to_abandon());
				didUpdate = true;
			}
		}

		// Update the cache
		if (didUpdate)
		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::Lobby);
			msg.set_object_data(object_cache.lobby.SerializeAsString());
			msg.set_version(GetRandom(std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()));
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Update, msg))
				gc_custom_pending.push_back(send.value());
		}

		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCMatchSignoutPermission, CMsgServerToGCMatchSignoutPermission>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCMatchSignoutPermission (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		uint64_t jobid = header.job_id_source();

		// We just always say yes and request all data
		{
			CMsgProtoBufHeader header;
			header.set_job_id_target(jobid);

			CMsgServerToGCMatchSignoutPermissionResponse msg;
			msg.set_can_sign_out(true);
			msg.add_requested_data(k_EServerSignoutData_Disconnections);
			msg.add_requested_data(k_EServerSignoutData_AccountStatChanges);
			msg.add_requested_data(k_EServerSignoutData_DetailedStats);
			msg.add_requested_data(k_EServerSignoutData_ServerPerfStats);
			msg.add_requested_data(k_EServerSignoutData_PerfData);
			msg.add_requested_data(k_EServerSignoutData_PlayerChat);
			msg.add_requested_data(k_EServerSignoutData_BookRewards);
			msg.add_requested_data(k_EServerSignoutData_PenalizedPlayers);
			if (auto send = CreateGCSendProto(k_EMsgServerToGCMatchSignoutPermissionResponse, msg, header))
				gc_custom_pending.push_back(send.value());
		}

		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCMatchSignout, CMsgServerToGCMatchSignout>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCMatchSignout (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		try
		{
			if (std::optional<nlohmann::json> json = MatchSignoutToFullJson(msg.value()))
			{
				std::ofstream f(GetMatchDirectory() / "result.json");
				f << std::setw(4) << json.value();
			}
		}
		catch (const std::exception &ex)
		{
			Msg("Failed to serialize convert 'CMsgServerToGCMatchSignout' to JSON and dump to file: {}, no match stats will be saved", ex.what());
		}
		catch (...)
		{
			Msg("Failed to serialize convert 'CMsgServerToGCMatchSignout' to JSON and dump to file, no match stats will be saved");
		}

		uint64_t jobid = header.job_id_source();

		// Destroy the caches
		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::Lobby);
			msg.set_object_data(object_cache.lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerStaticLobby);
			msg.set_object_data(object_cache.static_lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerDynamicLobby);
			msg.set_object_data(object_cache.dynamic_lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgProtoBufHeader header;
			header.set_job_id_target(jobid);

			CMsgServerToGCMatchSignoutResponse msg;
			msg.set_result(CMsgServerToGCMatchSignoutResponse_ESignoutResult_k_ESignout_Success);
			if (auto send = CreateGCSendProto(k_EMsgServerToGCMatchSignoutResponse, msg, header))
				gc_custom_pending.push_back(send.value());
		}
		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCAbandonMatch, CMsgServerToGCAbandonMatch>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCAbandonMatch (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		uint64_t jobid = header.job_id_source();

		// Destroy the caches
		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::Lobby);
			msg.set_object_data(object_cache.lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerStaticLobby);
			msg.set_object_data(object_cache.static_lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgSOSingleObject msg;
			msg.set_type_id(ObjectCache::ServerDynamicLobby);
			msg.set_object_data(object_cache.dynamic_lobby.SerializeAsString());
			if (CMsgSOIDOwner *owner = msg.mutable_owner_soid())
			{
				owner->set_type(1);
				owner->set_id(GetServerSteamID());
			}
			if (auto send = CreateGCSendProto(k_ESOMsg_Destroy, msg))
				gc_custom_pending.push_back(send.value());
		}

		{
			CMsgProtoBufHeader header;
			header.set_job_id_target(jobid);

			CMsgServerToGCAbandonMatchResponse msg;
			if (auto send = CreateGCSendProto(k_EMsgServerToGCAbandonMatchResponse, msg, header))
				gc_custom_pending.push_back(send.value());
		}

		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCTestConnection, CMsgServerToGCTestConnection>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCTestConnection (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());

		uint64_t jobid = header.job_id_source();

		{
			CMsgProtoBufHeader header;
			header.set_job_id_target(jobid);

			CMsgServerToGCTestConnectionResponse msg;
			msg.set_state(object_cache.lobby.server_state());
			msg.set_state(object_cache.lobby.lobby_id());
			if (auto send = CreateGCSendProto(k_EMsgServerToGCTestConnectionResponse, msg, header))
				gc_custom_pending.push_back(send.value());
		}

		return k_EGCResultOK;
	}
	else if (auto msg = CheckProtoAndRemoveHeader<k_EMsgServerToGCUpdateMatchInfo, CMsgServerToGCUpdateMatchInfo>(unMsgType, pubData, cubData, header))
	{
		MsgIf(wantsProtobufDebugLog, "CMsgServerToGCUpdateMatchInfo (Header)\n{}\n(Body)\n{}", header.Utf8DebugString(), msg->Utf8DebugString());
		return k_EGCResultOK;
	}

	return hook_ISteamGameCoordinator_SendMessage.call<EGCResults>(self, unMsgType, pubData, cubData);
}

bool ISteamGameCoordinator__IsMessageAvailable(ISteamGameCoordinator *self, uint32_t *pcubMsgSize)
{
	// Our own messages take priority
	if (!gc_custom_pending.empty())
	{
		if (pcubMsgSize)
			*pcubMsgSize = gc_custom_pending[0].second.size();
		return true;
	}
	return hook_ISteamGameCoordinator_IsMessageAvailable.call<bool>(self, pcubMsgSize);
}

EGCResults ISteamGameCoordinator__RetrieveMessage(ISteamGameCoordinator *self, uint32_t *punMsgType, void *pubDest, uint32_t cubDest, uint32_t *pcubMsgSize)
{
	if (!gc_custom_pending.empty())
	{
		if (punMsgType)
			*punMsgType = gc_custom_pending[0].first;
		if (pcubMsgSize)
			*pcubMsgSize = gc_custom_pending[0].second.size();
		if (cubDest < gc_custom_pending[0].second.size())
			return k_EGCResultBufferTooSmall;
		std::memcpy(pubDest, gc_custom_pending[0].second.data(), gc_custom_pending[0].second.size());
		gc_custom_pending.erase(gc_custom_pending.begin());
		return k_EGCResultOK;
	}
	return hook_ISteamGameCoordinator_RetrieveMessage.call<EGCResults>(self, punMsgType, pubDest, cubDest, pcubMsgSize);
}

void SteamGameServer_RunCallbacks_Hooked()
{
	static bool hooked_gc = false;
	if (!hooked_gc && pSteamClient && pSteamGameServer_GetHSteamUser && pSteamGameServer_GetHSteamPipe && pSteamAPI_ISteamClient_GetISteamGenericInterface)
	{
		ISteamClient *steamclient = pSteamClient();
		HSteamUser    user        = pSteamGameServer_GetHSteamUser();
		HSteamPipe    pipe        = pSteamGameServer_GetHSteamPipe();
		if (steamclient)
		{
			ISteamGameCoordinator *gc = reinterpret_cast<ISteamGameCoordinator *>(pSteamAPI_ISteamClient_GetISteamGenericInterface(steamclient, user, pipe, STEAMGAMECOORDINATOR_INTERFACE_VERSION));
			if (gc)
			{
				hook_ISteamGameCoordinator                    = safetyhook::create_vmt(gc);
				hook_ISteamGameCoordinator_SendMessage        = safetyhook::create_vm(hook_ISteamGameCoordinator, 0, &ISteamGameCoordinator__SendMessage);
				hook_ISteamGameCoordinator_IsMessageAvailable = safetyhook::create_vm(hook_ISteamGameCoordinator, 1, &ISteamGameCoordinator__IsMessageAvailable);
				hook_ISteamGameCoordinator_RetrieveMessage    = safetyhook::create_vm(hook_ISteamGameCoordinator, 2, &ISteamGameCoordinator__RetrieveMessage);

				hooked_gc = true;
			}
		}
	}

	// Try to only trigger once per loop, if messages aren't read we shouldn't spam
	if (!gc_custom_pending.empty() && steam_callbacks.GCMessageAvailable)
	{
		GCMessageAvailable_t msg;
		msg.m_nMessageSize = gc_custom_pending[0].second.size();
		steam_callbacks.GCMessageAvailable->Run(&msg);
	}

	hook_SteamGameServer_RunCallbacks.call<void>();
}

void SteamAPI_RegisterCallback_Hooked(CCallbackBase *pCallback, int iCallback)
{
	if (iCallback == GCMessageAvailable_t::k_iCallback)
		steam_callbacks.GCMessageAvailable = pCallback;

	hook_SteamAPI_RegisterCallback.call<void>(pCallback, iCallback);
}

void SteamAPI_UnregisterCallback_Hooked(CCallbackBase *pCallback)
{
	if (pCallback == steam_callbacks.GCMessageAvailable)
		steam_callbacks.GCMessageAvailable = nullptr;
}

bool HookSteamAPI(Library &steamapi)
{
	pSteamGameServer_GetSteamID = reinterpret_cast<SteamGameServer_GetSteamIDFn>(steamapi.Get("SteamGameServer_GetSteamID"));
	if (!pSteamGameServer_GetSteamID)
		return false;

	auto pSteamGameServer_RunCallbacks = reinterpret_cast<SteamGameServer_RunCallbacksFn>(steamapi.Get("SteamGameServer_RunCallbacks"));
	if (!pSteamGameServer_RunCallbacks)
		return false;
	hook_SteamGameServer_RunCallbacks = safetyhook::create_inline(pSteamGameServer_RunCallbacks, SteamGameServer_RunCallbacks_Hooked);

	auto pSteamAPI_RegisterCallback = reinterpret_cast<SteamAPI_RegisterCallbackFn>(steamapi.Get("SteamAPI_RegisterCallback"));
	if (!pSteamAPI_RegisterCallback)
		return false;
	hook_SteamAPI_RegisterCallback = safetyhook::create_inline(pSteamAPI_RegisterCallback, SteamAPI_RegisterCallback_Hooked);

	auto pSteamAPI_UnregisterCallback = reinterpret_cast<SteamAPI_UnregisterCallbackFn>(steamapi.Get("SteamAPI_UnregisterCallback"));
	if (!pSteamAPI_UnregisterCallback)
		return false;
	hook_SteamAPI_UnregisterCallback = safetyhook::create_inline(pSteamAPI_UnregisterCallback, SteamAPI_UnregisterCallback_Hooked);

	pSteamClient = reinterpret_cast<SteamClientFn>(steamapi.Get("SteamClient"));
	if (!pSteamClient)
		return false;

	pSteamGameServer_GetHSteamPipe = reinterpret_cast<SteamGameServer_GetHSteamPipeFn>(steamapi.Get("SteamGameServer_GetHSteamPipe"));
	if (!pSteamGameServer_GetHSteamPipe)
		return false;

	pSteamGameServer_GetHSteamUser = reinterpret_cast<SteamGameServer_GetHSteamUserFn>(steamapi.Get("SteamGameServer_GetHSteamUser"));
	if (!pSteamGameServer_GetHSteamUser)
		return false;

	pSteamAPI_ISteamClient_GetISteamGenericInterface = reinterpret_cast<SteamAPI_ISteamClient_GetISteamGenericInterfaceFn>(steamapi.Get("SteamAPI_ISteamClient_GetISteamGenericInterface"));
	if (!pSteamAPI_ISteamClient_GetISteamGenericInterface)
		return false;

	return true;
}

// Waiting for linux binaries...
typedef void (*Source2Main_t)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd, const char *szBaseDir, const char *szGame);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
	// Ensure "-dedicated" is set
	if (std::strstr(pCmdLine, "-dedicated") == nullptr)
	{
		MessageBox(NULL, "server-match can only run on dedicated servers", "Launcher Error", MB_OK);
		return 1;
	}

	if (std::strstr(pCmdLine, "-protobufdebug") != nullptr)
		wantsProtobufDebugLog = true;

	// Get all the paths we need
	char szExePath[MAX_PATH];
	if (!GetModuleFileName(NULL, szExePath, sizeof(szExePath)))
	{
		MessageBox(NULL, "GetModuleFileName failed", "Launcher Error", MB_OK);
		return 1;
	}

	std::filesystem::path exePath      = szExePath;
	std::filesystem::path engine2Path  = szExePath;
	std::filesystem::path tier0Path    = szExePath;
	std::filesystem::path steamapiPath = szExePath;
	std::filesystem::path basePath     = szExePath;
	engine2Path.replace_filename("engine2.dll");
	tier0Path.replace_filename("tier0.dll");
	steamapiPath.replace_filename("steam_api64.dll");
	basePath.remove_filename();
	basePath /= "../..";

	// Load tier0 for logging
	Library tier0(tier0Path.string());
	if (!tier0.IsOpen())
	{
		MessageBox(NULL, "Failed to load tier0", "Launcher Error", MB_OK);
		return 1;
	}
	pMsg = reinterpret_cast<MsgFn>(tier0.Get("Msg"));

	// Load Steam first so we can hook some stuff
	Library steamapi(steamapiPath.string());
	if (!steamapi.IsOpen())
	{
		MessageBox(NULL, "Could not load SteamAPI", "Launcher Error", MB_OK);
		return 1;
	}

	if (!HookSteamAPI(steamapi))
	{
		MessageBox(NULL, "Failed to hook SteamAPI", "Launcher Error", MB_OK);
		return 1;
	}

	// Now run the game
	Library engine(engine2Path.string());
	if (!engine.IsOpen())
	{
		MessageBox(NULL, "Could not load engine2", "Launcher Error", MB_OK);
		return 1;
	}

	Source2Main_t pSource2Main = (Source2Main_t)engine.Get("Source2Main");
	if (!pSource2Main)
	{
		MessageBox(NULL, "Could not find Source2Main from engine2", "Launcher Error", MB_OK);
		return 1;
	}

	pSource2Main(hInstance, hPrevInstance, pCmdLine, nCmdShow, basePath.string().c_str(), "citadel");
	return 0;
}
