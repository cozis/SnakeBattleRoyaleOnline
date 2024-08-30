#include "steam_api.h"
#include "steam_wrapper.h"

#if defined(_WIN32)
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
typedef void *os_handle;
#define OS_STDOUT ((os_handle) -2)
#define OS_STDERR ((os_handle) -3)
#elif defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
typedef int os_handle;
#define OS_STDOUT 1
#define OS_STDERR 2
#else
#error "Unknown platform"
#endif
#define OS_INVALID ((os_handle) -1)

static int os_write(os_handle handle, const void *data, size_t size)
{
    if (size > INT_MAX) size = INT_MAX;

#if defined(_WIN32)

    if (0) {}
    else if (handle == OS_STDOUT) handle = GetStdHandle(STD_OUTPUT_HANDLE);
    else if (handle == OS_STDERR) handle = GetStdHandle(STD_ERROR_HANDLE);

    unsigned long written;
    if (!WriteFile(handle, data, size, &written, NULL))
        return -1;

    return (int) written;

#elif defined(__linux__)

    size_t written = 0;
    do {
        int ret = write(handle, data + written, size - written);
        if (ret < 0) {
            if (errno == EINTR)
                ret = 0;
            else
                return -1;
        }
        written += ret;
    } while (written < size);

    return written;

#else

    return -1;
#endif
}

static void os_writes(os_handle handle, const char *str)
{
	os_write(handle, str, strlen(str));
}

static void os_writex(os_handle handle, void *str, size_t num)
{
	char out[1<<10];
	size_t nout = 0;

	uint8_t *ptr = (uint8_t*) str;
	for (size_t i = 0; i < num; i++) {
		uint8_t hi = ptr[i] >> 4;
		uint8_t lo = ptr[0] & 0xF;
		static const char table[] = "0123456789ABCDEF";

		if (sizeof(out) - nout < 3) {
			os_write(handle, out, nout);
			nout = 0;
		}
		out[nout+0] = table[hi];
		out[nout+1] = table[lo];
		out[nout+2] = ' ';
		nout += 3;
	}
	if (nout > 0)
		os_write(handle, out, nout);
}

void debug_func(ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg)
{
	os_writes(OS_STDOUT, pszMsg);
	os_write(OS_STDOUT, "\n", 1);
}

extern "C" bool steam_init(uint64_t app_id)
{
	if (SteamAPI_RestartAppIfNecessary(app_id)) {
		os_writes(OS_STDOUT, "Couldn't init the steam SDK. Is Steam open?\n");
		return false;
	}

	if (!SteamAPI_Init()) {
		os_writes(OS_STDOUT, "Couldn't init the steam SDK. Is Steam open?\n");
		return false;
	}

	os_writes(OS_STDOUT, "Steam initialized\n");
	SteamNetworkingUtils()->InitRelayNetworkAccess();
	//SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, debug_func);
	return true;
}

extern "C" void steam_free(void)
{
	SteamAPI_Shutdown();
}

extern "C" void steam_reset(void)
{
	steam_listen_stop();
	steam_connect_stop();
}

static bool steam_networking_initialized(void)
{
	SteamRelayNetworkStatus_t buffer;
	ESteamNetworkingAvailability code = SteamNetworkingUtils()->GetRelayNetworkStatus(&buffer);
	return code == k_ESteamNetworkingAvailability_Current;
}

extern "C" void steam_update(void)
{
	SteamAPI_RunCallbacks();

	if (steam_networking_initialized())
		SteamNetworkingSockets()->RunCallbacks();
}

typedef enum {
	API_MODE_NONE,
	API_MODE_CLIENT,
	API_MODE_SERVER,
} APIMode;

APIMode api_mode = API_MODE_NONE;

#define ACCEPT_QUEUE_SIZE 32
static HSteamNetConnection accepted_queue[ACCEPT_QUEUE_SIZE];
static int accepted_queue_head = 0;
static int accepted_queue_size = 0;
static HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
static HSteamNetConnection connect_socket = k_HSteamNetConnection_Invalid;
static bool connect_complete = false;
static bool connect_success  = false;

#define MAX_DISCONNECTED 128
static HSteamNetConnection disconnected[MAX_DISCONNECTED];
static int num_disconnected = 0;

static void mark_disconnected(HSteamNetConnection handle)
{
	// Make sure handles aren't marked twice
	int i = 0;
	while (i < num_disconnected && disconnected[i] != handle)
		i++;
	if (i < num_disconnected) {
		os_writes(OS_STDOUT, "Handle marked twice as disconnected\n");
		abort();
	}
	if (num_disconnected == MAX_DISCONNECTED) {
		os_writes(OS_STDOUT, "Disconnect queue full\n");
		abort();
	}
	disconnected[num_disconnected] = handle;
}

extern "C" SteamHandle steam_get_disconnect_message(void)
{
	if (num_disconnected == 0)
		return STEAM_HANDLE_INVALID;
	return disconnected[--num_disconnected];
}

static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
	os_writes(OS_STDOUT, "Connection status changed!\n");
	if (api_mode == API_MODE_NONE)
		os_writes(OS_STDOUT, "Connection status changed while in NONE mode\n");

	switch (pInfo->m_info.m_eState) {

		case k_ESteamNetworkingConnectionState_Connecting:
		if (api_mode == API_MODE_CLIENT) {
			os_writes(OS_STDOUT, "Socket connecting while in client mode\n");
		} else {
			if (accepted_queue_size == ACCEPT_QUEUE_SIZE) {
				os_writes(OS_STDOUT, "Incoming connection ... Discarded\n");
				SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, "oopsies", true);
			} else {
				os_writes(OS_STDOUT, "Incoming connection ... Accepted\n");
				SteamNetworkingSockets()->AcceptConnection(pInfo->m_hConn);
			}
		}
		break;

		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		os_writes(OS_STDOUT, "PROBLEM DETECTED LOCALLY\n");
		mark_disconnected(pInfo->m_hConn);
		break;

		case k_ESteamNetworkingConnectionState_Connected:
		os_writes(OS_STDOUT, "CONNECTED\n");
		if (api_mode == API_MODE_CLIENT) {
			connect_complete = true;
			connect_success = true;
		} else {
			accepted_queue[(accepted_queue_head + accepted_queue_size) % ACCEPT_QUEUE_SIZE] = pInfo->m_hConn;
			accepted_queue_size++;
		}
		break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		os_writes(OS_STDOUT, "CLOSED BY PEER\n");
		if (api_mode == API_MODE_CLIENT) {
			// We were rejected by the server
			connect_complete = true;
			connect_success = false;
		} else {
			mark_disconnected(pInfo->m_hConn);
		}
		break;

		case k_ESteamNetworkingConnectionState_None:
		os_writes(OS_STDOUT, "NONE .. API error?\n");
		mark_disconnected(pInfo->m_hConn); // Does this make sense?
		break;

		case k_ESteamNetworkingConnectionState_FindingRoute:
		os_writes(OS_STDOUT, "FINDING ROUTE\n");
		break;

		default:
		os_writes(OS_STDOUT, "Unexpected connection thing\n");
		//SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, "oopsies", true);
		break;
	}
}

extern "C" bool steam_listen_start(void)
{
	if (api_mode != API_MODE_NONE) {
		os_writes(OS_STDOUT, "Invalid state\n");
		return false;
	}

	if (listen_socket != k_HSteamListenSocket_Invalid)
		return false;
	
	if (SteamNetworkingSockets() == NULL) {
		os_writes(OS_STDOUT, "SteamNetworkingSockets() not available\n");
		return false;
	}

	SteamNetworkingConfigValue_t option;
	option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*) SteamNetConnectionStatusChangedCallback);

	listen_socket = SteamNetworkingSockets()->CreateListenSocketP2P(0, 1, &option);
	if (listen_socket == k_HSteamNetConnection_Invalid) {
		os_writes(OS_STDOUT, "Couldn't start listener\n");
		return false;
	}

	api_mode = API_MODE_SERVER;
	return true;
}

extern "C" void steam_listen_stop(void)
{
	if (listen_socket != k_HSteamListenSocket_Invalid) {

		for (int i = 0; i < accepted_queue_size; i++) {
			uint32_t handle = accepted_queue[(accepted_queue_head + i) % ACCEPT_QUEUE_SIZE];
			steam_close_accepted_connection(handle);
		}
		accepted_queue_head = 0;
		accepted_queue_size = 0;

		SteamNetworkingSockets()->CloseListenSocket(listen_socket);
		listen_socket = k_HSteamListenSocket_Invalid;
	}
	if (api_mode == API_MODE_SERVER)
		api_mode = API_MODE_NONE;
}

extern "C" void steam_close_accepted_connection(uint32_t handle)
{
	SteamNetworkingSockets()->CloseConnection(handle, 0, "oopsies", true);
}

extern "C" uint32_t steam_accept_connection(void)
{
	if (accepted_queue_size == 0)
		return -1;
	
	uint32_t handle = accepted_queue[accepted_queue_head];
	accepted_queue_head = (accepted_queue_head + 1) % ACCEPT_QUEUE_SIZE;
	accepted_queue_size--;

	return handle;
}

extern "C" bool steam_connect_start(uint64_t peer_id)
{
	if (api_mode != API_MODE_NONE) {
		os_writes(OS_STDOUT, "Invalid state\n");
		return false;
	}

	if (connect_socket != k_HSteamNetConnection_Invalid)
		return false;

    SteamNetworkingIdentity identity;
	memset(&identity, 0, sizeof(SteamNetworkingIdentity));
	identity.m_eType = k_ESteamNetworkingIdentityType_SteamID;
	identity.SetSteamID(peer_id);

	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);

	connect_socket = SteamNetworkingSockets()->ConnectP2P(identity, 0, 1, &opt);
	if (connect_socket == k_HSteamNetConnection_Invalid) {
		os_writes(OS_STDOUT, "Bad socket\n");
		return false;
	}

	connect_complete = false;
	connect_success = false;
	api_mode = API_MODE_CLIENT;
	os_writes(OS_STDOUT, "Started connecting\n");
	return true;
}

extern "C" void steam_connect_stop(void)
{
	if (connect_socket != k_HSteamNetConnection_Invalid) {
		SteamNetworkingSockets()->CloseConnection(connect_socket, 0, "oopsies", true);
		connect_socket = k_HSteamNetConnection_Invalid;
		connect_complete = false;
	}
	if (api_mode == API_MODE_CLIENT)
		api_mode = API_MODE_NONE;
}

extern "C" int steam_connect_status(void)
{
	if (connect_complete) {
		if (connect_success)
			return 0; // OK
		else
			return -1; // ERROR
	} else
		return 1; // PENDING
}

extern "C" bool steam_send(uint32_t conn, void *buf, int len)
{
	if (conn == STEAM_HANDLE_SERVER) {
		//if (connect_socket == k_HSteamNetConnection_Invalid)
		//	abort();
		conn = connect_socket;
	}

	int flags
		= k_nSteamNetworkingSend_Reliable
		| k_nSteamNetworkingSend_NoNagle
		| k_nSteamNetworkingSend_NoDelay;

	return SteamNetworkingSockets()->SendMessageToConnection(conn, buf, len, flags, NULL) == k_EResultOK;
}

extern "C" void *steam_recv(uint32_t conn, int *len)
{
	if (conn == STEAM_HANDLE_SERVER)
		conn = connect_socket;

	SteamNetworkingMessage_t *message;
	int num_messages = SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, &message, 1);
	if (num_messages < 1) {
		*len = 0;
		return NULL;
	}

	*len = message->m_cbSize;
	return message->m_pData;
}

extern "C" void steam_consume(uint32_t conn)
{
	SteamNetworkingMessage_t *message;
	int num_messages = SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, &message, 1);
	if (num_messages > 0)
		message->Release();
}

// 0=not created, 1=created, -1=failed
int create_lobby_status = 0;
uint64_t lobby_id;
bool own_lobby;

class LobbyManager {
public:

    void CreateLobby(int num_players) {
		SteamAPICall_t steamCall = SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, num_players);
		m_LobbyCreatedCallResult.Set(steamCall, this, &LobbyManager::OnLobbyCreated);
    }

private:
    // Lobby creation result callback handler
    CCallResult<LobbyManager, LobbyCreated_t> m_LobbyCreatedCallResult;

    // This function will be called when the lobby creation result is received
    void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure) {
        
		if (bIOFailure || pCallback->m_eResult != k_EResultOK) {
			os_writes(OS_STDOUT, "Couldn't create lobby\n");
            // There was an error creating the lobby
            create_lobby_status = -1;
            return;
        }

		os_writes(OS_STDOUT, "Lobby created\n");

        // Lobby created successfully, print lobby ID
		own_lobby = true;
        lobby_id = pCallback->m_ulSteamIDLobby;
		create_lobby_status = 1;

		SteamMatchmaking()->SetLobbyData(lobby_id, "snakebattleroyale", "yes");
		SteamMatchmaking()->SetLobbyData(lobby_id, "title", SteamFriends()->GetPersonaName());
    }
};
LobbyManager lobby_manager;

extern "C" void steam_create_lobby_start(int num_players)
{
	lobby_manager.CreateLobby(num_players);
}

extern "C" int steam_create_lobby_result(void)
{
	if (create_lobby_status == 0)
		return 0;
	int status = create_lobby_status;
	create_lobby_status = 0;
	return status;
}

extern "C" void steam_invite_to_lobby(void)
{
	SteamFriends()->ActivateGameOverlayInviteDialog(lobby_id);
}

//int lobby_list_status = 0;
int lobby_list_count = -1;
SteamAPICall_t lobby_list_api_call;

void steam_list_lobbies_owned_by_friends(void)
{
	SteamMatchmaking()->AddRequestLobbyListStringFilter("snakebattleroyale", "yes", k_ELobbyComparisonEqual);
	lobby_list_api_call = SteamMatchmaking()->RequestLobbyList();
}

int steam_lobby_list_count(void)
{
	return lobby_list_count;
}

int steam_lobby_list_status(void)
{
	LobbyMatchList_t buffer;
	bool failed;
	if (!SteamUtils()->GetAPICallResult(lobby_list_api_call, &buffer, sizeof(buffer), LobbyMatchList_t::k_iCallback, &failed))
		return false;
	lobby_list_count = buffer.m_nLobbiesMatching;
	return failed ? -1 : 1;
}

extern "C" uint64_t steam_get_lobby(int index)
{
	return SteamMatchmaking()->GetLobbyByIndex(index).ConvertToUint64();
}

extern "C" uint64_t steam_current_lobby_owner()
{
	return SteamMatchmaking()->GetLobbyOwner(lobby_id).ConvertToUint64();
}

extern "C" const char *steam_get_lobby_title(uint64_t lobby_id)
{
	return SteamMatchmaking()->GetLobbyData(lobby_id, "title");
}

extern "C" int steam_get_friend_name(uint64_t friend_id, char *dest, int max)
{
	const char *name = SteamFriends()->GetFriendPersonaName(friend_id);
	return snprintf(dest, max, "%s", name);
}

SteamAPICall_t join_lobby_api_call;

extern "C" void steam_join_lobby_start(uint64_t lobby_id)
{
	join_lobby_api_call = SteamMatchmaking()->JoinLobby(lobby_id);
}

extern "C" int steam_join_lobby_status(void)
{
	LobbyEnter_t buffer;
	bool failed;
	if (!SteamUtils()->GetAPICallResult(join_lobby_api_call, &buffer, sizeof(buffer), LobbyEnter_t::k_iCallback, &failed))
		return false;
	
	own_lobby = false;
	lobby_id = buffer.m_ulSteamIDLobby;
	return failed ? -1 : 1;
}

extern "C" uint64_t steam_get_current_time_us(void)
{
	return SteamNetworkingUtils()->GetLocalTimestamp();
}

static_assert(STEAM_PING_LOCATION_STRING_SIZE == k_cchMaxSteamNetworkingPingLocationString, "");
static_assert(sizeof(SteamPingLocation) == sizeof(SteamNetworkPingLocation_t), "");
static_assert(alignof(SteamPingLocation) == alignof(SteamNetworkPingLocation_t), "");

extern "C" __cdecl bool steam_get_local_ping_location(SteamPingLocation *location)
{
	float ret = SteamNetworkingUtils()->GetLocalPingLocation(*(SteamNetworkPingLocation_t*) location);
	if (ret < 0) {
		os_writes(OS_STDOUT, "Ping location information not available\n");
		return false;
	}
	return true;
}

extern "C" __cdecl void steam_ping_location_to_string(SteamPingLocation *location, char *dst, size_t max)
{
	if (max < k_cchMaxSteamNetworkingPingLocationString) {
		os_writes(OS_STDOUT, "Buffer too small\n");
		abort();
	}
	SteamNetworkingUtils()->ConvertPingLocationToString(*(SteamNetworkPingLocation_t*) location, dst, max);
	os_writes(OS_STDOUT, "Generated ping location string: ");
	os_writes(OS_STDOUT, dst);
	os_writes(OS_STDOUT, "\n");
}

extern "C" __cdecl void steam_parse_ping_location(char *str, SteamPingLocation *location)
{
	if (!SteamNetworkingUtils()->ParsePingLocationString(str, *(SteamNetworkPingLocation_t*) location)) {
		os_writes(OS_STDOUT, "Couldn't parse ping location\n");
		abort();
	}
}

extern "C" __cdecl uint64_t steam_estimate_ping_us(SteamPingLocation *remote_location)
{
	int result = SteamNetworkingUtils()->EstimatePingTimeFromLocalHost(*(SteamNetworkPingLocation_t*) remote_location);
	if (result < 0) {
		os_writes(OS_STDOUT, "Ping time is negative\n");
		abort();
	}
	return result * 1000;
}
