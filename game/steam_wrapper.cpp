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
	return true;
}

extern "C" void steam_free(void)
{
	SteamAPI_Shutdown();
}

extern "C" void steam_reset(void)
{
	// TODO
}

extern "C" void steam_update(void)
{
	SteamAPI_RunCallbacks();
	SteamNetworkingSockets()->RunCallbacks();
}

#define ACCEPT_QUEUE_SIZE 32
static HSteamNetConnection accepted_queue[ACCEPT_QUEUE_SIZE];
static int accepted_queue_head = 0;
static int accepted_queue_size = 0;
static HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
static HSteamNetConnection connect_socket = k_HSteamNetConnection_Invalid;
static bool connect_complete = false;
static bool connect_success  = false;

static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
	os_writes(OS_STDOUT, "Connection status changed!\n");
	switch (pInfo->m_info.m_eState) {

		case k_ESteamNetworkingConnectionState_Connecting:
		{
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
		// TODO
		//os_writes(OS_STDOUT, "Connection error\n");
		//SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, "oopsies", true);
		break;

		case k_ESteamNetworkingConnectionState_Connected:
		os_writes(OS_STDOUT, "CONNECTED\n");
		if (listen_socket == k_HSteamListenSocket_Invalid) {
			connect_complete = true;
			connect_success = true;
		} else {
			accepted_queue[(accepted_queue_head + accepted_queue_size) % ACCEPT_QUEUE_SIZE] = pInfo->m_hConn;
			accepted_queue_size++;
		}
		break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		os_writes(OS_STDOUT, "CLOSED BY PEER\n");
		// We were rejected by the server
		connect_complete = true;
		connect_success = false;
		break;

		case k_ESteamNetworkingConnectionState_None:
		os_writes(OS_STDOUT, "NONE .. API error?\n");
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
	if (listen_socket != k_HSteamListenSocket_Invalid)
		return false;

	SteamNetworkingConfigValue_t option;
	option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*) SteamNetConnectionStatusChangedCallback);

	listen_socket = SteamNetworkingSockets()->CreateListenSocketP2P(0, 1, &option);
	if (listen_socket == k_HSteamNetConnection_Invalid) {
		os_writes(OS_STDOUT, "Couldn't start listener\n");
		return false;
	}

	return true;
}

extern "C" void steam_listen_stop(void)
{
	// TODO: Close connections in the accepted queue
	SteamNetworkingSockets()->CloseListenSocket(listen_socket);
	listen_socket = k_HSteamListenSocket_Invalid;
}

extern "C" void steam_close_accepted_connection(uint32_t handle)
{
	// TODO
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
	if (connect_socket != k_HSteamNetConnection_Invalid)
		return false;

    SteamNetworkingIdentity identity;
	memset(&identity, 0, sizeof(SteamNetworkingIdentity));
	identity.m_eType = k_ESteamNetworkingIdentityType_SteamID;

	bool found = false;
	int friend_count = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
	for(int i = 0; i < friend_count; i++) {
		CSteamID friend_id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
		if(friend_id.ConvertToUint64() == peer_id) {
			identity.SetSteamID(friend_id);
			found = true;
			break;
		}
	}

	if (!found) {
		os_writes(OS_STDOUT, "Unable locate friend\n");
		return false;
	}

	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);

	connect_socket = SteamNetworkingSockets()->ConnectP2P(identity, 0, 1, &opt);
	if (connect_socket == k_HSteamNetConnection_Invalid) {
		os_writes(OS_STDOUT, "Bad socket\n");
		return false;
	}

	connect_complete = false;
	connect_success = false;
	os_writes(OS_STDOUT, "Started connecting\n");
	return true;
}

extern "C" void steam_connect_stop(void)
{
	SteamNetworkingSockets()->CloseConnection(connect_socket, 0, "oopsies", true);
	connect_socket = k_HSteamNetConnection_Invalid;
	connect_complete = false;
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
		if (num_messages == 0)
			os_writes(OS_STDOUT, "message count is 0\n");
		else
			os_writes(OS_STDOUT, "message count is negative\n");
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
