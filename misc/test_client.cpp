#include <cstdlib>
#include "steamencryptedappticket.h"

enum EDisconnectReason
{
	k_EDRClientDisconnect = k_ESteamNetConnectionEnd_App_Min + 1,
	k_EDRServerClosed = k_ESteamNetConnectionEnd_App_Min + 2,
	k_EDRServerReject = k_ESteamNetConnectionEnd_App_Min + 3,
	k_EDRServerFull = k_ESteamNetConnectionEnd_App_Min + 4,
	k_EDRClientKicked = k_ESteamNetConnectionEnd_App_Min + 5
};

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
        , m_CallbackSteamServerConnected   ( this, &MySteamCallbacks::OnSteamServersConnected )
        , m_CallbackSteamServerDisconnected( this, &MySteamCallbacks::OnSteamServersDisconnected )
    {
        // Initialize any other necessary data here
    }
    
    STEAM_CALLBACK( MySteamCallbacks, OnUserStatsReceived,        UserStatsReceived_t,        m_CallbackUserStatsReceived );
    STEAM_CALLBACK( MySteamCallbacks, OnUserStatsStored,          UserStatsStored_t,          m_CallbackUserStatsStored   );
    STEAM_CALLBACK( MySteamCallbacks, OnSteamServersConnected,    SteamServersConnected_t,    m_CallbackSteamServerConnected    );
    STEAM_CALLBACK( MySteamCallbacks, OnSteamServersDisconnected, SteamServersDisconnected_t, m_CallbackSteamServerDisconnected );
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

void MySteamCallbacks::OnSteamServersConnected(SteamServersConnected_t* pCallback) {
    printf("Server connected to Steam\n");
}

void MySteamCallbacks::OnSteamServersDisconnected(SteamServersDisconnected_t* pCallback) {
    fprintf(stderr, "Server disconnected from Steam\n");
}

MySteamCallbacks g_MySteamCallbacks;
bool connected = false;

static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t *pInfo)
{
	printf("Connection status changed!\n");
	switch (pInfo->m_info.m_eState) {

		case k_ESteamNetworkingConnectionState_Connected:
		printf("Connected!\n");
		connected = true;
		break;

		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		printf("We were rejected\n");
		break;

		default:
		printf("Unexpected connection thing (state=%d)\n", pInfo->m_info.m_eState);
		break;
	}
}

int main(int argc, char **argv)
{
    {
        if (SteamAPI_RestartAppIfNecessary(480)) {
            // if Steam is not running or the game wasn't started through Steam, SteamAPI_RestartAppIfNecessary starts the 
            // local Steam client and also launches this game again.
            
            // Once you get a public Steam AppID assigned for this game, you need to replace k_uAppIdInvalid with it and
            // removed steam_appid.txt from the game depot.

            return EXIT_FAILURE;
        }

        // Initialize SteamAPI, if this fails we bail out since we depend on Steam for lots of stuff.
        // You don't necessarily have to though if you write your code to check whether all the Steam
        // interfaces are NULL before using them and provide alternate paths when they are unavailable.
        //
        // This will also load the in-game steam overlay dll into your process.  That dll is normally
        // injected by steam when it launches games, but by calling this you cause it to always load,
        // even when not launched via steam.
        SteamErrMsg errMsg = {0};
        if (SteamAPI_InitEx(&errMsg) != k_ESteamAPIInitResult_OK) {
            printf("SteamAPI_Init() failed: %s\n", errMsg);
            printf("Fatal Error: Steam must be running to play this game (SteamAPI_Init() failed).\n");
            return EXIT_FAILURE;
        }

        // set our debug handler
        SteamClient()->SetWarningMessageHook(&SteamAPIDebugTextHook);

        // Ensure that the user has logged into Steam. This will always return true if the game is launched
        // from Steam, but if Steam is at the login prompt when you run your game from the debugger, it
        // will return false.
        if (!SteamUser()->BLoggedOn())
        {
            printf("Steam user is not logged in\n");
            printf("Fatal Error: Steam user must be logged in to play this game (SteamUser()->BLoggedOn() returned false).\n");
            return EXIT_FAILURE;
        }
    }

    SteamNetworkingUtils()->InitRelayNetworkAccess();

    #define TARGET_FRIEND_ID 76561199756405394ULL

    SteamNetworkingIdentity identity;
	HSteamNetConnection server_socket;
	{
		memset(&identity, 0, sizeof(SteamNetworkingIdentity));
        identity.m_eType = k_ESteamNetworkingIdentityType_SteamID;

        //IMPORTANT BIT HERE

		bool found = false;
        int friend_count = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
		printf("friend_count=%d\n", friend_count);
        for(int i = 0; i < friend_count; i++) {
            CSteamID friend_id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
            if(friend_id.ConvertToUint64() == TARGET_FRIEND_ID) {
                identity.SetSteamID(friend_id);
                found = true;
                break;
            }
        }

		if (!found) {
			fprintf(stderr, "Unable locate friend\n");
			abort();
		}

		SteamNetworkingConfigValue_t opt;
		opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);

		server_socket = SteamNetworkingSockets()->ConnectP2P(identity, 0, 1, &opt);
		if (server_socket == k_HSteamNetConnection_Invalid) {
			fprintf(stderr,"Bad socket\n");
			abort();
		}
	}

    printf("Connected! Socket=%d\n", server_socket);

    while (!connected) {
        SteamAPI_RunCallbacks();
		SteamNetworkingSockets()->RunCallbacks();
    }

	char msg[] = "Hello! I'm client";
	SteamNetworkingSockets()->SendMessageToConnection(server_socket, msg, sizeof(msg)-1, k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle | k_nSteamNetworkingSend_NoDelay, NULL);

	while (1) {
        SteamAPI_RunCallbacks();
		SteamNetworkingSockets()->RunCallbacks();
    }

    SteamNetworkingSockets()->CloseConnection(server_socket, k_EDRClientDisconnect, nullptr, false);
    return 0;
}
