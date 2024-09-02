#include <cstdlib>
#include <assert.h>
#include <winsock2.h> // INADDR_ANY
#include "steam_api.h"
#include "steam_gameserver.h"
//#include "isteamnetworkingutils.h"

extern "C" void __cdecl SteamAPIDebugTextHook( int nSeverity, const char *pchDebugText )
{
	// if you're running in the debugger, only warnings (nSeverity >= 1) will be sent
	// if you add -debug_steamapi to the command-line, a lot of extra informational messages will also be sent
	printf("%s\n", pchDebugText );

	if ( nSeverity >= 1 )
	{
		// place to set a breakpoint for catching API errors
		int x = 3;
		(void)x;
	}
}

class MySteamCallbacks {
public:

    MySteamCallbacks()
        : m_CallbackUserStatsReceived( this, &MySteamCallbacks::OnUserStatsReceived )
        , m_CallbackUserStatsStored  ( this, &MySteamCallbacks::OnUserStatsStored )
    {
        // Initialize any other necessary data here
    }
    
    STEAM_CALLBACK( MySteamCallbacks, OnUserStatsReceived, UserStatsReceived_t, m_CallbackUserStatsReceived );
    STEAM_CALLBACK( MySteamCallbacks, OnUserStatsStored,   UserStatsStored_t,   m_CallbackUserStatsStored   );
};

void MySteamCallbacks::OnUserStatsReceived(UserStatsReceived_t* pCallback) {
    if (pCallback->m_eResult == k_EResultOK) {
        printf("User stats received successfully.\n");
    } else {
        fprintf(stderr, "Failed to receive user stats: %d\n", pCallback->m_eResult);
    }
}

void MySteamCallbacks::OnUserStatsStored(UserStatsStored_t* pCallback) {
    if (pCallback->m_eResult == k_EResultOK) {
        printf("User stats stored successfully.\n");
    } else {
        fprintf(stderr, "Failed to store user stats: %d\n", pCallback->m_eResult);
    }
}

MySteamCallbacks g_SteamCallbacks;

HSteamNetConnection client = k_HSteamListenSocket_Invalid;

static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
	printf("Connection status changed!\n");
	switch (pInfo->m_info.m_eState) {

		case k_ESteamNetworkingConnectionState_Connecting:
		printf("New connection!\n");
		SteamNetworkingSockets()->AcceptConnection(pInfo->m_hConn);
		client = pInfo->m_hConn;
		break;

		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		printf("Connection error\n");
		SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, "oopsies", true);
		break;

		default:
		printf("Unexpected connection thing (state=%d)\n", pInfo->m_info.m_eState);
		//SteamNetworkingSockets()->CloseConnection(pInfo->m_hConn, 0, "oopsies", true);
		break;
	}
}

HSteamListenSocket listen_socket;
HSteamNetPollGroup poll_group;

bool networking_init(void)
{
	uint64_t appID = 480; //k_uAppIdInvalid;
	if (SteamAPI_RestartAppIfNecessary(appID))
		return false;

	if (!SteamAPI_Init())
		return false;

	SteamNetworkingUtils()->InitRelayNetworkAccess();

	SteamNetworkingConfigValue_t option;
	option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,   (void*) SteamNetConnectionStatusChangedCallback);

	listen_socket = SteamNetworkingSockets()->CreateListenSocketP2P(0, 1, &option);
	if (listen_socket == k_HSteamNetConnection_Invalid) {
		printf("Couldn't start listener\n");
		abort();
	}

	poll_group = SteamNetworkingSockets()->CreatePollGroup();
	// TODO: Check errors

	return true;
}

void networking_free(void)
{
	SteamNetworkingSockets()->DestroyPollGroup(poll_group);
	SteamNetworkingSockets()->CloseListenSocket(listen_socket);
	SteamAPI_Shutdown();
}

void networking_reset(void)
{
}

void networking_update(void)
{
	SteamAPI_RunCallbacks();
	SteamNetworkingSockets()->RunCallbacks();
}

int main(int argc, char **argv)
{
    networking_init();

    while (1) {
        networking_update();

		SteamNetworkingMessage_t *message;
		int num_messages = SteamNetworkingSockets()->ReceiveMessagesOnConnection(client, &message, 1);
		if (num_messages > 0) {
			char *str = (char*) message->m_pData;
			int   len = message->m_cbSize;
			printf("Client says: %.*s\n", len, str);
		}
    }

    networking_free();
    return 0;
}
