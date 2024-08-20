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

#ifdef __cplusplus
}
#endif