#define FPS 10
#define TILE_W 16
#define TILE_H 16
#define WORLD_W 10
#define WORLD_H 10
#define MAX_SNAKES 8
#define MAX_APPLES 4
#define TCP_PORT 8080
#define MAX_SNAKE_SIZE (WORLD_W * WORLD_H)

typedef enum {
    // Value are chosen such that -d is the direction opposite to d
    DIR_UP    = +1,
    DIR_DOWN  = -1,
    DIR_LEFT  = +2,
    DIR_RIGHT = -2,
} Direction;

typedef struct {
    bool used;
    Direction dir;
    u32 head_x;
    u32 head_y;
    u32 body_idx;
    u32 body_len;
    Direction body[MAX_SNAKE_SIZE];
    u32 iter_index;
    u32 iter_x;
    u32 iter_y;
} Snake;

typedef struct {
    bool used;
    u32 x;
    u32 y;
} Apple;

typedef struct {
    u64 frame_index;
    u64 seed;
    Snake snakes[MAX_SNAKES];
    Apple apples[MAX_APPLES];
} GameState;

typedef struct {
    u64 time;
    u32 player;
    Direction dir;
    bool disconnect;
} Input;

/*
 * FIXME: The way we avoid snakes changing direction to the one opposite
 *        to movement is to check that the new direction is not.. opposite
 *        to the movement. The problem is, players can change direction
 *        multiple times per frame with only the last one taking effect.
 *        This makes it possible to change from the current direction to
 *        and ortogonal one and then to the opposite direction to the initial
 *        one. This should not happen.
 */

bool multiplayer;
bool is_server;
int self_snake_index;
GameState oldest_game_state;
GameState latest_game_state;

#define MAX_INPUTS_LOG2 10
#define MAX_INPUTS (1 << MAX_INPUTS_LOG2)
#define INPUTS_MASK (MAX_INPUTS-1)
Input inputs[MAX_INPUTS];
int inputs_head;
int inputs_count;

TCPHandle server_handle;
TCPHandle client_handles[MAX_SNAKES-1]; // -1 because the host doesn't need one

Gfx_Font *font;

void push_input_struct_to_queue(Input input)
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

void push_disconnect_input_to_queue(u32 player, u64 time)
{
    Input input;
    input.disconnect = true;
    input.time = time;
    input.player = player;
    push_input_struct_to_queue(input);
}

void push_direction_input_to_queue(Direction dir, u32 player, u64 time)
{
    Input input;
    input.disconnect = false;
    input.dir = dir;
    input.time = time;
    input.player = player;
    push_input_struct_to_queue(input);
}

void init_game_state(GameState *state)
{
    state->seed = 1;
    state->frame_index = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        state->snakes[i].used = false;
    for (int i = 0; i < MAX_APPLES; i++)
        state->apples[i].used = false;
}

void create_snake(Snake *s, u32 x, u32 y)
{
    assert(!s->used);
    s->used = true;
    s->dir = DIR_LEFT;
    s->head_x = x;
    s->head_y = y;
    s->body_idx = 0;
    s->body_len = 0;
    s->iter_index = 0;
    s->iter_x = 0;
    s->iter_y = 0;
}

void change_snake_direction(Snake *s, Direction d)
{
    // Snakes can't face the opposite direction to their movement
    if (d != -s->dir) s->dir = d;
}

void start_iter_over_snake(Snake *s)
{
    s->iter_index = 0;
}

bool next_snake_body_part(Snake *s, u32 *x, u32 *y)
{
    if (s->iter_index == 0) {
        s->iter_x = s->head_x;
        s->iter_y = s->head_y;
    } else {
        if (s->iter_index > s->body_len)
            return false;
        switch (s->body[(s->body_idx + s->iter_index) % COUNTOF(s->body)]) {
            case DIR_UP   : s->iter_y--; break;
            case DIR_DOWN : s->iter_y++; break;
            case DIR_LEFT : s->iter_x++; break;
            case DIR_RIGHT: s->iter_x--; break;
        }

        // Clamp x to [0, WORLD_W-1]
        if (s->iter_x == -1)
            s->iter_x = WORLD_W-1;
        else if (s->iter_x == WORLD_W)
            s->iter_x = 0;

        // Clamp y to [0, WORLD_H-1]
        if (s->iter_y == -1)
            s->iter_y = WORLD_H-1;
        else if (s->iter_y == WORLD_H)
            s->iter_y = 0;
    }
    s->iter_index++;
    if (x) *x = s->iter_x;
    if (y) *y = s->iter_y;
    return true;
}

bool snake_head_collided_with_someone_else(Snake *p, GameState *game)
{
    for (int i = 0; i < MAX_SNAKES; i++) {
        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        if (s == p) next_snake_body_part(s, NULL, NULL); // Don't compare the snake's head with itself
        for (u32 x, y; next_snake_body_part(s, &x, &y); )
            if (x == p->head_x && y == p->head_y)
                return true;
    }
    return false;
}

bool consume_apple_at(Apple *apples, u32 x, u32 y)
{
    int i = 0;
    while (i < MAX_APPLES) {
        if (apples[i].used && apples[i].x == x && apples[i].y == y) {
            apples[i].used = false;
            return true;
        }
        i++;
    }
    return false;
}

void move_snake_forwards(Snake *s, Apple *apples)
{
    switch (s->dir) {
        case DIR_UP   : s->head_y++; break;
        case DIR_DOWN : s->head_y--; break;
        case DIR_LEFT : s->head_x--; break;
        case DIR_RIGHT: s->head_x++; break;
    }

    // Clamp x to [0, WORLD_W-1]
    if (s->head_x == -1)
        s->head_x = WORLD_W-1;
    else if (s->head_x == WORLD_W)
        s->head_x = 0;

    // Clamp y to [0, WORLD_H-1]
    if (s->head_y == -1)
        s->head_y = WORLD_H-1;
    else if (s->head_y == WORLD_H)
        s->head_y = 0;

    s->body[s->body_idx] = s->dir;
    if (s->body_idx > 0)
        s->body_idx--;
    else
        s->body_idx = COUNTOF(s->body)-1;

    if (consume_apple_at(apples, s->head_x, s->head_y))
        s->body_len++;
}

int count_apples(Apple *apples)
{
    int n = 0;
    for (int i = 0; i < MAX_APPLES; i++)
        if (apples[i].used)
            n++;
    return n;
}

bool location_occupied_by_snake_or_apple(GameState *game, u32 x, u32 y)
{
    for (int i = 0; i < MAX_SNAKES; i++) {
        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        u32 snake_body_part_x;
        u32 snake_body_part_y;
        while (next_snake_body_part(s, &snake_body_part_x, &snake_body_part_y))
            if (snake_body_part_x == x && snake_body_part_y == y)
                return true;
    }

    for (int i = 0; i < MAX_APPLES; i++) {
        Apple *a = &game->apples[i];
        if (a->used && a->x == x && a->y == y)
            return true;
    }

    return false;
}

u64 get_random_from_game(GameState *game)
{
    game->seed = next_random(game->seed);
    return game->seed;
}

void choose_apple_location(GameState *game, u32 *out_x, u32 *out_y)
{
    for (int i = 0; i < 1000; i++) {
        u32 x = get_random_from_game(game) % WORLD_W;
        u32 y = get_random_from_game(game) % WORLD_H;
        if (!location_occupied_by_snake_or_apple(game, x, y)) {
            if (out_x) *out_x = x;
            if (out_y) *out_y = y;
            return;
        }
    }
    for (u32 x = 0; x < WORLD_W; x++)
        for (u32 y = 0; y < WORLD_H; y++) {
            if (!location_occupied_by_snake_or_apple(game, x, y)) {
                if (out_x) *out_x = x;
                if (out_y) *out_y = y;
                return;
            }
        }
    assert(0); // No space left
}

void make_sure_there_is_at_least_this_amount_of_apples(GameState *game, int min_apples)
{
    int num_apples = count_apples(game->apples);
    if (num_apples >= min_apples) return;

    int missing = min_apples - num_apples;
    for (int i = 0; missing > 0; i++) {
        Apple *a = &game->apples[i];
        if (!a->used) {
            choose_apple_location(game, &a->x, &a->y);
            a->used = true;
            missing--;
        }
    }
}

void update_game_state(GameState *game)
{
    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        move_snake_forwards(s, game->apples);
        if (snake_head_collided_with_someone_else(s, game))
            s->used = false; // RIP
    }

    make_sure_there_is_at_least_this_amount_of_apples(game, 2);
    game->frame_index++;
}

bool pop_input_from_queue(Input *input)
{
    if (inputs_count == 0)
        return false;
    *input = inputs[inputs_head];
    inputs_head = (inputs_head + 1) & INPUTS_MASK;
    inputs_count--;
    return true;
}

bool peek_input_from_queue(u32 index, Input *input)
{
    if (inputs_count <= index)
        return false;
    *input = inputs[(inputs_head + index) & INPUTS_MASK];
    return true;
}

void apply_inputs_until_time(GameState *game_state, uint64_t frame_index_limit, bool pop)
{
    uint64_t last_frame_index = game_state->frame_index;
    u32 cursor = 0;

    for (;;) {

        Input input;
        bool done = !peek_input_from_queue(cursor, &input);
        if (done || input.time > frame_index_limit) break;

        if (pop)
            pop_input_from_queue(&input);
        else {
            cursor++;
        }

        assert(last_frame_index <= input.time);
        while (last_frame_index < input.time) {
            update_game_state(game_state);
            last_frame_index++;
        }

        assert(game_state->snakes[input.player].used == true);

        if (input.disconnect) {
            game_state->snakes[input.player].used = false;
        } else {
            switch (input.dir) {
                case DIR_UP   : change_snake_direction(&game_state->snakes[input.player], DIR_UP);    break;
                case DIR_DOWN : change_snake_direction(&game_state->snakes[input.player], DIR_DOWN);  break;
                case DIR_LEFT : change_snake_direction(&game_state->snakes[input.player], DIR_LEFT);  break;
                case DIR_RIGHT: change_snake_direction(&game_state->snakes[input.player], DIR_RIGHT); break;
            }
        }

    }

    while (last_frame_index < frame_index_limit) {
        update_game_state(game_state);
        last_frame_index++;
    }
}

void recalculate_latest_state(void)
{
    u64 lookback = 8;

    u64 latest_frame_index = latest_game_state.frame_index;

    // Apply old inputs permanently
    if (latest_frame_index >= lookback)
        apply_inputs_until_time(&oldest_game_state, latest_frame_index - lookback, true);

    // Apply newer inputs
    memcpy(&latest_game_state, &oldest_game_state, sizeof(GameState));
    apply_inputs_until_time(&latest_game_state, latest_frame_index, false);
}

void draw_game_state(GameState *game)
{
    float pad_x = 50;
    float pad_y = 20;
    float nopad_window_w = window.width  - 2 * pad_x;
    float nopad_window_h = window.height - 2 * pad_y;

    float scale;
    {
        float scale_x = nopad_window_w / (WORLD_W * TILE_W);
        float scale_y = nopad_window_h / (WORLD_H * TILE_H);
        scale = MIN(scale_x, scale_y);
    }

    float px_world_w = scale * WORLD_W * TILE_W;
    float px_world_h = scale * WORLD_H * TILE_H;

    // Offsets needed to center stuff
    float offset_x = (window.width  - px_world_w) / 2;
    float offset_y = (window.height - px_world_h) / 2;

    draw_rect(v2(offset_x, offset_y), v2(px_world_w, px_world_h), COLOR_WHITE);

    for (int i = 0; i < MAX_SNAKES; i++) {

        Snake *s = &game->snakes[i];
        if (!s->used) continue;

        start_iter_over_snake(s);
        for (u32 x, y; next_snake_body_part(s, &x, &y); )
            draw_rect(v2(offset_x + x * scale * TILE_W, offset_y + y * scale * TILE_H), v2(scale * TILE_W, scale * TILE_H), COLOR_GREEN);
    }

    for (int i = 0; i < MAX_APPLES; i++) {
        Apple *a = &game->apples[i];
        if (a->used)
            draw_rect(v2(offset_x + a->x * scale * TILE_W, offset_y + a->y * scale * TILE_H), v2(scale * TILE_W, scale * TILE_H), COLOR_RED);
    }
}

int count_snakes(GameState *game)
{
    int n = 0;
    for (int i = 0; i < MAX_SNAKES; i++)
        if (game->snakes[i].used)
            n++;
    return n;
}

void init_globals(void)
{
    inputs_head = 0;
    inputs_count = 0;
    server_handle = TCP_INVALID;
    for (int i = 0; i < MAX_SNAKES-1; i++)
        client_handles[i] = TCP_INVALID;
}

void apply_local_input(Direction dir)
{
    if (!multiplayer) {
        change_snake_direction(&latest_game_state.snakes[self_snake_index], dir);
    } else if (is_server) {
        u32 id = htonl(self_snake_index);
        u32 msg = htonl(dir);
        u64 time = htonll(latest_game_state.frame_index);
        for (u32 i = 0; i < MAX_SNAKES-1; i++) {
            if (client_handles[i] != TCP_INVALID) {
                tcp_client_write(client_handles[i], &time, sizeof(time));
                tcp_client_write(client_handles[i], &id, sizeof(id));
                tcp_client_write(client_handles[i], &msg, sizeof(msg));
            }
        }
        push_direction_input_to_queue(dir, self_snake_index, latest_game_state.frame_index);
        change_snake_direction(&latest_game_state.snakes[self_snake_index], dir);
    } else {
        u64 time = htonll(latest_game_state.frame_index);
        u32 msg = htonl(dir);
        tcp_client_write(server_handle, &time, sizeof(time));
        tcp_client_write(server_handle, &msg, sizeof(msg));
        push_direction_input_to_queue(dir, self_snake_index, latest_game_state.frame_index);
        change_snake_direction(&latest_game_state.snakes[self_snake_index], dir);
    }
}

void apply_remote_input(Direction dir, u32 player, u64 time)
{
    assert(multiplayer);

    if (is_server) {

        push_direction_input_to_queue(dir, player, time);

        time = htonll(time);
        u32 dir2 = htonl(dir);
        u32 id = htonl(player);

        // Relay the input to all other players
        for (int i = 0; i < MAX_SNAKES-1; i++) {
            if (client_handles[i] != TCP_INVALID && i != player-1) {
                tcp_client_write(client_handles[i], &time, sizeof(time));
                tcp_client_write(client_handles[i], &id, sizeof(id));
                tcp_client_write(client_handles[i], &dir2, sizeof(dir2));
            }
        }

    } else {

        push_direction_input_to_queue(dir, player, time);
    }
}

void process_player_inputs(void)
{
    if (is_key_just_pressed(KEY_ARROW_UP))    apply_local_input(DIR_UP);
    if (is_key_just_pressed(KEY_ARROW_DOWN))  apply_local_input(DIR_DOWN);
    if (is_key_just_pressed(KEY_ARROW_LEFT))  apply_local_input(DIR_LEFT);
    if (is_key_just_pressed(KEY_ARROW_RIGHT)) apply_local_input(DIR_RIGHT);

    if (!multiplayer)
        return;
    
    if (is_server) {

        tcp_server_poll(server_handle, 0);
        for (;;) {
            TCPEvent event = tcp_server_event(server_handle);
            if (event.type == TCP_EVENT_NONE) break;

            if (event.type == TCP_EVENT_CONNECT) {
                tcp_client_close(event.handle);
                continue;
            }

            if (event.type == TCP_EVENT_DISCONNECT) {
                int i = 0;
                while (client_handles[i] != event.handle) {
                    i++;
                    assert(i < MAX_SNAKES-1);
                }
                latest_game_state.snakes[i+1].used = false;
                tcp_client_close(client_handles[i]);
                client_handles[i] = TCP_INVALID;
                push_disconnect_input_to_queue(i, latest_game_state.frame_index);
                continue;
            }

            assert(event.type == TCP_EVENT_DATA);

            // Get the snake
            int i = 0;
            while (client_handles[i] != event.handle) {
                i++;
                assert(i < MAX_SNAKES-1);
            }
            i++;

            string input = tcp_client_get_input(event.handle);
            while (input.count >= sizeof(u64) + sizeof(u32)) {

                u32 value;
                u64 time;
                memcpy(&time,  input.data + 0, sizeof(u64));
                memcpy(&value, input.data + 8, sizeof(u32));
                tcp_client_read(event.handle, sizeof(u64) + sizeof(u32));
                Direction dir = ntohl(value);
                time = ntohll(time);

                apply_remote_input(dir, i, time);
                input = tcp_client_get_input(event.handle);
            }
        }
    } else {

        tcp_client_poll(server_handle, 0);
        for (;;) {
            TCPEvent event = tcp_client_event(server_handle);
            if (event.type == TCP_EVENT_NONE) break;

            if (event.type == TCP_EVENT_DISCONNECT) {
                printf("Server disconnected\n");
                abort();
            }

            assert(event.type == TCP_EVENT_DATA);

            string input = tcp_client_get_input(event.handle);
            while (input.count >= 2 * sizeof(u32) + sizeof(u64)) {

                u64 time;
                u32 buffer0;
                u32 buffer1;
                memcpy(&time,    input.data + 0, sizeof(u64));
                memcpy(&buffer0, input.data + 8, sizeof(u32));
                memcpy(&buffer1, input.data + 12, sizeof(u32));
                tcp_client_read(event.handle, 2 * sizeof(u32) + sizeof(u64));

                time = ntohll(time);
                u32 id = ntohl(buffer0);
                Direction dir = ntohl(buffer1);

                // TODO: Validate the ID
                assert(id < MAX_SNAKES && latest_game_state.snakes[id].used);

                apply_remote_input(dir, id, time);

                input = tcp_client_get_input(event.handle);
            }
        }
    }
}

void cleanup_network_resources()
{
    if (!multiplayer)
        return;
    
    if (is_server) {
        for (int i = 0; i < MAX_SNAKES; i++)
            if (client_handles[i] != TCP_INVALID) {
                tcp_client_close(client_handles[i]);
                client_handles[i] = TCP_INVALID;
            }
        tcp_server_delete(server_handle);
        server_handle = TCP_INVALID;
    } else {
        tcp_client_close(server_handle);
        server_handle = TCP_INVALID;
    }
}

void connecting_loop(void)
{
    assert(multiplayer && !is_server);

    // Connect to host
    server_handle = tcp_client_create(STR("127.0.0.1"), TCP_PORT);
    if (server_handle == TCP_INVALID) {
        printf("Couldn't connect to the server\n");
        abort();
    }
    printf("Connected!\n");

    // Receive starting state
    int num_snakes = 0;
    for (bool done = false; !done && !window.should_close; ) {

        reset_temporary_storage();

        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        printf(".. Waiting game state ..\n");
        tcp_client_poll(server_handle, 1000.0/FPS);

        do {
            TCPEvent event = tcp_client_event(server_handle);
            printf("Event!\n");

            if (event.type == TCP_EVENT_NONE) {
                printf("It's none :/\n");
                break;
            }

            if (event.type == TCP_EVENT_DISCONNECT) {
                printf("Server disconnected unexpectedly\n");
                abort();
            }

            assert(event.type == TCP_EVENT_DATA);
            printf("Got some data!\n");
            
            string input = tcp_client_get_input(server_handle);

            if (num_snakes == 0) {

                if (input.count < 2 * sizeof(u32))
                    continue;
                u32 buffer0;
                u32 buffer1;

                // Get snake count and client index
                memcpy(&buffer0, input.data + 0, sizeof(u32));
                memcpy(&buffer1, input.data + 4, sizeof(u32));
                tcp_client_read(server_handle, 2 * sizeof(u32));
                buffer0 = ntohl(buffer0);
                buffer1 = ntohl(buffer1);

                printf("num_snakes=%lu\n", buffer0);
                printf("self_snake_index=%lu\n", buffer1);
                
                if (buffer0 > MAX_SNAKES || buffer1 >= buffer0) {
                    // TODO
                    abort();
                }
                num_snakes       = (int) buffer0;
                self_snake_index = (int) buffer1;
            }

            if (num_snakes > 0) {

                if (input.count < num_snakes * sizeof(u32) * 2)
                    continue;
                for (int i = 0; i < num_snakes; i++) {

                    input = tcp_client_get_input(server_handle);

                    u32 head_x;
                    u32 head_y;
        
                    // Get head position
                    memcpy(&head_x, input.data + 0, sizeof(u32));
                    memcpy(&head_y, input.data + 4, sizeof(u32));
                    tcp_client_read(server_handle, 2 * sizeof(u32));

                    head_x = ntohl(head_x);
                    head_y = ntohl(head_y);
                    printf("snake[%d].head={x=%lu, y=%lu}\n", i, head_x, head_y);
                    if (head_x >= WORLD_W || head_y >= WORLD_H) {
                        // TODO
                        printf("Invalid snake coordinates\n");
                        abort();
                    }

                    create_snake(&latest_game_state.snakes[i], head_x, head_y);
                }
                done = true;
            }
        } while (!done && !window.should_close);

        os_update();
        gfx_update();
    }
}

void wait_for_players_loop(void)
{
    assert(multiplayer && is_server);

    // Listen for connections
    server_handle = tcp_server_create(STR(""), TCP_PORT, MAX_SNAKES);
    if (server_handle == TCP_INVALID) {
        printf("Could not start server\n");
        abort();
    }

    self_snake_index = 0;
    create_snake(&latest_game_state.snakes[0], get_random() % WORLD_W, get_random() % WORLD_H);

    // Wait for other players
    bool all_players_connected = false;
    while (!all_players_connected && !window.should_close) {

        reset_temporary_storage();

        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        static int dots = 0;
        string text;
        switch (dots) {
            case 0: text = STR("Waiting for players");     dots++;   break;
            case 1: text = STR("Waiting for players .");   dots++;   break;
            case 2: text = STR("Waiting for players ..");  dots++;   break;
            case 3: text = STR("Waiting for players ..."); dots = 0; break;
        }
        draw_text(font, text, 30, v2(30, 30), v2(1, 1), COLOR_BLACK);

        printf(".. Waiting for connections ..\n");
        tcp_server_poll(server_handle, 1000.0/FPS); // TODO: Wait until timeout
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
                create_snake(&latest_game_state.snakes[i+1],
                    get_random() % WORLD_W,
                    get_random() % WORLD_H);
                printf("Player connected\n");
            }

            if (event.type == TCP_EVENT_DISCONNECT) {
                int i = 0;
                while (client_handles[i] != event.handle) {
                    i++;
                    assert(i < MAX_SNAKES);
                }
                latest_game_state.snakes[i+1].used = false;
                client_handles[i] = TCP_INVALID;
                tcp_client_close(event.handle);
                printf("Player disconnected\n");
                continue;
            }

            if (event.type == TCP_EVENT_DATA) {
                printf("Player sent unexpected data\n");
                abort();
            }
        }

        int num_snakes = count_snakes(&latest_game_state);
        if (num_snakes == 2) all_players_connected = true;

        os_update(); 
        gfx_update();

    }

    printf("Ready to play!\n");

    // Compact snake structs so that their layout is the
    // same as the client's. Note that the first struct is
    // always occupied by the master
    int free_slots[MAX_SNAKES];
    int free_slots_head = 0;
    int free_slots_count = 0;
    for (int i = 0; i < MAX_SNAKES; i++) {
        if (latest_game_state.snakes[i].used && free_slots_count > 0) {
            int k = free_slots[free_slots_head];
            free_slots_head++;
            free_slots_count--;
            client_handles[k-1] = client_handles[i-1];
            client_handles[i-1] = TCP_INVALID;
            latest_game_state.snakes[k] = latest_game_state.snakes[i];
            latest_game_state.snakes[i].used = false;
        }
        if (!latest_game_state.snakes[i].used) {
            free_slots[free_slots_head + free_slots_count] = i;
            free_slots_count++;
        }
    }

    // Send player positions to the clients
    int num_snakes = count_snakes(&latest_game_state);
    for (int i = 0, j = 1; i < MAX_SNAKES-1; i++) {
        if (client_handles[i] == TCP_INVALID)
            continue;
        u32 buffer;

        // Send the player count
        buffer = htonl(num_snakes);
        tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

        // Send the index associated to this client
        buffer = htonl(j); // <-- This is j and not i
        tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

        // Send the player information
        for (int k = 0; k < MAX_SNAKES; k++) {
            Snake *s = &latest_game_state.snakes[k];
            if (!s->used) continue;

            printf("sending head_x=%d, head_y=%d\n", s->head_x, s->head_y);

            buffer = htonl(s->head_x);
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            buffer = htonl(s->head_y);
            tcp_client_write(client_handles[i], &buffer, sizeof(buffer));

            assert(s->body_len == 0); // We are assuming the starting size is 0. If that
                                        // wasn't the case we would need to send the body
        }

        j++;
    }
}

bool we_are_dead(void)
{
    return !latest_game_state.snakes[self_snake_index].used;
}

int entry(int argc, char **argv)
{
    init_globals();
    init_game_state(&latest_game_state);

	window.title = STR("Snake Battle Royale");
	window.scaled_width = 1280/4; // We need to set the scaled size if we want to handle system scaling (DPI)
	window.scaled_height = 720/4;
	window.x = 200;
	window.y = 90;
	window.clear_color = hex_to_rgba(0x6495EDff);

    string font_file = STR("assets/Minecraft.ttf");
    font = load_font_from_disk(font_file, get_heap_allocator());
    if (!font) {
        printf("Couldn't load font '%s'\n", font_file);
        abort();
    }

    /*
     * Choose between client or server
     */
    while (!window.should_close) {

		reset_temporary_storage();

        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        static int cursor = 0;

        if (is_key_just_pressed(KEY_ARROW_UP))
            if (cursor > 0) cursor--;

        if (is_key_just_pressed(KEY_ARROW_DOWN))
            if (cursor < 2) cursor++;

        if (is_key_just_pressed('A')) {
            switch (cursor) {
                case 0:
                is_server = true;
                multiplayer = false;
                break;
                case 1:
                is_server = true;
                multiplayer = true;
                break;
                case 2:
                is_server = false;
                multiplayer = true;
                break;
            }
            break;
        }
        draw_text(font, STR("PLAY"), 30, v2(30, 200), v2(1, 1), cursor == 0 ? COLOR_RED : COLOR_BLACK);
        draw_text(font, STR("HOST"), 30, v2(30, 100), v2(1, 1), cursor == 1 ? COLOR_RED : COLOR_BLACK);
        draw_text(font, STR("JOIN"), 30, v2(30,   0), v2(1, 1), cursor == 2 ? COLOR_RED : COLOR_BLACK);

        os_update();
		gfx_update();
    }

    if (!multiplayer) {
        printf("Single player mode\n");
        self_snake_index = 0;
        create_snake(&latest_game_state.snakes[0], get_random() % WORLD_W, get_random() % WORLD_H);
    } else if (is_server) {
        wait_for_players_loop();
    } else {
        connecting_loop();
    }

    memcpy(&oldest_game_state, &latest_game_state, sizeof(GameState));

	while (!window.should_close) {

        float64 frame_start_time = os_get_current_time_in_seconds();

        reset_temporary_storage();

        draw_frame.projection = m4_make_orthographic_projection(0, window.width, 0, window.height, -1, 10);

        process_player_inputs();
        if (multiplayer)
            recalculate_latest_state();
        update_game_state(&latest_game_state);
        if (we_are_dead()) {
            // We're dead x_x
        }
        draw_game_state(&latest_game_state);

		os_update(); 
		gfx_update();

        float64 elapsed = os_get_current_time_in_seconds() - frame_start_time;
        if (elapsed < 1.0F/FPS)
            os_high_precision_sleep(1000.0F/FPS - elapsed * 1000);
	}

    destroy_font(font);
    cleanup_network_resources();
	return 0;
}
