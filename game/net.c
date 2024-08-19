
typedef struct {
	SteamHandle handle;
	ByteQueue input;
	ByteQueue output;
	bool failed;
} ClientData;

#define MAX_CLIENTS (MAX_SNAKES-1)

ClientData server_data;
ClientData client_data[MAX_CLIENTS];

void init_client_data(ClientData *client)
{
	client->handle = STEAM_HANDLE_INVALID;
	client->failed = false;
	byte_queue_init(&client->input);
	byte_queue_init(&client->output);
}

void reset_client_data(ClientData *client)
{
	if (client->handle != STEAM_HANDLE_INVALID)
		steam_close_accepted_connection(client->handle);

	client->handle = STEAM_HANDLE_INVALID;
	client->failed = false;

	byte_queue_reset(&client->input);
	byte_queue_reset(&client->output);
}

bool net_init(void)
{
	init_client_data(&server_data);
	for (int i = 0; i < MAX_CLIENTS; i++)
		init_client_data(&client_data[i]);
	return steam_init(480);
}

void net_free(void)
{
	steam_free();
}

void net_reset(void)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		reset_client_data(&client_data[i]);
	reset_client_data(&server_data);
	steam_reset();
}

void client_update(ClientData *client)
{
	if (client->failed) printf("UPDATING FAILED CLIENT\n");

	if (client->handle != STEAM_HANDLE_INVALID && !client->failed) {

		{
			char *src = byte_queue_start_read(&client->output);
			int   len = byte_queue_used_space(&client->output);
			if (len > 0) {
				printf("Sending %d bytes\n", len);
				if (!steam_send(client->handle, src, len)) {
					printf("steam_send failed\n");
					client->failed = true;
				} else
					byte_queue_end_read(&client->output, len);
			}
		}

		if (!client->failed) {

			int len;
			char *src = steam_recv(client->handle, &len);

			if (len > 0) {
				printf("Buffering %d bytes\n", len);
				if (!byte_queue_ensure_min_free_space(&client->input, len))
					client->failed = true;
				else {
					char *dst = byte_queue_start_write(&client->input);
					memcpy(dst, src, len);
					byte_queue_end_write(&client->input, len);
					steam_consume(client->handle);
				}
			}
		}
	}
}

void net_update(void)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		client_update(&client_data[i]);
	client_update(&server_data);
	steam_update();
}

uint32_t net_accept(void)
{
	SteamHandle handle = steam_accept_connection();
	int i = 0;
	while (i < MAX_CLIENTS && client_data[i].handle != STEAM_HANDLE_INVALID)
		i++;
	if (i == MAX_CLIENTS)
		return STEAM_HANDLE_SERVER;
	ClientData *client = &client_data[i];
	client->handle = handle;
	// TODO: Init client data
	return handle;
}

ClientData *get_client_data_from_steam_handle(uint32_t handle)
{
	if (handle == STEAM_HANDLE_SERVER)
		return &server_data;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (client_data[i].handle == handle)
			return &client_data[i];
	assert(0);
	return NULL;
}

bool net_failed(uint32_t conn)
{
	return get_client_data_from_steam_handle(conn)->failed;
}

void net_write(uint32_t conn, void *msg, int len)
{
	ClientData *client = get_client_data_from_steam_handle(conn);
	if (client->failed) return;

	if (!byte_queue_ensure_min_free_space(&client->output, len)) {
		printf("OUT OF MEMORY\n");
		abort();
	}
	char *dst = byte_queue_start_write(&client->output);
	memcpy(dst, msg, len);
	byte_queue_end_write(&client->output, len);
	printf("WROTE %d BYTES\n", len);
}

string net_peekmsg(uint32_t conn)
{
	ClientData *client = get_client_data_from_steam_handle(conn);
	if (client->failed) return (string) {.data=NULL, .count=0};
	return (string) {
		.data = (u8*) byte_queue_start_read(&client->input),
		.count = byte_queue_used_space(&client->input),
	};
}

void net_popmsg(uint32_t conn, size_t len)
{
	byte_queue_end_read(&get_client_data_from_steam_handle(conn)->input, len);
}

bool net_listen_start(void)
{
	return steam_listen_start();
}

void net_listen_stop(void)
{
	steam_listen_stop();
}

enum {
	CONNECT_OK = 0,
	CONNECT_FAILED = -1,
	CONNECT_PENDING = 1,
};

bool net_connect_start(uint64_t peer_id)
{
	if (steam_connect_start(peer_id)) {
		server_data.handle = STEAM_HANDLE_SERVER;
		return true;
	}
	return false;
}

void net_connect_stop(void)
{
	steam_connect_stop();
}

int net_connect_status(void)
{
	return steam_connect_status();
}

typedef struct {
    u64 time;
    u32 player;
    Direction dir;
    bool disconnect;
} Input;

typedef struct { // TODO: Make sure there is no padding
    u32 head_x;
    u32 head_y;
} InitialSnakeStateMessage;

typedef struct { // TODO: Make sure there is no padding
    u64 time_us;
    u64 seed;
    u32 num_snakes;
    u32 self_index;
    InitialSnakeStateMessage snakes[MAX_SNAKES];
} InitialGameStateMessage;

bool is_server;
bool multiplayer;

// Results:
//   0  No message
//   1  Message received
//  -1  Server disconnected
int poll_for_initial_state(InitialGameStateMessage *initial);

bool start_waiting_for_players(int num_players);
void stop_waiting_for_players(void);
bool wait_for_players(void);

void broadcast_input_to_clients(Input input)
{
    // NOTE: We can only change the fields in place because
    //       the argument is a copy!!!
    input.time   = htonll(input.time);
    input.dir    = htonl(input.dir);
    input.player = htonl(input.player);

    for (u32 i = 0; i < MAX_CLIENTS; i++) {
        if (client_data[i].handle != STEAM_HANDLE_INVALID) {
            net_write(client_data[i].handle, &input.time,   sizeof(input.time));
            net_write(client_data[i].handle, &input.player, sizeof(input.player));
            net_write(client_data[i].handle, &input.dir,    sizeof(input.dir));
        }
    }
}

int count_client_handles(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_data[i].handle != STEAM_HANDLE_INVALID)
            n++;
    return n;
}

void compact_client_handles(void)
{
    ClientData client_data_copy[MAX_CLIENTS];

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_data_copy[i] = client_data[i];
        client_data[i].handle = STEAM_HANDLE_INVALID;
    }

    for (int i = 0, j = 0; i < MAX_CLIENTS; i++)
        if (client_data_copy[i].handle != STEAM_HANDLE_INVALID)
            client_data[j++] = client_data_copy[i];
}

bool up_press = false;
bool down_press = false;
bool left_press = false;
bool right_press = false;

void poll_for_inputs(void)
{
    up_press    = is_key_just_pressed(KEY_ARROW_UP);
    down_press  = is_key_just_pressed(KEY_ARROW_DOWN);
    left_press  = is_key_just_pressed(KEY_ARROW_LEFT);
    right_press = is_key_just_pressed(KEY_ARROW_RIGHT);

    if (multiplayer)
        net_update();
}

void send_local_input(Input input)
{
    assert(!input.disconnect);

    if (is_server) {
        broadcast_input_to_clients(input);
    } else {
        // NOTE: We can only change the fields in place because
        //       the argument is a copy!!!
        input.time = htonll(input.time);
        input.dir  = htonl(input.dir);
        net_write(STEAM_HANDLE_SERVER, &input.time, sizeof(input.time));
        net_write(STEAM_HANDLE_SERVER, &input.dir,  sizeof(input.dir));
    }
}

bool get_client_input_from_network(Input *input)
{
	static int cursor = 0;

	while (cursor < MAX_CLIENTS) {

		if (client_data[cursor].handle == STEAM_HANDLE_INVALID) {
			cursor++;
			continue;
		}

		u32 player_id = cursor+1;

		string msg = net_peekmsg(client_data[cursor].handle);
        if (msg.count < sizeof(u64) + sizeof(u32)) {
			cursor++;
            continue;
		}

		printf("Input available (msg={%p, %d})\n", msg.data, msg.count);

        u32 value;
        u64 time;
        memcpy(&time,  msg.data + 0, sizeof(u64));
        memcpy(&value, msg.data + 8, sizeof(u32));
        net_popmsg(client_data[cursor].handle, sizeof(u64) + sizeof(u32));
        Direction dir = ntohl(value);
        time = ntohll(time);

        input->dir = dir;
        input->disconnect = false;
        input->player = player_id;
        input->time = time;

        broadcast_input_to_clients(*input);
        return true;
	}

	cursor = 0;
	return false;
}

bool get_server_input_from_network(Input *input)
{
	// TODO: Handle server disconnect

    string msg = net_peekmsg(STEAM_HANDLE_SERVER);
	if (msg.count < 2 * sizeof(u32) + sizeof(u64))
		return false;

	u64 time;
	u32 buffer0;
	u32 buffer1;
	memcpy(&time,    msg.data + 0, sizeof(u64));
	memcpy(&buffer0, msg.data + 8, sizeof(u32));
	memcpy(&buffer1, msg.data + 12, sizeof(u32));
	net_popmsg(STEAM_HANDLE_SERVER, 2 * sizeof(u32) + sizeof(u64));

	time = ntohll(time);
	u32 id = ntohl(buffer0);
	Direction dir = ntohl(buffer1);

	// TODO: Validate the ID

	input->dir = dir;
	input->disconnect = false;
	input->player = id;
	input->time = time;
	return true;
}

u32 get_current_player_id(void);
u64 get_current_frame_index(void);

bool get_input(Input *input)
{
    if (up_press) {
        input->dir = DIR_UP;
        input->disconnect = false;
        input->player = get_current_player_id();
        input->time = get_current_frame_index();
        up_press = false;
        if (multiplayer) send_local_input(*input);
        return true;
    }

    if (down_press) {
        input->dir = DIR_DOWN;
        input->disconnect = false;
        input->player = get_current_player_id();
        input->time = get_current_frame_index();
        down_press = false;
        if (multiplayer) send_local_input(*input);
        return true;
    }

    if (left_press) {
        input->dir = DIR_LEFT;
        input->disconnect = false;
        input->player = get_current_player_id();
        input->time = get_current_frame_index();
        left_press = false;
        if (multiplayer) send_local_input(*input);
        return true;
    }

    if (right_press) {
        input->dir = DIR_RIGHT;
        input->disconnect = false;
        input->player = get_current_player_id();
        input->time = get_current_frame_index();
        right_press = false;
        if (multiplayer) send_local_input(*input);
        return true;
    }

    if (multiplayer) {
        if (is_server)
            return get_client_input_from_network(input);
        else
            return get_server_input_from_network(input);
    }

    return false;
}

// -1 if not waiting for players
int num_players_to_wait = -1;

bool start_waiting_for_players(int num_players)
{
    assert(num_players >= 0 && num_players <= MAX_CLIENTS);

	if (net_listen_start()) {
		num_players_to_wait = num_players;
		return true;
	}
	net_reset();
	return false;
}

void stop_waiting_for_players(void)
{
    num_players_to_wait = -1;
    net_reset();
}

bool wait_for_players(void)
{
	assert(num_players_to_wait >= 0);
	while (net_accept() != STEAM_HANDLE_INVALID);

	// TODO: Handle disconnect

	if (count_client_handles() < num_players_to_wait)
		return false;

	compact_client_handles();
	return true;
}

// Results:
//   0  No message
//   1  Message received
//  -1  Server disconnected
int poll_for_initial_state(InitialGameStateMessage *initial)
{
	// TODO: Handle server disconnect

	string input_buffer = net_peekmsg(STEAM_HANDLE_SERVER);

	if (input_buffer.count < 2 * sizeof(u64) + 2 * sizeof(u32))
		return 0;

	memcpy(initial, input_buffer.data, 2 * sizeof(u64) +  2 * sizeof(u32));
	initial->time_us    = ntohll(initial->time_us);
	initial->seed       = ntohll(initial->seed);
	initial->num_snakes = ntohl(initial->num_snakes);
	initial->self_index = ntohl(initial->self_index);

	if (input_buffer.count < 2 * sizeof(u64) + 2 * sizeof(u32) + initial->num_snakes * sizeof(InitialSnakeStateMessage))
		return 0;

	memcpy(initial, input_buffer.data, sizeof(InitialGameStateMessage));
	initial->time_us    = ntohll(initial->time_us);
	initial->seed       = ntohll(initial->seed);
	initial->num_snakes = ntohl(initial->num_snakes);
	initial->self_index = ntohl(initial->self_index);

	for (int i = 0; i < initial->num_snakes; i++) {
		initial->snakes[i].head_x = ntohl(initial->snakes[i].head_x);
		initial->snakes[i].head_y = ntohl(initial->snakes[i].head_y);
	}

	net_popmsg(STEAM_HANDLE_SERVER, 2 * sizeof(u64) + 2 * sizeof(u32) + initial->num_snakes * sizeof(InitialSnakeStateMessage));
	return 1;
}