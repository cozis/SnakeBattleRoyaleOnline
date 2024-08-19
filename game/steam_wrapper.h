#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t SteamHandle;
#define STEAM_HANDLE_INVALID ((SteamHandle) -1ULL)
#define STEAM_HANDLE_SERVER  ((SteamHandle) -2ULL)

bool     steam_init(uint64_t app_id);
void     steam_free(void);
void     steam_reset(void);
void     steam_update(void);

bool     steam_listen_start(void);
void     steam_listen_stop(void);
uint32_t steam_accept_connection(void);
void     steam_close_accepted_connection(uint32_t handle);

bool     steam_connect_start(uint64_t peer_id);
void     steam_connect_stop(void);
int      steam_connect_status(void);

bool     steam_send(uint32_t conn, void *buf, int len);
void    *steam_recv(uint32_t conn, int *len);
void     steam_consume(uint32_t conn);


#ifdef __cplusplus
}
#endif