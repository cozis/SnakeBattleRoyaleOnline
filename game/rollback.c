#define INPUTS_MASK (MAX_INPUTS-1)

Input inputs[MAX_INPUTS];
int inputs_head;
int inputs_count;

int self_snake_index;
GameState oldest_game_state;
GameState latest_game_state;

u64 get_current_frame_index(void)
{
    return latest_game_state.frame_index;
}

void input_queue_init(void)
{
    inputs_head = 0;
    inputs_count = 0;
}

void input_queue_free(void)
{
}

void input_queue_push(Input input)
{
    if (inputs_count == MAX_INPUTS) {
        printf("Input queue is full\n");
        abort();
    }

    if (input.time < oldest_game_state.frame_index) {
        printf("Input is too old\n");
        abort();
    }

    // Find the first entry that is older from the tail
    // (Here by tail we mean the last element, not the
    // first free slot).
    uint64_t i = (uint64_t) inputs_count - 1;
    while (i != (uint64_t) -1 && input.time < inputs[(inputs_head + i) & INPUTS_MASK].time) {
        i--;
    }
    i++;
    // i here is the insert position

    for (uint64_t j = inputs_count; j > i; j--)
        inputs[(inputs_head + j) & INPUTS_MASK] = inputs[(inputs_head + j - 1) & INPUTS_MASK];

    inputs[(inputs_head + i) & INPUTS_MASK] = input;
    inputs_count++;
}

bool input_queue_pop(Input *input)
{
    if (inputs_count == 0)
        return false;
    *input = inputs[inputs_head];
    inputs_head = (inputs_head + 1) & INPUTS_MASK;
    inputs_count--;
    return true;
}

bool input_queue_peek(u32 index, Input *input)
{
    if (inputs_count <= index)
        return false;
    *input = inputs[(inputs_head + index) & INPUTS_MASK];
    return true;
}

bool we_are_dead(void)
{
    return !latest_game_state.snakes[self_snake_index].used;
}

void apply_inputs_to_game_instance_until_time(GameState *game, uint64_t frame_index_limit, bool pop)
{
    uint64_t last_frame_index = game->frame_index;
    u32 cursor = 0;

    for (;;) {

        Input input;
        bool done = !input_queue_peek(cursor, &input);
        if (done || input.time > frame_index_limit) break;

        if (pop)
            input_queue_pop(&input);
        else {
            cursor++;
        }

        assert(last_frame_index <= input.time);
        while (last_frame_index < input.time) {
            update_game_instance(game);
            last_frame_index++;
        }

        apply_input_to_game_instance(game, input);
    }

    while (last_frame_index < frame_index_limit) {
        update_game_instance(game);
        last_frame_index++;
    }
}

void recalculate_latest_state(void)
{
    u64 lookback = 32;
    u64 latest_frame_index = latest_game_state.frame_index;

    // Apply old inputs permanently
    if (latest_frame_index >= lookback)
        apply_inputs_to_game_instance_until_time(&oldest_game_state, latest_frame_index - lookback, true);

    // Apply newer inputs
    memcpy(&latest_game_state, &oldest_game_state, sizeof(GameState));
    apply_inputs_to_game_instance_until_time(&latest_game_state, latest_frame_index, false);
}

void apply_input_to_game(Input input)
{
    if (multiplayer)
		input.time += INPUT_FRAME_DELAY_COUNT;

	if (input.time < oldest_game_state.frame_index) {
    	printf("Input is too old\n");
        abort();
    }
    input_queue_push(input);

    if (input.time == latest_game_state.frame_index)
        apply_input_to_game_instance(&latest_game_state, input);
}

double last_update_time = -1;
double last_sync_time = -1;

#if HAVE_MULTIPLAYER
u64 game_start_time;

void send_initial_state(void)
{
	game_start_time = steam_get_current_time_us();

	input_globals_init();
	init_game_state(&latest_game_state);
	int num_players = 1 + count_client_handles();
	for (int i = 0; i < num_players; i++)
		spawn_snake(&latest_game_state);
	memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

	self_snake_index = 0;

	char current_location_string[STEAM_PING_LOCATION_STRING_SIZE];
	{
		memset(current_location_string, 0, STEAM_PING_LOCATION_STRING_SIZE);

		SteamPingLocation location;
		steam_get_local_ping_location(&location);
		steam_ping_location_to_string(&location, current_location_string, sizeof(current_location_string));
	}

	// Send player positions to clients
	for (int i = 0, j = 0; i < MAX_CLIENTS; i++) {

		if (client_data[i].handle == STEAM_HANDLE_INVALID) {
			continue;
		} else {
			j++;
		}

		u64 time = htonll(game_start_time);
		net_write(client_data[i].handle, &time, sizeof(time));

		u64 seed = htonll(latest_game_state.seed);
		net_write(client_data[i].handle, &seed, sizeof(seed));

		// Send the player count
		u32 buffer = htonl(num_players);
		net_write(client_data[i].handle, &buffer, sizeof(buffer));

		// Send the index associated to this client
		buffer = htonl(j); // <-- This is j and not i
		net_write(client_data[i].handle, &buffer, sizeof(buffer));

		net_write(client_data[i].handle, current_location_string, STEAM_PING_LOCATION_STRING_SIZE);

		// Send the player information
		for (int k = 0; k < MAX_SNAKES; k++) {
			Snake *s = &latest_game_state.snakes[k];
			if (!s->used) continue;

			buffer = htonl(s->head_x);
			net_write(client_data[i].handle, &buffer, sizeof(buffer));

			buffer = htonl(s->head_y);
			net_write(client_data[i].handle, &buffer, sizeof(buffer));

			assert(s->body_len == 0); // We are assuming the starting size is 0. If that
										// wasn't the case we would need to send the body
		}

		j++;
	}
}

u64 ping_time_us;

void start_client_game(InitialGameStateMessage *initial)
{
	game_start_time = steam_get_current_time_us();

	input_globals_init();
	init_game_state(&latest_game_state);
	for (int i = 0; i < initial->num_snakes; i++)
		init_snake(&latest_game_state.snakes[i],
				initial->snakes[i].head_x,
				initial->snakes[i].head_y);
	latest_game_state.seed = initial->seed;
	memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

	{
		SteamPingLocation remote_location;
		steam_parse_ping_location(initial->location, &remote_location);
		ping_time_us = steam_estimate_ping_us(&remote_location);
		printf("ping time = %f ms\n", (double) ping_time_us / 1000);
	}

	u32 latency_frames = (double) ping_time_us * FPS / 1000000;
	latest_game_state.frame_index = latency_frames;
	printf("latency_frames=%d\n", latency_frames);

	self_snake_index = (int) initial->self_index;
}

void sync_frame_index(uint64_t frame_index)
{
	uint64_t time = steam_get_current_time_us() - game_start_time;

	//printf("Sending sync time=%llu, frame=%llu\n", time, frame_index);

	time = htonll(time);
	frame_index = htonll(frame_index);

	uint8_t type = MESSAGE_SYNC;
    for (u32 i = 0; i < MAX_CLIENTS; i++) {
        if (client_data[i].handle != STEAM_HANDLE_INVALID) {
			net_write(client_data[i].handle, &type,        sizeof(type));
            net_write(client_data[i].handle, &frame_index, sizeof(frame_index));
			net_write(client_data[i].handle, &time,        sizeof(time));
        }
    }
}
#endif /* HAVE_MULTIPLAYER */

#define CONVERGE_INSTANTLY 1

u64   last_target_frame_index = -1;
double last_target_update_time = -1;

u64 get_target_frame_index(void)
{
	if (last_target_frame_index == -1)
		return get_current_frame_index();
	double current_time = os_get_current_time_in_seconds();
	return last_target_frame_index + (current_time - last_target_update_time) * FPS;
}

void update_game(SyncMessage sync)
{
    double current_time = os_get_current_time_in_seconds();
	u64   current_frame_index = get_current_frame_index();

	if (current_frame_index == 0) {
		last_target_frame_index = -1;
		last_target_update_time = -1;
		last_update_time = -1;
		last_sync_time = -1;
	}

#if HAVE_MULTIPLAYER
    if (multiplayer) {

		if (is_server) {

			if (last_sync_time < 0 || current_time - last_sync_time > 1) {
				sync_frame_index(current_frame_index);
				last_sync_time = current_time;
			}

		} else {

			if (!sync.empty) {
#if CONVERGE_INSTANTLY
				latest_game_state.frame_index = sync.frame_index;
#endif
				last_target_frame_index = sync.frame_index;
				last_target_update_time = current_time + (double) ping_time_us / 1000000;
			}
		}
	}
#endif

	recalculate_latest_state();

	u64 target_frame_index = get_target_frame_index();

	//printf("%s: frame = %llu -> target = %llu\n", STR(is_server ? "SERVER" : "CLIENT"), current_frame_index, target_frame_index);

	int num_steps;
	if (last_update_time < 0)
		num_steps = 1;
	else {

#if CONVERGE_INSTANTLY
		if (is_server)
			num_steps = (current_time - last_update_time) * FPS;
		else {
			if (target_frame_index+1 > current_frame_index)
				num_steps = target_frame_index+1 - current_frame_index;
			else
				num_steps = 0;
		}
#else
		if (is_server)
			fps = FPS;
		else {

			s64 delta = (s64) target_frame_index - (s64) current_frame_index;

			if (FPS < -delta * 5)
				fps = 0;
			else
				fps = FPS + delta * 5;

			//printf("frame %llu -> %llu (delta %lld)\n", current_frame_index, target_frame_index, (s64) target_frame_index - (s64) current_frame_index);
		}

		num_steps = (current_time - last_update_time) * fps;
#endif
	}

    if (num_steps > 0) {
        for (int i = 0; i < num_steps; i++)
            update_game_instance(&latest_game_state);
        last_update_time = current_time;
    }
}

bool game_apple_consumed_this_frame(void)
{
    return latest_game_state.apple_consumed_this_frame;
}

bool game_complete(void)
{
    return latest_game_state.game_complete;
}

void draw_game(void)
{
    draw_game_instance(&latest_game_state);
}

typedef enum {
	GAME_RESULT_NONE,
	GAME_RESULT_WIN,
	GAME_RESULT_LOSE,
	GAME_RESULT_DRAW,
} GameResult;

GameResult game_result(void)
{
	GameState *game = &latest_game_state;
	if (!game->game_complete)
		return GAME_RESULT_NONE;

	if (!multiplayer)
		return GAME_RESULT_LOSE;

	if (game->winner_when_multiplayer == -1)
		return GAME_RESULT_DRAW;

	if (game->winner_when_multiplayer == self_snake_index)
		return GAME_RESULT_WIN;

	return GAME_RESULT_LOSE;
}