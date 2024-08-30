#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t SteamAppID;
typedef uint64_t SteamUserID;
typedef uint32_t SteamHandle;
#define STEAM_HANDLE_INVALID ((SteamHandle) -1ULL)
#define STEAM_HANDLE_SERVER  ((SteamHandle) -2ULL)

bool        steam_init(SteamAppID app_id);
void        steam_free(void);
void        steam_reset(void);
void        steam_update(void);

bool        steam_listen_start(void);
void        steam_listen_stop(void);
SteamHandle steam_accept_connection(void);
void        steam_close_accepted_connection(SteamHandle handle);
SteamHandle steam_get_disconnect_message(void);

bool        steam_connect_start(SteamUserID peer_id);
void        steam_connect_stop(void);
int         steam_connect_status(void);

bool        steam_send(SteamHandle conn, void *buf, int len);
void*       steam_recv(SteamHandle conn, int *len);
void        steam_consume(SteamHandle conn);

void        steam_create_lobby_start(int num_players);
int         steam_create_lobby_result(void);
void        steam_invite_to_lobby(void);
void        steam_list_lobbies_owned_by_friends(void);
int         steam_lobby_list_count(void);
int         steam_lobby_list_status(void);
uint64_t    steam_get_lobby(int index);
uint64_t    steam_current_lobby_owner(void);
int         steam_get_friend_name(uint64_t friend_id, char *dest, int max);
const char *steam_get_lobby_title(uint64_t lobby_id);
void        steam_join_lobby_start(uint64_t lobby_id);
int         steam_join_lobby_status(void);

uint64_t steam_get_current_time_us(void);

typedef struct { char mem[512]; } SteamPingLocation;
#define STEAM_PING_LOCATION_STRING_SIZE 1024

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC __cdecl bool     steam_get_local_ping_location(SteamPingLocation *location);
EXTERNC __cdecl void     steam_ping_location_to_string(SteamPingLocation *location, char *dst, size_t max);
EXTERNC __cdecl void     steam_parse_ping_location(char *str, SteamPingLocation *location);
EXTERNC __cdecl uint64_t steam_estimate_ping_us(SteamPingLocation *remote_location);

#ifdef __cplusplus
}
#endif