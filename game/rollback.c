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
    u64 lookback = 8;
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
    if (multiplayer) {
        if (input.time < oldest_game_state.frame_index) {
            printf("Input is too old\n");
            abort();
        }
        input_queue_push(input);
    }
    if (input.time == latest_game_state.frame_index)
        apply_input_to_game_instance(&latest_game_state, input);
}

float last_update_time = -1;

void update_game(void)
{
    float current_time = os_get_current_time_in_seconds();
    int num_steps = last_update_time < 0 ? 1 : (current_time - last_update_time) * FPS;

    if (multiplayer)
        recalculate_latest_state();

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
    int final_snake_count = multiplayer ? 1 : 0;
    return count_snakes(&latest_game_state) == final_snake_count;
}

void draw_game(void)
{
    draw_game_instance(&latest_game_state);
}