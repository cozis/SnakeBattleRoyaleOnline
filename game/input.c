typedef struct {
    u64 time;
    u32 player;
    Direction dir;
    bool disconnect;
} Input;

// -1 because the host doesn't need one
#define MAX_CLIENTS (MAX_SNAKES-1)

bool is_server;
bool multiplayer;

TCPHandle server_handle;
TCPHandle client_handles[MAX_CLIENTS];

void connect_stuff_init(void);
void connect_stuff_free(void);

void network_init(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        client_handles[i] = TCP_INVALID;
    server_handle = TCP_INVALID;
    connect_stuff_init();
}

void network_reset(void)
{
    if (is_server) {
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (client_handles[i] != TCP_INVALID) {
                tcp_client_close(client_handles[i]);
                client_handles[i] = TCP_INVALID;
            }
        if (server_handle != TCP_INVALID) {
            tcp_server_delete(server_handle);
            server_handle = TCP_INVALID;
        }
    } else {
        if (server_handle != TCP_INVALID) {
            tcp_client_close(server_handle);
            server_handle = TCP_INVALID;
        }
    }
}

void network_free(void)
{
    network_reset();
    connect_stuff_free();
}

typedef struct { // TODO: Make sure there is no padding
    u32 head_x;
    u32 head_y;
} InitialSnakeStateMessage;

typedef struct { // TODO: Make sure there is no padding
    u64 time_us;
    u32 num_snakes;
    u32 self_index;
    InitialSnakeStateMessage snakes[MAX_SNAKES];
} InitialGameStateMessage;

// Results:
//   0  No message
//   1  Message received
//  -1  Server disconnected
int poll_for_initial_state(InitialGameStateMessage *initial)
{
    tcp_client_poll(server_handle, 0);
    for (;;) {
        TCPEvent event = tcp_client_event(server_handle);

        if (event.type == TCP_EVENT_NONE)
            return 0;

        if (event.type == TCP_EVENT_DISCONNECT)
            return -1;

        assert(event.type == TCP_EVENT_DATA);

        string input_buffer = tcp_client_get_input(server_handle);

        printf("Buffer has %d bytes\n", input_buffer.count);

        if (input_buffer.count < sizeof(u64) + 2 * sizeof(u32))
            return 0;

        memcpy(initial, input_buffer.data, sizeof(u64) +  2 * sizeof(u32));
        initial->time_us    = ntohll(initial->time_us);
        initial->num_snakes = ntohl(initial->num_snakes);
        initial->self_index = ntohl(initial->self_index);

        if (input_buffer.count < sizeof(u64) + 2 * sizeof(u32) + initial->num_snakes * sizeof(InitialSnakeStateMessage))
            return 0;

        memcpy(initial, input_buffer.data, sizeof(InitialGameStateMessage));
        initial->time_us    = ntohll(initial->time_us);
        initial->num_snakes = ntohl(initial->num_snakes);
        initial->self_index = ntohl(initial->self_index);

        for (int i = 0; i < initial->num_snakes; i++) {
            initial->snakes[i].head_x = ntohl(initial->snakes[i].head_x);
            initial->snakes[i].head_y = ntohl(initial->snakes[i].head_y);
        }

        tcp_client_read(server_handle, sizeof(u64) + 2 * sizeof(u32) + initial->num_snakes * sizeof(InitialSnakeStateMessage));

        // TODO: May need to handle other messaged
        return 1;
    }

    /*
     * Unreachable
     */
    return -2;
}

Thread           connect_thread_desc;
_Atomic bool     connect_thread_should_quit;
Binary_Semaphore connect_request_available_semaphore;
Binary_Semaphore connect_result_available_semaphore;
_Atomic bool     connect_result_available;
TCPHandle        connect_result;
bool             connect_started;

void connect_routine(Thread *thread)
{
    (void) thread;
    for (;;) {
        printf("Waiting for connect request\n");
        binary_semaphore_wait(&connect_request_available_semaphore);
        if (connect_thread_should_quit) break;
        printf("Received connect request\n");
        connect_result = tcp_client_create(STR("127.0.0.1"), TCP_PORT);
        printf("Connection completed\n");
        atomic_store(&connect_result_available, true);
        binary_semaphore_signal(&connect_result_available_semaphore);
    }
}

void connect_stuff_init(void)
{
    connect_result = TCP_INVALID;
    connect_started = false;
    atomic_store(&connect_result_available, false);
    atomic_store(&connect_thread_should_quit, false);
    binary_semaphore_init(&connect_request_available_semaphore, 0);
    binary_semaphore_init(&connect_result_available_semaphore, 0);
    os_thread_init(&connect_thread_desc, connect_routine);
    os_thread_start(&connect_thread_desc);
}

void connect_stuff_free(void)
{
    atomic_store(&connect_thread_should_quit, true);
    binary_semaphore_signal(&connect_request_available_semaphore);
    os_thread_destroy(&connect_thread_desc);
    binary_semaphore_destroy(&connect_request_available_semaphore);
    binary_semaphore_destroy(&connect_result_available_semaphore);
}

void start_connecting(void)
{
    if (connect_started) {
        binary_semaphore_wait(&connect_result_available_semaphore);
        atomic_store(&connect_result_available, false);
        if (connect_result != TCP_INVALID) {
            tcp_client_close(connect_result);
            connect_result = TCP_INVALID;
        }
    }
    connect_started = true;
    binary_semaphore_signal(&connect_request_available_semaphore);
}

void stop_connecting(void)
{
}

bool done_connecting(void)
{
    assert(connect_started);
    if (!connect_result_available)
        return false;

    binary_semaphore_wait(&connect_result_available_semaphore);
    connect_result_available = false;
    connect_started = false;
    server_handle = connect_result;
    return true;
}

void compact_client_handles(void)
{
    TCPHandle client_handles_copy[MAX_CLIENTS];

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_handles_copy[i] = client_handles[i];
        client_handles[i] = TCP_INVALID;
    }

    for (int i = 0, j = 0; i < MAX_CLIENTS; i++)
        if (client_handles_copy[i] != TCP_INVALID)
            client_handles[j++] = client_handles_copy[i];
}

// -1 if not waiting for players
int num_players_to_wait = -1;

bool start_waiting_for_players(int num_players)
{
    assert(num_players >= 0 && num_players <= MAX_CLIENTS);

    server_handle = tcp_server_create(STR(""), TCP_PORT, MAX_CLIENTS);
    if (server_handle == TCP_INVALID) {
        network_reset();
        return false;
    }
    num_players_to_wait = num_players;
    return true;
}

void stop_waiting_for_players(void)
{
    num_players_to_wait = -1;
    network_reset();
}

int count_client_handles(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (client_handles[i] != TCP_INVALID)
            n++;
    return n;
}

bool wait_for_players(void)
{
    assert(num_players_to_wait >= 0);
    tcp_server_poll(server_handle, 0);
    for (;;) {

        TCPEvent event = tcp_server_event(server_handle);
        if (event.type == TCP_EVENT_NONE) break;

        if (event.type == TCP_EVENT_CONNECT) {
            int i = 0;
            while (client_handles[i] != TCP_INVALID) {
                i++;
                assert(i < MAX_SNAKES-1);
            }
            client_handles[i] = event.handle;
            continue;
        }

        if (event.type == TCP_EVENT_DISCONNECT) {
            int i = 0;
            while (client_handles[i] != event.handle) {
                i++;
                assert(i < MAX_CLIENTS);
            }
            client_handles[i] = TCP_INVALID;
            tcp_client_close(event.handle);
            continue;
        }

        if (event.type == TCP_EVENT_DATA) {
            // TODO
            printf("Player sent unexpected data\n");
            abort();
        }
    }

    if (count_client_handles() < num_players_to_wait)
        return false;

    compact_client_handles();
    return true;
}

void broadcast_input_to_clients(Input input)
{
    // NOTE: We can only change the fields in place because
    //       the argument is a copy!!!
    input.time   = htonll(input.time);
    input.dir    = htonl(input.dir);
    input.player = htonl(input.player);

    for (u32 i = 0; i < MAX_CLIENTS; i++) {
        if (client_handles[i] != TCP_INVALID) {
            tcp_client_write(client_handles[i], &input.time,   sizeof(input.time));
            tcp_client_write(client_handles[i], &input.player, sizeof(input.player));
            tcp_client_write(client_handles[i], &input.dir,    sizeof(input.dir));
        }
    }
}

u32 get_snake_index_from_tcp_handle(TCPHandle handle)
{
    u32 i = 0;
    while (client_handles[i] != handle) {
        i++;
        assert(i < MAX_CLIENTS);
    }
    i++;
    return i;
}

u64 get_current_frame_index(void);

TCPEvent last_data_event_or_none = {.type=TCP_EVENT_NONE};

bool get_client_input_from_network(Input *input)
{
    for (;;) {

        TCPEvent event;

        if (last_data_event_or_none.type == TCP_EVENT_NONE) {

            event = tcp_server_event(server_handle);
            if (event.type == TCP_EVENT_NONE)
                return false;

            if (event.type == TCP_EVENT_CONNECT) {
                tcp_client_close(event.handle);
                continue;
            }

            if (event.type == TCP_EVENT_DISCONNECT) {
                u32 player_id = get_snake_index_from_tcp_handle(event.handle);

                tcp_client_close(client_handles[player_id-1]);
                client_handles[player_id-1] = TCP_INVALID;

                input->disconnect = true;
                input->dir = DIR_LEFT;
                input->player = player_id;
                input->time = get_current_frame_index();

                // TODO: Broadcast to other clients
                return true;
            }

        } else {
            event = last_data_event_or_none;
        }

        assert(event.type == TCP_EVENT_DATA);

        u32 player_id = get_snake_index_from_tcp_handle(event.handle);

        string input_buffer = tcp_client_get_input(event.handle);
        if (input_buffer.count < sizeof(u64) + sizeof(u32)) {
            last_data_event_or_none = (TCPEvent) {.type=TCP_EVENT_NONE};
            continue;
        }

        u32 value;
        u64 time;
        memcpy(&time,  input_buffer.data + 0, sizeof(u64));
        memcpy(&value, input_buffer.data + 8, sizeof(u32));
        tcp_client_read(event.handle, sizeof(u64) + sizeof(u32));
        Direction dir = ntohl(value);
        time = ntohll(time);

        input->dir = dir;
        input->disconnect = false;
        input->player = player_id;
        input->time = time;

        broadcast_input_to_clients(*input);
        return true;
    }
}

bool get_server_input_from_network(Input *input)
{
    for (;;) {

        TCPEvent event;

        if (last_data_event_or_none.type != TCP_EVENT_DATA) {

            event = tcp_client_event(server_handle);
            if (event.type == TCP_EVENT_NONE) return false;

            if (event.type == TCP_EVENT_DISCONNECT) {

                input->dir = DIR_LEFT;
                input->disconnect = true;
                input->player = 0; // Player id of the server is always 0
                input->time = get_current_frame_index();
                return true;
            }
        } else {
            event = last_data_event_or_none;
        }

        assert(event.type == TCP_EVENT_DATA);

        string input_buffer = tcp_client_get_input(event.handle);
        if (input_buffer.count < 2 * sizeof(u32) + sizeof(u64)) {
            last_data_event_or_none = (TCPEvent) {.type=TCP_EVENT_NONE};
            continue;
        }

        u64 time;
        u32 buffer0;
        u32 buffer1;
        memcpy(&time,    input_buffer.data + 0, sizeof(u64));
        memcpy(&buffer0, input_buffer.data + 8, sizeof(u32));
        memcpy(&buffer1, input_buffer.data + 12, sizeof(u32));
        tcp_client_read(event.handle, 2 * sizeof(u32) + sizeof(u64));

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
}

bool up_press = false;
bool down_press = false;
bool left_press = false;
bool right_press = false;

void poll_for_inputs(void)
{
    up_press = is_key_just_pressed(KEY_ARROW_UP);
    down_press = is_key_just_pressed(KEY_ARROW_DOWN);
    left_press = is_key_just_pressed(KEY_ARROW_LEFT);
    right_press = is_key_just_pressed(KEY_ARROW_RIGHT);

    if (multiplayer) {
        if (is_server)
            tcp_server_poll(server_handle, 0);
        else
            tcp_client_poll(server_handle, 0);
    }
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
        tcp_client_write(server_handle, &input.time, sizeof(input.time));
        tcp_client_write(server_handle, &input.dir,  sizeof(input.dir));
    }
}

u32 get_current_player_id(void);

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
